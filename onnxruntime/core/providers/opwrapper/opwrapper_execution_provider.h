// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <string>
#include "core/framework/execution_provider.h"

namespace onnxruntime {

class OpWrapperExecutionProvider : public IExecutionProvider {
 public:
  explicit OpWrapperExecutionProvider(ProviderOptionsMap provider_options_map);

  OpWrapperExecutionProvider(const OpWrapperExecutionProvider& other) = default;
  OpWrapperExecutionProvider& operator=(const OpWrapperExecutionProvider& other) = default;
  OpWrapperExecutionProvider(OpWrapperExecutionProvider&& other) = default;
  OpWrapperExecutionProvider& operator=(OpWrapperExecutionProvider&& other) = default;

  ~OpWrapperExecutionProvider() override = default;

  ProviderOptions GetOpProviderOptions(const std::string& op_name) const;

 private:
  ProviderOptionsMap provider_options_map_;
};
}  // namespace onnxruntime
