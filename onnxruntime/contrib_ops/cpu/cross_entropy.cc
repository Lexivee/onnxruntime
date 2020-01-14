// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "cross_entropy.h"
#include "core/util/math.h"
#include "core/util/math_cpuonly.h"
#include "core/providers/common.h"
#include <unsupported/Eigen/SpecialFunctions>
#include "core/util/math.h"
#include "core/providers/cpu/math/matmul_helper.h"
#include "gsl/gsl"

namespace onnxruntime {
namespace contrib {

void ComputeShareSoftmaxCrossEntropyCPU(const int n,
                                        const int d,
                                        const int nd,
                                        const float* logit_data,
                                        float* shifted_logit,
                                        float* probability_data,
                                        float* log_sum_exp) {
  // Find the max in each batch, resulting in a tensor of shape [batch]
  // logit_max = max(logit_data)
  std::vector<float> logit_max(n);
  math::RowwiseMax<float, CPUMathUtil>(n, d, logit_data, logit_max.data(), nullptr);

  // Subtract the max in batch b from every element in batch b.
  // Broadcasts along the batch dimension.
  // shifted_logit = logit_data - logit_max
  gsl::copy(gsl::make_span(logit_data, nd), gsl::make_span(shifted_logit, nd));
  math::SubToCol<float, CPUMathUtil>(n, d, logit_max.data(), shifted_logit, nullptr);

  // exp_shifted_logit = exp(shifted_logit)
  math::Exp<float, CPUMathUtil>(nd, shifted_logit, probability_data, nullptr);

  // sum_exp = sum_{class} (exp_shifted_logit)
  float* sum_exp = log_sum_exp;
  math::RowwiseSum<float, CPUMathUtil>(n, d, probability_data, sum_exp, nullptr);

  // probability = exp(shifted_logit) / sum(exp(shifted_logit))
  // the division broadcasts along the batch dimension
  math::DivToCol<float, CPUMathUtil>(n, d, sum_exp, probability_data, nullptr);

  // log_sum_exp = log(sum(exp(shifted_logit)))
  math::Log<float, CPUMathUtil>(n, sum_exp, log_sum_exp, nullptr);
}

ONNX_OPERATOR_KERNEL_EX(
    SoftmaxCrossEntropy,
    kOnnxDomain,
    9,
    kCpuExecutionProvider,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    SoftmaxCrossEntropy<float>);

template <typename T>
Status SoftmaxCrossEntropy<T>::Compute(OpKernelContext* context) const {
  const Tensor& logit = *context->Input<Tensor>(0);
  const Tensor& label = *context->Input<Tensor>(1);

  const TensorShape logit_shape{logit.Shape()};
  const TensorShape label_shape{label.Shape()};

  ORT_ENFORCE(label_shape == logit_shape, "The shape of logit and label is not identical");

  int64_t N = logit_shape.SizeToDimension(logit_shape.NumDimensions() - 1);
  int64_t D = logit_shape[logit_shape.NumDimensions() - 1];
  const int n = gsl::narrow_cast<int>(N);
  const int d = gsl::narrow_cast<int>(D);
  const int nd = gsl::narrow_cast<int>(N * D);

  Tensor* loss = context->Output(0, TensorShape({1}));
  Tensor* probability = context->Output(1, logit_shape);

  const float* logit_data = logit.template Data<float>();
  const float* label_data = label.template Data<float>();
  float* loss_data = loss->template MutableData<float>();
  float* probability_data = probability->template MutableData<float>();

  // computation begins here
  std::vector<float> shifted_logit(nd);
  std::vector<float> log_sum_exp(n);
  // probability = exp(shifted_logit) / sum(exp(shifted_logit))
  // where shifted_logit = logit - max_logit
  // along classes
  ComputeShareSoftmaxCrossEntropyCPU(n, d, nd, logit_data,
                                     shifted_logit.data(),
                                     probability_data,
                                     log_sum_exp.data());

  // loss = sum(label * (shifted_logit - log_sum_exp))
  auto& sub = shifted_logit;
  math::SubToCol<float, CPUMathUtil>(n, d, log_sum_exp.data(), sub.data(), nullptr);

  auto& mul = sub;
  math::Mul<float, CPUMathUtil>(nd, label_data, sub.data(), mul.data(), nullptr);

  // Sum over batches and classes
  math::Sum<float, CPUMathUtil>(nd, mul.data(), loss_data, nullptr);

  if (reduction_ == ReductionType::MEAN) {
    *loss_data /= -n;
  } else if (reduction_ == ReductionType::SUM) {
    *loss_data *= -1;
  }

  return Status::OK();
}

ONNX_OPERATOR_KERNEL_EX(
    SoftmaxCrossEntropyGrad,
    kOnnxDomain,
    9,
    kCpuExecutionProvider,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    SoftmaxCrossEntropyGrad<float>);

template <typename T>
Status SoftmaxCrossEntropyGrad<T>::Compute(OpKernelContext* context) const {
  const Tensor& dY = *context->Input<Tensor>(0);
  const Tensor& probability = *context->Input<Tensor>(1);
  const Tensor& label = *context->Input<Tensor>(2);

  const TensorShape probability_shape{probability.Shape()};
  const TensorShape label_shape{label.Shape()};

  ORT_ENFORCE(label_shape == probability_shape, "The shape of probability and label is not identical");

  int64_t N = probability_shape.SizeToDimension(probability_shape.NumDimensions() - 1);
  const int n = gsl::narrow_cast<int>(N);
  const int nd = gsl::narrow_cast<int>(probability_shape.Size());

  Tensor* d_logit = context->Output(0, probability_shape);

  const float* dY_data = dY.template Data<float>();
  const float* probability_data = probability.template Data<float>();
  const float* label_data = label.template Data<float>();
  float* d_logit_data = d_logit->template MutableData<float>();

  // computation begins here
  // backprop: prob - label
  math::Sub<float, CPUMathUtil>(nd, probability_data, label_data, d_logit_data, nullptr);

  float dY_scaled;
  if (reduction_ == ReductionType::MEAN) {
    dY_scaled = *dY_data / n;
  } else if (reduction_ == ReductionType::SUM) {
    dY_scaled = *dY_data;
  }

  // d_logit = dY * backprop, dY is a scalar
  math::Scale<float, CPUMathUtil>(nd, &dY_scaled, d_logit_data, d_logit_data, nullptr);

  return Status::OK();
}

ONNX_OPERATOR_KERNEL_EX(
    SparseSoftmaxCrossEntropy,
    kOnnxDomain,
    9,
    kCpuExecutionProvider,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    SparseSoftmaxCrossEntropy<float>);

template <typename T>
Status SparseSoftmaxCrossEntropy<T>::Compute(OpKernelContext* context) const {
  const Tensor& logit = *context->Input<Tensor>(0);
  const Tensor& label = *context->Input<Tensor>(1);

  const TensorShape logit_shape{logit.Shape()};
  const TensorShape label_shape{label.Shape()};
  ORT_ENFORCE(logit_shape.NumDimensions() == label_shape.NumDimensions() + 1,
              "logit_shape must be (1 + label_shape)");
  for (size_t i = 0; i < label_shape.NumDimensions(); i++) {
    ORT_ENFORCE(label_shape[i] == logit_shape[i], "The shape of logit and label does not match");
  }

  int64_t N = label_shape.Size();
  int64_t D = logit_shape[logit_shape.NumDimensions() - 1];
  const int n = gsl::narrow_cast<int>(N);
  const int d = gsl::narrow_cast<int>(D);
  const int nd = gsl::narrow_cast<int>(N * D);

  Tensor* loss = context->Output(0, TensorShape({}));
  Tensor* probability = context->Output(1, logit_shape);

  const float* logit_data = logit.template Data<float>();
  const int64_t* label_data = label.template Data<int64_t>();
  float* loss_data = loss->template MutableData<float>();
  float* probability_data = probability->template MutableData<float>();

  // computation begins here
  std::vector<float> shifted_logit(nd);
  std::vector<float> log_sum_exp(n);
  ComputeShareSoftmaxCrossEntropyCPU(n, d, nd, logit_data,
                                     shifted_logit.data(),
                                     probability_data,
                                     log_sum_exp.data());

  std::vector<float> loss_sample(n);

  if (OpKernel::Node().InputDefs().size() == 3) {
    const Tensor& weight = *context->Input<Tensor>(2);
    const TensorShape weight_shape{weight.Shape()};
    ORT_ENFORCE(weight_shape == label_shape, "The shape of weight and label is different");
    const float* weight_data = weight.template Data<float>();
    for (int i = 0; i < n; i++) {
      loss_sample[i] = (log_sum_exp[i] - shifted_logit[i * d + label_data[i]]) * weight_data[i];
    }

    // Sum loss over n samples
    math::Sum<float, CPUMathUtil>(n, loss_sample.data(), loss_data, nullptr);

    // Average sum_loss over sum_weights
    if (reduction_ == ReductionType::MEAN) {
      float sum_weight;
      math::Sum<float, CPUMathUtil>(n, weight_data, &sum_weight, nullptr);
      *loss_data /= sum_weight;
    }
  } else {
    for (int i = 0; i < n; i++) {
      loss_sample[i] = log_sum_exp[i] - shifted_logit[i * d + label_data[i]];
    }
    // Sum loss over n samples
    math::Sum<float, CPUMathUtil>(n, loss_sample.data(), loss_data, nullptr);

    if (reduction_ == ReductionType::MEAN) {
      *loss_data /= n;
    }
  }
  return Status::OK();
}

ONNX_OPERATOR_KERNEL_EX(
    SparseSoftmaxCrossEntropyGrad,
    kOnnxDomain,
    9,
    kCpuExecutionProvider,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    SparseSoftmaxCrossEntropyGrad<float>);

template <typename T>
Status SparseSoftmaxCrossEntropyGrad<T>::Compute(OpKernelContext* context) const {
  const Tensor& dY = *context->Input<Tensor>(0);
  const Tensor& probability = *context->Input<Tensor>(1);
  const Tensor& label = *context->Input<Tensor>(2);

  const TensorShape probability_shape{probability.Shape()};
  const TensorShape label_shape{label.Shape()};
  ORT_ENFORCE(probability_shape.NumDimensions() == label_shape.NumDimensions() + 1,
              "probability_shape must be (1 + label_shape)");
  for (size_t i = 0; i < label_shape.NumDimensions(); i++) {
    ORT_ENFORCE(label_shape[i] == probability_shape[i], "The shape of probability and label does not match");
  }

  int64_t N = label_shape.Size();
  int64_t D = probability_shape[probability_shape.NumDimensions() - 1];
  const int n = gsl::narrow_cast<int>(N);
  const int d = gsl::narrow_cast<int>(D);

  Tensor* d_logit = context->Output(0, probability_shape);

  const float* dY_data = dY.template Data<float>();
  const float* probability_data = probability.template Data<float>();
  const int64_t* label_data = label.template Data<int64_t>();
  float* d_logit_data = d_logit->template MutableData<float>();

  // computation begins here
  if (OpKernel::Node().InputDefs().size() == 4) {
    const Tensor& weight = *context->Input<Tensor>(3);
    const TensorShape weight_shape{weight.Shape()};
    ORT_ENFORCE(weight_shape == label_shape, "The shape of weight and label is different");
    const float* weight_data = weight.template Data<float>();

    float dY_scaled = *dY_data;
    if (reduction_ == ReductionType::MEAN) {
      float sum_weight;
      math::Sum<float, CPUMathUtil>(n, weight_data, &sum_weight, nullptr);
      dY_scaled = *dY_data / sum_weight;
    }

    for (int i = 0; i < n; i++) {
      int64_t label_sample = label_data[i];
      float weight_smaple = weight_data[i] * dY_scaled;
      for (int j = 0; j < d; j++) {
        int index = i * d + j;
        d_logit_data[index] = (probability_data[index] - (label_sample == j)) * weight_smaple;
      }
    }
  } else {
    float dY_scaled = *dY_data;
    if (reduction_ == ReductionType::MEAN) {
      dY_scaled = *dY_data / n;
    }

    for (int i = 0; i < n; i++) {
      int64_t label_sample = label_data[i];
      for (int j = 0; j < d; j++) {
        int index = i * d + j;
        d_logit_data[index] = (probability_data[index] - (label_sample == j)) * dY_scaled;
      }
    }
  }

  return Status::OK();
}

}  // namespace contrib
}  // namespace onnxruntime
