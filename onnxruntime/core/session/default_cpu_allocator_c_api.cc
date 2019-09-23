// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <atomic>
#include "core/framework/utils.h"
#include "core/session/onnxruntime_cxx_api.h"
#include "core/session/ort_apis.h"
#include <assert.h>

// In the future we'll have more than one allocator type. Since all allocators are of type 'OrtAllocator' and there is a single
// OrtReleaseAllocator function, we need to have a common base type that lets us delete them.
struct OrtAllocatorImpl : OrtAllocator {
  virtual ~OrtAllocatorImpl() = default;
};

// This replaces the macro that's in onnxruntime_cxx_inline.h but uses the internal symbols vs the public API interface
#undef ORT_THROW_ON_ERROR
#define ORT_THROW_ON_ERROR(expr)                                           \
  if (OrtStatus* onnx_status = (expr)) {                                   \
    std::string ort_error_message = OrtApis::GetErrorMessage(onnx_status); \
    OrtErrorCode ort_error_code = OrtApis::GetErrorCode(onnx_status);      \
    OrtApis::ReleaseStatus(onnx_status);                                   \
    throw Ort::Exception(std::move(ort_error_message), ort_error_code);    \
  }

struct OrtDefaultAllocator : OrtAllocatorImpl {
  OrtDefaultAllocator() {
    OrtAllocator::version = ORT_API_VERSION;
    OrtAllocator::Alloc = [](OrtAllocator* this_, size_t size) { return static_cast<OrtDefaultAllocator*>(this_)->Alloc(size); };
    OrtAllocator::Free = [](OrtAllocator* this_, void* p) { static_cast<OrtDefaultAllocator*>(this_)->Free(p); };
    OrtAllocator::Info = [](const OrtAllocator* this_) { return static_cast<const OrtDefaultAllocator*>(this_)->Info(); };
    ORT_THROW_ON_ERROR(OrtApis::CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &cpu_memory_info));
  }

  ~OrtDefaultAllocator() override { OrtApis::ReleaseMemoryInfo(cpu_memory_info); }

  void* Alloc(size_t size) {
    return onnxruntime::utils::DefaultAlloc(size);
  }
  void Free(void* p) {
    onnxruntime::utils::DefaultFree(p);
  }
  const OrtMemoryInfo* Info() const {
    return cpu_memory_info;
  }

 private:
  OrtDefaultAllocator(const OrtDefaultAllocator&) = delete;
  OrtDefaultAllocator& operator=(const OrtDefaultAllocator&) = delete;

  OrtMemoryInfo* cpu_memory_info;
};

#define API_IMPL_BEGIN try {
#define API_IMPL_END                                                \
  }                                                                 \
  catch (std::exception & ex) {                                     \
    return OrtApis::CreateStatus(ORT_RUNTIME_EXCEPTION, ex.what()); \
  }

ORT_API_STATUS_IMPL(OrtApis::GetAllocatorWithDefaultOptions, _Out_ OrtAllocator** out) {
  API_IMPL_BEGIN
  static OrtDefaultAllocator ort_default_allocator;
  *out = &ort_default_allocator;
  return nullptr;
  API_IMPL_END
}
