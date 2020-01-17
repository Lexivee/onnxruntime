#include "pch.h"

#include "OnnxruntimeEngine.h"
#include "OnnxruntimeEngineBuilder.h"
#include "OnnxruntimeCpuSessionBuilder.h"
#include "OnnxruntimeDmlSessionBuilder.h"

#include "core/providers/winml/winml_provider_factory.h"

using namespace WinML;

HRESULT OnnxruntimeEngineBuilder::RuntimeClassInitialize(OnnxruntimeEngineFactory* engine_factory) {
  engine_factory_ = engine_factory;
  return S_OK;
}

STDMETHODIMP OnnxruntimeEngineBuilder::CreateEngine(Windows::AI::MachineLearning::IEngine** out) {
  auto ort_api = engine_factory_->UseOrtApi();

  Microsoft::WRL::ComPtr<IOrtSessionBuilder> onnxruntime_session_builder;

  if (device_ == nullptr) {
    RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<OnnxruntimeCpuSessionBuilder>(&onnxruntime_session_builder, engine_factory_.Get()));
  }
  else {
    RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<OnnxruntimeDmlSessionBuilder>(&onnxruntime_session_builder, engine_factory_.Get(), device_.Get(), queue_.Get()));
  }

  OrtSessionOptions* ort_options;
  RETURN_IF_FAILED(onnxruntime_session_builder->CreateSessionOptions(&ort_options));
  auto session_options = UniqueOrtSessionOptions(ort_options, ort_api->ReleaseSessionOptions);
  
  if (batch_size_override_.has_value()) {
    constexpr const char* DATA_BATCH = "DATA_BATCH";
    ort_api->AddFreeDimensionOverride(session_options.get(), DATA_BATCH, batch_size_override_.value());
  }

  OrtSession* ort_session = nullptr;
  onnxruntime_session_builder->CreateSession(session_options.get(), &ort_session);
  auto session = UniqueOrtSession(ort_session, ort_api->ReleaseSession);

  Microsoft::WRL::ComPtr<OnnxruntimeEngine> onnxruntime_engine;
  RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<OnnxruntimeEngine>(&onnxruntime_engine,
	  engine_factory_.Get(), std::move(session), onnxruntime_session_builder.Get()));
  RETURN_IF_FAILED(onnxruntime_engine.CopyTo(out));
  return S_OK;
}

STDMETHODIMP OnnxruntimeEngineBuilder::GetD3D12Device(ID3D12Device** device) {
  *device = device_.Get();
  return S_OK;
}

STDMETHODIMP OnnxruntimeEngineBuilder::SetD3D12Device(ID3D12Device* device) {
  device_ = device;
  return S_OK;
}

STDMETHODIMP OnnxruntimeEngineBuilder::GetID3D12CommandQueue(ID3D12CommandQueue** queue) {
  *queue = queue_.Get();
  return S_OK;
}

STDMETHODIMP OnnxruntimeEngineBuilder::SetID3D12CommandQueue(ID3D12CommandQueue* queue) {
  queue_ = queue;
  return S_OK;
}

STDMETHODIMP OnnxruntimeEngineBuilder::SetBatchSizeOverride(uint32_t batch_size_override) {
  batch_size_override_ = batch_size_override;
  return S_OK;
}