// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/framework/op_kernel.h"
#include "core/framework/allocator.h"
#include "core/providers/xnnpack/detail/utils.h"

namespace onnxruntime {
class GraphViewer;
namespace xnnpack {

class Softmax final : public OpKernel {
 public:
  Softmax(const OpKernelInfo& info);

  Status Compute(OpKernelContext* ctx) const override;
  static bool IsSoftmaxOnnxNodeSupported(const NodeUnit& nodeunit, const GraphViewer& graph);

 private:
  int axis_;
  int opset_;
  OpComputeType op_type_ = OpComputeType::op_compute_type_invalid;
  XnnpackOperator op0_;
  QuantParam quant_param_;
};
}  // namespace xnnpack
}  // namespace onnxruntime
