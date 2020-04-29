// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

// This is the provider DLL side of the provider API to let providers be built as a DLL

#include "provider_author.h"
#include <assert.h>
#include <mutex>

onnxruntime::ProviderHost* g_host{};

namespace onnxruntime {

void SetProviderHost(ProviderHost& host) {
  g_host = &host;
}

struct OnUnloadFunction {
  OnUnloadFunction(std::function<void()> function) : function_(std::move(function)) {}

  std::function<void()> function_;
  bool enabled_{true};
};

static std::unique_ptr<std::vector<std::unique_ptr<OnUnloadFunction>>> s_run_on_unload_;

RunOnUnload::RunOnUnload(std::function<void()> deleter) {
  static std::mutex mutex;
  std::lock_guard<std::mutex> guard{mutex};
  if (!s_run_on_unload_)
    s_run_on_unload_ = onnxruntime::make_unique<std::vector<std::unique_ptr<OnUnloadFunction>>>();
  auto unload_function = std::make_unique<OnUnloadFunction>(std::move(deleter));
  enabled_ = &unload_function->enabled_;
  s_run_on_unload_->push_back(std::move(unload_function));
}

RunOnUnload::~RunOnUnload() {
  *enabled_ = false;  // If the thread_local gets destroyed, then disalble the delete function
}

struct OnUnload {
  ~OnUnload() {
    if (!s_run_on_unload_)
      return;

    for (auto& function : *s_run_on_unload_) {
      if (function->enabled_)
        function->function_();
    }

    s_run_on_unload_.reset();
  }

} g_on_unload;

}  // namespace onnxruntime

// Override default new/delete so that we match the host's allocator
void* operator new(size_t n) { return g_host->HeapAllocate(n); }
void operator delete(void* p) { return g_host->HeapFree(p); }
void operator delete(void* p, size_t /*size*/) { return g_host->HeapFree(p); }

namespace onnx {
std::unique_ptr<ONNX_NAMESPACE::Prov_AttributeProto> Prov_AttributeProto::Create() {
  return g_host->AttributeProto_Create();
}
}  // namespace onnx

namespace onnxruntime {

Prov_AllocatorPtr CreateAllocator(Prov_DeviceAllocatorRegistrationInfo& info, int16_t device_id) {
  return g_host->CreateAllocator(info, device_id);
}

std::unique_ptr<Prov_KernelDefBuilder> Prov_KernelDefBuilder::Create() {
  return g_host->KernelDefBuilder_Create();
}

std::shared_ptr<Prov_KernelRegistry> Prov_KernelRegistry::Create() {
  return g_host->KernelRegistry_Create();
}

std::unique_ptr<Prov_OrtMemoryInfo> Prov_OrtMemoryInfo::Create(const char* name_, OrtAllocatorType type_, Prov_OrtDevice* device_, int id_, OrtMemType mem_type_) {
  return g_host->OrtMemoryInfo_Create(name_, type_, device_, id_, mem_type_);
}

std::unique_ptr<Prov_IndexedSubGraph> Prov_IndexedSubGraph::Create() {
  return g_host->IndexedSubGraph_Create();
}

#if 0
	template <>
	MLDataType DataTypeImpl::GetType<bool>() {
		return nullptr;
	}

template <>
MLDataType DataTypeImpl::GetType<Tensor>() {
  return g_host->DataTypeImpl_GetType_Tensor();
}
#endif

template <>
MLDataType DataTypeImpl::GetType<float>() {
  return g_host->DataTypeImpl_GetType_float();
}

#if 0

	template <>
	MLDataType DataTypeImpl::GetType<double>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<uint8_t>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<int8_t>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<int16_t>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<uint16_t>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<int32_t>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<uint32_t>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<int64_t>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<uint64_t>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<BFloat16>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<MLFloat16>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<std::string>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetType<std::vector<std::map<int64_t, float>>>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetType<std::vector<std::map<std::string, float>>>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetType<std::map<int64_t, double>>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetType<std::map<std::string, double>>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetType<std::map<std::string, float>>() {
		return nullptr;
	}

	template <>
	MLDataType DataTypeImpl::GetType<std::map<std::string, int64_t>>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetType<std::map<int64_t, float>>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetType<std::map<int64_t, std::string>>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<bool>() { return nullptr; }
#endif

template <>
MLDataType DataTypeImpl::GetTensorType<float>() {
  return g_host->DataTypeImpl_GetTensorType_float();
}

#if 0
	template <>
	MLDataType DataTypeImpl::GetTensorType<double>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<int8_t>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<uint8_t>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<int16_t>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<uint16_t>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<int32_t>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<uint32_t>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<int64_t>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<uint64_t>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<BFloat16>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<MLFloat16>() { return nullptr; }

	template <>
	MLDataType DataTypeImpl::GetTensorType<std::string>() { return nullptr; }
#endif

TensorShape::TensorShape(const int64_t* dimension_sizes, size_t dimension_count)
    : std::vector<int64_t>(dimension_count) {
  for (size_t i = 0; i < dimension_count; ++i) {
    (*this)[i] = dimension_sizes[i];
  }
}

TensorShape::TensorShape(const std::vector<int64_t>& dims, size_t start, size_t end) {
  assign(dims.begin() + start, dims.begin() + end);
}

int64_t TensorShape::Size() const {
  size_t arraySize = size();
  int64_t size = SizeHelper(0, arraySize);
  //should we cache the size? as multiple operation may be expensive.
  return size;
}

int64_t TensorShape::SizeHelper(size_t start, size_t end) const {
  // Must return 1 for an empty sequence
  int64_t size = 1;
  for (size_t i = start; i < end; i++) {
    if ((*this)[i] < 0) return -1;
    size *= (*this)[i];
  }
  return size;
}

TensorShape TensorShape::Slice(size_t dimstart, size_t dimend) const {
  assert(dimstart <= dimend && dimend <= size());  // "Invalid tensor shape slice argument."
  return TensorShape(*this, dimstart, dimend);
}

TensorShape TensorShape::Slice(size_t dimstart) const {
  return Slice(dimstart, size());
}

std::string TensorShape::ToString() const {
  PROVIDER_NOT_IMPLEMENTED
  return "";
}

CPUIDInfo g_info;

const CPUIDInfo& CPUIDInfo::GetCPUIDInfo() {
  return g_info;
}

bool CPUIDInfo::HasAVX2() const {
  return g_host->CPU_HasAVX2();
}

bool CPUIDInfo::HasAVX512f() const {
  return g_host->CPU_HasAVX512f();
}

Prov_AllocatorPtr CreateAllocator(Prov_DeviceAllocatorRegistrationInfo info, int16_t device_id) {
  return g_host->CreateAllocator(info, device_id);
}

std::unique_ptr<Prov_IDeviceAllocator> CreateCPUAllocator(std::unique_ptr<Prov_OrtMemoryInfo> info) {
  return g_host->CreateCPUAllocator(std::move(info));
}

Prov_AllocatorPtr CreateDummyArenaAllocator(Prov_AllocatorPtr resource_allocator) {
  PROVIDER_NOT_IMPLEMENTED
  ORT_UNUSED_PARAMETER(resource_allocator);
  return nullptr;
}

Prov_IExecutionProvider::Prov_IExecutionProvider(const std::string& type) {
  p_ = g_host->Create_IExecutionProvider_Router(this, type).release();
}

namespace logging {

bool Logger::OutputIsEnabled(Severity severity, DataType data_type) const noexcept {
  ORT_UNUSED_PARAMETER(severity);
  ORT_UNUSED_PARAMETER(data_type);
  return false;
  // TODO: Logging not essential to make it work initially, do later
}

static Logger g_default_logger;

const Logger& LoggingManager::DefaultLogger() {
  return g_default_logger;
}

Capture::Capture(const Logger& logger, logging::Severity severity, const char* category,
                 logging::DataType dataType, const CodeLocation& location) {
  PROVIDER_NOT_IMPLEMENTED
  ORT_UNUSED_PARAMETER(logger);
  ORT_UNUSED_PARAMETER(severity);
  ORT_UNUSED_PARAMETER(category);
  ORT_UNUSED_PARAMETER(dataType);
  ORT_UNUSED_PARAMETER(location);
}

std::ostream& Capture::Stream() noexcept {
  PROVIDER_NOT_IMPLEMENTED
  return *(std::ostream*)nullptr;
}

const char* Category::onnxruntime = "foo";

}  // namespace logging

namespace common {

Status::Status(StatusCategory category, int code, const std::string& msg) {
  PROVIDER_NOT_IMPLEMENTED
  ORT_UNUSED_PARAMETER(category);
  ORT_UNUSED_PARAMETER(code);
  ORT_UNUSED_PARAMETER(msg);
}

Status::Status(StatusCategory category, int code, const char* msg) {
  PROVIDER_NOT_IMPLEMENTED
  ORT_UNUSED_PARAMETER(category);
  ORT_UNUSED_PARAMETER(code);
  ORT_UNUSED_PARAMETER(msg);
}

std::string Status::ToString() const {
  PROVIDER_NOT_IMPLEMENTED
  return "";
}

const std::string& Status::ErrorMessage() const noexcept {
  PROVIDER_NOT_IMPLEMENTED
  static std::string dummy;
  return dummy;
}

}  // namespace common

std::vector<std::string> GetStackTrace() {
  PROVIDER_NOT_IMPLEMENTED
  return {};
}

void LogRuntimeError(uint32_t session_id, const common::Status& status, const char* file, const char* function, uint32_t line) {
  return g_host->LogRuntimeError(session_id, status, file, function, line);
}

}  // namespace onnxruntime
