// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/framework/allocator.h"

namespace onnxruntime {

class HIPAllocator : public IDeviceAllocator {
 public:
  HIPAllocator(int device_id, const char* name) : info_(name, OrtAllocatorType::OrtDeviceAllocator, OrtDevice(OrtDevice::GPU, OrtDevice::MemType::DEFAULT, device_id), device_id, OrtMemTypeDefault) {}
  virtual void* Alloc(size_t size) override;
  virtual void Free(void* p) override;
  virtual const OrtMemoryInfo& Info() const override;
  virtual FencePtr CreateFence(const SessionState* session_state) override;

 private:
  void CheckDevice() const;

 private:
  const OrtMemoryInfo info_;
};

//TODO: add a default constructor
class HIPPinnedAllocator : public IDeviceAllocator {
 public:
  HIPPinnedAllocator(int device_id, const char* name) : info_(name, OrtAllocatorType::OrtDeviceAllocator, OrtDevice(OrtDevice::CPU, OrtDevice::MemType::HIP_PINNED, device_id), device_id, OrtMemTypeCPUOutput) {}
  virtual void* Alloc(size_t size) override;
  virtual void Free(void* p) override;
  virtual const OrtMemoryInfo& Info() const override;
  virtual FencePtr CreateFence(const SessionState* session_state) override;

 private:
  const OrtMemoryInfo info_;
};

}  // namespace onnxruntime
