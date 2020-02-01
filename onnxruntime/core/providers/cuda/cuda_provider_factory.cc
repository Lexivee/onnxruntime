// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cuda/cuda_provider_factory.h"
#include <atomic>
#include "cuda_execution_provider.h"
#include "core/session/abi_session_options_impl.h"

using namespace onnxruntime;

namespace onnxruntime {

struct CUDAProviderFactory : IExecutionProviderFactory {
  CUDAProviderFactory(int device_id, size_t cuda_mem_limit = std::numeric_limits<size_t>::max()) : device_id_(device_id), cuda_mem_limit_(cuda_mem_limit) {}
  ~CUDAProviderFactory() override {}

  std::unique_ptr<IExecutionProvider> CreateProvider() override;

 private:
  int device_id_;
  size_t cuda_mem_limit_;
};

std::unique_ptr<IExecutionProvider> CUDAProviderFactory::CreateProvider() {
  CUDAExecutionProviderInfo info;
  info.device_id = device_id_;
  return onnxruntime::make_unique<CUDAExecutionProvider>(info, cuda_mem_limit_);
}

std::shared_ptr<IExecutionProviderFactory> CreateExecutionProviderFactory_CUDA(int device_id, size_t cuda_mem_limit) {
  return std::make_shared<onnxruntime::CUDAProviderFactory>(device_id, cuda_mem_limit);
}

}  // namespace onnxruntime

ORT_API_STATUS_IMPL(OrtSessionOptionsAppendExecutionProvider_CUDA, _In_ OrtSessionOptions* options, int device_id, size_t cuda_mem_limit = std::numeric_limits<size_t>::max()) {
  options->provider_factories.push_back(onnxruntime::CreateExecutionProviderFactory_CUDA(device_id, cuda_mem_limit));
  return nullptr;
}
