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
#include "core/providers/cuda/nn/dropout_impl.h"
#include <curand_kernel.h>
#include <algorithm>

namespace onnxruntime {
namespace cuda {

constexpr int UNROLL = 4;

template <typename T>
__global__ void DropoutKernel(
    const int64_t N,
    const float ratio,
    const std::pair<uint64_t, uint64_t> seeds,
    const T* X_data,
    T* Y_data,
    bool* mask_data) {
  const float p = 1.0f - ratio;
  const float scale = 1.0f / p;

  CUDA_LONG idx = blockDim.x * blockIdx.x + threadIdx.x;
  CUDA_LONG step_size = gridDim.x * blockDim.x * UNROLL;
  CUDA_LONG rounded_size = ((N - 1) / step_size + 1) * step_size;

  curandStatePhilox4_32_10_t state;
  curand_init(seeds.first, idx, seeds.second, &state);

  float4 rand;

  // We ensure every thread generates the same number of random numbers (by rounding
  // up the size) and at the same timestep (by syncing threads).
  // From CUDA curand documentation:
  //   The Philox_4x32_10 algorithm is closely tied to the thread and block count.
  //   Each thread computes 4 random numbers in the same time thus the most efficient
  //   use of Philox_4x32_10 is to generate a multiple of 4 times number of threads.
  for (CUDA_LONG id = idx; id < rounded_size; id += step_size) {
    rand = curand_uniform4(&state);
  
    #pragma unroll
    for (CUDA_LONG i = 0; i < UNROLL; i++) {
      CUDA_LONG li = id + gridDim.x * blockDim.x * i;
      if (li < N) {
        mask_data[li] = (&rand.x)[i] < p;
        Y_data[li] = T(float(X_data[li]) * mask_data[li] * scale);
      }
    }

    __syncthreads();
  }

}

template <typename T>
__global__ void DropoutVectorizedKernel(
    const int64_t N,
    const float ratio,
    const std::pair<uint64_t, uint64_t> seeds,
    const T* X_data,
    T* Y_data,
    bool* mask_data) {
  const float p = 1.0f - ratio;
  const float scale = 1.0f / p;

  CUDA_LONG idx = blockDim.x * blockIdx.x + threadIdx.x;
  CUDA_LONG step_size = gridDim.x * blockDim.x * UNROLL;
  CUDA_LONG rounded_size = ((N - 1) / step_size + 1) * step_size;

  curandStatePhilox4_32_10_t state;
  curand_init(seeds.first, idx, seeds.second, &state);

  float4 rand;

  // using vectorized data load/store approach when N % 4 == 0 since this is 
  // typical case for input shape size
  using LoadT = aligned_vector<T, UNROLL>;
  using MaskLoadT = aligned_vector<bool, UNROLL>;

  for (CUDA_LONG id = idx * UNROLL; id < N; id += step_size) {
    rand = curand_uniform4(&state);
  
    // vectorized load into storage
    T src[UNROLL];
    LoadT *value = reinterpret_cast<LoadT*>(&src);
    *value = *reinterpret_cast<const LoadT*>(&X_data[id]);

    T r[UNROLL];
    bool mask[UNROLL];

    // actual computation
    #pragma unroll
    for (int ii = 0; ii < UNROLL; ii++) {
      mask[ii] = (&rand.x)[ii] < p;
      r[ii] = T(float(src[ii]) * mask[ii] * scale);
    }
    // Vectorized writes for mask_data & Y_data
    *(reinterpret_cast<LoadT*>(&Y_data[id])) = *reinterpret_cast<LoadT*>(&r[0]);
    *(reinterpret_cast<MaskLoadT*>(&mask_data[id])) = *reinterpret_cast<MaskLoadT*>(&mask[0]);

    __syncthreads();
  }

}

template <typename T>
void DropoutKernelImpl(
    const cudaDeviceProp& prop,
    cudaStream_t stream,
    const int64_t N,
    const float ratio,
    PhiloxGenerator& generator,
    const T* X_data,
    T* Y_data,
    bool* mask_data) {
  const int block_size = 256;
  const int blocks_per_sm = prop.maxThreadsPerMultiProcessor / block_size;
  const int grid_size = std::min(prop.multiProcessorCount * blocks_per_sm, static_cast<int>(CeilDiv(N, block_size * UNROLL)));

  // Compute the number of random numbers generated by each thread, and increment philox generator offset by that amount.
  const uint64_t counter_offset = static_cast<uint64_t>(((N - 1) / (block_size * grid_size * UNROLL) + 1) * UNROLL);
  auto seeds = generator.NextPhiloxSeeds(counter_offset);

  if ( N % UNROLL != 0) {
    DropoutKernel<T><<<grid_size, block_size, 0, stream>>>(N, ratio, seeds, X_data, Y_data, mask_data);
  } else {
    DropoutVectorizedKernel<T><<<grid_size, block_size, 0, stream>>>(N, ratio, seeds, X_data, Y_data, mask_data);
  }
}

#define SPECIALIZED_DROPOUT_IMPL(T) \
  template void DropoutKernelImpl(  \
      const cudaDeviceProp& prop,   \
      cudaStream_t stream,          \
      const int64_t N,              \
      const float ratio,            \
      PhiloxGenerator& generator,   \
      const T* X_data,              \
      T* Y_data,                    \
      bool* mask_data);

SPECIALIZED_DROPOUT_IMPL(float)
SPECIALIZED_DROPOUT_IMPL(double)
SPECIALIZED_DROPOUT_IMPL(half)
#if CUDA_VERSION >= 11000 && (__CUDA_ARCH__ >= 800 || !defined(__CUDA_ARCH__))
SPECIALIZED_DROPOUT_IMPL(nv_bfloat16)
#endif

}  // namespace cuda
}  // namespace onnxruntime
