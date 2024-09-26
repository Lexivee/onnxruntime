// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "layer_norm_impl.h"

#include "core/common/safeint.h"
#include "core/framework/tensor.h"
#include "core/platform/threadpool.h"
#include "core/providers/common.h"
#include "core/util/force_inline.h"
#include "core/util/math_cpuonly.h"

namespace onnxruntime {

namespace {

// Utility to convert from MLFloat16 to float only when the input type is MLFloat16.
template <typename T, typename Ret>
ORT_FORCEINLINE Ret ConvertMLFloat16ToDoubleOrFloatIfNeeded(T val);

template <>
ORT_FORCEINLINE float ConvertMLFloat16ToDoubleOrFloatIfNeeded<MLFloat16, float>(MLFloat16 val) {
  return val.ToFloat();
}

template <>
ORT_FORCEINLINE constexpr float ConvertMLFloat16ToDoubleOrFloatIfNeeded<float, float>(float val) {
  return val;
}

template <>
ORT_FORCEINLINE constexpr double ConvertMLFloat16ToDoubleOrFloatIfNeeded<double, double>(double val) {
  return val;
}

ORT_FORCEINLINE constexpr float ConvertToFloatIfNeeded(float val) {
  return val;
}

ORT_FORCEINLINE constexpr float ConvertToFloatIfNeeded(double val) {
  // ONNX spec doesn't support 'double' for 'Ret' so when 'T' == double, 'Ret' == float and we need to narrow
  return gsl::narrow_cast<float>(val);
}

// Function template that only converts the input value to MLFloat16 if T is MLFloat16.
template <typename T>
ORT_FORCEINLINE constexpr typename std::enable_if_t<std::is_same_v<T, float> || std::is_same_v<T, double>, float>
ConvertToMLFloat16IfNeeded(float val) {
  return val;
}

template <typename T>
ORT_FORCEINLINE constexpr typename std::enable_if_t<std::is_same_v<T, MLFloat16>, MLFloat16>
ConvertToMLFloat16IfNeeded(float val) {
  return MLFloat16(val);
}

template <typename T>
ORT_FORCEINLINE constexpr double ConvertToMLFloat16IfNeeded(double val) {
  return val;
}

} // namespace

LayerNormImpl::LayerNormImpl(const OpKernelInfo& op_kernel_info, bool simplified, bool contrib_op)
    : OpKernel(op_kernel_info), simplified_{simplified}, contrib_op_{contrib_op} {
  ORT_ENFORCE(op_kernel_info.GetAttr("axis", &axis_).IsOK());
  ORT_ENFORCE(op_kernel_info.GetAttr<float>("epsilon", &epsilon_).IsOK());
}

template <typename T, typename U>
Status LayerNormImpl::ComputeImpl(OpKernelContext* p_ctx, int64_t orig_axis, float epsilon, bool simplified) const {
  // Inputs
  const Tensor* X = p_ctx->Input<Tensor>(0);
  const Tensor* scale = p_ctx->Input<Tensor>(1);
  const Tensor* bias = p_ctx->Input<Tensor>(2);
  const T* X_data = X->Data<T>();
  const T* scale_data = scale->Data<T>();
  const T* bias_data = (simplified || nullptr == bias) ? nullptr : bias->Data<T>();

  const TensorShape& x_shape = X->Shape();
  const TensorShape& scale_shape = scale->Shape();
  const TensorShape& bias_shape = bias->Shape();
  Tensor* Y = p_ctx->Output(0, x_shape);
  T* Y_data = Y->MutableData<T>();

  const int64_t axis = HandleNegativeAxis(orig_axis, x_shape.NumDimensions());

  std::vector<int64_t> mean_inv_std_dev_dim;
  mean_inv_std_dev_dim.reserve(x_shape.NumDimensions());
  for (int i = 0; i < static_cast<int>(x_shape.NumDimensions()); ++i) {
    if (i < axis) {
      mean_inv_std_dev_dim.emplace_back(x_shape.GetDims()[i]);
    } else {
      mean_inv_std_dev_dim.emplace_back(1);
    }
  }

  int output_index = 1;
  Tensor* mean = p_ctx->Output(output_index++, TensorShape(mean_inv_std_dev_dim));
  U* mean_data = nullptr;
  if (mean != nullptr) {
    mean_data = mean->MutableData<U>();
  }

  U* inv_std_dev_data = nullptr;
  Tensor* inv_std_dev = p_ctx->Output(output_index, TensorShape(mean_inv_std_dev_dim));
  if (inv_std_dev != nullptr) {
    inv_std_dev_data = inv_std_dev->MutableData<U>();
  }

  onnxruntime::concurrency::ThreadPool* thread_pool = p_ctx->GetOperatorThreadPool();

  return ComputeWithoutContext<T, U>(X_data, x_shape, scale_data, scale_shape, bias_data, bias_shape,
    Y_data, mean_data, inv_std_dev_data, thread_pool, axis, epsilon, simplified);
}

Status LayerNormImpl::Compute(OpKernelContext* p_ctx) const {
  const auto elem_type = p_ctx->Input<Tensor>(0)->GetElementType();

  using SupportedTypeList = boost::mp11::mp_list<float, double, MLFloat16>;

  utils::MLTypeCallDispatcherFromTypeList<SupportedTypeList> t_disp(elem_type);
  return t_disp.InvokeRet<Status, SrcDispatcher>(this, p_ctx, axis_, epsilon_, simplified_, contrib_op_);
}

template <typename T, typename U>
Status LayerNormImpl::ComputeWithoutContext(
  const T* X_data,
  const TensorShape& x_shape,
  const T* scale_data,
  const TensorShape& scale_shape,
  const T* bias_data,
  const TensorShape& bias_shape,
  T* Y_data,
  U* mean_data,
  U* inv_std_dev_data,
  onnxruntime::concurrency::ThreadPool* thread_pool,
  int64_t axis,
  float epsilon,
  bool simplified
) const {
  int64_t norm_count = x_shape.SizeToDimension(onnxruntime::narrow<size_t>(axis));
  int64_t norm_size = x_shape.SizeFromDimension(onnxruntime::narrow<size_t>(axis));

  const auto scale_size = scale_shape.Size();
  const auto bias_size = (bias_data) ? bias_shape.Size() : 0;
  if (scale_size != norm_size || (bias_data && bias_size != norm_size)) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                           "Size of X.shape()[axis:] == ", norm_size,
                           ". Size of scale and bias (if provided) must match this. Got scale size of ",
                           scale_size, " and bias size of ", bias_size);
  }

  concurrency::ThreadPool::TryBatchParallelFor(
      thread_pool, static_cast<int32_t>(norm_count),
      [&](ptrdiff_t task_idx) {
        const T* p_input = X_data + task_idx * norm_size;
        T* p_output = Y_data + task_idx * norm_size;

        using DoubleOrFloat = typename std::conditional<
            std::is_same<T, double>::value,  // If T is double
            double,                          // Use double
            float                            // Otherwise, use float (covers float and MLFloat16)
            >::type;

        DoubleOrFloat mean(0.0f);
        DoubleOrFloat mean_square(0.0f);

        for (int64_t h = 0; h < norm_size; h++) {
          DoubleOrFloat input_value = ConvertMLFloat16ToDoubleOrFloatIfNeeded<T, DoubleOrFloat>(p_input[h]);
          mean += input_value;
          mean_square += input_value * input_value;
        }

        mean = mean / norm_size;
        if (simplified) {
          mean_square = sqrt(mean_square / norm_size + epsilon);
        } else {
          mean_square = sqrt(mean_square / norm_size - mean * mean + epsilon);
        }

        for (int64_t h = 0; h < norm_size; h++) {
          DoubleOrFloat input_value = ConvertMLFloat16ToDoubleOrFloatIfNeeded<T, DoubleOrFloat>(p_input[h]);
          DoubleOrFloat scale_value = ConvertMLFloat16ToDoubleOrFloatIfNeeded<T, DoubleOrFloat>(scale_data[h]);
          if (simplified) {
            p_output[h] = ConvertToMLFloat16IfNeeded<T>(input_value / mean_square * scale_value);
          } else if (nullptr == bias_data) {
            p_output[h] = ConvertToMLFloat16IfNeeded<T>((input_value - mean) / mean_square * scale_value);
          } else {
            DoubleOrFloat bias_value = ConvertMLFloat16ToDoubleOrFloatIfNeeded<T, DoubleOrFloat>(bias_data[h]);
            p_output[h] = ConvertToMLFloat16IfNeeded<T>((input_value - mean) / mean_square * scale_value + bias_value);
          }
        }

        if (mean_data != nullptr) {
          // ONNX spec doesn't support 'double' for 'U' so when 'T' == double, 'U' == float and we need to narrow
          mean_data[task_idx] = ConvertToMLFloat16IfNeeded<U>(ConvertToFloatIfNeeded(mean));
        }

        if (inv_std_dev_data != nullptr) {
          inv_std_dev_data[task_idx] = ConvertToMLFloat16IfNeeded<U>(ConvertToFloatIfNeeded(1 / mean_square));
        }
      },
      0);

  return Status::OK();
}

}  // namespace onnxruntime
