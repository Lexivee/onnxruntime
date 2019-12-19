// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "dynamicquantizelinear.h"
#include "core/providers/common.h"
#include "core/util/math_cpuonly.h"
#include <cmath>
#include <cfenv>

namespace onnxruntime {

ONNX_CPU_OPERATOR_TYPED_KERNEL(
    DynamicQuantizeLinear,
    11,
    uint8_t,
    KernelDefBuilder()
        .TypeConstraint("T2", DataTypeImpl::GetTensorType<uint8_t>()),
    DynamicQuantizeLinear<uint8_t>);


static float RoundHalfToEven(float input) {
  std::fesetround(FE_TONEAREST);
  auto result = std::nearbyintf(input);
  return result;
}

// formula is Y = X / Scale + ZeroPoint
template <typename T>
Status DynamicQuantizeLinear<T>::Compute(OpKernelContext* ctx) const {
  auto x_ptr = ctx->Input<Tensor>(0);
  ORT_ENFORCE(x_ptr != nullptr);
  auto& x = *x_ptr;
  const auto* x_data = x.template Data<float>();

  auto& y = *ctx->Output(0, x.Shape());
  std::vector<int64_t> shape({});
  auto& y_scale = *ctx->Output(1, shape);
  auto& y_zeropoint = *ctx->Output(2, shape); 
  
  // find quantization range min and max
  float qmax = std::numeric_limits<T>::max();
  float qmin = std::numeric_limits<T>::min();
  // Adjust the int8 range to -127 to 127 so that zero point can be 0
  if (qmin == -128) {
    qmin = -127;
  }

  // find input range min and max
  auto min = ConstEigenVectorMap<float>(x_data, x.Shape().Size()).minCoeff();
  min = std::min(min, qmin);
  auto max = ConstEigenVectorMap<float>(x_data, x.Shape().Size()).maxCoeff();
  max = std::max(max, qmin);

  // find scale and zero point
  auto scale = (max - min) / (qmax - qmin);
  auto* output_scale = y_scale.template MutableData<float>();
  *output_scale = scale;

  const auto initial_zero_point = qmin - min / scale;
  auto zero_point = static_cast<T>(RoundHalfToEven(std::max(qmin, std::min(qmax, initial_zero_point))));
  auto* output_zp = y_zeropoint.template MutableData<T>();
  *output_zp = zero_point;

  // quantize the data
  auto* output = y.template MutableData<T>();
  const auto num_of_elements = x.Shape().Size();

  for (int i = 0; i < num_of_elements; ++i) {
    output[i] = static_cast<T>(clamp(RoundHalfToEven(static_cast<float>(x_data[i] / scale)) + zero_point, qmin, qmax));
  }

  return Status::OK();
}

}  // namespace onnxruntime
