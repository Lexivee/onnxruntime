// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cpu/math/hardmax.h"
#include "core/providers/common.h"
#include "core/util/math_cpuonly.h"
#include "core/util/math.h"
#include "core/providers/cpu/tensor/transpose.h"

namespace onnxruntime {

template <>
Status Hardmax<float>::Compute(OpKernelContext* ctx) const {
  const auto* X = ctx->Input<Tensor>(0);
  const TensorShape& X_shape = X->Shape();
  size_t rank = X_shape.NumDimensions();
  Tensor* Y = ctx->Output(0, X_shape);

  auto axis = HandleNegativeAxis(axis_, X_shape.NumDimensions());  // handle negative and enforce axis is valid

  bool is_transpose_required = false;
  Tensor transposed_input;
  std::vector<int64_t> transposed_input_dims;
  Tensor intermediate_output;  // output that the hardmax implementation will write into while using transposed input
  std::vector<size_t> permutation(rank);

  // The "semantic" meaning of axis has changed in opset-13.
  // Please compare: https://github.com/onnx/onnx/blob/master/docs/Operators.md#Hardmax
  // with https://github.com/onnx/onnx/blob/master/docs/Changelog.md#Hardmax-11 for detailed explanations
  // To account for the opset-13 behavior, our plan will be to transpose the "axis" dim to the innermost dim
  // and perform softmax and then reverse the transpose. We can skip the transposing aspect if the axis is already
  // the innermost dim
  if (opset_ >= 13 && axis != (static_cast<int64_t>(rank) - 1)) {
    is_transpose_required = true;
  }

  if (is_transpose_required) {
    AllocatorPtr alloc;
    auto status = ctx->GetTempSpaceAllocator(&alloc);
    if (!status.IsOK())
      return status;

    std::iota(std::begin(permutation), std::end(permutation), 0);

    // swap the innermost dim with the dim corresponding to axis
    permutation[axis] = static_cast<int64_t>(rank) - 1;
    permutation[rank - 1] = axis;

    transposed_input_dims.reserve(rank);
    for (auto e : permutation) {
      transposed_input_dims.push_back(X_shape[e]);
    }

    // Allocate a temporary tensor to hold transposed input
    Tensor temp_input(X->DataType(), TensorShape(transposed_input_dims), alloc);

    // Perform the transpose
    TransposeBase::DoTranspose(permutation, *X, temp_input);
    transposed_input = std::move(temp_input);

    // Allocate memory for the intermediate output
    Tensor temp_output(Y->DataType(), TensorShape(transposed_input_dims), alloc);
    intermediate_output = std::move(temp_output);
  }

  size_t tmpN = is_transpose_required ? TensorShape(transposed_input_dims).SizeToDimension(rank - 1) : X_shape.SizeToDimension(axis);
  size_t tmpD = is_transpose_required ? TensorShape(transposed_input_dims).SizeFromDimension(rank - 1) : X_shape.SizeFromDimension(axis);

  // Math::RowwiseMax expects int N and D.
  if (tmpN * tmpD > INT32_MAX || tmpN > INT32_MAX || tmpD > INT32_MAX) {
    std::ostringstream ss;
    ss << "Hardmax inputs N, D and N * D must be < " << INT32_MAX << ". N=" << tmpN << ", D=" << tmpD;
    std::string msg = ss.str();

    return Status(common::ONNXRUNTIME, common::INVALID_ARGUMENT, msg);
  }

  const int N = gsl::narrow_cast<int>(tmpN);
  const int D = gsl::narrow_cast<int>(tmpD);

  std::vector<float> rowmax_(N);
  float* rowmax_data = rowmax_.data();
  float* Ydata = nullptr;
  const float* Xdata = X->template Data<float>();

  if (is_transpose_required) {  // use intermediate buffers to compute the hardmax values
    Xdata = transposed_input.template Data<float>();
    Ydata = intermediate_output.template MutableData<float>();
  } else {  // use the node input/output directly
    Xdata = X->template Data<float>();
    Ydata = Y->template MutableData<float>();
  }

  math::RowwiseMax<float, CPUMathUtil>(N, D, Xdata, rowmax_data, nullptr);

  math::Set<float, CPUMathUtil>(X_shape.Size(), 0.f, Ydata, &CPUMathUtil::Instance());

  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < D; ++j) {
      if (Xdata[i * D + j] == rowmax_data[i]) {
        Ydata[i * D + j] = 1;
        break;
      }
    }
  }

  if (is_transpose_required) {
    std::vector<size_t> reverse_permutation(rank);
    for (size_t i = 0; i < permutation.size(); ++i) {
      reverse_permutation[permutation[i]] = i;
    }
    // Perform the transpose to get the axes back to the original ordering
    TransposeBase::DoTranspose(reverse_permutation, intermediate_output, *Y);
  }

  return Status::OK();
}

ONNX_CPU_OPERATOR_VERSIONED_KERNEL(
    Hardmax,
    1,
    10,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    Hardmax<float>);

// Opset 11 starts to support Neg Axis.
ONNX_CPU_OPERATOR_VERSIONED_KERNEL(
    Hardmax,
    11,
    12,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    Hardmax<float>);

// Opset 13 changed the semantic meaning of the axis attribute.
ONNX_CPU_OPERATOR_KERNEL(
    Hardmax,
    13,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    Hardmax<float>);

}  // namespace onnxruntime
