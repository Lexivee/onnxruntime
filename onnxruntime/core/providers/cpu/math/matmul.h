// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/common/common.h"
#include "core/framework/op_kernel.h"

namespace onnxruntime {

template <typename T>
class MatMul final : public OpKernel {
 public:
  MatMul(const OpKernelInfo& info)
      : OpKernel(info) {
  }

  Status Compute(OpKernelContext* context) const override;
};

}  // namespace onnxruntime
