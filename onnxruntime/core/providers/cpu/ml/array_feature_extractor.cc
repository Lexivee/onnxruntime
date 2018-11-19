// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cpu/ml/array_feature_extractor.h"

/**
https://github.com/onnx/onnx/blob/master/onnx/defs/traditionalml/defs.cc
ONNX_OPERATOR_SCHEMA(ArrayFeatureExtractor)
    .SetDomain("ai.onnx.ml")
    .SetDoc(R"DOC(
    Select a subset of the data X based on the indices provided Y.
)DOC")
    .Input(0, "X", "Data to be selected", "T")
    .Input(
        1,
        "Y",
        "The index values to select as a int64 tensor",
        "tensor(int64)")
    .Output(0, "Z", "Selected output data as an array", "T")
    .TypeConstraint(
        "T",
        {"tensor(float)",
         "tensor(double)",
         "tensor(int64)",
         "tensor(int32)",
         "tensor(string)"},
        "allowed types.");
*/
using namespace ::onnxruntime::common;
using namespace std;
namespace onnxruntime {
namespace ml {
#define REG_ARRAYFEATUREEXTRACTOR(in_type)                                            \
  ONNX_CPU_OPERATOR_TYPED_ML_KERNEL(                                                  \
      ArrayFeatureExtractor,                                                          \
      1,                                                                              \
      in_type,                                                                        \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<in_type>()), \
      ArrayFeatureExtractorOp<in_type>);

REG_ARRAYFEATUREEXTRACTOR(float);
REG_ARRAYFEATUREEXTRACTOR(double);
REG_ARRAYFEATUREEXTRACTOR(int32_t);
REG_ARRAYFEATUREEXTRACTOR(int64_t);
REG_ARRAYFEATUREEXTRACTOR(string);

template <typename T>
ArrayFeatureExtractorOp<T>::ArrayFeatureExtractorOp(const OpKernelInfo& info)
    : OpKernel(info) {
}

template <typename T>
common::Status ArrayFeatureExtractorOp<T>::Compute(OpKernelContext* context) const {
  const Tensor& X = *context->Input<Tensor>(0);
  const TensorShape& x_shape = X.Shape();
  const vector<int64_t>& x_dims = x_shape.GetDims();
  const T* x_data = X.template Data<T>();

  if (x_dims.empty()) {
    return Status(ONNXRUNTIME, INVALID_ARGUMENT, "Invalid argument: X input has empty dimensions.");
  }

  int64_t stride = x_dims.size() == 1 ? x_dims[0] : x_dims[1];
  int64_t N = x_dims.size() == 1 ? 1 : x_dims[0];

  const Tensor& Y = *context->Input<Tensor>(1);
  const TensorShape& y_shape = Y.Shape();
  const int64_t* y_data = Y.template Data<int64_t>();
  int64_t num_indices = y_shape.Size();

  // validate Y
  if (num_indices == 0) {
    return Status(ONNXRUNTIME, INVALID_ARGUMENT, "Invalid Y argument: num_indices = 0");
  }

  if (num_indices - 1 >= stride) {
    std::ostringstream err_msg;
    err_msg << "Invalid Y argument: num_indices - 1 (" << num_indices - 1 << ") >= stride (" << stride << ")";
    return Status(ONNXRUNTIME, INVALID_ARGUMENT, err_msg.str());
  }

  Tensor* Z = context->Output(0, TensorShape({N, num_indices}));
  T* z_data = Z->template MutableData<T>();

  for (int64_t n = 0; n < N; ++n) {
    for (int64_t j = 0; j < num_indices; ++j) {
      *z_data++ = x_data[y_data[j]];
    }
    x_data += stride;
  }

  return Status::OK();
}

}  // namespace ml
}  // namespace onnxruntime
