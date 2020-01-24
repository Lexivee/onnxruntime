// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#pragma warning(push)
#pragma warning(disable : 4505)

namespace Windows::AI ::MachineLearning {

using UniqueOrtEnv = std::unique_ptr<OrtEnv, void (*)(OrtEnv*)>;

class OnnxruntimeEnvironment {
 public:
  OnnxruntimeEnvironment(const OrtApi* ort_api);

  HRESULT GetOrtEnvironment(_Out_ OrtEnv** ert_env);
  HRESULT EnableDebugOutput(bool is_enabled);

 private:
  UniqueOrtEnv ort_env_;
};

}  // namespace Windows::AI::MachineLearning

#pragma warning(pop)