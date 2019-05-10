// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/common.h"
#include "core/framework/op_kernel.h"
#include "core/providers/cuda/cuda_common.h"
#include "core/providers/cpu/tensor/pad.h"

namespace onnxruntime {
namespace cuda {

template <typename T>
class Pad final : public PadBase<T>, public CudaKernel {
 public:
  Pad(const OpKernelInfo& info) : PadBase<T>(info), CudaKernel(info) {}

  Status ComputeInternal(OpKernelContext* context) const override;
};

}  // namespace cuda
}  // namespace onnxruntime
