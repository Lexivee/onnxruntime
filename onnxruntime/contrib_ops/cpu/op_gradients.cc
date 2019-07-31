// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "op_gradients.h"
#include "core/util/math.h"
#include "core/util/math_cpuonly.h"
#include "core/providers/common.h"
#include <unsupported/Eigen/SpecialFunctions>
#include "core/util/math.h"
#include "core/providers/cpu/math/element_wise_ops.h"
#include "core/providers/cpu/math/matmul_helper.h"
#include "gsl/gsl_algorithm"
#include "gsl/gsl_util"

namespace onnxruntime {
namespace contrib {

std::vector<VectorInt64> InferOutputShapes(OpKernelInfo info) {
  std::vector<VectorInt64> output_tensor_shapes = {};

  auto& node = info.node();
  auto output_defs = node.OutputDefs();
  auto outputCount = output_defs.size();

  for (size_t outputIndex = 0; outputIndex < outputCount; outputIndex++) {
    output_tensor_shapes.push_back({});
    if (!output_defs[outputIndex]->Exists())
      continue;

    auto shape = output_defs[outputIndex]->Shape();
    for (auto dim : shape->dim()) {
      output_tensor_shapes[outputIndex].push_back(dim.dim_value());
    }
  }
  return output_tensor_shapes;
}

ONNX_CPU_OPERATOR_KERNEL(
    SinGrad,
    9,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    SinGrad<float>);

template <typename T>
Status SinGrad<T>::Compute(OpKernelContext* context) const {
  auto& dY = *context->Input<Tensor>(0);
  auto& X = *context->Input<Tensor>(1);
  auto& dX = *context->Output(0, X.Shape());
  MakeEigenArrayMap<float>(dX) = MakeEigenArrayMap<float>(dY) * MakeEigenArrayMap<float>(X).cos();
  return Status::OK();
}

ONNX_CPU_OPERATOR_KERNEL(
    ReluGrad,
    9,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    ReluGrad<float>);

template <typename T>
Status ReluGrad<T>::Compute(OpKernelContext* context) const {
  auto& dY = *context->Input<Tensor>(0);
  auto& X = *context->Input<Tensor>(1);
  auto& dX = *context->Output(0, dY.Shape());

  EigenVectorArrayMap<float>(dX.template MutableData<T>(), dX.Shape().Size()) =
      (ConstEigenVectorArrayMap<float>(X.template Data<T>(), X.Shape().Size()) > T(0))
          .select(ConstEigenVectorArrayMap<float>(dY.template Data<T>(), dY.Shape().Size()), T(0));

  return Status::OK();
}

ONNX_CPU_OPERATOR_KERNEL(
    PowGrad,
    9,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    PowGrad<float>);

// This is currently implemented for when a is a single element.
template <typename T>
Status PowGrad<T>::Compute(OpKernelContext* context) const {
  auto& dz = *context->Input<Tensor>(0);
  auto& w = *context->Input<Tensor>(1);
  auto& a = *context->Input<Tensor>(2);

  auto& dw = *context->Output(0, w.Shape());

  // df/dw = a * w^(a-1) - all operations are element wise
  float scalarA = a.Data<float>()[0];
  MakeEigenArrayMap<float>(dw) = scalarA * MakeEigenArrayMap<float>(w).pow(scalarA - 1) * MakeEigenArrayMap<float>(dz);

  // df/da =  w^a * ln w
  // this is not implemented yet . needs ln

  return Status::OK();
}

ONNX_CPU_OPERATOR_KERNEL(
    SigmoidGrad,
    9,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    SigmoidGrad<float>);

template <typename T>
Status SigmoidGrad<T>::Compute(OpKernelContext* context) const {
  auto& dY = *context->Input<Tensor>(0);
  auto& Y = *context->Input<Tensor>(1);
  auto& dX = *context->Output(0, Y.Shape());

  // dx = dy * y * (1 - y)
  // TODO: Would this be preferable as dx = dy * sigmoid(x) * (1 - sigmoid(x)) ???
  MakeEigenArrayMap<float>(dX) = MakeEigenArrayMap<float>(dY) * MakeEigenArrayMap<float>(Y) * (T(1) - MakeEigenArrayMap<float>(Y));
  return Status::OK();
}

ONNX_CPU_OPERATOR_KERNEL(
    SoftmaxGrad,
    9,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    SoftmaxGrad<float>);

template <typename T>
Status SoftmaxGrad<T>::Compute(OpKernelContext* context) const {
  auto& dY = *context->Input<Tensor>(0);
  auto& Y = *context->Input<Tensor>(1);
  const TensorShape input_shape{Y.Shape()};
  auto& dX = *context->Output(0, Y.Shape());

  auto axis = HandleNegativeAxis(axis_, Y.Shape().NumDimensions());

  size_t N = input_shape.SizeToDimension(axis);
  size_t D = input_shape.SizeFromDimension(axis);

  if (N == 0) {
    return Status::OK();
  }

  std::vector<float> scale_(N);
  std::vector<float> sum_multiplier_(D, 1.f);  // initialize all multiplier values to 1.0
  const int n = gsl::narrow_cast<int>(N);
  const int d = gsl::narrow_cast<int>(D);
  const int nd = gsl::narrow_cast<int>(N * D);

  float* scaledata = scale_.data();
  const float* Ydata = Y.template Data<float>();
  const float* dYdata = dY.template Data<float>();
  float* dXdata = dX.template MutableData<float>();

  gsl::copy(gsl::make_span(dYdata, nd), gsl::make_span(dXdata, nd));

  for (size_t i = 0; i < N; ++i) {
    math::Dot<float, CPUMathUtil>(d, Ydata + i * d, dYdata + i * d,
                                  scaledata + i, nullptr);
  }

  math::Gemm<float, CPUMathUtil>(CblasNoTrans, CblasNoTrans, n, d, 1, -1,
                                 scaledata, sum_multiplier_.data(), 1,
                                 dXdata, nullptr);

  math::Mul<float, CPUMathUtil>(gsl::narrow_cast<int>(Y.Shape().Size()), dXdata, Ydata, dXdata, nullptr);

  return Status::OK();
}

}  // namespace contrib
}  // namespace onnxruntime
