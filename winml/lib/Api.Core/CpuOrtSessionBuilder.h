// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "inc/IOrtSessionBuilder.h"

namespace Windows::AI::MachineLearning {

class CpuOrtSessionBuilder : public IOrtSessionBuilder {
 public:
  HRESULT __stdcall CreateSessionOptions(
      onnxruntime::SessionOptions* p_options);

  HRESULT __stdcall CreateSession(
      const onnxruntime::SessionOptions& options,
      std::unique_ptr<onnxruntime::InferenceSession>* p_session,
      onnxruntime::IExecutionProvider** pp_provider);

  HRESULT __stdcall Initialize(
      onnxruntime::InferenceSession* p_session,
      onnxruntime::IExecutionProvider* p_provider);
};

}  // namespace Windows::AI::MachineLearning