/**
* Copyright (c) 2016-present, Facebook, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/* Modifications Copyright (c) Microsoft. */

#include "core/providers/cuda/cu_inc/common.cuh"
#include "orttraining/training_ops/cuda/nn/dropout_impl.h"
#include <curand_kernel.h>
#include <algorithm>

namespace onnxruntime {
namespace cuda {

template <typename T, int NumThreadsPerBlock, int NumElementsPerThread>
__global__ void DropoutGradientKernel(
    const int64_t N,
    const T* dY_data,
    const bool* mask_data,
    const float scale,
    T* dX_data) {
  CUDA_LONG id = NumElementsPerThread * NumThreadsPerBlock * blockIdx.x + threadIdx.x;
#pragma unroll
  for (int i = 0; i < NumElementsPerThread; i++) {
    if (id < N) {
      dX_data[id] = T(float(dY_data[id]) * mask_data[id] * scale);
      id += NumThreadsPerBlock;
    }
  }
}

template <typename T>
void DropoutGradientKernelImpl(
    const int64_t N,
    const T* dY_data,
    const bool* mask_data,
    const float ratio,
    T* dX_data) {
  if (ratio == 0.0f) {
    if (dY_data != dX_data) {
      CUDA_CALL_THROW(cudaMemcpyAsync(dX_data, dY_data, N * sizeof(T), cudaMemcpyDeviceToDevice));
    }
  } else {
    const float scale = 1.f / (1.f - ratio);
    const int blocksPerGrid = static_cast<int>(CeilDiv(N, GridDim::maxThreadsPerBlock * GridDim::maxElementsPerThread));
    DropoutGradientKernel<T, GridDim::maxThreadsPerBlock, GridDim::maxElementsPerThread>
                         <<<blocksPerGrid, GridDim::maxThreadsPerBlock, 0>>>(N, dY_data, mask_data, scale, dX_data);
  }
}

#define SPECIALIZED_DROPOUT_GRAD_IMPL(T)   \
  template void DropoutGradientKernelImpl( \
      const int64_t N,                     \
      const T* dY_data,                    \
      const bool* mask_data,               \
      const float scale,                   \
      T* dX_data);

SPECIALIZED_DROPOUT_GRAD_IMPL(float)
SPECIALIZED_DROPOUT_GRAD_IMPL(double)
SPECIALIZED_DROPOUT_GRAD_IMPL(half)

constexpr int UNROLL = 4;

template <typename T, bool has_residual>
__global__ void BiasDropoutKernel(
    const int64_t N,
    const fast_divmod fdm_dim,
    const float ratio,
    const std::pair<uint64_t, uint64_t> seeds,
    const T* X_data,
    const T* bias_data,
    const T* residual_data,
    T* Y_data,
    bool* mask_data) {
  const float p = 1.0f - ratio;
  const float scale = 1.0f / p;

  CUDA_LONG idx = blockDim.x * blockIdx.x + threadIdx.x;
  CUDA_LONG step_size = gridDim.x * blockDim.x * UNROLL;
  CUDA_LONG rounded_size = ((N - 1) / step_size + 1) * step_size;

  curandStatePhilox4_32_10_t state;
  curand_init(seeds.first, idx, seeds.second, &state);

  // We ensure every thread generates the same number of random numbers (by rounding
  // up the size) and at the same timestep (by syncing threads).
  // From CUDA curand documentation:
  //   The Philox_4x32_10 algorithm is closely tied to the thread and block count.
  //   Each thread computes 4 random numbers in the same time thus the most efficient
  //   use of Philox_4x32_10 is to generate a multiple of 4 times number of threads.
  for (CUDA_LONG id = idx; id < rounded_size; id += step_size) {
    float4 rand = curand_uniform4(&state);

  #pragma unroll
    for (CUDA_LONG i = 0; i < UNROLL; i++) {
      CUDA_LONG li = id + gridDim.x * blockDim.x * i;
      if (li < N) {
        int offset = fdm_dim.mod(li);
        float bias = float(bias_data[offset]);

        mask_data[li] = (&rand.x)[i] < p;
        float output_data = (float(X_data[li]) + bias) * mask_data[li] * scale;
        if (has_residual) {
          output_data += float(residual_data[li]);
        }

        Y_data[li] = T(output_data);
      }
    }

    __syncthreads();
  }
}

template <typename T>
void BiasDropoutKernelImpl(
    const cudaDeviceProp& prop,
    const int64_t N,
    const fast_divmod fdm_dim,
    const float ratio,
    PhiloxGenerator& generator,
    const T* X_data,
    const T* bias_data,
    const T* residual_data,
    T* Y_data,
    bool* mask_data) {
  const int block_size = 256;
  const int blocks_per_sm = prop.maxThreadsPerMultiProcessor / block_size;
  const int grid_size = std::min(prop.multiProcessorCount * blocks_per_sm, static_cast<int>(CeilDiv(N, block_size * UNROLL)));

  // Compute the number of random numbers generated by each thread, and increment philox generator offset by that amount.
  const uint64_t counter_offset = static_cast<uint64_t>(((N - 1) / (block_size * grid_size * UNROLL) + 1) * UNROLL);
  auto seeds = generator.NextPhiloxSeeds(counter_offset);

  if (residual_data == nullptr) {
    BiasDropoutKernel<T, false><<<grid_size, block_size, 0>>>(N, fdm_dim, ratio, seeds, X_data, bias_data, residual_data, Y_data, mask_data);
  } else {
    BiasDropoutKernel<T, true><<<grid_size, block_size, 0>>>(N, fdm_dim, ratio, seeds, X_data, bias_data, residual_data, Y_data, mask_data);
  }
}

#define SPECIALIZED_BIAS_DROPOUT_IMPL(T) \
  template void BiasDropoutKernelImpl(  \
      const cudaDeviceProp& prop,   \
      const int64_t N,              \
      const fast_divmod fdm_dim,    \
      const float ratio,            \
      PhiloxGenerator& generator,   \
      const T* X_data,              \
      const T* bias_data,           \
      const T* residual_data,       \
      T* Y_data,                    \
      bool* mask_data);

SPECIALIZED_BIAS_DROPOUT_IMPL(float)
SPECIALIZED_BIAS_DROPOUT_IMPL(double)
SPECIALIZED_BIAS_DROPOUT_IMPL(half)


}  // namespace cuda
}  // namespace onnxruntime
