// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "lora_adapters.h"
#include "lora_format_utils.h"

#include "core/session/onnxruntime_c_api.h"
#include "core/session/ort_apis.h"
#include "core/framework/error_code_helper.h"

#include <fstream>
#include <stdexcept>

namespace onnxruntime {
namespace lora {

LoraAdapter::LoraParam::LoraParam(std::string name, OrtValue ort_value) noexcept
    : name_(std::move(name)), ort_value_(std::move(ort_value)) {}

void LoraAdapter::Load(const std::filesystem::path& file_path) {
  auto buffer = utils::LoadLoraAdapterBytes(file_path);
  Load(std::move(buffer));
}

void LoraAdapter::Load(std::vector<uint8_t> buffer) {
  adapter_ = utils::ValidateAndGetAdapterFromBytes(buffer);
  buffer_.emplace<BufferHolder>(std::move(buffer));
  InitializeParamsValues();
}

void LoraAdapter::MemoryMap(const std::filesystem::path& file_path) {
  auto [mapped_memory, file_size] = utils::MemoryMapAdapterFile(file_path);
  auto u8_span = ReinterpretAsSpan<const uint8_t>(gsl::make_span(mapped_memory.get(), file_size));
  adapter_ = utils::ValidateAndGetAdapterFromBytes(u8_span);
  buffer_.emplace<MemMapHolder>(std::move(mapped_memory), file_size);

  InitializeParamsValues();
}

void LoraAdapter::InitializeParamsValues() {
  if (adapter_ == nullptr) {
    ORT_THROW("Adapter is not loaded yet.");
  }

  const auto* params = adapter_->parameters();
  InlinedHashMap<std::string, LoraParam> params_values;
  params_values.reserve(params->size());
  for (const auto* param : *params) {
    auto [name, ort_value] = utils::CreateOrtValueOverLoraParameter(*param);
    params_values.emplace(name, LoraParam(std::move(name), std::move(ort_value)));
  }
  params_values_.swap(params_values);
}

size_t LoraAdapter::GetBufferSize() const {
  if (std::holds_alternative<MemMapHolder>(buffer_)) {
    return std::get<1>(buffer_).file_size_;
  } else if (std::holds_alternative<BufferHolder>(buffer_)) {
    return std::get<2>(buffer_).buffer_.size();
  }
  ORT_THROW("Non-exhaustive visitor for BinaryFormatHolder::GetSize()");
}

}  // namespace lora
}  // namespace onnxruntime

ORT_API_STATUS_IMPL(OrtApis::CreateLoraAdapter, const ORTCHAR_T* adapter_file_path,
                    _Outptr_ OrtLoraAdapter** adapter) {
  API_IMPL_BEGIN
  auto lora_adapter = std::make_unique<onnxruntime::lora::LoraAdapter>();
  // For platforms that do not support Memmap, we can #ifdef it to ->Load(adapter_file_path)
  lora_adapter->Load(adapter_file_path);
  *adapter = reinterpret_cast<OrtLoraAdapter*>(lora_adapter.release());
  return nullptr;
  API_IMPL_END
}

ORT_API(void, OrtApis::ReleaseLoraAdapter, _Frees_ptr_opt_ OrtLoraAdapter* adapter) {
  delete reinterpret_cast<onnxruntime::lora::LoraAdapter*>(adapter);
}
