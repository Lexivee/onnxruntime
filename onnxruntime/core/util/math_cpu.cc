/**
* Copyright (c) 2016-present, Facebook, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

// Implements the math functions for CPU.
// The implementation in this file allows us to route the underlying numerical
// computation library to different backends. Notably:
// (1) For all BLAS-related functions, one can explicitly request a BLAS backend
//     such as MKL, openblas or Atlas. To see the set of supported backends
//     currently provided, check //third_party/blas/.
// (2) If one chooses to link against MKL, we utilize MKL's vector math library
//     (VML) for a few functions such as Exp and Log.
// (3) Fallback implementations are provided in Eigen for cross-platform
//     support. Since Eigen is a header-only library and supports a number of
//     platforms, it allows one to quickly port Caffe2 to different platforms
//     where BLAS may not be present.
// Modifications Copyright (c) Microsoft.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <unordered_set>
#include "core/platform/env.h"
#include "core/common/logging/logging.h"
#include "core/providers/cpu/cpu_execution_provider.h"
#include "core/util/math.h"
#include "core/util/math_cpuonly.h"
#include "Eigen/src/Core/arch/CUDA/Half.h"

#if defined(USE_MLAS)
#include "core/mlas/inc/mlas.h"
#endif

#ifdef USE_MKLDNN
#include "mkldnn.h"
#endif

namespace onnxruntime {
namespace math {

////////////////////////////////////////////////////////////////////////////////
// BLAS alternatives.
// Depending on whether we have specified an external BLAS library or not, we
// will delegate the Caffe math functions that are BLAS-related to either the
// CBLAS call or the Eigen implementation.
////////////////////////////////////////////////////////////////////////////////
// when USE_MKLDNN and USE_MKLML are defined, use cblas APIs for MKLML
#if defined(USE_EIGEN_FOR_BLAS) && !defined(USE_MKLML_FOR_BLAS)

// Gemm implementation purely based on Eigen.
template <typename T>
void GemmEigen(
    CBLAS_TRANSPOSE TransA,
    CBLAS_TRANSPOSE TransB,
    int64_t M,
    int64_t N,
    int64_t K,
    float alpha,
    const T* A,
    const T* B,
    float beta,
    T* C) {
  auto C_mat = EigenMatrixMap<T>(C, N, M);
  if (beta == 0) {
    C_mat.setZero();
  } else {
    C_mat *= static_cast<T>(beta);
  }
  switch (TransA) {
    case CblasNoTrans: {
      switch (TransB) {
        case CblasNoTrans:
          C_mat.noalias() += static_cast<T>(alpha) * (ConstEigenMatrixMap<T>(B, N, K) *
                                      ConstEigenMatrixMap<T>(A, K, M));
          return;
        case CblasTrans:
          C_mat.noalias() += static_cast<T>(alpha) * (ConstEigenMatrixMap<T>(B, K, N).transpose() *
                                      ConstEigenMatrixMap<T>(A, K, M));
          return;
        default:
          ORT_THROW("CblasNoTrans Unexpected CBLAS_TRANSPOSE for TransB of ", TransB);
      }
    }
    case CblasTrans: {
      switch (TransB) {
        case CblasNoTrans:
          C_mat.noalias() += static_cast<T>(alpha) * (ConstEigenMatrixMap<T>(B, N, K) *
                                      ConstEigenMatrixMap<T>(A, M, K).transpose());
          return;
        case CblasTrans:
          C_mat.noalias() += static_cast<T>(alpha) * (ConstEigenMatrixMap<T>(B, K, N).transpose() *
                                      ConstEigenMatrixMap<T>(A, M, K).transpose());
          return;
        default:
          ORT_THROW("CblasTrans Unexpected CBLAS_TRANSPOSE for TransB of ", TransB);
      }
    }
    default:
      ORT_THROW("Unexpected CBLAS_TRANSPOSE for TransA of ", TransA);
  }
}

// Caffe2 gemm provides a simpler interface to the gemm functions, with the
// limitation that the data has to be contiguous in memory.
//
// The gemm call implements the following operation:
//
//                  C = alpha * op(A) * op(B) + beta * C
//
// where op(A) has size M x K, op(B) has size K x N, and C has size M x N. Each
// of A, B, and C are matrices and alpha and beta are scalars. Note that the
// most common use case of gemm will involve setting alpha to 1 and beta to 0.
//
// op(A) and op(B) represent the transformations that are done to A and B before
// the matrix multiply; depending on the flags set, op(A) is equal to A or A^T
// (transpose) if the argument TransA or TransB is set to CblasNoTrans or
// CblasTrans, respectively, for each of A and B.
template <>
void Gemm<float, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int64_t M,
    const int64_t N,
    const int64_t K,
    const float alpha,
    const float* A,
    const float* B,
    const float beta,
    float* C,
    CPUMathUtil* /*provider*/,
    MLDataType /*math_type*/) {
#if defined(USE_MKLDNN)
  int lda = (int)((TransA == CblasTrans) ? M : K);
  int ldb = (int)((TransB == CblasTrans) ? K : N);
  int M_ = (int)M;
  int N_ = (int)N;
  int K_ = (int)K;
  // mkldnn_sgemm expects col major matrices, so we need to swap the operands A and B
  auto status = mkldnn_sgemm(TransB == CblasNoTrans ? "N" : "T",
                             TransA == CblasNoTrans ? "N" : "T",
                             &N_, &M_, &K_,
                             &alpha, B, &ldb,
                             A, &lda,
                             &beta, C, &N_);
  if (status != mkldnn_success) {
    ORT_THROW("mkldnn_sgemm failed with status: ", status);
  }
#elif defined(USE_MLAS)
  int lda = (int)((TransA == CblasNoTrans) ? K : M);
  int ldb = (int)((TransB == CblasNoTrans) ? N : K);
  MlasSgemm(TransA, TransB, M, N, K, alpha, A, lda, B, ldb, beta, C, N);
#else
  GemmEigen<float>(TransA, TransB, M, N, K, alpha, A, B, beta, C);
#endif
}

template <>
void Gemm<double, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int64_t M,
    const int64_t N,
    const int64_t K,
    const float alpha,
    const double* A,
    const double* B,
    const float beta,
    double* C,
    CPUMathUtil* provider,
    MLDataType /*math_type*/) {
  GemmEigen<double>(TransA, TransB, M, N, K, alpha, A, B, beta, C);
}

template <>
void Gemm<int32_t, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int64_t M,
    const int64_t N,
    const int64_t K,
    const float alpha,
    const int32_t* A,
    const int32_t* B,
    const float beta,
    int32_t* C,
    CPUMathUtil* provider,
    MLDataType /*math_type*/) {
  GemmEigen<int32_t>(TransA, TransB, M, N, K, alpha, A, B, beta, C);
}

template <>
void Gemm<uint32_t, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int64_t M,
    const int64_t N,
    const int64_t K,
    const float alpha,
    const uint32_t* A,
    const uint32_t* B,
    const float beta,
    uint32_t* C,
    CPUMathUtil* provider,
    MLDataType /*math_type*/) {
  GemmEigen<uint32_t>(TransA, TransB, M, N, K, alpha, A, B, beta, C);
}

template <>
void Gemm<int64_t, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int64_t M,
    const int64_t N,
    const int64_t K,
    const float alpha,
    const int64_t* A,
    const int64_t* B,
    const float beta,
    int64_t* C,
    CPUMathUtil* provider,
    MLDataType /*math_type*/) {
  GemmEigen<int64_t>(TransA, TransB, M, N, K, alpha, A, B, beta, C);
}

template <>
void Gemm<uint64_t, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int64_t M,
    const int64_t N,
    const int64_t K,
    const float alpha,
    const uint64_t* A,
    const uint64_t* B,
    const float beta,
    uint64_t* C,
    CPUMathUtil* provider,
    MLDataType /*math_type*/) {
  GemmEigen<uint64_t>(TransA, TransB, M, N, K, alpha, A, B, beta, C);
}

template <>
void GemmEx<float, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int M,
    const int N,
    const int K,
    const float alpha,
    const float* A,
    const int lda,
    const float* B,
    const int ldb,
    const float beta,
    float* C,
    const int ldc,
    CPUMathUtil*) {
#if defined(USE_MKLDNN)
  // mkldnn_sgemm expects col major matrices, so we need to swap the operands A and B
  auto status = mkldnn_sgemm(TransB == CblasNoTrans ? "N" : "T",
                             TransA == CblasNoTrans ? "N" : "T",
                             &N, &M, &K,
                             &alpha, B, &ldb,
                             A, &lda,
                             &beta, C, &ldc);
  if (status != mkldnn_success) {
    ORT_THROW("mkldnn_sgemm failed with status: ", status);
  }
#elif defined(USE_MLAS)
  MlasSgemm(TransA, TransB, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
#else
  using OuterStride = Eigen::OuterStride<Eigen::Dynamic>;
  using StridedMap = Eigen::Map<Eigen::MatrixXf, 0, OuterStride>;
  using ConstStridedMap = Eigen::Map<const Eigen::MatrixXf, 0, OuterStride>;
  auto C_mat = StridedMap(C, N, M, OuterStride(ldc));
  if (beta == 0) {
    C_mat.setZero();
  } else {
    C_mat *= beta;
  }
  switch (TransA) {
    case CblasNoTrans: {
      switch (TransB) {
        case CblasNoTrans:
          C_mat.noalias() +=
              alpha * (ConstStridedMap(B, N, K, OuterStride(ldb)) *
                       ConstStridedMap(A, K, M, OuterStride(lda)));
          return;
        case CblasTrans:
          C_mat.noalias() +=
              alpha * (ConstStridedMap(B, K, N, OuterStride(ldb)).transpose() *
                       ConstStridedMap(A, K, M, OuterStride(lda)));
          return;
        default:
          ORT_THROW("CblasNoTrans Unexpected CBLAS_TRANSPOSE for TransB of ", TransB);
      }
    }
    case CblasTrans: {
      switch (TransB) {
        case CblasNoTrans:
          C_mat.noalias() +=
              alpha * (ConstStridedMap(B, N, K, OuterStride(ldb)) *
                       ConstStridedMap(A, M, K, OuterStride(lda)).transpose());
          return;
        case CblasTrans:
          C_mat.noalias() +=
              alpha * (ConstStridedMap(B, K, N, OuterStride(ldb)).transpose() *
                       ConstStridedMap(A, M, K, OuterStride(lda)).transpose());
          return;
        default:
          ORT_THROW("CblasTrans Unexpected CBLAS_TRANSPOSE for TransB of ", TransB);
      }
    }
    default:
      ORT_THROW("Unexpected CBLAS_TRANSPOSE for TransA of ", TransA);
  }
#endif
}

template <>
void Gemv<float, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const int M,
    const int N,
    const float alpha,
    const float* A,
    const float* x,
    const float beta,
    float* y,
    CPUMathUtil* /*provider*/,
    MLDataType /*math_type*/) {
  EigenVectorMap<float> y_vec(y, TransA == CblasNoTrans ? M : N);
  if (beta == 0) {
    // In Caffe2 we often do a lazy initialization, which may contain NaNs in
    // the float values. As a result, if beta is 0, we explicitly do a setzero.
    y_vec.setZero();
  } else {
    y_vec *= beta;
  }
  switch (TransA) {
    case CblasNoTrans: {
      y_vec.noalias() += alpha * (ConstEigenMatrixMap<float>(A, N, M).transpose() *
                                  ConstEigenVectorMap<float>(x, N));
      return;
    }
    case CblasTrans: {
      y_vec.noalias() += alpha * (ConstEigenMatrixMap<float>(A, N, M) *
                                  ConstEigenVectorMap<float>(x, M));
      return;
    }
    default:
      ORT_THROW("Gemv float found an unexpected CBLAS_TRANSPOSE input of", TransA);
  }
}

#define SPECIALIZED_SCALE(T)                                                         \
  template <>                                                                        \
  void Scale<T, CPUMathUtil>(                                                        \
      const int n, const float alpha, const T* x, T* y, CPUMathUtil* /*provider*/) { \
    EigenVectorMap<T>(y, n) = ConstEigenVectorMap<T>(x, n) * alpha;                  \
  }                                                                                  \
  template <>                                                                        \
  void Scale<T, CPUMathUtil>(                                                        \
      const int n,                                                                   \
      const float* alpha,                                                            \
      const T* x,                                                                    \
      T* y,                                                                          \
      CPUMathUtil* /*provider*/) {                                                   \
    EigenVectorMap<T>(y, n) = ConstEigenVectorMap<T>(x, n) * (*alpha);               \
  }
SPECIALIZED_SCALE(float)
#undef SPECIALIZED_SCALE

#define SPECIALIZED_DOT(T)                                               \
  template <>                                                            \
  void Dot<T, CPUMathUtil>(                                              \
      const int N, const T* a, const T* b, T* y,                         \
      CPUMathUtil* /*provider*/) {                                       \
    *y = ConstEigenVectorMap<T>(a, N).dot(ConstEigenVectorMap<T>(b, N)); \
  }
SPECIALIZED_DOT(float)
#undef SPECIALIZED_DOT

#define SPECIALIZED_AXPY(T)                                                       \
  template <>                                                                     \
  void Axpy<T, CPUMathUtil>(                                                      \
      const int N, const T alpha, const T* x, T* Y, CPUMathUtil* /*provider*/) {  \
    EigenVectorMap<T>(Y, N) += ConstEigenVectorMap<T>(x, N) * alpha;              \
  }                                                                               \
  template <>                                                                     \
  void Axpy<T, CPUMathUtil>(                                                      \
      const int N, const T* alpha, const T* x, T* Y, CPUMathUtil* /*provider*/) { \
    EigenVectorMap<T>(Y, N) += ConstEigenVectorMap<T>(x, N) * (*alpha);           \
  }
SPECIALIZED_AXPY(float)
#undef SPECIALIZED_AXPY

#define SPECIALIZED_AXPBY(T)                                                 \
  template <>                                                                \
  void Axpby<T, CPUMathUtil>(const int N, const T alpha, const T* x,         \
                             const T beta, T* y, CPUMathUtil* /*context*/) { \
    EigenVectorMap<T> y_vec(y, N);                                           \
    y_vec = y_vec * beta + ConstEigenVectorMap<T>(x, N) * alpha;             \
  }
SPECIALIZED_AXPBY(float)
#undef SPECIALIZED_AXPBY

#else  // USE_EIGEN_FOR_BLAS

template <>
void Gemm<float, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int64_t M,
    const int64_t N,
    const int64_t K,
    const float alpha,
    const float* A,
    const float* B,
    const float beta,
    float* C,
    CPUMathUtil* /*context*/,
    MLDataType /*math_type*/) {
  int lda = gsl::narrow_cast<int>((TransA == CblasNoTrans) ? K : M);
  int ldb = gsl::narrow_cast<int>((TransB == CblasNoTrans) ? N : K);
  cblas_sgemm(CblasRowMajor, TransA, TransB,
              gsl::narrow_cast<int>(M),
              gsl::narrow_cast<int>(N),
              gsl::narrow_cast<int>(K),
              alpha, A, lda, B, ldb,
              beta, C, gsl::narrow_cast<int>(N));
}

template <>
void GemmEx<float, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int M,
    const int N,
    const int K,
    const float alpha,
    const float* A,
    const int lda,
    const float* B,
    const int ldb,
    const float beta,
    float* C,
    const int ldc,
    CPUMathUtil* /*context*/) {
  cblas_sgemm(CblasRowMajor, TransA, TransB, M, N, K, alpha, A, lda, B, ldb,
              beta, C, ldc);
}

template <>
void Gemv<float, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const int M,
    const int N,
    const float alpha,
    const float* A,
    const float* x,
    const float beta,
    float* y,
    CPUMathUtil* /*context*/,
    MLDataType /*math_type*/) {
  cblas_sgemv(CblasRowMajor, TransA, M, N, alpha, A, N, x, 1, beta, y, 1);
}

#define CAFFE2_SPECIALIZED_SCALE(T, prefix)                              \
  template <>                                                            \
  void Scale<T, CPUMathUtil>(                                            \
      const int n, const float alpha, const T* x, T* y, CPUMathUtil*) {  \
    if (y != x)                                                          \
      cblas_##prefix##copy(n, x, 1, y, 1);                               \
    cblas_##prefix##scal(n, static_cast<float>(alpha), y, 1);            \
  }                                                                      \
  template <>                                                            \
  void Scale<T, CPUMathUtil>(                                            \
      const int n, const float* alpha, const T* x, T* y, CPUMathUtil*) { \
    if (y != x)                                                          \
      cblas_##prefix##copy(n, x, 1, y, 1);                               \
    cblas_##prefix##scal(n, static_cast<float>(*alpha), y, 1);           \
  }
CAFFE2_SPECIALIZED_SCALE(float, s)
#undef CAFFE2_SPECIALIZED_SCALE

#define CAFFE2_SPECIALIZED_DOT(T, prefix)                        \
  template <>                                                    \
  void Dot<T, CPUMathUtil>(                                      \
      const int N, const T* a, const T* b, T* y, CPUMathUtil*) { \
    *y = cblas_##prefix##dot(N, a, 1, b, 1);                     \
  }
CAFFE2_SPECIALIZED_DOT(float, s)
#undef CAFFE2_SPECIALIZED_DOT

#define CAFFE2_SPECIALIZED_AXPY(T, prefix)                           \
  template <>                                                        \
  void Axpy<T, CPUMathUtil>(                                         \
      const int N, const T alpha, const T* x, T* y, CPUMathUtil*) {  \
    cblas_##prefix##axpy(N, alpha, x, 1, y, 1);                      \
  }                                                                  \
  template <>                                                        \
  void Axpy<T, CPUMathUtil>(                                         \
      const int N, const T* alpha, const T* x, T* y, CPUMathUtil*) { \
    cblas_##prefix##axpy(N, *alpha, x, 1, y, 1);                     \
  }
CAFFE2_SPECIALIZED_AXPY(float, s)
#undef CAFFE2_SPECIALIZED_AXPY

#define CAFFE2_SPECIALIZED_AXPBY(T, prefix)     \
  template <>                                   \
  void Axpby<T, CPUMathUtil>(                   \
      const int N,                              \
      const T alpha,                            \
      const T* x,                               \
      const T beta,                             \
      T* y,                                     \
      CPUMathUtil*) {                           \
    cblas_##prefix##scal(N, beta, y, 1);        \
    cblas_##prefix##axpy(N, alpha, x, 1, y, 1); \
  }
CAFFE2_SPECIALIZED_AXPBY(float, s)
#undef CAFFE2_SPECIALIZED_AXPBY

#endif  // USE_EIGEN_FOR_BLAS

template <>
void GemmBatched<float, CPUMathUtil>(
    const CBLAS_TRANSPOSE TransA,
    const CBLAS_TRANSPOSE TransB,
    const int A_size,
    const int A_batches,
    const int B_size,
    const int B_batches,
    const int M,
    const int N,
    const int K,
    const float /*alpha*/,
    const float* A,
    const float* B,
    const float /*beta*/,
    float* C,
    CPUMathUtil* provider,
    Tensor*, /* scratch */
    MLDataType /* math_type */) {
  auto a_offset = A_size / A_batches;
  auto b_offset = B_size / B_batches;
  auto y_offset = M * N;
  // loop over matrices in the batch
  for (int i = 0; i < A_batches; ++i) {
    math::Gemm<float, CPUMathUtil>(
        TransA,
        TransB,
        M,
        N,
        K,
        1,
        A + a_offset * i,
        B + b_offset * i,
        0,
        C + y_offset * i,
        provider);
  }
}

  // MKL will be implmenet as an execution provider
  ////////////////////////////////////////////////////////////////////////////////
  // MKL VML alternatives.
  // Depending on whether we are using MKL, we will delegate the Caffe math
  // functions that are VML-related to either the VML call or the Eigen
  // implementation. If you are setting the flags (such as AVX) right for your CPU
  // architecture, usually Eigen will deliver a throughput as fast as the VML
  // functions.
  ////////////////////////////////////////////////////////////////////////////////

#define DELEGATE_SIMPLE_UNARY_FUNCTION(T, Funcname, expr)                      \
  template <>                                                                  \
  void Funcname<T, CPUMathUtil>(const int N, const T* x, T* y, CPUMathUtil*) { \
    EigenVectorMap<T>(y, N) = ConstEigenVectorMap<T>(x, N).array().expr();     \
  }
DELEGATE_SIMPLE_UNARY_FUNCTION(float, Exp, exp)
DELEGATE_SIMPLE_UNARY_FUNCTION(float, Log, log)
DELEGATE_SIMPLE_UNARY_FUNCTION(float, Cos, cos)
DELEGATE_SIMPLE_UNARY_FUNCTION(float, Sin, sin)
DELEGATE_SIMPLE_UNARY_FUNCTION(float, Abs, abs)
DELEGATE_SIMPLE_UNARY_FUNCTION(float, Sqrt, sqrt)
DELEGATE_SIMPLE_UNARY_FUNCTION(float, InvSqrt, rsqrt)
DELEGATE_SIMPLE_UNARY_FUNCTION(float, Sqr, square)
#undef DELEGATE_SIMPLE_UNARY_FUNCTION

#define DELEGATE_SINCOS_FUNCTION(T)                                        \
  template <>                                                              \
  void SinCos<T, CPUMathUtil>(                                             \
      const int N, const T* x, T* ys, T* yc, CPUMathUtil*) {               \
    EigenVectorMap<T>(ys, N) = ConstEigenVectorMap<T>(x, N).array().sin(); \
    EigenVectorMap<T>(yc, N) = ConstEigenVectorMap<T>(x, N).array().cos(); \
  }
DELEGATE_SINCOS_FUNCTION(float)
DELEGATE_SINCOS_FUNCTION(double)
#undef DELEGATE_SINCOS_FUNCTION

#define DELEGATE_POWX_FUNCTION(T)                                               \
  template <>                                                                   \
  void Powx<T, CPUMathUtil>(const int N, const T* a, T b, T* y, CPUMathUtil*) { \
    EigenVectorMap<T>(y, N) = ConstEigenVectorMap<T>(a, N).array().pow(b);      \
  }
DELEGATE_POWX_FUNCTION(float)
#undef DELEGATE_POWX_FUNCTION

#define EIGEN_SIMPLE_BINARY_FUNCTION(T, Funcname, expr) \
  template <>                                           \
  void Funcname<T, CPUMathUtil>(                        \
      const int N, const T* a, const T* b, T* y,        \
      CPUMathUtil*) {                                   \
    EigenVectorMap<T>(y, N) =                           \
        ConstEigenVectorMap<T>(a, N).array() expr       \
            ConstEigenVectorMap<T>(b, N)                \
                .array();                               \
  }

#define DEFINE_SIMPLE_BINARY_FUNCTION(Funcname, expr)   \
  EIGEN_SIMPLE_BINARY_FUNCTION(float, Funcname, expr)   \
  EIGEN_SIMPLE_BINARY_FUNCTION(int32_t, Funcname, expr) \
  EIGEN_SIMPLE_BINARY_FUNCTION(int64_t, Funcname, expr)

DEFINE_SIMPLE_BINARY_FUNCTION(Add, +)
DEFINE_SIMPLE_BINARY_FUNCTION(Sub, -)
DEFINE_SIMPLE_BINARY_FUNCTION(Mul, *)
DEFINE_SIMPLE_BINARY_FUNCTION(Div, /)

#undef EIGEN_SIMPLE_BINARY_FUNCTION
#undef DEFINE_FLOAT_BINARY_FUNCTION

////////////////////////////////////////////////////////////////////////////////
// common math functions being used in Caffe that do not have a BLAS or MKL
// equivalent. For all these functions, we will simply implement them either via
// Eigen or via custom code.
////////////////////////////////////////////////////////////////////////////////

#define SPECIALIZED_REDUCEMIN(T)      \
  template <>                         \
  void ReduceMin<T, CPUMathUtil>(     \
      const int N,                    \
      const T* x,                     \
      T* y,                           \
      Tensor* /*scratch_ptr*/,        \
      CPUMathUtil* /*context*/) {     \
    *y = *std::min_element(x, x + N); \
  }
SPECIALIZED_REDUCEMIN(float)
#undef SPECIALIZED_REDUCEMIN

#define SPECIALIZED_REDUCEMAX(T)      \
  template <>                         \
  void ReduceMax<T, CPUMathUtil>(     \
      const int N,                    \
      const T* x,                     \
      T* y,                           \
      Tensor* /*scratch_ptr*/,        \
      CPUMathUtil* /*context*/) {     \
    *y = *std::max_element(x, x + N); \
  }
SPECIALIZED_REDUCEMAX(float)
SPECIALIZED_REDUCEMAX(int32_t)
SPECIALIZED_REDUCEMAX(int64_t)

#undef SPECIALIZED_REDUCEMAX

#define SPECIALIZED_ROWWISEMAX(T)                                 \
  template <>                                                     \
  void RowwiseMax<T, CPUMathUtil>(                                \
      const int N, const int D, const T* x, T* y, CPUMathUtil*) { \
    EigenVectorMap<T>(y, N) =                                     \
        ConstEigenMatrixMap<T>(x, D, N).colwise().maxCoeff();     \
  }
SPECIALIZED_ROWWISEMAX(float)
#undef SPECIALIZED_ROWWISEMAX

#define SPECIALIZED_COLWISEMAX(T)                                 \
  template <>                                                     \
  void ColwiseMax<T, CPUMathUtil>(                                \
      const int N, const int D, const T* x, T* y, CPUMathUtil*) { \
    EigenVectorMap<T>(y, D) =                                     \
        ConstEigenMatrixMap<T>(x, D, N).rowwise().maxCoeff();     \
  }
SPECIALIZED_COLWISEMAX(float)
#undef SPECIALIZED_COLWISEMAX

#define SPECIALIZED_ELEMWISEMAX(T)                                           \
  template <>                                                                \
  void ElemwiseMax<T, CPUMathUtil>(                                          \
      const int N, const T* x, const T* y, T* z, CPUMathUtil* /*context*/) { \
    std::transform(x, x + N, y, z, [](const T& x_i, const T& y_i) {          \
      return std::max(x_i, y_i);                                             \
    });                                                                      \
  }
SPECIALIZED_ELEMWISEMAX(float)
#undef SPECIALIZED_ELEMWISEMAX

#define SPECIALIZED_MAXIMUM(T)                                                       \
  template <>                                                                        \
  void Maximum<T, CPUMathUtil>(                                                      \
      const int N, const float alpha, const T* x, T* y, CPUMathUtil* /*provider*/) { \
    std::transform(                                                                  \
        x, x + N, y, [&alpha](const T& x_i) { return std::max(x_i, alpha); });       \
  }
SPECIALIZED_MAXIMUM(float)
#undef SPECIALIZED_MAXIMUM

// AddToRow and AddToCol adds the corresponding row/col vector b to the matrix a
// of shape M x N. The actual implementation uses eigen which is column major,
// so notice the row/column swap in the actual implementation.
#define DELEGATE_BROADCAST_BINARY_FUNCTION(T, Funcname, expr)                 \
  template <>                                                                 \
  void Funcname##ToRow<T, CPUMathUtil>(                                       \
      const int M, const int N, const T* a, const T* b, T* y, CPUMathUtil*) { \
    EigenArrayMap<T>(y, N, M) = ConstEigenArrayMap<T>(a, N, M).colwise()      \
                                    expr ConstEigenVectorArrayMap<T>(b, N);   \
  }                                                                           \
  /* inplace versions */                                                      \
  template <>                                                                 \
  void Funcname##ToRow<T, CPUMathUtil>(                                       \
      const int M, const int N, const T* x, T* y, CPUMathUtil*) {             \
    EigenArrayMap<T>(y, N, M).colwise() expr## =                              \
        ConstEigenVectorArrayMap<T>(x, N);                                    \
  }                                                                           \
  template <>                                                                 \
  void Funcname##ToCol<T, CPUMathUtil>(                                       \
      const int M, const int N, const T* x, T* y, CPUMathUtil*) {             \
    EigenArrayMap<T>(y, N, M).rowwise() expr## =                              \
        ConstEigenVectorArrayMap<T>(x, M).transpose();                        \
  }

#define DEFINE_BROADCAST_BINARY_FUNCTION(name, op)      \
  DELEGATE_BROADCAST_BINARY_FUNCTION(int32_t, name, op) \
  DELEGATE_BROADCAST_BINARY_FUNCTION(int64_t, name, op) \
  DELEGATE_BROADCAST_BINARY_FUNCTION(float, name, op)

DEFINE_BROADCAST_BINARY_FUNCTION(Add, +)
DEFINE_BROADCAST_BINARY_FUNCTION(Sub, -)
DEFINE_BROADCAST_BINARY_FUNCTION(Mul, *)
DEFINE_BROADCAST_BINARY_FUNCTION(Div, /)

#undef DEFINE_BROADCAST_BINARY_FUNCTION
#undef DELEGATE_BROADCAST_BINARY_FUNCTION

#define SPECIALIZED_SET(T)                                                       \
  template <>                                                                    \
  void Set<T, CPUMathUtil>(const int64_t N, const T alpha, T* Y, CPUMathUtil*) { \
    if (alpha == (T)0) {                                                         \
      memset(Y, 0, N * sizeof(T));                                               \
    } else {                                                                     \
      EigenVectorMap<T>(Y, N).setConstant(alpha);                                \
    }                                                                            \
  }

SPECIALIZED_SET(float);
SPECIALIZED_SET(double);
SPECIALIZED_SET(int8_t);
SPECIALIZED_SET(int16_t);
SPECIALIZED_SET(int32_t);
SPECIALIZED_SET(int64_t);
SPECIALIZED_SET(bool);
SPECIALIZED_SET(char);
SPECIALIZED_SET(uint8_t);
SPECIALIZED_SET(uint16_t);
#undef SPECIALIZED_SET

#define INSTANTIATE_BINARY_OP(name, op, T)                          \
  template <>                                                       \
  void name<T, CPUMathUtil>(                                        \
      const int n, const T* a, const T* b, bool* y, CPUMathUtil*) { \
    for (int i = 0; i < n; ++i) {                                   \
      y[i] = a[i] op b[i];                                          \
    }                                                               \
  }                                                                 \
  template <>                                                       \
  void name##ToRow<T, CPUMathUtil>(                                 \
      const int m,                                                  \
      const int n,                                                  \
      const T* a,                                                   \
      const T* b,                                                   \
      bool* y,                                                      \
      CPUMathUtil*) {                                               \
    for (int i = 0; i < n * m; ++i) {                               \
      y[i] = a[i] op b[i % n];                                      \
    }                                                               \
  }

#define DEFINE_BINARY_OP(name, op)         \
  INSTANTIATE_BINARY_OP(name, op, float)   \
  INSTANTIATE_BINARY_OP(name, op, int32_t) \
  INSTANTIATE_BINARY_OP(name, op, int64_t)

DEFINE_BINARY_OP(LT, <);
DEFINE_BINARY_OP(LE, <=);
DEFINE_BINARY_OP(GT, >);
DEFINE_BINARY_OP(GE, >=);

INSTANTIATE_BINARY_OP(Or, |, bool);
INSTANTIATE_BINARY_OP(And, &, bool);
INSTANTIATE_BINARY_OP(Xor, ^, bool);

template <>
void Not<bool, CPUMathUtil>(
    const int n,
    const bool* x,
    bool* y,
    CPUMathUtil* /*context*/) {
  for (int i = 0; i < n; ++i) {
    y[i] = !x[i];
  }
}

#undef DEFINE_BINARY_OP
#undef INSTANTIATE_BINARY_OP

#define SPECIALIZED_CPU_ADD_STRIPED_BATCH(T)                      \
  template <>                                                     \
  void AddStripedBatch(                                           \
      const int N,                                                \
      const T* first,                                             \
      T* y,                                                       \
      const int stripe,                                           \
      const int batch,                                            \
      CPUMathUtil* provider) {                                    \
    for (int j = 0; j < batch; j++) {                             \
      Add<T, CPUMathUtil>(N, first + j * stripe, y, y, provider); \
    }                                                             \
  }

SPECIALIZED_CPU_ADD_STRIPED_BATCH(float);
#undef SPECIALIZED_CPU_ADD_STRIPED_BATCH

template <>
void RandUniform<float, CPUMathUtil>(
    const int n, const float a, const float b, float* r,
    CPUMathUtil* /*provider*/) {
  std::uniform_real_distribution<float> distribution(a, b);
  //todo: need implmenet "RandGenerator()" in execution provider
  ORT_UNUSED_PARAMETER(n);
  ORT_UNUSED_PARAMETER(r);
  ORT_NOT_IMPLEMENTED(__FUNCTION__, " is not implemented");
  /*for (int i = 0; i < n; ++i) {
                r[i] = distribution(context->RandGenerator());
            }*/
}

template <>
void RandUniform<int, CPUMathUtil>(
    const int n, const int a, const int b, int* r,
    CPUMathUtil* /*provider*/) {
  std::uniform_int_distribution<int> distribution(a, b);
  //todo: need implmenet "RandGenerator()" in execution provider
  ORT_UNUSED_PARAMETER(n);
  ORT_UNUSED_PARAMETER(r);
  ORT_NOT_IMPLEMENTED(__FUNCTION__, " is not implemented");
  /*for (int i = 0; i < n; ++i) {
                r[i] = distribution(context->RandGenerator());
            }*/
}

//todo: need implmenet "RandGenerator()" in execution provider

//#define CAFFE2_SPECIALIZED_RAND_UNIFORM_UNIQUE(T)                      \
//  template <>                                                          \
//  void RandUniformUnique<T, CPUContext>(                               \
//      const size_t n,                                                  \
//      const T a,                                                       \
//      const T b,                                                       \
//      T* r,                                                            \
//      const size_t m,                                                  \
//      const T* avoid,                                                  \
//      CPUContext* context) {                                           \
//    CAFFE_ENFORCE_LE(                                                  \
//        n, b - a - m + 1, "Cannot satisfy the unique requirement");    \
//    std::unordered_set<T> avoid_set(n);                                \
//    if (m) {                                                           \
//      avoid_set.insert(avoid, avoid + m);                              \
//      CAFFE_ENFORCE_EQ(m, avoid_set.size(), "Avoid should be unique"); \
//    }                                                                  \
//    std::uniform_int_distribution<T> distribution(a, b);               \
//    T v = 0;                                                           \
//    for (size_t i = 0; i < n; ++i) {                                   \
//      do {                                                             \
//        v = distribution(context->RandGenerator());                    \
//      } while (avoid_set.count(v));                                    \
//      r[i] = v;                                                        \
//      avoid_set.insert(v);                                             \
//    }                                                                  \
//  }
//
//        CAFFE2_SPECIALIZED_RAND_UNIFORM_UNIQUE(int32_t);
//        CAFFE2_SPECIALIZED_RAND_UNIFORM_UNIQUE(int64_t);
//#undef CAFFE2_SPECIALIZED_RAND_UNIFORM_UNIQUE

template <>
void RandGaussian<float, CPUMathUtil>(
    const int n, const float mean, const float std, float* r,
    CPUMathUtil* /*provider*/) {
  std::normal_distribution<float> distribution(mean, std);
  ORT_UNUSED_PARAMETER(n);
  ORT_UNUSED_PARAMETER(r);
  ORT_NOT_IMPLEMENTED(__FUNCTION__, " is not implemented");
  /*for (int i = 0; i < n; ++i) {
                r[i] = distribution(context->RandGenerator());
            }*/
}

#define SPECIALIZED_SUM(T)                   \
  template <>                                \
  void Sum<T, CPUMathUtil>(                  \
      const int N,                           \
      const T* x,                            \
      T* y,                                  \
      CPUMathUtil* /* unused */,             \
      Tensor* /* unused */) {                \
    *y = ConstEigenVectorMap<T>(x, N).sum(); \
  }

SPECIALIZED_SUM(float);
SPECIALIZED_SUM(int32_t);
SPECIALIZED_SUM(int64_t);

#undef SPECIALIZED_SUM

template <>
void SumSqr<float, CPUMathUtil>(
    const int N,
    const float* x,
    float* y,
    CPUMathUtil* /*context*/ /* unused */,
    Tensor* /*scratch_ptr*/ /* unused */) {
  *y = ConstEigenVectorMap<float>(x, N).squaredNorm();
}

template <>
void Select<float, CPUMathUtil>(
    const int N,
    const int D,
    const float* x,
    const int* idx,
    float* y,
    CPUMathUtil* /*context*/) {
  for (int i = 0; i < N; ++i) {
    ORT_ENFORCE(idx[i] < D);
    y[i] = x[i * D + idx[i]];
  }
}

template <>
void Col2imNd<float, CPUMathUtil, StorageOrder::NCHW>(
    const float* data_col,
    const int64_t* img_shape,
    const int64_t* col_shape,
    const int64_t img_size,
    const int64_t col_size,
    const int64_t* kernel_shape,
    const int64_t* stride,
    const int64_t* dilation,
    const int64_t* pad,
    const int64_t N,
    float* data_img,
    CPUMathUtil* context) {
  Set<float, CPUMathUtil>(img_size, 0, data_img, context);
  Im2colNd<float, CPUMathUtil, StorageOrder::NCHW>()(
      data_col,
      img_shape,
      col_shape,
      img_size,
      col_size,
      kernel_shape,
      stride,
      dilation,
      pad,
      N,
      data_img,
      context,
      true);
}

static void Im2colWithEqualPadding(int64_t output_h, int64_t output_w, const float* data_im,
                                   const int64_t channels,
                                   const int64_t height,
                                   const int64_t width,
                                   const int64_t kernel_h,
                                   const int64_t kernel_w,
                                   const int64_t dilation_h,
                                   const int64_t dilation_w,
                                   const int64_t pad_t,
                                   const int64_t pad_l,
                                   const int64_t stride_h,
                                   const int64_t stride_w,
                                   float* data_col) {
  // From Intel, https://github.com/BVLC/caffe/pull/3536
  const int64_t pad_h = pad_t;
  const int64_t pad_w = pad_l;
  const int64_t channel_size = height * width;
  for (int64_t channel = channels; channel--; data_im += channel_size) {
    for (int64_t kernel_row = 0; kernel_row < kernel_h; kernel_row++) {
      for (int64_t kernel_col = 0; kernel_col < kernel_w; kernel_col++) {
        int64_t input_row = -pad_h + kernel_row * dilation_h;
        for (int64_t output_rows = output_h; output_rows; output_rows--) {
          if (!is_a_ge_zero_and_a_lt_b(input_row, height)) {
            memset(data_col, 0, output_w * sizeof(float));
            data_col += output_w;
          } else {
            int64_t input_col = -pad_w + kernel_col * dilation_w;
            const float* rdptr = data_im + input_row * width + input_col;
            for (int64_t i = 0; i != output_w; ++i) {
              if (is_a_ge_zero_and_a_lt_b(input_col, width)) {
                *(data_col++) = rdptr[i * stride_w];
              } else {
                *(data_col++) = 0;
              }
              input_col += stride_w;
            }
          }
          input_row += stride_h;
        }
      }
    }
  }
}
template <>
void Im2col<float, CPUMathUtil, StorageOrder::NCHW>(
    const float* data_im,
    const int64_t channels,
    const int64_t height,
    const int64_t width,
    const int64_t kernel_h,
    const int64_t kernel_w,
    const int64_t dilation_h,
    const int64_t dilation_w,
    const int64_t pad_t,
    const int64_t pad_l,
    const int64_t pad_b,
    const int64_t pad_r,
    const int64_t stride_h,
    const int64_t stride_w,
    float* data_col,
    CPUMathUtil* /*context*/) {
  const int64_t output_h =
      (height + pad_b + pad_t - (dilation_h * (kernel_h - 1) + 1)) / stride_h +
      1;
  const int64_t output_w =
      (width + pad_l + pad_r - (dilation_w * (kernel_w - 1) + 1)) / stride_w +
      1;

  // Fast path for zero padding and no dilation
  // From Torch, THNN_(unfolded_copy)
  if (dilation_h == 1 && dilation_w == 1 && pad_l == 0 && pad_r == 0 &&
      pad_t == 0 && pad_b == 0) {
    for (auto k = 0; k < channels * kernel_h * kernel_w; k++) {
      const auto nip = k / (kernel_h * kernel_w);
      const auto rest = k % (kernel_h * kernel_w);
      const auto kh = rest / kernel_w;
      const auto kw = rest % kernel_w;
      auto* dst = data_col + nip * (kernel_h * kernel_w * output_h * output_w) +
                  kh * (kernel_w * output_h * output_w) + kw * (output_h * output_w);
      const auto* src = data_im + nip * (height * width);
      for (auto y = 0; y < output_h; y++) {
        const auto iy = y * stride_h + kh;
        const auto ix = kw;
        if (stride_w == 1) {
          memcpy(
              dst + (y * output_w),
              src + (iy * width + ix),
              sizeof(float) * output_w);
        } else {
          for (auto x = 0; x < output_w; x++) {
            memcpy(
                dst + (y * output_w + x),
                src + (iy * width + ix + x * stride_w),
                sizeof(float));
          }
        }
      }
    }
    return;
  }

  // Fast path for equal padding
  if (pad_l == pad_r && pad_t == pad_b) {
    Im2colWithEqualPadding(output_h, output_w, data_im, channels, height, width, kernel_h, kernel_w, dilation_h, dilation_w, pad_t, pad_l, stride_h, stride_w, data_col);
    return;
  }

  // Baseline
  const int64_t dkernel_h = dilation_h * (kernel_h - 1) + 1;
  const int64_t dkernel_w = dilation_w * (kernel_w - 1) + 1;

  int64_t height_col = (height + pad_t + pad_b - dkernel_h) / stride_h + 1;
  int64_t width_col = (width + pad_l + pad_r - dkernel_w) / stride_w + 1;

  int64_t channels_col = channels * kernel_h * kernel_w;
  for (int64_t c = 0; c < channels_col; ++c) {
    int64_t w_offset = c % kernel_w;
    int64_t h_offset = (c / kernel_w) % kernel_h;
    int64_t c_im = c / kernel_h / kernel_w;
    for (int64_t h = 0; h < height_col; ++h) {
      for (int64_t w = 0; w < width_col; ++w) {
        int64_t h_pad = h * stride_h - pad_t + h_offset * dilation_h;
        int64_t w_pad = w * stride_w - pad_l + w_offset * dilation_w;
        if (h_pad >= 0 && h_pad < height && w_pad >= 0 && w_pad < width)
          data_col[(c * height_col + h) * width_col + w] =
              data_im[(c_im * height + h_pad) * width + w_pad];
        else
          data_col[(c * height_col + h) * width_col + w] = 0;
      }
    }
  }
}

template <>
void Im2col<float, CPUMathUtil, StorageOrder::NHWC>(
    const float* data_im,
    const int64_t channels,
    const int64_t height,
    const int64_t width,
    const int64_t kernel_h,
    const int64_t kernel_w,
    const int64_t dilation_h,
    const int64_t dilation_w,
    const int64_t pad_t,
    const int64_t pad_l,
    const int64_t pad_b,
    const int64_t pad_r,
    const int64_t stride_h,
    const int64_t stride_w,
    float* data_col,
    CPUMathUtil* /*context*/) {
  const int64_t dkernel_h = dilation_h * (kernel_h - 1) + 1;
  const int64_t dkernel_w = dilation_w * (kernel_w - 1) + 1;

  int64_t height_col = (height + pad_t + pad_b - dkernel_h) / stride_h + 1;
  int64_t width_col = (width + pad_l + pad_r - dkernel_w) / stride_w + 1;

  int64_t h_pad = -pad_t;
  for (int64_t h = 0; h < height_col; ++h) {
    int64_t w_pad = -pad_l;
    for (int64_t w = 0; w < width_col; ++w) {
      for (int64_t ih = h_pad; ih < h_pad + dkernel_h; ih += dilation_h) {
        for (int64_t iw = w_pad; iw < w_pad + dkernel_w; iw += dilation_w) {
          if (ih >= 0 && ih < height && iw >= 0 && iw < width) {
            memcpy(data_col, data_im + (ih * width + iw) * channels,
                   sizeof(float) * channels);
          } else {
            // This should be simply padded with zero.
            memset(data_col, 0, sizeof(float) * channels);
          }
          data_col += channels;
        }
      }
      w_pad += stride_w;
    }
    h_pad += stride_h;
  }
}

template <>
void Col2im<float, CPUMathUtil, StorageOrder::NCHW>(
    const float* data_col,
    const int64_t channels,
    const int64_t height,
    const int64_t width,
    const int64_t kernel_h,
    const int64_t kernel_w,
    const int64_t dilation_h,
    const int64_t dilation_w,
    const int64_t pad_t,
    const int64_t pad_l,
    const int64_t pad_b,
    const int64_t pad_r,
    const int64_t stride_h,
    const int64_t stride_w,
    float* data_im,
    CPUMathUtil* context) {
  const int64_t output_h =
      (height + pad_b + pad_t - (dilation_h * (kernel_h - 1) + 1)) / stride_h +
      1;
  const int64_t output_w =
      (width + pad_l + pad_r - (dilation_w * (kernel_w - 1) + 1)) / stride_w +
      1;

  Set<float, CPUMathUtil>(height * width * channels, 0, data_im, context);

  // Fast path for zero padding and no dilation
  // From Torch, modified THNN_(unfolded_acc)
  if (dilation_h == 1 && dilation_w == 1 && pad_l == 0 && pad_r == 0 &&
      pad_t == 0 && pad_b == 0) {
    for (auto k = 0; k < channels * kernel_h * kernel_w; k++) {
      const auto nip = k / (kernel_h * kernel_w);
      const auto rest = k % (kernel_h * kernel_w);
      const auto kh = rest / kernel_w;
      const auto kw = rest % kernel_w;
      const auto* dst = data_col +
                        nip * (kernel_h * kernel_w * output_h * output_w) +
                        kh * (kernel_w * output_h * output_w) + kw * (output_h * output_w);
      auto* src = data_im + nip * (height * width);
      for (auto y = 0; y < output_h; y++) {
        const auto iy = y * stride_h + kh;
        const auto ix = kw;
        if (stride_w == 1) {
          auto offsrc = src + (iy * width + ix);
          const auto offdst = dst + (y * output_w);
          for (auto i = 0; i < output_w; ++i) {
            offsrc[i] += offdst[i];
          }
        } else {
          for (auto x = 0; x < output_w; x++) {
            auto offsrc = src + (iy * width + ix + x * stride_w);
            const auto offdst = dst + (y * output_w + x);
            *offsrc += *offdst;
          }
        }
      }
    }
    return;
  }

  // Fast path for equal padding
  if (pad_l == pad_r && pad_t == pad_b) {
    // From Intel, https://github.com/BVLC/caffe/pull/3536
    const int64_t pad_h = pad_t;
    const int64_t pad_w = pad_l;
    const int64_t channel_size = height * width;
    for (int64_t channel = channels; channel--; data_im += channel_size) {
      for (int64_t kernel_row = 0; kernel_row < kernel_h; kernel_row++) {
        for (int64_t kernel_col = 0; kernel_col < kernel_w; kernel_col++) {
          int64_t input_row = -pad_h + kernel_row * dilation_h;
          for (int64_t output_rows = output_h; output_rows; output_rows--) {
            if (!is_a_ge_zero_and_a_lt_b(input_row, height)) {
              data_col += output_w;
            } else {
              int64_t input_col = -pad_w + kernel_col * dilation_w;
              for (int64_t output_col = output_w; output_col; output_col--) {
                if (is_a_ge_zero_and_a_lt_b(input_col, width)) {
                  data_im[input_row * width + input_col] += *data_col;
                }
                data_col++;
                input_col += stride_w;
              }
            }
            input_row += stride_h;
          }
        }
      }
    }
    return;
  }

  // Fallback
  const int64_t dkernel_h = dilation_h * (kernel_h - 1) + 1;
  const int64_t dkernel_w = dilation_w * (kernel_w - 1) + 1;

  int64_t height_col = (height + pad_t + pad_b - dkernel_h) / stride_h + 1;
  int64_t width_col = (width + pad_l + pad_r - dkernel_w) / stride_w + 1;
  int64_t channels_col = channels * kernel_h * kernel_w;
  for (int64_t c = 0; c < channels_col; ++c) {
    int64_t w_offset = c % kernel_w;
    int64_t h_offset = (c / kernel_w) % kernel_h;
    int64_t c_im = c / kernel_h / kernel_w;
    for (int64_t h = 0; h < height_col; ++h) {
      for (int64_t w = 0; w < width_col; ++w) {
        int64_t h_pad = h * stride_h - pad_t + h_offset * dilation_h;
        int64_t w_pad = w * stride_w - pad_l + w_offset * dilation_w;
        if (h_pad >= 0 && h_pad < height && w_pad >= 0 && w_pad < width) {
          data_im[(c_im * height + h_pad) * width + w_pad] +=
              data_col[(c * height_col + h) * width_col + w];
        }
      }
    }
  }
}

template <>
void Col2im<float, CPUMathUtil, StorageOrder::NHWC>(
    const float* data_col,
    const int64_t channels,
    const int64_t height,
    const int64_t width,
    const int64_t kernel_h,
    const int64_t kernel_w,
    const int64_t dilation_h,
    const int64_t dilation_w,
    const int64_t pad_t,
    const int64_t pad_l,
    const int64_t pad_b,
    const int64_t pad_r,
    const int64_t stride_h,
    const int64_t stride_w,
    float* data_im,
    CPUMathUtil* context) {
  const int64_t dkernel_h = dilation_h * (kernel_h - 1) + 1;
  const int64_t dkernel_w = dilation_w * (kernel_w - 1) + 1;

  Set<float, CPUMathUtil>(height * width * channels, 0, data_im, context);
  int64_t height_col = (height + pad_t + pad_b - dkernel_h) / stride_h + 1;
  int64_t width_col = (width + pad_l + pad_r - dkernel_w) / stride_w + 1;
  int64_t h_pad = -pad_t;
  for (int64_t h = 0; h < height_col; ++h) {
    int64_t w_pad = -pad_l;
    for (int64_t w = 0; w < width_col; ++w) {
      for (int64_t ih = h_pad; ih < h_pad + dkernel_h; ih += dilation_h) {
        for (int64_t iw = w_pad; iw < w_pad + dkernel_w; iw += dilation_w) {
          if (ih >= 0 && ih < height && iw >= 0 && iw < width) {
            auto* data_im_patch = data_im + (ih * width + iw) * channels;
            Add<float, CPUMathUtil>(
                static_cast<int>(channels), data_im_patch, data_col, data_im_patch, context);
          }
          data_col += channels;
        }
      }
      w_pad += stride_w;
    }
    h_pad += stride_h;
  }
}


#define SPECIALIZED_COPYVECTOR(T)                                    \
  template <>                                                        \
  void CopyVector<T, CPUMathUtil>(                                   \
      const int N, const T* src, T* dst, CPUMathUtil* /*context*/) { \
    if (src != dst && N > 0) {                                       \
      memcpy(dst, src, sizeof(T) * N);                               \
    }                                                                \
  }
SPECIALIZED_COPYVECTOR(float)
#undef SPECIALIZED_COPYVECTOR

uint32_t randomNumberSeed() {
  // Originally copied from folly::randomNumberSeed (at 418ad4)
  // modified to use chrono instead of sys/time.h
  static std::atomic<uint32_t> seedInput(0);
  auto tv = std::chrono::system_clock::now().time_since_epoch();
  uint64_t usec = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(tv).count());
  uint32_t tv_sec = static_cast<uint32_t>(usec / 1000000);
  uint32_t tv_usec = static_cast<uint32_t>(usec % 1000000);
  const uint32_t kPrime0 = 51551;
  const uint32_t kPrime1 = 61631;
  const uint32_t kPrime2 = 64997;
  const uint32_t kPrime3 = 111857;
  static const uint32_t pid = static_cast<uint32_t>(Env::Default().GetSelfPid());
  return kPrime0 * (seedInput++) + kPrime1 * pid +
         kPrime2 * tv_sec + kPrime3 * tv_usec;
}

uint16_t floatToHalf(float f) {
  return Eigen::half_impl::float_to_half_rtne(f).x;
}

float halfToFloat(uint16_t h) {
  return Eigen::half_impl::half_to_float(Eigen::half_impl::raw_uint16_to_half(h));
}

}  // namespace math
}  // namespace onnxruntime
