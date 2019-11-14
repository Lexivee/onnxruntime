// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "inc/WinMLAdapter.h"

namespace Windows::AI::MachineLearning {

class CpuOrtSessionBuilder : public Microsoft::WRL::RuntimeClass <
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    _winmla::IOrtSessionBuilder> {

 public:
     CpuOrtSessionBuilder();

  HRESULT STDMETHODCALLTYPE CreateSessionOptions(
      onnxruntime::SessionOptions* p_options) override;

  HRESULT STDMETHODCALLTYPE CreateSession(
      const onnxruntime::SessionOptions& options,
      _winmla::IInferenceSession** p_session,
      onnxruntime::IExecutionProvider** pp_provider) override;

  HRESULT STDMETHODCALLTYPE Initialize(
      _winmla::IInferenceSession* p_session,
      onnxruntime::IExecutionProvider* p_provider) override;
};

}  // namespace Windows::AI::MachineLearning