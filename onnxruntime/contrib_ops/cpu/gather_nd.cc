// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "contrib_ops/cpu/gather_nd.h"
#include "core/framework/op_kernel_context_internal.h"

using namespace onnxruntime::concurrency;

namespace onnxruntime {
namespace contrib     {

ONNX_OPERATOR_KERNEL_EX(
    GatherND,
    kMSDomain,
    1,
    kCpuExecutionProvider,
    KernelDefBuilder()
        .TypeConstraint("T",    DataTypeImpl::AllTensorTypes())
        .TypeConstraint("Tind", {DataTypeImpl::GetTensorType<int32_t>(),DataTypeImpl::GetTensorType<int64_t>()}),
    GatherND);

template<typename Tind>
Status GatherNDBase::PrepareForCompute(OpKernelContext* context, Prepare& p) const {

  auto input_tensor  = context->Input<Tensor>(0);
  auto indice_tensor = context->Input<Tensor>(1);
  ORT_ENFORCE(input_tensor  != nullptr);
  ORT_ENFORCE(indice_tensor != nullptr);

  auto input_shape   = input_tensor->Shape();
  auto indice_shape  = indice_tensor->Shape();
  if (indice_shape.NumDimensions() == 0) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
      "indices tensor must has rank larger than 0");
  }

  auto last_indice_dimension = indice_shape[indice_shape.NumDimensions() - 1];
  if (last_indice_dimension > static_cast<int64_t>(input_shape.NumDimensions())) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
      "last dimension of indices must not be larger than rank of input tensor");
  }

  std::vector<int64_t> shape(indice_shape.GetDims().begin(),
                             indice_shape.GetDims().end() - 1);
  shape.insert(shape.end(),
               input_shape.GetDims().begin() + last_indice_dimension,
               input_shape.GetDims().end());
  auto output_tensor = context->Output(0,TensorShape(shape));
  std::vector<int64_t> element_counts(last_indice_dimension, 0LL); // Number of elements for each input dimension

  // Get access to the internal threadpool
  auto ctx_internal = static_cast<OpKernelContextInternal*>(context);
  auto thread_pool = ctx_internal->GetOperatorThreadPool();

  std::function<void(int)> work_object_counts = [&](int32_t i) {
    element_counts[i] = input_shape.SizeFromDimension(i + 1);
  };
  const_cast<ThreadPool*>(thread_pool)->ParallelFor((int32_t)last_indice_dimension, work_object_counts);

  int64_t err_indice = 0;
  p.element_bytes    = input_tensor->DataType()->Size();
  p.element_to_copy  = input_shape.SizeFromDimension(last_indice_dimension);
  p.bytes_to_copy    = p.element_bytes * p.element_to_copy;
  auto indice_offset = indice_tensor->Data<Tind>();
  auto offset_count  = indice_shape.Size() / last_indice_dimension; // Times to copy
  p.element_offsets.assign(offset_count, 0LL);

  if (input_tensor->DataType() == DataTypeImpl::GetType<std::string>()) {
    p.input_str_base  = static_cast<const std::string*>(input_tensor->DataRaw());
    p.output_str_base = static_cast<std::string*>(output_tensor->MutableDataRaw());
  } else {
    p.input_base      = static_cast<const uint8_t*>(input_tensor->DataRaw());
    p.output_base     = static_cast<uint8_t*>(output_tensor->MutableDataRaw());
  }

  std::function<void(int32_t)> work_object_offsets = [&](int32_t i) {
    for (int64_t j = 0; j < last_indice_dimension; ++j) {
      auto indice = *(indice_offset + i * last_indice_dimension + j);
      if (indice < 0 || indice >= input_shape[j]) {
        err_indice = indice;
      }
      p.element_offsets[i] += indice * element_counts[j];
    }
  };
  const_cast<ThreadPool*>(thread_pool)->ParallelFor((int32_t)offset_count, work_object_offsets);

  return err_indice == 0 ? Status::OK() :
    ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "invalid indice found, indice = ", err_indice);
}

template Status GatherNDBase::PrepareForCompute<int32_t>(OpKernelContext*, Prepare&) const;
template Status GatherNDBase::PrepareForCompute<int64_t>(OpKernelContext*, Prepare&) const;

Status GatherND::Compute(OpKernelContext* context) const {
  Prepare p;
  ORT_RETURN_IF_ERROR(context->Input<Tensor>(1)->DataType() == DataTypeImpl::GetType<int32_t>() ? 
                              PrepareForCompute<int32_t>(context, p) : PrepareForCompute<int64_t>(context, p));

  // Get access to the internal threadpool
  auto ctx_internal = static_cast<OpKernelContextInternal*>(context);
  auto thread_pool = ctx_internal->GetOperatorThreadPool();

  return nullptr == p.input_str_base ? GatherNumber(p, thread_pool) : GatherString(p, thread_pool);
}

Status GatherND::GatherNumber(const Prepare& p, const ThreadPool *ttp) const {
  std::function<void(int32_t)> work_object = [&](int32_t i) {
    memcpy(p.output_base + i * p.bytes_to_copy,
           p.input_base + p.element_offsets[i] * p.element_bytes,
           p.bytes_to_copy);
  };
  const_cast<ThreadPool*>(ttp)->ParallelFor(static_cast<int32_t>(p.element_offsets.size()), work_object);

  return Status::OK();
}

Status GatherND::GatherString(const Prepare& p, const ThreadPool* ttp) const {
  std::function<void(int32_t)> work_object = [&](int32_t i) {
    for (int64_t j = 0; j < static_cast<int64_t>(p.element_to_copy); ++j) {
      p.output_str_base[i * p.element_to_copy + j] = p.input_str_base[p.element_offsets[i] + j];
    }
  };
  const_cast<ThreadPool*>(ttp)->ParallelFor(static_cast<int32_t>(p.element_offsets.size()), work_object);

  return Status::OK();
}

}
}
