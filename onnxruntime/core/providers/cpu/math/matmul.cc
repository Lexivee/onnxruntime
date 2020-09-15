// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/framework/op_kernel.h"
#include "core/providers/cpu/math/matmul_helper.h"
#include "core/util/math.h"
#include "core/util/math_cpuonly.h"
#include "core/mlas/inc/mlas.h"

namespace onnxruntime {

template <typename T>
class MatMul final : public OpKernel {
 public:
  MatMul(const OpKernelInfo& info) : OpKernel(info) {}

  Status Compute(OpKernelContext* context) const override;
};

#if !defined(USE_MKLML_FOR_BLAS)

template <>
class MatMul<float> final : public OpKernel {
 public:
  MatMul(const OpKernelInfo& info) : OpKernel(info) {}

  Status PrePack(const Tensor& tensor, int input_idx, bool& is_packed) override;
  Status Compute(OpKernelContext* context) const override;

 private:
  TensorShape b_shape_;
  BufferUniquePtr packed_b_;
};

#endif

ONNX_CPU_OPERATOR_VERSIONED_TYPED_KERNEL(
    MatMul,
    1, 8,
    float,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    MatMul<float>);

ONNX_CPU_OPERATOR_VERSIONED_TYPED_KERNEL(
    MatMul,
    1, 8,
    double,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<double>()),
    MatMul<double>);

// opset 9 supports more types
ONNX_CPU_OPERATOR_TYPED_KERNEL(
    MatMul,
    9,
    float,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    MatMul<float>);

ONNX_CPU_OPERATOR_TYPED_KERNEL(
    MatMul,
    9,
    double,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<double>()),
    MatMul<double>);

ONNX_CPU_OPERATOR_TYPED_KERNEL(
    MatMul,
    9,
    int32_t,
    KernelDefBuilder()
        .TypeConstraint("T", {DataTypeImpl::GetTensorType<int32_t>(), DataTypeImpl::GetTensorType<uint32_t>()}),
    MatMul<int32_t>);

ONNX_CPU_OPERATOR_TYPED_KERNEL(
    MatMul,
    9,
    int64_t,
    KernelDefBuilder()
        .TypeConstraint("T", {DataTypeImpl::GetTensorType<int64_t>(), DataTypeImpl::GetTensorType<uint64_t>()}),
    MatMul<int64_t>);

template <typename T>
Status MatMul<T>::Compute(OpKernelContext* ctx) const {
  concurrency::ThreadPool* thread_pool = ctx->GetOperatorThreadPool();

  const auto* a = ctx->Input<Tensor>(0);
  const auto* b = ctx->Input<Tensor>(1);

  MatMulComputeHelper helper;
  ORT_RETURN_IF_ERROR(helper.Compute(a->Shape(), b->Shape()));
  Tensor* y = ctx->Output(0, helper.OutputShape());

  // Bail out early if the output is going to be empty
  if (y->Shape().Size() == 0)
    return Status::OK();

  // Using DataRaw as int32_t/uint32_t and int64_t/uint64_t share a common
  // operator body.
  const auto* a_data = reinterpret_cast<const T*>(a->DataRaw());
  const auto* b_data = reinterpret_cast<const T*>(b->DataRaw());
  auto* y_data = reinterpret_cast<T*>(y->MutableDataRaw());

  // TODO: replace it with GemmBatch for performance, it's OK for now as GemmBatch unrolls as well
  size_t max_len = helper.OutputOffsets().size();
  for (size_t i = 0; i < max_len; i++) {
    math::MatMul<T>(
        static_cast<int>(helper.M()),
        static_cast<int>(helper.N()),
        static_cast<int>(helper.K()),
        a_data + helper.LeftOffsets()[i],
        b_data + helper.RightOffsets()[i],
        y_data + helper.OutputOffsets()[i],
        thread_pool);
  }

  return Status::OK();
}

#if !defined(USE_MKLML_FOR_BLAS)

Status MatMul<float>::PrePack(const Tensor& tensor, int input_idx, bool& is_packed) {
  is_packed = false;

  // only pack Matrix B
  if (input_idx == 1) {
    // Only handle the common case of a 2D weight matrix. Additional matrices
    // could be handled by stacking the packed buffers.
    b_shape_ = tensor.Shape();
    if (b_shape_.NumDimensions() != 2) {
      return Status::OK();
    }

    const size_t K = static_cast<size_t>(b_shape_[0]);
    const size_t N = static_cast<size_t>(b_shape_[1]);

    const size_t packed_b_size = MlasGemmPackBSize(N, K);
    if (packed_b_size == 0) {
      return Status::OK();
    }

    auto alloc = Info().GetAllocator(0, OrtMemTypeDefault);
    auto* packed_b_data = alloc->Alloc(packed_b_size);
    packed_b_ = BufferUniquePtr(packed_b_data, BufferDeleter(alloc));
    MlasGemmPackB(CblasNoTrans, N, K, tensor.Data<float>(), N, packed_b_data);
    is_packed = true;
  }
  return Status::OK();
}

Status MatMul<float>::Compute(OpKernelContext* ctx) const {
  concurrency::ThreadPool* thread_pool = ctx->GetOperatorThreadPool();

  const Tensor* a = ctx->Input<Tensor>(0);
  const Tensor* b = packed_b_ ? nullptr : ctx->Input<Tensor>(1);

  MatMulComputeHelper helper;
  ORT_RETURN_IF_ERROR(helper.Compute(a->Shape(), b ? b->Shape() : b_shape_));
  Tensor* y = ctx->Output(0, helper.OutputShape());

  // Bail out early if the output is going to be empty
  if (y->Shape().Size() == 0)
    return Status::OK();

  const auto* a_data = a->Data<float>();
  const auto* b_data = b ? b->Data<float>() : nullptr;
  auto* y_data = y->MutableData<float>();

  // TODO: replace it with GemmBatch for performance, it's OK for now as GemmBatch unrolls as well
  size_t max_len = helper.OutputOffsets().size();
  for (size_t i = 0; i < max_len; i++) {
    if (packed_b_) {
      MlasGemm(CblasNoTrans,
               static_cast<size_t>(helper.M()),
               static_cast<size_t>(helper.N()),
               static_cast<size_t>(helper.K()),
               1.0f,
               a_data + helper.LeftOffsets()[i],
               static_cast<size_t>(helper.K()),
               packed_b_.get(),
               0.0f,
               y_data + helper.OutputOffsets()[i],
               static_cast<size_t>(helper.N()),
               thread_pool);
    } else {
      math::MatMul(
          static_cast<int>(helper.M()),
          static_cast<int>(helper.N()),
          static_cast<int>(helper.K()),
          a_data + helper.LeftOffsets()[i],
          b_data + helper.RightOffsets()[i],
          y_data + helper.OutputOffsets()[i],
          thread_pool);
    }
  }

  return Status::OK();
}

#endif

}  // namespace onnxruntime
