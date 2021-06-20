//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#include "core/providers/common.h"
#include "core/providers/cpu/tensor/copy.h"
#include "core/platform/threadpool.h"

namespace onnxruntime {

std::vector<int64_t> StridesForTensor(const Tensor& dst) {
  std::vector<int64_t> strides(dst.Shape().NumDimensions());
  for (std::size_t i = 0; i < dst.Shape().NumDimensions(); i++) {
    strides[i] = 1;
    for (std::size_t j = i + 1; j < dst.Shape().NumDimensions(); j++) {
      strides[i] *= dst.Shape()[j];
    }
  }
  return strides;
}

Status DispatchStridedCopy(concurrency::ThreadPool* thread_pool,
                           Tensor& dst,
                           std::ptrdiff_t dst_offset,
                           const std::vector<int64_t> dst_strides,
                           const TensorShape& copy_shape,
                           const Tensor& src,
                           const std::vector<int64_t> src_strides) {
#define CALL_FOR_TYPE(T) \
  StridedCopy<T>(thread_pool, dst.MutableData<T>() + dst_offset, copy_shape, dst_strides, src.Data<T>(), src_strides)
  ORT_RETURN_IF_NOT(dst.DataType() == src.DataType(), "src and dst types must match");
  if (dst.IsDataType<float>()) {
    CALL_FOR_TYPE(float);
  } else if (dst.IsDataType<double>()) {
    CALL_FOR_TYPE(double);
  } else if (dst.IsDataType<int32_t>()) {
    CALL_FOR_TYPE(int32_t);
  } else if (dst.IsDataType<int64_t>()) {
    CALL_FOR_TYPE(int64_t);
  } else if (dst.IsDataTypeString()) {
    CALL_FOR_TYPE(std::string);
  } else {
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "unsupported data type");
  }
  return Status::OK();
}

template <typename T>
void StridedCopy(concurrency::ThreadPool* thread_pool,
                 T* dst,
                 const TensorShape& dst_shape,
                 const std::vector<int64_t>& dst_strides,
                 const T* src,
                 const std::vector<int64_t>& src_strides) {
  const size_t dims = dst_shape.NumDimensions();
  // We will iterate over the output dimensions
  int64_t num_iterations = 1;
  for (size_t dim = 0; dim < dims; dim++) {
    num_iterations *= dst_shape[dim];
  }

  // TODO(orausch): Reorder dimensions so that we iterate along the smallest strides first
  // TODO(orausch): remove single size dimensions
  concurrency::ThreadPool::TryParallelFor(
      thread_pool, num_iterations,
      {static_cast<float>(sizeof(T)), static_cast<float>(sizeof(T)), 1.0F},
      [dst_shape, dst_strides, dst, src, src_strides, dims](std::ptrdiff_t first, std::ptrdiff_t last) {
        // Compute the initial n-dimensional index and addresses
        std::vector<int64_t> current_nd_idx(dims);
        {
          size_t current_index = first;
          for (size_t dim = dims; dim > 0; dim--) {
            // Iterate from dims to 1 so we don't roll over to positive on the bounds check
            current_nd_idx[dim - 1] = current_index % dst_shape[dim - 1];
            current_index /= dst_shape[dim - 1];
          }
        }

        for (std::ptrdiff_t outer_i = first; outer_i < last;) {
          // Compute the src and dst addresses
          size_t dst_idx = 0;
          size_t src_idx = 0;
          for (size_t dim = 0; dim < dims; dim++) {
            dst_idx += current_nd_idx[dim] * dst_strides[dim];
            src_idx += current_nd_idx[dim] * src_strides[dim];
          }

          // 1d vectorizable inner loop along last dimension
          std::ptrdiff_t inner_end = std::min(last, outer_i + dst_shape[dims - 1] - current_nd_idx[dims - 1]);
          for (std::ptrdiff_t i = outer_i; i < inner_end; i++) {
            dst[dst_idx] = src[src_idx];
            dst_idx += dst_strides[dims - 1];
            src_idx += src_strides[dims - 1];
          }
          current_nd_idx[dims - 1] += inner_end - outer_i;

          outer_i = inner_end;

          // update the current_nd_idx if needed
          size_t dim = dims - 1;
          while (dim > 0 && current_nd_idx[dim] >= dst_shape[dim]) {
            current_nd_idx[dim] = 0;
            dim--;
            current_nd_idx[dim]++;
          }
        }
      });
}

#define STRIDED_COPY_IMPL(T)                                            \
  template void StridedCopy<T>(concurrency::ThreadPool * thread_pool,   \
                               T * dst,                                 \
                               const TensorShape& dst_shape,            \
                               const std::vector<int64_t>& dst_strides, \
                               const T* src,                            \
                               const std::vector<int64_t>& src_strides);

STRIDED_COPY_IMPL(int32_t)
STRIDED_COPY_IMPL(int64_t)
STRIDED_COPY_IMPL(float)
STRIDED_COPY_IMPL(double)
}  // namespace onnxruntime
