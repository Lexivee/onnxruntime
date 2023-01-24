/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Licensed under the MIT License.

Module Name:

    half gemm.cpp

Abstract:

    This module implements the half precision (fp16) matrix/matrix multiply
    operation (QGEMM).

--*/

#include "mlasi.h"
#include "mlas_float16.h"

#include "halfgemm.h"

#include <exception>


void
MLASCALL
MlasHalfGemmBatch(
    const size_t M,
    const size_t N,
    const size_t K,
    const size_t BatchN,
    const MLAS_HALF_GEMM_DATA_PARAMS* DataParams,
    MLAS_THREADPOOL* ThreadPool
    )
{
    const MLAS_HALF_GEMM_DISPATCH* dispatch = MlasHalfGemmGetDispatch();
    MLAS_HALF_GEMM_OPERATION* operation = dispatch->Operation;

    if (ThreadPool == nullptr) {
        for (size_t gemm_i = 0; gemm_i < BatchN; gemm_i++) {
            auto Data = &DataParams[gemm_i];
            operation(N, K, Data, 0, M, 0, N);
        }
        return;
    }

    //
    // Compute the number of target threads given the complexity of the SGEMM
    // operation. Small requests should run using the single threaded path.
    //

    const double Complexity = double(M) * double(N) * double(K) * double(BatchN);

    ptrdiff_t TargetThreadCount = ptrdiff_t(Complexity / double(MLAS_QGEMM_THREAD_COMPLEXITY)) + 1;

    ptrdiff_t MaximumThreadCount = MlasGetMaximumThreadCount(ThreadPool);

    if (TargetThreadCount >= MaximumThreadCount) {
        TargetThreadCount = MaximumThreadCount;
    }

    ptrdiff_t ThreadsPerGemm = TargetThreadCount / BatchN;
    if (ThreadsPerGemm < 1) {
        ThreadsPerGemm = 1;
    }

    const size_t StrideM = dispatch->StrideM;

    size_t nc = N;
    if ((size_t)MlasGetMaximumThreadCount(ThreadPool) > BatchN) {
        // more than one thread per GEMM

        const size_t BlockedM = MlasDivRoundup(M, StrideM);
        const size_t max_nc = MlasDivRoundup(N * BlockedM, ThreadsPerGemm);
        if (max_nc < nc) {
            nc = std::min(nc, MlasDivRoundup(nc, max_nc * MLAS_QGEMM_STRIDEN_THREAD_ALIGN) *
                                  MLAS_QGEMM_STRIDEN_THREAD_ALIGN);
        }
    }
    const size_t StrideN = nc;

    const size_t ThreadCountM = MlasDivRoundup(M, StrideM);
    const size_t ThreadCountN = MlasDivRoundup(N, StrideN);
    ThreadsPerGemm = ThreadCountM * ThreadCountN;

    MlasTrySimpleParallel(ThreadPool, ThreadsPerGemm * BatchN, [&](ptrdiff_t tid) {
        const auto gemm_i = tid / ThreadsPerGemm;
        const auto blk_i = tid % ThreadsPerGemm;
        auto Data = &DataParams[gemm_i];

        const ptrdiff_t ThreadIdN = blk_i / ThreadCountM;
        const ptrdiff_t ThreadIdM = blk_i % ThreadCountM;

        const size_t RangeStartM = ThreadIdM * StrideM;
        const size_t RangeCountM = std::min(M - RangeStartM, (size_t)StrideM);

        const size_t RangeStartN = ThreadIdN * StrideN;
        const size_t RangeCountN = std::min(N - RangeStartN, (size_t)StrideN);

        operation(N, K, Data, RangeStartM, RangeCountM, RangeStartN, RangeCountN);
    });
}


size_t
MLASCALL
MlasHalfGemmPackBSize(
    size_t N,
    size_t K,
    bool float2half
    )
{
    const auto* dispatch = MlasHalfGemmGetDispatch();
    const auto PackedK = dispatch->PackededK;
    if (!float2half && dispatch->CopyPackBRoutine == nullptr) {
        // No packing routine provided
        return 0;
    }
    const size_t AlignedK = (K + PackedK - 1) & ~(PackedK - 1);
    const size_t BytesRequired = N * AlignedK * FP16_SIZE;
    const size_t BufferAlignment = MlasGetPreferredBufferAlignment();
    const size_t AlignedBytesRequired =
        (BytesRequired + BufferAlignment - 1) & ~(BufferAlignment - 1);
    return AlignedBytesRequired;
}

void
MLASCALL
MlasHalfGemmPackB(
    size_t N,
    size_t K,
    const MLAS_FP16* B,
    size_t ldb,
    void* PackedB
    )
{
    const auto* dispatch = MlasHalfGemmGetDispatch();
    dispatch->CopyPackBRoutine((_mlas_fp16_*)PackedB, (const _mlas_fp16_*)B, ldb, N, K);
}

void
MLASCALL
MlasHalfGemmConvertPackB(
    size_t N,
    size_t K,
    const float* B,
    size_t ldb,
    void* PackedB
    )
{
    const auto* dispatch = MlasHalfGemmGetDispatch();
    dispatch->ConvertPackBRoutine((_mlas_fp16_*)PackedB, B, ldb, N, K);
}


//
// Dummy C++ implementation that runs very slowly
//

struct MLAS_HALF_GEMM_KERNEL_DEFAULT {

    static constexpr bool PackNeeded = false;
    static constexpr size_t KernelMaxM = 128; // max # rows the vectorized kernel can process
    static constexpr size_t PackedK = 1;

    static constexpr MLAS_HALF_GEMM_STRIDES Strides{8, 16, 32};
};

template<>
MLAS_FORCEINLINE
void
MlasHalfGemmConvertPackA<MLAS_HALF_GEMM_KERNEL_DEFAULT>(
    _mlas_fp16_* D,
    const float* A,
    size_t lda,
    size_t CountM,
    size_t CountK
)
{
    for (size_t m = 0; m < CountM; m++) {
        for (size_t k = 0; k < CountK; k++) {
            *D++ = MLAS_Float2Half(*(A + m * lda + k));
        }
    }
}

template<>
MLAS_FORCEINLINE
void
MlasHalfGemmConvertPackB<MLAS_HALF_GEMM_KERNEL_DEFAULT>(
    _mlas_fp16_* D,
    const float* B,
    size_t ldb,
    size_t CountN,
    size_t CountK
)
{
    for (size_t k = 0; k < CountK; k++) {
        for (size_t n = 0; n < CountN; n++) {
            *D++ = MLAS_Float2Half(*(B + k * ldb + n));
        }
    }
}


template<>
MLAS_FORCEINLINE
void
MlasHalfGemmKernel<MLAS_HALF_GEMM_KERNEL_DEFAULT>(
    size_t CountM,
    size_t CountN,
    size_t CountK,
    _mlas_fp16_* C,
    size_t ldc,
    const _mlas_fp16_* Bias,
    const _mlas_fp16_* A,
    size_t lda,
    const _mlas_fp16_* B,
    size_t ldb,
    const bool ZeroMode)
{
    for (size_t m = 0; m < CountM; m++) {
        for (size_t n = 0; n < CountN; n++) {
            const auto* a = A + (m * lda);
            const auto* b = B + n;
            auto* c = C + (m * ldc) + n;

            float sum = Bias == nullptr ? 0.0f : MLAS_Half2Float(Bias[n]);
            if (!ZeroMode) {
                sum += MLAS_Half2Float(*c);
            }

            for (size_t k = 0; k < CountK; k++) {
                auto down = MLAS_Float2Half(MLAS_Half2Float(*a) * MLAS_Half2Float(*b) + sum);
                sum = MLAS_Half2Float(down);
                b += ldb;
                a += 1;
            }

            *c = MLAS_Float2Half(sum);
        }
    }
}


const MLAS_HALF_GEMM_DISPATCH MlasHalfGemmDispatchDefault = {
    MlasHalfGemmOperation<MLAS_HALF_GEMM_KERNEL_DEFAULT>,
    nullptr, 
    MlasHalfGemmConvertPackB<MLAS_HALF_GEMM_KERNEL_DEFAULT>,
    MLAS_HALF_GEMM_KERNEL_DEFAULT::PackedK,
    MLAS_HALF_GEMM_KERNEL_DEFAULT::KernelMaxM
};
