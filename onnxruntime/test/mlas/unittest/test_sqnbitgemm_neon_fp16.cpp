/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Licensed under the MIT License.

Module Name:

    test_sqnbitgemm_neon_fp16.cpp

Abstract:

    Tests for MLAS n-bit int block quantized GEMM on ARM CPU with input A type T1 fp16.

--*/

#include <vector>

#include "test_util.h"
#include "core/mlas/lib/mlasi.h"

#if defined(MLAS_F16VEC_INTRINSICS_SUPPORTED) && defined(MLAS_TARGET_ARM64)

class MlasNeonFp16CastTest : public MlasTestBase {
private:

    void TestFp16ToFp32(size_t count) {
        std::vector<unsigned short> src(count);
        std::vector<float> dest(count);

        for (size_t i = 0; i < count; i++) {
            src[i] = static_cast<unsigned short>(i);
        }

        MlasCastF16ToF32KernelNeon(src.data(), dest.data(), count);

        for (size_t i = 0; i < count; i++) {
            if ((src[i] & 0x1c00) == 0x1c00) continue; // skip inf and nan
            ASSERT_EQ(dest[i], MLAS_FP16::FromBits(src[i]).ToFloat());
        }
    }

    void TestFp32ToFp16(size_t count) {
        std::vector<float> src(count);
        std::vector<unsigned short> dest(count);

        for (size_t i = 0; i < count; i++) {
            src[i] = static_cast<float>(i);
        }

        MlasCastF32ToF16KernelNeon(src.data(), dest.data(), count);

        for (size_t i = 0; i < count; i++) {
            ASSERT_EQ(dest[i], MLAS_FP16(src[i]).val);
        }
    }


public:
  static const char* GetTestSuiteName() {
    return "NeonFp16Cast";
  }

  void ExecuteShort(void) override {
    TestFp16ToFp32(1 << 16);
    TestFp32ToFp16((1 << 15) - 5);
  }
};


static UNUSED_VARIABLE bool added_to_main = AddTestRegister([](bool is_short_execute) {
  size_t count = 0;
  if (is_short_execute) {
    count += MlasDirectShortExecuteTests<MlasNeonFp16CastTest>::RegisterShortExecute();
  }
  return count;
});

#endif  // defined(MLAS_F16VEC_INTRINSICS_SUPPORTED) && defined(MLAS_TARGET_ARM64)
