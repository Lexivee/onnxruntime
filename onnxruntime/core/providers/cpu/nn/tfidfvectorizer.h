// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/common/common.h"
#include "core/framework/op_kernel.h"
#include <memory>
#include <vector>

namespace onnxruntime {

class TfIdfVectorizer final : public OpKernel {
 public:
  explicit TfIdfVectorizer(const OpKernelInfo& info);
  ~TfIdfVectorizer() override;
  ORT_DISALLOW_COPY_ASSIGNMENT_AND_MOVE(TfIdfVectorizer);

  Status Compute(OpKernelContext* ctx) const override;

 private:
  template <typename T>
  Status ComputeImpl(OpKernelContext* ctx, size_t B, size_t C, size_t total_items,
                     std::vector<uint32_t>& frequencies) const;

  // Apply weighing criteria and output
  void OutputResult(OpKernelContext* ctx, size_t b_dim, const std::vector<uint32_t>& frequences) const;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace onnxruntime
