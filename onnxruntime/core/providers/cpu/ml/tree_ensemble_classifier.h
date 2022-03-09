// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "tree_ensemble_common.h"

namespace onnxruntime {
namespace ml {
template <typename T, typename TH=T, typename TO=float>
class TreeEnsembleClassifier final : public OpKernel {
 public:
  explicit TreeEnsembleClassifier(const OpKernelInfo& info);
  common::Status Compute(OpKernelContext* context) const override;

 private:
  detail::TreeEnsembleCommonClassifier<T, TH> tree_ensemble_;
};
}  // namespace ml
}  // namespace onnxruntime
