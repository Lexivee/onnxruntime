// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define PLATFORM_X86
#endif

#if defined(PLATFORM_X86)
#if defined(_MSC_VER)
#include <intrin.h>
#define DENORMAL_INTRINC
// intrins headers at gcc 4.8 and older are not usable without compiler flags
#elif defined(__GNUC__) && ((__GNUC__ >= 5) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 8)))
#include <cpuid.h>
#include <x86intrin.h>
#define DENORMAL_INTRINC
#endif
#endif

#include <mutex>

#include "core/common/common.h"
#include "core/common/cpuid_info.h"
#include "core/common/denormal.h"

namespace onnxruntime {

bool SetDenormalAsZero(bool on) {
#ifdef DENORMAL_INTRINC
  if (CPUIDInfo::GetCPUIDInfo().HasSSE3()) {
    if (on) {
      _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
      _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    } else {
      _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF);
      _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF);
    }
    return true;
  }
#else
  ORT_UNUSED_PARAMETER(on);
#endif
  return false;
}

#ifdef _OPENMP
class DenormalAsZeroInitializer {
 public:
  explicit DenormalAsZeroInitializer(bool on) : on_(on) {}

  DenormalAsZeroInitializer(const DenormalAsZeroInitializer& init) : on_(init.on_) {
    SetDenormalAsZero(on_);
  }

 private:
  bool on_;
};

void InitializeWithDenormalAsZero(bool on) {
  DenormalAsZeroInitializer init(on);
#pragma omp parallel for firstprivate(init)
  for (auto i = 0; i < 1; ++i) {
  }
}
#endif

}  // namespace onnxruntime
