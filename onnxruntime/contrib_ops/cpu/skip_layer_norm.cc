// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/framework/tensor.h"
#include "core/mlas/inc/mlas.h"
#include "core/util/math_cpuonly.h"
#include "core/providers/common.h"
#include "core/platform/threadpool.h"
#include "core/util/force_inline.h"
#include "skip_layer_norm.h"
#include "skip_layer_norm_helper.h"

namespace onnxruntime {
namespace contrib {

#define REGISTER_KERNEL_TYPED(T)                                  \
  ONNX_OPERATOR_TYPED_KERNEL_EX(                                  \
      SkipLayerNormalization,                                     \
      kMSDomain,                                                  \
      1,                                                          \
      T,                                                          \
      kCpuExecutionProvider,                                      \
      KernelDefBuilder()                                          \
          .TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      SkipLayerNorm<T, false>);                                   \
  ONNX_OPERATOR_TYPED_KERNEL_EX(                                  \
      SkipSimplifiedLayerNormalization,                           \
      kMSDomain,                                                  \
      1,                                                          \
      T,                                                          \
      kCpuExecutionProvider,                                      \
      KernelDefBuilder()                                          \
          .TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      SkipLayerNorm<T, true>);

REGISTER_KERNEL_TYPED(float)
REGISTER_KERNEL_TYPED(double)
REGISTER_KERNEL_TYPED(MLFloat16)

namespace {

template <typename T, typename = std::enable_if_t<std::is_same_v<T, float> || std::is_same_v<T, double>, void>>
void ComputeJob(
    const T* input_data,
    const T* skip_data,
    const T* gamma_data,
    const T* beta_data,
    const T* bias_data,
    const IAllocatorUniquePtr<float>& skip_fp32,
    const IAllocatorUniquePtr<float>& gamma_fp32,
    const IAllocatorUniquePtr<float>& beta_fp32,
    const IAllocatorUniquePtr<float>& bias_fp32,
    ptrdiff_t task_idx,
    int hidden_size,
    int64_t skip_size,
    float epsilon,
    bool simplified,
    T* output_data,
    T* skip_input_bias_add_output_data,
    AllocatorPtr alloc) {
  ORT_UNUSED_PARAMETER(skip_fp32);   // only used in MLFloat16 overload
  ORT_UNUSED_PARAMETER(gamma_fp32);  // only used in MLFloat16 overload
  ORT_UNUSED_PARAMETER(beta_fp32);   // only used in MLFloat16 overload
  ORT_UNUSED_PARAMETER(bias_fp32);   // only used in MLFloat16 overload
  ORT_UNUSED_PARAMETER(alloc);

  auto offset = task_idx * hidden_size;
  const T* p_input = input_data + offset;
  const T* p_skip = skip_data + (offset % skip_size);
  T* p_output = output_data + offset;
  T* p_skip_input_bias_add_output = skip_input_bias_add_output_data == nullptr ? nullptr : skip_input_bias_add_output_data + offset;

  T mean(0.0f);
  T mean_square(0.0f);

  for (decltype(hidden_size) h = 0; h < hidden_size; h++) {
    T val = p_input[h] + p_skip[h];

    if (nullptr != bias_data) {
      val += bias_data[h];
    }

    if (nullptr != p_skip_input_bias_add_output) {
      p_skip_input_bias_add_output[h] = val;
    }

    p_output[h] = val;
    mean += val;
    mean_square += val * val;
  }

  mean = mean / hidden_size;
  if (simplified) {
    mean_square = sqrt(mean_square / hidden_size + epsilon);
  } else {
    mean_square = sqrt(mean_square / hidden_size - mean * mean + epsilon);
  }

  for (decltype(hidden_size) h = 0; h < hidden_size; h++) {
    if (simplified) {
      p_output[h] = p_output[h] / mean_square * gamma_data[h];
    } else if (nullptr == beta_data) {
      p_output[h] = (p_output[h] - mean) / mean_square * gamma_data[h];
    } else {
      p_output[h] = (p_output[h] - mean) / mean_square * gamma_data[h] + beta_data[h];
    }
  }
}

void ComputeJob(
    const MLFloat16* input_data,
    const MLFloat16* skip_data,
    const MLFloat16* gamma_data,
    const MLFloat16* beta_data,
    const MLFloat16* bias_data,
    const IAllocatorUniquePtr<float>& skip_fp32,
    const IAllocatorUniquePtr<float>& gamma_fp32,
    const IAllocatorUniquePtr<float>& beta_fp32,
    const IAllocatorUniquePtr<float>& bias_fp32,
    ptrdiff_t task_idx,
    int hidden_size,
    int64_t skip_size,
    float epsilon,
    bool simplified,
    MLFloat16* output_data,
    MLFloat16* skip_input_bias_add_output_data,
    AllocatorPtr alloc) {
  auto offset = task_idx * hidden_size;
  const MLFloat16* p_input = input_data + offset;
  const MLFloat16* p_skip = skip_data + (offset % skip_size);
  MLFloat16* p_output = output_data + offset;
  MLFloat16* p_skip_input_bias_add_output = skip_input_bias_add_output_data == nullptr ? nullptr : skip_input_bias_add_output_data + offset;

  float mean(0.0f);
  float mean_square(0.0f);
  const size_t num_elems = static_cast<size_t>(hidden_size);

  float* float_input = (float*)alloc->Alloc(num_elems * sizeof(float));
  MlasConvertHalfToFloatBuffer(p_input, float_input, num_elems);

  float* float_skip = skip_fp32.get();
  if (nullptr == float_skip) {
    float_skip = (float*)alloc->Alloc(num_elems * sizeof(float));
    MlasConvertHalfToFloatBuffer(p_skip, float_skip, num_elems);
  }

  float* float_bias = nullptr;
  if (bias_data) {
    if (nullptr != bias_fp32) {
      float_bias = bias_fp32.get();
    } else {
      float_bias = (float*)alloc->Alloc(num_elems * sizeof(float));
      MlasConvertHalfToFloatBuffer(bias_data, float_bias, num_elems);
    }
  }

  float* float_output = (float*)alloc->Alloc(num_elems * sizeof(float));

  for (size_t h = 0; h < num_elems; h++) {
    float val = float_input[h] + float_skip[h];

    if (nullptr != float_bias) {
      val += float_bias[h];
    }

    float_output[h] = val;
    mean += val;
    mean_square += val * val;
  }

  if (float_bias && (nullptr == bias_fp32)) {
    delete[] float_bias;
  }

  if (nullptr != p_skip_input_bias_add_output) {
    MlasConvertFloatToHalfBuffer(float_output, p_skip_input_bias_add_output, num_elems);
  }

  mean = mean / hidden_size;
  if (simplified) {
    mean_square = sqrt(mean_square / hidden_size + epsilon);
  } else {
    mean_square = sqrt(mean_square / hidden_size - mean * mean + epsilon);
  }

  float* float_gamma = gamma_fp32.get();
  if (nullptr == float_gamma) {
    float_gamma = float_input;  // overwrite float_input with gamma values, since they have the same size
    MlasConvertHalfToFloatBuffer(gamma_data, float_gamma, num_elems);
  }

  float* float_beta = nullptr;
  if (beta_data) {
    if (nullptr != beta_fp32) {
      float_beta = beta_fp32.get();
    } else {
      float_beta = (float*)alloc->Alloc(num_elems * sizeof(float));
      MlasConvertHalfToFloatBuffer(beta_data, float_beta, num_elems);
    }
  }

  for (size_t h = 0; h < num_elems; h++) {
    if (simplified) {
      float_output[h] = float_output[h] / mean_square * float_gamma[h];
    } else if (nullptr == float_beta) {
      float_output[h] = (float_output[h] - mean) / mean_square * float_gamma[h];
    } else {
      float_output[h] = (float_output[h] - mean) / mean_square * float_gamma[h] + float_beta[h];
    }
  }

  alloc->Free(float_input); // also takes care of float_gamma if reused
  if (float_skip && (nullptr == skip_fp32)) {
    alloc->Free(float_skip);
  }
  if (beta_data && (nullptr == beta_fp32)) {
    alloc->Free(float_beta);
  }

  MlasConvertFloatToHalfBuffer(float_output, p_output, num_elems);
  alloc->Free(float_output);
}

void ConvertMLFloat16ToFloatIfNeeded(const Tensor& tensor, AllocatorPtr alloc, IAllocatorUniquePtr<float>& dest, bool& is_packed) {
  if (tensor.GetElementType() == utils::ToTensorProtoElementType<MLFloat16>()) {
    auto tensor_data_ptr = tensor.Data<MLFloat16>();
    auto tensor_size = static_cast<size_t>(tensor.Shape().Size());
    auto float_ptr = IAllocator::MakeUniquePtr<float>(alloc, tensor_size, true);

    MlasConvertHalfToFloatBuffer(tensor_data_ptr, float_ptr.get(), tensor_size);
    dest = std::move(float_ptr);
    is_packed = true;
  }
}

}  // namespace

template <typename T, bool simplified>
SkipLayerNorm<T, simplified>::SkipLayerNorm(const OpKernelInfo& op_kernel_info)
    : OpKernel(op_kernel_info), skip_fp32_(nullptr), gamma_fp32_(nullptr), beta_fp32_(nullptr), bias_fp32_(nullptr) {
  ORT_ENFORCE(op_kernel_info.GetAttr<float>("epsilon", &epsilon_).IsOK());
  ORT_ENFORCE(epsilon_ >= 0);
}

template <typename T, bool simplified>
Status SkipLayerNorm<T, simplified>::Compute(OpKernelContext* p_ctx) const {
  const Tensor* input = p_ctx->Input<Tensor>(0);
  const Tensor* skip = p_ctx->Input<Tensor>(1);
  const Tensor* gamma = p_ctx->Input<Tensor>(2);
  const Tensor* beta = p_ctx->Input<Tensor>(3);
  const Tensor* bias = p_ctx->Input<Tensor>(4);
  Tensor* output = p_ctx->Output(0, input->Shape());
  // For inferencing, we support one more optional output which is the sum of the input and skip tensors
  Tensor* skip_input_bias_add_output = p_ctx->Output(3, input->Shape());

  const auto& input_dims = input->Shape().GetDims();
  size_t input_dims_size = input_dims.size();
  int hidden_size = static_cast<int>(input_dims[input_dims_size - 1]);

  ORT_RETURN_IF_ERROR(onnxruntime::contrib::skip_layer_norm_helper::CheckInputs<Tensor>(input,
                                                                                        skip,
                                                                                        gamma,
                                                                                        beta,
                                                                                        bias,
                                                                                        hidden_size,
                                                                                        input_dims_size));

  int64_t task_count = input->Shape().SizeToDimension(input_dims_size - 1);

  const T* input_data = input->Data<T>();
  const T* skip_data = skip->Data<T>();
  const T* gamma_data = gamma->Data<T>();
  const T* beta_data = beta == nullptr ? nullptr : beta->Data<T>();
  const T* bias_data = bias == nullptr ? nullptr : bias->Data<T>();

  T* output_data = output->MutableData<T>();

  // For inferencing, we support one more optional output which is the sum of the input and skip tensors
  T* skip_input_bias_add_output_data = skip_input_bias_add_output == nullptr ? nullptr : skip_input_bias_add_output->MutableData<T>();

  const int64_t& skip_size = skip->Shape().Size();

  AllocatorPtr alloc;
  ORT_RETURN_IF_ERROR(p_ctx->GetTempSpaceAllocator(&alloc));

  concurrency::ThreadPool::TryBatchParallelFor(
      p_ctx->GetOperatorThreadPool(), static_cast<int32_t>(task_count),
      [&](ptrdiff_t task_idx) {
        ComputeJob(input_data, skip_data, gamma_data, beta_data, bias_data, skip_fp32_, gamma_fp32_, beta_fp32_,
                   bias_fp32_, task_idx, hidden_size, skip_size, epsilon_, simplified, output_data,
                   skip_input_bias_add_output_data, alloc);
      },
      0);

  return Status::OK();
}

template <typename T, bool simplified>
Status SkipLayerNorm<T, simplified>::PrePack(const Tensor& tensor, int input_idx, AllocatorPtr alloc,
                                             bool& is_packed, PrePackedWeights* prepacked_weights) {
  ORT_UNUSED_PARAMETER(prepacked_weights);

  is_packed = false;
  if (input_idx == 1) {  // skip
    ConvertMLFloat16ToFloatIfNeeded(tensor, alloc, skip_fp32_, is_packed);
  } else if (input_idx == 2) {  // gamma
    ConvertMLFloat16ToFloatIfNeeded(tensor, alloc, gamma_fp32_, is_packed);
  } else if (input_idx == 3) {  // beta
    ConvertMLFloat16ToFloatIfNeeded(tensor, alloc, beta_fp32_, is_packed);
  } else if (input_idx == 4) {  // bias
    ConvertMLFloat16ToFloatIfNeeded(tensor, alloc, bias_fp32_, is_packed);
  }

  return Status::OK();
}

}  // namespace contrib
}  // namespace onnxruntime
