// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#pragma once

#include "onnxruntime_c_api.h"
#include "onnxruntime_cxx_api.h"

namespace onnxruntime {
namespace server {
class MemBuffer {
 public:
  MemBuffer(void* buffer, size_t len, const OrtMemoryInfo& alloc_info)
      : buffer_(buffer), len_(len), alloc_info_(alloc_info) {}
  void* GetBuffer() const { return buffer_; }

  size_t GetLen() const { return len_; }
  const OrtMemoryInfo& GetAllocInfo() const { return alloc_info_; }

 private:
  void* const buffer_;
  const size_t len_;
  const OrtMemoryInfo& alloc_info_;
};
}  // namespace server
}  // namespace onnxruntime
