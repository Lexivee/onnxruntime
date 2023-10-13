// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "core/providers/cuda/shared_inc/cuda_utils.h"
#include <cuda_fp16.h>
#include <cublas_v2.h>
#include "contrib_ops/cpu/bert/attention_common.h"

namespace onnxruntime {
namespace contrib {
namespace cuda {

void single_query_cached_kv_attention(
    const cudaStream_t stream,
    const void* out,          // [num_seqs, num_heads, head_size]
    const void* query,        // [num_seqs, num_heads, head_size]
    const void* key_cache,    // [num_blocks, num_kv_heads, head_size/x, block_size, x]
    const void* value_cache,  // [num_blocks, num_kv_heads, head_size, block_size]
    const int* head_mapping,  // [num_heads]
    float scale,
    const int* block_tables,  // [num_seqs, max_num_blocks_per_seq]
    const int max_num_blocks_per_seq,
    const int* context_lens,  // [num_seqs]
    int block_size,
    int max_context_len,
    const float* __restrict__ alibi_slopes_ptr,
    const int64_t* query_shapes,
    int num_queries_per_kv,
    int dtype,
    const void* kv_quant_params_cache = nullptr, // [num_blocks, 2, num_kv_heads, head_size / kv_quant_chunk_size, block_size]
    int kv_quant_chunk_size = 0,
    int kv_quant_param_dtype = 0);

void reshape_and_cache(
    const cudaStream_t stream,
    const void* key,          // [num_tokens, num_heads, head_size]
    const void* value,        // [num_tokens, num_heads, head_size]
    const void* key_cache,    // [num_blocks, num_heads, head_size/x, block_size, x]
    const void* value_cache,  // [num_blocks, num_heads, head_size, block_size]
    const int* slot_mapping,  // [num_tokens]
    const int64_t* key_shapes,
    const int64_t* value_shapes,
    const int64_t block_size,
    const int vec_x,
    const int dtype,
    void* kv_quant_param = nullptr, // [num_blocks, 2, num_heads, head_size / kv_quant_chunk_size, block_size]
    const int kv_quant_chunk_size = 0,
    const int kv_quant_param_dtype = 1);

template <typename T>
void gather_cached_kv(
    const T* key,
    const T* value,
    const T* key_cache,
    const T* value_cache,
    const int* slot_mapping);

void rotary_embedding_neox(
    const cudaStream_t stream,
    const int64_t* positions,  // [num_tokens]
    void* query,               // [num_tokens, num_heads * head_size]
    void* key,                 // [num_tokens, num_kv_heads * head_size]
    int head_size,
    const void* cos_sin_cache,  // [max_position, rot_dim]
    int num_tokens,
    int rot_dim,
    int num_heads,
    int num_kv_heads,
    int dtype);

template <typename scalar_t>
void LaunchRepeatKeyValue(
    const cudaStream_t stream,
    scalar_t* key_out,      // [num_tokens, repeat*num_heads * head_size]
    scalar_t* value_out,    // [num_tokens, repeat*num_heads * head_size]
    const scalar_t* key,    // [num_tokens, num_heads * head_size]
    const scalar_t* value,  // [num_tokens, num_heads * head_size]
    const int64_t* input_shape,
    int repeat);

}  // namespace cuda
}  // namespace contrib
}  // namespace onnxruntime
