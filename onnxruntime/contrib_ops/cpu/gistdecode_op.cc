// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "gistdecode_op.h"

namespace onnxruntime {
namespace contrib {
ONNX_CPU_OPERATOR_KERNEL(
    GistBinarizeDecoder,
    9,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::AllTensorTypes()),
    GistBinarizeDecoderOp);

Status GistBinarizeDecoderOp::Compute(OpKernelContext* context) const {
  const auto* X = context->Input<Tensor>(1);
  ORT_ENFORCE(X != nullptr);
  const TensorShape& shape = X->Shape();

  Tensor* Y = context->Output(0, shape);

  const auto* src = X->template Data<bool>();
  auto* dst = Y->template MutableData<float>();
#ifdef USE_OPENMP
#pragma omp parallel for
#endif
  for (int64_t i = 0; i < X->Shape().Size(); ++i) {
    dst[i] = src[i] ? 1.0f : 0.0f;
  }

  return Status::OK();
}
}  // namespace contrib
}  // namespace onnxruntime
