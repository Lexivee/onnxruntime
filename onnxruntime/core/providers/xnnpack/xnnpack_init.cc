
#include "xnnpack_init.h"

#include "core/framework/allocatormgr.h"
#include "core/graph/constants.h"

#include "xnnpack.h"

namespace onnxruntime {
namespace xnnpack {
namespace {
void* xnn_allocate(void* context, size_t size) {
  IAllocator* allocator = (*reinterpret_cast<AllocatorPtr*>(context)).get();
  return allocator->Alloc(size);
}

void* xnn_reallocate(void* context, void* pointer, size_t size) {
  if (pointer == nullptr) {
    return xnn_allocate(context, size);
  }
  ORT_NOT_IMPLEMENTED("xnn_reallocate is not implemented");
}

void xnn_deallocate(void* context, void* pointer) {
  if (pointer != nullptr) {
    IAllocator* allocator = (*reinterpret_cast<AllocatorPtr*>(context)).get();
    allocator->Free(pointer);
  }
}

void* xnn_aligned_allocate(void* context, size_t alignment, size_t size) {
#if defined(__wasm__) && !defined(__wasm_relaxed_simd__) && !defined(__wasm_simd128__)
  ORT_ENFORCE(alignment <= 2 * sizeof(void*));
  return xnn_allocate(context, size);
#else
  void* ptr = xnn_allocate(context, size);
  ORT_ENFORCE((int64_t(ptr) & (alignment - 1)) == 0,
              " xnnpack wants to allocate a space with ", alignment, "bytes aligned. But it's not satisfied");
  // if ptr is not aligned, we have to find a way to return a aligned ptr and store the original ptr
  return ptr;
#endif
}

void xnn_aligned_deallocate(void* context, void* pointer) {
  return xnn_deallocate(context, pointer);
}
}  // namespace

std::pair<AllocatorPtr, xnn_allocator*> GetOrCreateAllocator() {
  // create our allocator
  AllocatorCreationInfo const allocator_info(
      [](int) {
        // lazy create the allocator
        return std::make_unique<CPUAllocator>(OrtMemoryInfo(kXnnpackExecutionProvider,
                                                            OrtAllocatorType::OrtDeviceAllocator));
      });
  // thread safe and create only once
  static AllocatorPtr ort_allocator = CreateAllocator(allocator_info);
  static xnn_allocator xnn_allocator_wrapper_ = {&ort_allocator,
                                                 xnn_allocate,
                                                 xnn_reallocate,
                                                 xnn_deallocate,
                                                 xnn_aligned_allocate,
                                                 xnn_aligned_deallocate};
  return {ort_allocator, &xnn_allocator_wrapper_};
}

}  // namespace xnnpack
}  // namespace onnxruntime
