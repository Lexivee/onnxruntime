// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/common.h"
#pragma once

namespace onnxruntime {
namespace cuda {

typedef struct {
  // Pointer to ONNX tensor's data on CPU.
  // It should be removed once we enable CUDA-aware MPI.
  void* buffer;
  // The size of buffer's content in bytes.
  int size;
  // Dst rank for Send and src rank for Recv.
  int rank;
  // Message's tag.
  int tag;
} CommInfo_t;

inline size_t GetAggregatedAlignedAddress(size_t old_addr) {
  constexpr size_t alignment = 256;
  size_t new_addr = (old_addr + alignment - 1) / alignment * alignment;
}

}  // namespace cuda
}  // namespace onnxruntime
