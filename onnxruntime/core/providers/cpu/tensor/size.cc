// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cpu/tensor/size.h"
#include <cassert>

namespace onnxruntime {

Status Size::Compute(OpKernelContext* ctx) const {
  const Tensor* input_tensor = ctx->Input<Tensor>(0);
  if (input_tensor == nullptr) return Status(common::ONNXRUNTIME, common::FAIL, "input count mismatch");
  TensorShape scalar_shape;
  Tensor* p_output_tensor = ctx->Output(0, scalar_shape);
  int64_t* p_output_scalar = p_output_tensor->template MutableData<int64_t>();
  assert(p_output_tensor->Size() == sizeof(int64_t));

  *p_output_scalar = input_tensor->Shape().Size();

  return Status::OK();
}

// The implementation of Size works for tensors of any type. The types listed below are
// based on the ones the datatypes in data_types.cc.
// TODO: we should not have to add the TypeConstraint below, since it is meant to be in
// addition to the ONNX specification. But the registration doesn't seem to work if we
// omit this.
// TODO: Both onnxruntime and ONNX lists of types seem somewhat incomplete and incomparable.

ONNX_CPU_OPERATOR_KERNEL(
    Size,
    1,
    KernelDefBuilder().TypeConstraint("T",
                                      std::vector<MLDataType>({DataTypeImpl::GetTensorType<float>(),
                                                               DataTypeImpl::GetTensorType<double>(),
                                                               DataTypeImpl::GetTensorType<int8_t>(),
                                                               DataTypeImpl::GetTensorType<int16_t>(),
                                                               DataTypeImpl::GetTensorType<int32_t>(),
                                                               DataTypeImpl::GetTensorType<int64_t>(),
                                                               DataTypeImpl::GetTensorType<uint8_t>(),
                                                               DataTypeImpl::GetTensorType<uint16_t>(),
                                                               DataTypeImpl::GetTensorType<uint32_t>(),
                                                               DataTypeImpl::GetTensorType<uint64_t>(),
                                                               DataTypeImpl::GetTensorType<std::string>(),
                                                               DataTypeImpl::GetTensorType<bool>()})),
    Size);

}  // namespace onnxruntime
