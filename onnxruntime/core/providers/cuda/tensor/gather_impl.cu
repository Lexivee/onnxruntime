// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cuda/cu_inc/common.cuh"
#include "gather_impl.h"

namespace onnxruntime {
namespace cuda {

template <typename T, typename Tin, int NumThreadsPerBlock, int NumElementsPerThread>
__global__ void _GatherKernel(
    const int64_t input_block_size,
    const int64_t indices_max,
    const Tin* indices_data,
    const fast_divmod* div_strides,
    const T* input_data,
    T* output_data,
    const CUDA_LONG N) {
  CALCULATE_ELEMENTWISE_INDEX_OR_EXIT(id, N, NumElementsPerThread);

  #pragma unroll
  for (int i = 0; i < NumElementsPerThread; i++) {
    if (id < N) {
      CUDA_LONG input_index = 0;
      int input_block_index, block_offset;
      div_strides[0].divmod(id, input_block_index, block_offset);
      int indices_index, offset;
      div_strides[1].divmod(block_offset, indices_index, offset);
      int block_size = div_strides[1].d_;
      int64_t idx = indices_data[indices_index];
      if (idx < 0 || idx >= indices_max) {
        output_data[id] = 0;
        return;
      }

      input_index = input_block_index * input_block_size + idx * block_size + offset;
      output_data[id] = input_data[input_index];
      id += NumThreadsPerBlock;
    }
  }
}

template <typename T, typename Tin>
void GatherImpl(
    const int64_t input_block_size,
    const int64_t indices_max,
    const Tin* indices_data,
    const fast_divmod* div_strides,
    const T* input_data,
    T* output_data,
    const size_t N) {
  int blocksPerGrid = static_cast<int>(CeilDiv(N, GridDim::maxThreadsPerBlock * GridDim::maxElementsPerThread));
  _GatherKernel<T, Tin, GridDim::maxThreadsPerBlock, GridDim::maxElementsPerThread>\
    <<<blocksPerGrid, GridDim::maxThreadsPerBlock, 0>>>(
      input_block_size, indices_max, indices_data, div_strides, input_data, output_data, (CUDA_LONG)N);
}

#define SPECIALIZED_IMPL(T)                                                                                                                                                                                          \
  template void GatherImpl<T, int32_t>(const int64_t input_block_size, const int64_t indices_max, const int32_t* indices_data, const fast_divmod* div_strides, const T* input_data, T* output_data, const size_t N); \
  template void GatherImpl<T, int64_t>(const int64_t input_block_size, const int64_t indices_max, const int64_t* indices_data, const fast_divmod* div_strides, const T* input_data, T* output_data, const size_t N);

SPECIALIZED_IMPL(int8_t)
SPECIALIZED_IMPL(int16_t)
SPECIALIZED_IMPL(int32_t)
SPECIALIZED_IMPL(int64_t)
SPECIALIZED_IMPL(uint8_t)
SPECIALIZED_IMPL(uint16_t)
SPECIALIZED_IMPL(uint32_t)
SPECIALIZED_IMPL(uint64_t)
SPECIALIZED_IMPL(half)
SPECIALIZED_IMPL(float)
SPECIALIZED_IMPL(double)
SPECIALIZED_IMPL(bool)

template <typename T, typename Tin, int NumThreadsPerBlock, int NumElementsPerThread>
__global__ void _GatherGradKernel(
    const int64_t input_block_size,
    const int64_t indices_max,
    const Tin* indices_data,
    const fast_divmod* div_strides,
    const T* grad_data,
    T* output_data,
    const CUDA_LONG N) {
  CALCULATE_ELEMENTWISE_INDEX_OR_EXIT(id, N, NumElementsPerThread);

  #pragma unroll
  for (int i = 0; i < NumElementsPerThread; i++) {
    if (id < N) {
      CUDA_LONG input_index = 0;
      int input_block_index, block_offset;
      div_strides[0].divmod(id, input_block_index, block_offset);
      int indices_index, offset;
      div_strides[1].divmod(block_offset, indices_index, offset);
      int block_size = div_strides[1].d_;
      int64_t idx = indices_data[indices_index];
      if (idx < 0 || idx >= indices_max) {
        output_data[id] = 0;
        return;
      }

      input_index = input_block_index * input_block_size + idx * block_size + offset;
      atomicAdd(output_data + input_index, grad_data[id]);
      id += NumThreadsPerBlock;
    }
  }
}

template <typename T, typename Tin>
void GatherGradImpl(
    const int64_t input_block_size,
    const int64_t indices_max,
    const Tin* indices_data,
    const fast_divmod* div_strides,
    const T* grad_data,
    T* output_data,
    const size_t N) {
  int blocksPerGrid = static_cast<int>(CeilDiv(N, GridDim::maxThreadsPerBlock * GridDim::maxElementsPerThread));
  _GatherGradKernel<T, Tin, GridDim::maxThreadsPerBlock, GridDim::maxElementsPerThread>\
    <<<blocksPerGrid, GridDim::maxThreadsPerBlock, 0>>>(
      input_block_size, indices_max, indices_data, div_strides, grad_data, output_data, (CUDA_LONG)N);
}

#define SPECIALIZED_GRAD_IMPL(T)                                                                                                                                                                                        \
  template void GatherGradImpl<T, int32_t>(const int64_t input_block_size, const int64_t indices_max, const int32_t* indices_data, const fast_divmod* div_strides, const T* grad_data, T* output_data, const size_t N); \
  template void GatherGradImpl<T, int64_t>(const int64_t input_block_size, const int64_t indices_max, const int64_t* indices_data, const fast_divmod* div_strides, const T* grad_data, T* output_data, const size_t N);

// TODO: AtomicAdd doesn't have full support in all date types

//SPECIALIZED_GRAD_IMPL(int8_t)
//SPECIALIZED_GRAD_IMPL(int16_t)
SPECIALIZED_GRAD_IMPL(int32_t)
//SPECIALIZED_GRAD_IMPL(int64_t)
//SPECIALIZED_GRAD_IMPL(uint8_t)
//SPECIALIZED_GRAD_IMPL(uint16_t)
SPECIALIZED_GRAD_IMPL(uint32_t)
//SPECIALIZED_GRAD_IMPL(uint64_t)
#if !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 700
SPECIALIZED_GRAD_IMPL(half)
#endif
SPECIALIZED_GRAD_IMPL(float)
//SPECIALIZED_GRAD_IMPL(double)
//SPECIALIZED_GRAD_IMPL(bool)

}  // namespace cuda
}  // namespace onnxruntime
