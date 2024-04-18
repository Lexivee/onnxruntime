// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cuda/cuda_common.h"
#include "core/providers/cuda/cu_inc/common.cuh"
#include "cub/util_type.cuh"
#include <cub/cub.cuh>
#include <cub/device/device_segmented_radix_sort.cuh>
#include "contrib_ops/cuda/bert/utils.cuh"
#include "contrib_ops/cuda/transformers/generation_cuda_impl.h"

namespace onnxruntime {
namespace contrib {
namespace cuda {
__global__ void InitKernel(float* beam_scores,
                           int num_beams,
                           int total_elements) {
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  if (index < total_elements) {
    int beam_index = index % num_beams;
    beam_scores[index] = beam_index > 0 ? static_cast<float>(-1e9) : 0.0f;
  }
}

void LaunchInitKernel(
    float* beam_scores,
    int batch_size,
    int num_beams,
    cudaStream_t stream) {
  int total_elements = batch_size * num_beams;
  constexpr int blockSize = 256;
  const int gridSize = (total_elements + blockSize - 1) / blockSize;
  InitKernel<<<gridSize, blockSize, 0, stream>>>(beam_scores, num_beams, total_elements);
}

__global__ void NextTokenKernel(const int64_t* next_token_indices,
                                int32_t* next_indices,
                                int32_t* next_tokens,
                                int vocab_size,
                                int total_elements) {
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  if (index < total_elements) {
    next_indices[index] = next_token_indices[index] / vocab_size;
    next_tokens[index] = next_token_indices[index] % vocab_size;
  }
}

void LaunchNextTokenKernel(const int64_t* next_token_indices,
                           int32_t* next_indices,
                           int32_t* next_tokens,
                           int batch_size,
                           int top_k,
                           int vocab_size,
                           cudaStream_t stream) {
  int total_elements = batch_size * top_k;
  constexpr int blockSize = 256;
  const int gridSize = (total_elements + blockSize - 1) / blockSize;
  NextTokenKernel<<<gridSize, blockSize, 0, stream>>>(next_token_indices,
                                                      next_indices,
                                                      next_tokens,
                                                      vocab_size,
                                                      total_elements);
}

template <typename T>
__global__ void LogitsProcessKernel(
    T* next_token_scores,
    const int* vocab_mask,
    const int* prefix_vocab_mask,
    const int* presence_mask,
    float presence_penalty,
    float temperature,
    int num_beams,
    int vocab_size,
    int padded_vocab_size,
    int total_elements,
    int demote_token_id,
    int32_t* sequences,
    int max_sequence_length,
    int current_sequence_length,
    float repetition_penalty,
    int no_repeat_ngram_size) {
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  if (index < total_elements) {
    int batch_beam_index = index / padded_vocab_size;
    int word_id = index % padded_vocab_size;

    if (word_id >= vocab_size) {
      // Set any value within the padding region to the lowest value so that it isn't picked
      next_token_scores[index] = cub::FpLimits<T>::Lowest();
    } else {
      // RepetitionPenaltyLogitsProcessor
      if (repetition_penalty != 1.0f) {
        int32_t* current_sequence = sequences + batch_beam_index * max_sequence_length;
        bool found = false;
        for (int i = 0; i < current_sequence_length; i++) {
          if (current_sequence[i] == word_id) {
            found = true;
            break;
          }
        }
        if (found) {
          float score = (float)next_token_scores[index];
          next_token_scores[index] = (T)(score < 0 ? score * repetition_penalty : score / repetition_penalty);
        }
      }

      // NoRepeatNGramLogitsProcessor
      if (no_repeat_ngram_size > 0 && current_sequence_length >= no_repeat_ngram_size) {
        int32_t* current_sequence = sequences + batch_beam_index * max_sequence_length;
        bool found = false;
        for (int i = no_repeat_ngram_size - 1; i < current_sequence_length; i++) {
          if (current_sequence[i] == word_id) {  // last token of n-gram matched
            found = true;
            for (int j = 0; j < no_repeat_ngram_size - 1; j++) {  // match the remaining N-1 tokens
              if (current_sequence[i - j - 1] != current_sequence[current_sequence_length - 1 - j]) {
                found = false;
                break;
              }
            }
            if (found) {
              break;
            }
          }
        }

        if (found) {
          next_token_scores[index] = cub::FpLimits<T>::Lowest();
          return;
        }
      }

      // VocabMaskLogitsProcessor
      if (vocab_mask != nullptr && vocab_mask[word_id] == 0) {
        next_token_scores[index] = cub::FpLimits<T>::Lowest();
        return;
      }

      // PrefixVocabMaskLogitsProcessor
      int batch_id = batch_beam_index / num_beams;
      if (prefix_vocab_mask != nullptr && prefix_vocab_mask[batch_id * vocab_size + word_id] == 0) {
        next_token_scores[index] = cub::FpLimits<T>::Lowest();
        return;
      }

      // MinLengthLogitsProcessor
      if (word_id == demote_token_id) {
        next_token_scores[index] = cub::FpLimits<T>::Lowest();
      }

      // PresencePenaltyLogitsProcessor
      if (presence_mask != nullptr && presence_mask[index] == 1) {
        float score = (float)next_token_scores[index] - presence_penalty;
        next_token_scores[index] = (T)score;
      }

      // TemperatureLogitsProcessor
      if (temperature != 1.0f) {
        float score = (float)(next_token_scores[index]);
        next_token_scores[index] = (T)(score / temperature);
      }
    }
  }
}

template <typename T>
void LaunchLogitsProcessKernel(
    T* next_token_scores,
    const int* vocab_mask,
    const int* prefix_vocab_mask,
    int* presence_mask,
    float presence_penalty,
    float temperature,
    int batch_size,
    int num_beams,
    int vocab_size,
    int padded_vocab_size,
    int demote_token_id,
    int32_t* sequences,
    int max_sequence_length,
    int current_sequence_length,
    float repetition_penalty,
    int no_repeat_ngram_size,
    cudaStream_t stream) {
  int total_elements = batch_size * num_beams * padded_vocab_size;
  constexpr int blockSize = 256;
  const int gridSize = (total_elements + blockSize - 1) / blockSize;
  LogitsProcessKernel<T><<<gridSize, blockSize, 0, stream>>>(
      next_token_scores,
      vocab_mask,
      prefix_vocab_mask,
      presence_mask,
      presence_penalty,
      temperature,
      num_beams,
      vocab_size,
      padded_vocab_size,
      total_elements,
      demote_token_id,
      sequences,
      max_sequence_length,
      current_sequence_length,
      repetition_penalty,
      no_repeat_ngram_size);
}

// Instantiation
template void LaunchLogitsProcessKernel(
    float* next_token_scores,
    const int* vocab_mask,
    const int* prefix_vocab_mask,
    int* presence_mask,
    float presence_penalty,
    float temperature,
    int batch_size,
    int num_beams,
    int vocab_size,
    int padded_vocab_size,
    int demote_token_id,
    int32_t* sequences,
    int max_sequence_length,
    int current_sequence_length,
    float repetition_penalty,
    int no_repeat_ngram_size,
    cudaStream_t stream);

template void LaunchLogitsProcessKernel(
    half* next_token_scores,
    const int* vocab_mask,
    const int* prefix_vocab_mask,
    int* presence_mask,
    float presence_penalty,
    float temperature,
    int batch_size,
    int num_beams,
    int vocab_size,
    int padded_vocab_size,
    int demote_token_id,
    int32_t* sequences,
    int max_sequence_length,
    int current_sequence_length,
    float repetition_penalty,
    int no_repeat_ngram_size,
    cudaStream_t stream);

__global__ void AddProbsKernel(float* log_probs,
                               float* cum_log_probs,
                               const int vocab_size,
                               const int total_elements) {
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  int batch_beam_index = index / vocab_size;

  if (index < total_elements)
    log_probs[index] += cum_log_probs[batch_beam_index];
}

template <typename T>
void LaunchAddProbsKernel(T* log_probs,
                          T* cum_log_probs,
                          const int batch_size,
                          const int num_beams,
                          const int vocab_size,
                          cudaStream_t stream) {
  int total_elements = batch_size * num_beams * vocab_size;
  constexpr int blockSize = 256;
  const int gridSize = (total_elements + blockSize - 1) / blockSize;
  AddProbsKernel<<<gridSize, blockSize, 0, stream>>>(log_probs, cum_log_probs, vocab_size, total_elements);
}

template void LaunchAddProbsKernel(
    float* log_probs,
    float* cum_log_probs,
    const int batch_size,
    const int num_beams,
    const int vocab_size,
    cudaStream_t stream);

template <typename T>
__global__ void UpdateGptInputsKernel(const T* old_mask_data,
                                      T* mask_data,
                                      int32_t* next_positions,
                                      int batch_beam_size,
                                      int current_length) {
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  if (index < batch_beam_size * current_length) {
    // Update attention mask.
    int i = index / current_length;
    int j = index % current_length;
    mask_data[index] = (j < current_length - 1) ? old_mask_data[i * (current_length - 1) + j] : static_cast<T>(1);

    if (next_positions != nullptr) {
      // Update sequence length (or next positions).
      if (index < batch_beam_size) {
        next_positions[index]++;
      }
    }
  }
}

void LaunchUpdateGptKernel(const int32_t* old_mask_data,
                           int32_t* mask_data,
                           int32_t* next_positions,
                           int batch_beam_size,
                           int current_length,
                           cudaStream_t stream) {
  assert(current_length > 0);
  int total_elements = batch_beam_size * current_length;
  constexpr int blockSize = 256;
  const int gridSize = (total_elements + blockSize - 1) / blockSize;
  UpdateGptInputsKernel<int32_t><<<gridSize, blockSize, 0, stream>>>(
      old_mask_data, mask_data, next_positions, batch_beam_size, current_length);
}

template <typename T>
void GetTempStorageSize(const T* d_keys_in,
                        const int* d_values_in,
                        int* d_offsets,
                        int num_items,
                        int num_segments,
                        cudaStream_t stream,
                        bool is_descending,
                        size_t& temp_storage_bytes) {
  if (is_descending) {
    CUDA_CALL_THROW(cub::DeviceSegmentedRadixSort::SortPairsDescending(nullptr,
                                                                       temp_storage_bytes,
                                                                       d_keys_in,
                                                                       (T*)nullptr,
                                                                       d_values_in,
                                                                       (int*)nullptr,
                                                                       num_items,
                                                                       num_segments,
                                                                       d_offsets,
                                                                       d_offsets + 1,
                                                                       0,
                                                                       sizeof(T) * 8,
                                                                       stream));
  } else {
    CUDA_CALL_THROW(cub::DeviceSegmentedRadixSort::SortPairs(nullptr,
                                                             temp_storage_bytes,
                                                             d_keys_in,
                                                             (T*)nullptr,
                                                             d_values_in,
                                                             (int*)nullptr,
                                                             num_items,
                                                             num_segments,
                                                             d_offsets,
                                                             d_offsets + 1,
                                                             0,
                                                             sizeof(T) * 8,
                                                             stream));
  }
}

template void GetTempStorageSize(
    const float* d_keys_in,
    const int* d_values_in,
    int* d_offsets,
    int num_items,
    int num_segments,
    cudaStream_t stream,
    bool is_descending,
    size_t& temp_storage_bytes);

template void GetTempStorageSize(
    const half* d_keys_in,
    const int* d_values_in,
    int* d_offsets,
    int num_items,
    int num_segments,
    cudaStream_t stream,
    bool is_descending,
    size_t& temp_storage_bytes);

// TODO: merge to one kernel
__global__ void SetupParamsKernel(int* d_values_in,
                                  int* d_offsets,
                                  int batch_size,
                                  int vocab_size) {
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  int total_elements = batch_size * vocab_size;
  if (index < total_elements) {
    d_values_in[index] = index % vocab_size;
  }
  if (index < batch_size + 1) {
    d_offsets[index] = index * vocab_size;
  }
}

void LaunchSetupParamsKernel(int* d_values_in,
                             int* d_offsets,
                             int batch_size,
                             int vocab_size,
                             cudaStream_t stream) {
  int total_elements = batch_size * vocab_size;
  constexpr int blockSize = 256;
  const int gridSize = (total_elements + blockSize - 1) / blockSize;
  SetupParamsKernel<<<gridSize, blockSize, 0, stream>>>(d_values_in,
                                                        d_offsets,
                                                        batch_size,
                                                        vocab_size);
}

template <typename T>
void LaunchSortPairs(void* d_temp_storage,
                     size_t temp_storage_bytes,
                     const T* d_keys_in,
                     T* d_keys_out,
                     const int* d_values_in,
                     int* d_values_out,
                     int num_items,
                     int num_segments,
                     int* d_offsets,
                     cudaStream_t stream,
                     bool is_descending) {
  if (is_descending) {
    CUDA_CALL_THROW(cub::DeviceSegmentedRadixSort::SortPairsDescending(d_temp_storage,
                                                                       temp_storage_bytes,
                                                                       d_keys_in,
                                                                       d_keys_out,
                                                                       d_values_in,
                                                                       d_values_out,
                                                                       num_items,
                                                                       num_segments,
                                                                       d_offsets,
                                                                       d_offsets + 1,
                                                                       0,
                                                                       sizeof(T) * 8,
                                                                       stream));
  } else {
    CUDA_CALL_THROW(cub::DeviceSegmentedRadixSort::SortPairs(d_temp_storage,
                                                             temp_storage_bytes,
                                                             d_keys_in,
                                                             d_keys_out,
                                                             d_values_in,
                                                             d_values_out,
                                                             num_items,
                                                             num_segments,
                                                             d_offsets,
                                                             d_offsets + 1,
                                                             0,
                                                             sizeof(T) * 8,
                                                             stream));
  }
}

template void LaunchSortPairs(void* d_temp_storage,
                              size_t temp_storage_bytes,
                              const float* d_keys_in,
                              float* d_keys_out,
                              const int* d_values_in,
                              int* d_values_out,
                              int num_items,
                              int num_segments,
                              int* d_offsets,
                              cudaStream_t stream,
                              bool is_descending);

template void LaunchSortPairs(void* d_temp_storage,
                              size_t temp_storage_bytes,
                              const half* d_keys_in,
                              half* d_keys_out,
                              const int* d_values_in,
                              int* d_values_out,
                              int num_items,
                              int num_segments,
                              int* d_offsets,
                              cudaStream_t stream,
                              bool is_descending);

// A stateful callback functor that maintains a running prefix to be applied
// during consecutive scan operations.
struct BlockPrefixCallbackOp {
  float running_total;  // running prefix

  __device__ BlockPrefixCallbackOp(float running_total) : running_total(running_total) {}
  // Callback operator to be entered by the first warp of threads in the block.
  // Thread-0 is responsible for returning a value for seeding the block-wide scan.
  __device__ float operator()(float block_aggregate) {
    float old_prefix = running_total;
    running_total += block_aggregate;
    return old_prefix;
  }
};

template <typename T, int kBlockSize>
__global__ void FilterLogitsKernelCustom(float* d_sorted_logits_in,
                                         const int* d_sorted_indices,
                                         T* d_logits_in_out,
                                         float top_p_threshold,
                                         float filter_value,
                                         int batch_size,
                                         int vocab_size) {
  int vocab_idx = threadIdx.x;
  int batch_id = blockIdx.x;
  int offset = batch_id * vocab_size;

  typedef cub::BlockScan<float, kBlockSize> BlockScan;
  __shared__ typename BlockScan::TempStorage temp_storage;
  BlockPrefixCallbackOp prefix_op(0);

  for (int idx = vocab_idx; idx < vocab_size; idx += kBlockSize) {
    float sum = d_sorted_logits_in[offset + idx];
    BlockScan(temp_storage).ExclusiveSum(sum, sum, prefix_op);

    __syncthreads();
    if (sum >= top_p_threshold) {
      int original_index = offset + d_sorted_indices[offset + idx];
      d_logits_in_out[original_index] = (T)filter_value;
    }
  }
}

template <typename T, int kBlockSize>
__global__ void FilterLogitsKernel(float* d_sorted_logits_in,
                                   const int* d_sorted_indices,
                                   T* d_logits_in_out,
                                   float top_p_threshold,
                                   float filter_value,
                                   int min_tokens_to_keep,
                                   int batch_size,
                                   int vocab_size) {
  int vocab_idx = threadIdx.x;
  int batch_id = blockIdx.x;
  int offset = batch_id * vocab_size;

  typedef cub::BlockScan<float, kBlockSize> BlockScan;
  __shared__ typename BlockScan::TempStorage temp_storage;
  BlockPrefixCallbackOp prefix_op(0);

  for (int idx = vocab_idx; idx < vocab_size; idx += kBlockSize) {
    float sum = d_sorted_logits_in[offset + idx];
    BlockScan(temp_storage).InclusiveSum(sum, sum, prefix_op);

    __syncthreads();

    if (sum <= top_p_threshold) {
      if (idx + min_tokens_to_keep < vocab_size) {
        int original_index = offset + d_sorted_indices[offset + idx];
        d_logits_in_out[original_index] = (T)filter_value;
      }
    }
  }
}

template <typename T>
void LaunchFilterLogitsKernel(float* d_sorted_logits_in,
                              const int* d_sorted_indices,
                              T* d_logits_in_out,
                              float top_p,
                              float filter_value,
                              int min_tokens_to_keep,
                              int batch_size,
                              int vocab_size,
                              cudaStream_t stream,
                              bool is_descending) {
  constexpr int kBlockSize = 256;

  if (is_descending) {
    FilterLogitsKernelCustom<T, kBlockSize><<<batch_size, kBlockSize, 0, stream>>>(d_sorted_logits_in,
                                                                                   d_sorted_indices,
                                                                                   d_logits_in_out,
                                                                                   top_p,
                                                                                   filter_value,
                                                                                   batch_size,
                                                                                   vocab_size);
  } else {
    FilterLogitsKernel<T, kBlockSize><<<batch_size, kBlockSize, 0, stream>>>(d_sorted_logits_in,
                                                                             d_sorted_indices,
                                                                             d_logits_in_out,
                                                                             1 - top_p,
                                                                             filter_value,
                                                                             min_tokens_to_keep,
                                                                             batch_size,
                                                                             vocab_size);
  }
}

template void LaunchFilterLogitsKernel(float* d_sorted_logits_in,
                                       const int* d_sorted_indices,
                                       float* d_logits_in_out,
                                       float top_p,
                                       float filter_value,
                                       int min_tokens_to_keep,
                                       int batch_size,
                                       int vocab_size,
                                       cudaStream_t stream,
                                       bool is_descending);

template void LaunchFilterLogitsKernel(float* d_sorted_logits_in,
                                       const int* d_sorted_indices,
                                       half* d_logits_in_out,
                                       float top_p,
                                       float filter_value,
                                       int min_tokens_to_keep,
                                       int batch_size,
                                       int vocab_size,
                                       cudaStream_t stream,
                                       bool is_descending);

// Ref: https://github.com/pytorch/pytorch/blob/release/1.13/aten/src/ATen/native/cuda/MultinomialKernel.cu
template <typename scalar_t, typename accscalar_t>
__global__ void sampleMultinomialOnce(int32_t* dest,
                                      int distributions,
                                      int categories,
                                      scalar_t* sampled,
                                      scalar_t* dist,
                                      int stride_dist,        // dist->stride(0)
                                      int stride_categories,  // dist->stride(1)
                                      int* d_presence_mask) {
  extern __shared__ unsigned char my_smem[];
  __shared__ bool found;
  __shared__ unsigned foundPos;
  accscalar_t* smem = reinterpret_cast<accscalar_t*>(my_smem);
  accscalar_t accZero = static_cast<accscalar_t>(0);
  scalar_t zero = static_cast<scalar_t>(0);
  for (int curDist = blockIdx.x;
       curDist < distributions; curDist += gridDim.x) {
    // Assume sum = 1 in Top P sampling as the input is softmaxed.
    accscalar_t sum = 1;

    // Broadcast sum and sample value
    if (threadIdx.x == 0) {
      // Make sure the sum of our distribution didn't overflow
      // CUDA_KERNEL_ASSERT(!_isinf(val));
      // CUDA_KERNEL_ASSERT(sum > accZero);
      foundPos = 0;
      smem[0] = sum;
      smem[1] = sampled[curDist];
    }
    __syncthreads();
    sum = smem[0];
    scalar_t sample = static_cast<scalar_t>(smem[1]);
    __syncthreads();
    if (sum == accZero) {
      // Choose the first element
      if (threadIdx.x == 0) {
        dest[curDist] = 0;
      }
      continue;
    }
    int chunks = (categories + (int)blockDim.x - 1) / blockDim.x;
    accscalar_t prevHighProb = accZero;
    found = false;
    for (int chunk = 0; chunk < chunks && !found; ++chunk) {
      // All threads in bounds load a value
      int cat = chunk * blockDim.x + threadIdx.x;
      accscalar_t dist_val = cat < categories ? static_cast<accscalar_t>(dist[curDist * stride_dist + cat * stride_categories]) / sum : accZero;
      smem[threadIdx.x] = dist_val;
      __syncthreads();
      // Perform an inclusive prefix sum of the shared memory contents
      for (int offset = 1; offset < blockDim.x; offset *= 2) {
        accscalar_t val = accZero;
        if (threadIdx.x >= offset) {
          val = smem[threadIdx.x - offset] + smem[threadIdx.x];
        }
        __syncthreads();
        if (threadIdx.x >= offset) {
          smem[threadIdx.x] = val;
        }
        __syncthreads();
      }
      // Each thread will check to see if the sample falls in its bucket
      scalar_t curBucket =
          static_cast<scalar_t>(smem[threadIdx.x] + prevHighProb);
      scalar_t prevBucket = static_cast<scalar_t>(
          threadIdx.x == 0 ? prevHighProb
                           : smem[threadIdx.x - 1] + prevHighProb);
      bool inBucket =
          (cat < categories) &&
          (!(sample >= curBucket) &&
           (sample >= prevBucket) &&
           (dist_val > zero));
      if (inBucket) {
        // We're done; we have the sample
        // Torch indices are 1-based
        atomicMax(&foundPos, cat);
        found = true;
      }
      // Store the previous scan's high value for future use
      prevHighProb = prevHighProb + smem[blockDim.x - 1];
      __syncthreads();
    }
    if (threadIdx.x == 0) {
      if (found) {
        dest[curDist] = foundPos;
      } else {
        // This should address a rare bug where we don't select a valid index. This likely occurs when
        // due to floating point arithmetic rounding errors, our cumulative sum does not add up to 1, but
        // and our uniform sample is greater than this value. In this case we likely have unitialized memory
        // in dest[curDist]. So basically we will loop through the distribution and pick the largest index
        // where the distribution is non-zero. This is obviously terribly inefficient, but due to the
        // rarity in which this occurs, this should not be an issue.
        for (int cat = categories - 1; cat >= 0; --cat) {
          if (dist[curDist * stride_dist + cat * stride_categories] > zero) {
            dest[curDist] = cat;
            break;
          }
        }
      }
    }
  }

  // update presence mask
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  if (index >= distributions * categories) {
    return;
  }
  int dist_idx = index / categories;
  int cat_idx = index % categories;
  if (dest[dist_idx] == cat_idx) {
    d_presence_mask[index] = 1;
  }
}

// Only support n_sample = 1
void TorchMultinomialKernelLauncher(float* d_input,
                                    float* d_sampled,
                                    int32_t* d_output,
                                    int batch_size,
                                    int vocab_size,
                                    int* d_presence_mask,
                                    cudaStream_t stream) {
  // Store the props in class variables
  int device;
  CUDA_CALL_THROW(cudaGetDevice(&device));
  cudaDeviceProp props;
  CUDA_CALL_THROW(cudaGetDeviceProperties(&props, device));

  int numSM = props.multiProcessorCount;
  int maxThreads = props.maxThreadsPerBlock;
  int warp_size = 32;  // at::cuda::warp_size();
  int requiredWarps = (vocab_size + warp_size - 1) / warp_size;
  int requiredThreads = std::min(maxThreads, requiredWarps * warp_size);
  int requiredShared = requiredThreads * sizeof(float);

  dim3 block(requiredThreads);
  dim3 grid(std::min(batch_size, numSM * 4));

  sampleMultinomialOnce<float, float>
      <<<grid, block, requiredShared, stream>>>(d_output,
                                                batch_size,
                                                vocab_size,
                                                d_sampled,
                                                d_input,
                                                vocab_size,
                                                1,
                                                d_presence_mask);
}

__global__ void UpdateDecoderMaskedMultiHeadAttentionCacheIndirectionKernel(int32_t* tgt_indir_cache,
                                                                            const int32_t* src_indir_cache,
                                                                            const int32_t* beam_ids,
                                                                            int batch_size,
                                                                            int beam_width,
                                                                            int input_seq_length,
                                                                            int max_seq_length,
                                                                            int current_length) {
  int time_step = threadIdx.x + blockIdx.x * blockDim.x;
  int bb_id = threadIdx.y + blockIdx.y * blockDim.y;
  const int batch_id = bb_id / beam_width;
  const int beam_id = bb_id % beam_width;

  if (bb_id >= beam_width * batch_size || time_step >= current_length) {
    return;
  }

  const int src_beam = beam_ids[batch_id * beam_width + beam_id] % beam_width;

  const int tgt_offset = batch_id * beam_width * max_seq_length + beam_id * max_seq_length + time_step;

  if (time_step < input_seq_length) {
    // For time steps that correspond to the input sequence,
    // the beam that it comes from is always 0.
    tgt_indir_cache[tgt_offset] = static_cast<int32_t>(0);
  } else if (time_step == (current_length - 1)) {
    // For the final (newly generated) time step,
    // the beam that it comes from is always the beam that we
    // are currently processing (i.e.) from this point on, these time-steps
    // form the new beams.
    tgt_indir_cache[tgt_offset] = static_cast<int32_t>(beam_id);
  } else {
    // For all other time-steps, we look up the source indirection, to
    // see which beam it came from based on the `src_beam`.
    const int src_offset = batch_id * beam_width * max_seq_length + src_beam * max_seq_length + time_step;
    tgt_indir_cache[tgt_offset] = src_indir_cache[src_offset];
  }
}

void UpdateDecoderMaskedMultiHeadAttentionCacheIndirection(int32_t* tgt_indir_cache,
                                                           const int32_t* src_indir_cache,
                                                           const int32_t* beam_ids,
                                                           int batch_size,
                                                           int beam_width,
                                                           int input_seq_length,
                                                           int max_seq_length,
                                                           int current_length,
                                                           cudaStream_t stream) {
  const dim3 block(32);
  const dim3 grid((current_length + block.x - 1) / block.x, batch_size * beam_width);
  UpdateDecoderMaskedMultiHeadAttentionCacheIndirectionKernel<<<grid, block, 0, stream>>>(tgt_indir_cache,
                                                                                          src_indir_cache,
                                                                                          beam_ids,
                                                                                          batch_size,
                                                                                          beam_width,
                                                                                          input_seq_length,
                                                                                          max_seq_length,
                                                                                          current_length);
}

#ifndef USE_ROCM
namespace {
template <typename T, size_t size>
struct TypeMapper : public V_vec_m_<T, size> {};

template <>
struct TypeMapper<int32_t, 2> {
  using Type = uint2;
};

template <>
struct TypeMapper<int32_t, 4> {
  using Type = uint4;
};
}  // namespace
#endif

template <typename T>
__global__ void KeyCacheExpansionKernel(const T* input,
                                        T* output,
                                        int beam_width,
                                        int max_seq_length,
                                        int head_size) {
  const int num_heads = gridDim.y;
  const int sequence_length = gridDim.z;

  const int bbid = blockIdx.x;
  const int batch_id = bbid / beam_width;
  const int head_id = blockIdx.y;
  const int s = blockIdx.z;
  const int tidx = threadIdx.x;

  const int input_offset = ((batch_id * num_heads + head_id) * sequence_length + s) * head_size + tidx;
  const int output_offset = ((bbid * num_heads + head_id) * max_seq_length + s) * head_size + tidx;

  if (tidx < head_size) {
    output[output_offset] = input[input_offset];
  }
}

template <typename T>
void KeyCacheExpansionKernelLauncher(const T* key_cache,
                                     T* key_cache_expanded,
                                     int batch_size,
                                     int beam_width,
                                     int num_heads,
                                     int sequence_length,
                                     int max_seq_length,
                                     int head_size,
                                     cudaStream_t stream) {
  const dim3 grid(batch_size * beam_width, num_heads, sequence_length);

  int equiv_head_size = (head_size & 1) == 0 ? (head_size >> 1) : head_size;
  equiv_head_size = (equiv_head_size & 1) == 0 ? (equiv_head_size >> 1) : equiv_head_size;

  // Here we know head_size is smaller than max_thread_num_per_block
  int tpb = std::max(32, equiv_head_size);

  // round up tpb to power of 2
  --tpb;
  tpb |= (tpb >> 1);
  tpb |= (tpb >> 2);
  tpb |= (tpb >> 4);
  tpb |= (tpb >> 8);
  tpb |= (tpb >> 16);
  tpb++;

#ifndef USE_ROCM
  if ((head_size % 4) == 0) {
    using vec_type = typename TypeMapper<T, 4>::Type;
    const dim3 block(tpb);
    KeyCacheExpansionKernel<<<grid, block, 0, stream>>>(reinterpret_cast<const vec_type*>(key_cache),
                                                        reinterpret_cast<vec_type*>(key_cache_expanded),
                                                        beam_width,
                                                        max_seq_length,
                                                        equiv_head_size);
  } else if ((head_size & 1) == 0) {
    using vec_type = typename TypeMapper<T, 2>::Type;
    const dim3 block(tpb);
    KeyCacheExpansionKernel<<<grid, block, 0, stream>>>(reinterpret_cast<const vec_type*>(key_cache),
                                                        reinterpret_cast<vec_type*>(key_cache_expanded),
                                                        beam_width,
                                                        max_seq_length,
                                                        equiv_head_size);
  } else {
#endif
    const dim3 block(tpb);
    KeyCacheExpansionKernel<<<grid, block, 0, stream>>>(key_cache,
                                                        key_cache_expanded,
                                                        beam_width,
                                                        max_seq_length,
                                                        head_size);
#ifndef USE_ROCM
  }
#endif
}

template void KeyCacheExpansionKernelLauncher(const float* key_cache,
                                              float* key_cache_expanded,
                                              int batch_size,
                                              int beam_width,
                                              int num_heads,
                                              int sequence_length,
                                              int max_seq_length,
                                              int head_size,
                                              cudaStream_t stream);

template void KeyCacheExpansionKernelLauncher(const half* key_cache,
                                              half* key_cache_expanded,
                                              int batch_size,
                                              int beam_width,
                                              int num_heads,
                                              int sequence_length,
                                              int max_seq_length,
                                              int head_size,
                                              cudaStream_t stream);

template void KeyCacheExpansionKernelLauncher(const int32_t* key_cache,
                                              int32_t* key_cache_expanded,
                                              int batch_size,
                                              int beam_width,
                                              int num_heads,
                                              int sequence_length,
                                              int max_seq_length,
                                              int head_size,
                                              cudaStream_t stream);

template <typename T>
__global__ void BufferExpansionKernel(const T* input,
                                      T* output,
                                      int chunk_size) {
  const int batch_id = blockIdx.x;
  const int beam_id = blockIdx.y;
  const int tidx = threadIdx.x;
  const int beam_size = gridDim.y;
  const int idx = blockIdx.z * blockDim.x + tidx;

  const int input_offset = batch_id * chunk_size + idx;
  const int output_offset = batch_id * beam_size * chunk_size + beam_id * chunk_size + idx;

  if (idx < chunk_size) {
    output[output_offset] = input[input_offset];
  }
}

template <typename T>
void BufferExpansionKernelLauncher(const T* input,
                                   T* output,
                                   int batch_size,
                                   int beam_width,
                                   int chunk_size,
                                   cudaStream_t stream) {
  const dim3 block(128);

#ifndef USE_ROCM
  if ((chunk_size % 4) == 0) {
    using vec_type = typename TypeMapper<T, 4>::Type;
    const dim3 grid(batch_size, beam_width, (chunk_size / 4 + block.x - 1) / block.x);
    BufferExpansionKernel<<<grid, block, 0, stream>>>(reinterpret_cast<const vec_type*>(input),
                                                      reinterpret_cast<vec_type*>(output),
                                                      chunk_size / 4);
  } else if ((chunk_size & 1) == 0) {
    using vec_type = typename TypeMapper<T, 2>::Type;
    const dim3 grid(batch_size, beam_width, (chunk_size / 2 + block.x - 1) / block.x);
    BufferExpansionKernel<<<grid, block, 0, stream>>>(reinterpret_cast<const vec_type*>(input),
                                                      reinterpret_cast<vec_type*>(output),
                                                      chunk_size / 2);
  } else {
#endif
    const dim3 grid(batch_size, beam_width, (chunk_size + block.x - 1) / block.x);
    BufferExpansionKernel<<<grid, block, 0, stream>>>(input,
                                                      output,
                                                      chunk_size);
#ifndef USE_ROCM
  }
#endif
}

template void BufferExpansionKernelLauncher(const float* input,
                                            float* output,
                                            int batch_size,
                                            int beam_width,
                                            int chunk_size,
                                            cudaStream_t stream);

template void BufferExpansionKernelLauncher(const half* input,
                                            half* output,
                                            int batch_size,
                                            int beam_width,
                                            int chunk_size,
                                            cudaStream_t stream);

template void BufferExpansionKernelLauncher(const int32_t* input,
                                            int32_t* output,
                                            int batch_size,
                                            int beam_width,
                                            int chunk_size,
                                            cudaStream_t stream);

template <typename T>
__global__ void CopyCrossQKSingleDecodeStepKernel(
    T* target, // shape [batchxbeam, layer_head_pair_count, max_length, frame]
    T** qk_layer_pointers,
    int token_index,
    int num_layers,
    int num_heads,
    const int* cross_qk_layer_head_pairs,
    int frames,
    int max_length
) {
  const int pair = blockIdx.x;
  const int layer_head_pair_count = gridDim.x;
  const int bbm = blockIdx.y;
  cross_qk_layer_head_pairs += (pair * 2);
  const int layer = *cross_qk_layer_head_pairs;
  const int head = *(cross_qk_layer_head_pairs + 1);

  target += ((int64_t)bbm * layer_head_pair_count + pair) * max_length * frames + ((int64_t)token_index * frames);
  T* src = qk_layer_pointers[layer] + ((int64_t)bbm * num_heads + head) * frames;

  for (int tid = threadIdx.x; tid < frames; tid += blockDim.x) {
    target[tid] = src[tid]; // use vectorized read write in future if needed
  }
}

void LaunchCopyCrossQKSingleDecodeStep(
    cudaStream_t stream,
    float* cross_qk_buffer_data,
    float** qk_layer_pointers,
    int token_index,
    int batchxbeam,
    int num_layers,
    int num_heads,
    int cross_qk_layer_head_pair_count,
    const int* cross_qk_layer_head_pairs,
    int frames,
    int max_length
) {
  dim3 block(512);
  dim3 grid(cross_qk_layer_head_pair_count, batchxbeam);
  typedef typename ToCudaType<float>::MappedType CudaT;

  CopyCrossQKSingleDecodeStepKernel<<<grid, block, 0, stream>>>(
      (CudaT*)cross_qk_buffer_data,
      (CudaT**)qk_layer_pointers,
      token_index,
      num_layers,
      num_heads,
      cross_qk_layer_head_pairs,
      frames,
      max_length
  );
}


template <typename T>
__global__ void CopyDecoderCrossQKAllStepsKernel(
    int context_decoding_len,
    int num_beams,
    int num_return_sequences,
    int max_length,
    int frames_of_k,
    const T* cross_qk_buffer_data, // [batch, num_beams, layer_head_pair_count, max_length, frames]
    T* cross_qk_output, // [batch, num_return_sequences, layer_head_pair_count, total_decoding_length, frames]
    const int* cache_indir_data, // [batch, num_return_sequences, max_length]
    const int32_t* beam_indices
) {
  const int pair = blockIdx.y;
  const int layer_head_pair_count = gridDim.y;
  const int total_decoding_length = gridDim.x;
  const int token_decoding_index = blockIdx.x;
  const int br = blockIdx.z;
  const int batch = br / num_return_sequences;
  const int ret_seq_id = br % num_return_sequences;

  const int64_t offset_in_cache = ((int64_t)batch * num_return_sequences + ret_seq_id) * max_length + token_decoding_index + context_decoding_len;
  int bi_src = batch * num_beams + cache_indir_data[offset_in_cache];

  T* target    = cross_qk_output      + (((int64_t)br     * layer_head_pair_count + (int64_t)pair) * total_decoding_length + token_decoding_index) * frames_of_k;
  const T* src = cross_qk_buffer_data + (((int64_t)bi_src * layer_head_pair_count + (int64_t)pair) * max_length            + token_decoding_index) * frames_of_k;
  for (int tid = threadIdx.x; tid < frames_of_k; tid += blockDim.x) {
    target[tid] = src[tid]; // use vectorized read write in future if needed
  }
}

void LaunchFinalizeCrossQK(
    cudaStream_t stream,
    int iteration_number,
    int context_decoding_len,
    int batch_size,
    int num_beams,
    int max_length,
    int cross_qk_layer_head_pair_count,
    [[maybe_unused]] const int* cross_qk_layer_head_pairs,
    int frames_of_k,
    const float* cross_qk_buffer_data,
    float* cross_qk_output,
    int num_return_sequences,
    const int* cache_indir_data,
    const int32_t* beam_indices
) {
  int64_t br = (int64_t)batch_size * num_return_sequences;
  ORT_ENFORCE(br < 65536L && cross_qk_layer_head_pair_count < 65536);
  const int total_decoding_length = iteration_number - 1;
  dim3 block(512);
  dim3 grid(total_decoding_length, cross_qk_layer_head_pair_count, (unsigned)br);
  typedef typename ToCudaType<float>::MappedType CudaT;

  CopyDecoderCrossQKAllStepsKernel<<<grid, block, 0, stream>>>(
    context_decoding_len,
    num_beams,
    num_return_sequences,
    max_length,
    frames_of_k,
    (const CudaT*)cross_qk_buffer_data,
    (CudaT*)cross_qk_output,
    cache_indir_data,
    beam_indices);
}

template <int ElementsPerThreads>
__global__ void ForceDecodingIdsKernel(
    float* beam_scores,
    const int vocab_size,
    const int32_t* force_ids,
    int id_len,
    int step
) {
  const int num_beams = gridDim.y;
  const int beam = blockIdx.y;
  const int batch = blockIdx.z;
  beam_scores += (((int64_t)batch * num_beams + beam)* vocab_size); // move to (batch, beam)
  const int32_t id_wanted = force_ids[((int64_t)batch * id_len) + step];
  if (id_wanted < 0 || id_wanted >= vocab_size) return;

  const int32_t elements_per_block = (int32_t)blockDim.x * ElementsPerThreads;
  const int32_t block_start_id = blockIdx.x * elements_per_block;

  int32_t token_id = block_start_id + (int)threadIdx.x;
  #pragma unroll
  for (int elem = 0; elem < ElementsPerThreads; elem++) {
    if (token_id < vocab_size) {
      beam_scores[token_id] = ((token_id == id_wanted) ? 0.0f : cub::FpLimits<float>::Lowest());
    }
    token_id += (int)blockDim.x;
  }
}


void LaunchForceDecodingIds(
    float* beam_scores,
    const int batch_size,
    const int num_beams,
    const int vocab_size,
    const int32_t* force_ids,
    int id_len,
    int step,
    cudaStream_t stream
) {
  dim3 blocks(512);
  constexpr int ElementsPerThreads = 4;
  unsigned gridx = static_cast<unsigned>((vocab_size + 512 * ElementsPerThreads - 1) / (512 * ElementsPerThreads));
  dim3 grids(gridx, num_beams, batch_size);
  ForceDecodingIdsKernel<ElementsPerThreads><<<grids, blocks, 0, stream>>>(
    beam_scores, vocab_size, force_ids, id_len, step
  );
}

template <typename T>
__global__ void SaveNoSpeechProbsKernel(
    T* result_no_speech_probs,
    const float* probs,
    const int batch_size,
    const int num_beams,
    const int vocab_size,
    const int no_speech_token_id
) {
  int b = blockIdx.x * blockDim.x + threadIdx.x;
  if (b < batch_size) {
    int64_t src_offset = b * num_beams * vocab_size + no_speech_token_id;
    result_no_speech_probs[b] = (T)(probs[src_offset]);
  }
}

template <typename T>
void LaunchSaveNoSpeechProbs(
    T* result_no_speech_probs,      /* [batch]*/
    const float* probs,             /* [batch, num_beams, vocab_size]*/
    const int batch_size,
    const int num_beams,
    const int vocab_size,
    const int no_speech_token_id,
    cudaStream_t stream
) {
  int tpb = 256;
  int bpg = (batch_size + 255) / 256;

  typedef typename ToCudaType<T>::MappedType CudaT;
  SaveNoSpeechProbsKernel<CudaT><<<bpg, tpb, 0, stream>>>(
    (CudaT*)result_no_speech_probs, probs, batch_size, num_beams, vocab_size, no_speech_token_id);
}

template void LaunchSaveNoSpeechProbs<float>(
    float* result_no_speech_probs,
    const float* probs,
    const int batch_size,
    const int num_beams,
    const int vocab_size,
    const int no_speech_token_id,
    cudaStream_t stream
);

template void LaunchSaveNoSpeechProbs<MLFloat16>(
    MLFloat16* result_no_speech_probs,
    const float* probs,
    const int batch_size,
    const int num_beams,
    const int vocab_size,
    const int no_speech_token_id,
    cudaStream_t stream
);

}  // namespace cuda
}  // namespace contrib
}  // namespace onnxruntime
