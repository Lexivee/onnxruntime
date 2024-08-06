// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/common/common.h"
#include "core/providers/common.h"
#include "contrib_ops/cpu/bert/attention_common.h"

namespace onnxruntime {
namespace contrib {
namespace group_query_attention_helper {

Status CheckInputs(const Tensor* query,
                   const Tensor* key,
                   const Tensor* value,
                   const Tensor* past_key,
                   const Tensor* past_value,
                   const Tensor* cos_cache,
                   const Tensor* sin_cache,
                   void* parameters,
                   int num_heads,
                   int kv_num_heads,
                   const Tensor* seqlens_k,
                   const Tensor* total_seqlen,
                   bool is_past_bsnh,
                   float scale) {
  // Note: Here S* is past_cache_sequence_length, S- is past_sequence_length, S+ is sequence_length
  //     past_key                   : (B, N_k, S*, H) or (B, N_k, S-, H) or nullptr
  //     past_value                 : (B, N_k, S*, H) or (B, N_k, S-, H) or nullptr
  // no packing for q/k/v:
  //     query            (Q)       : (B, S, D) or (B, S, (D_q + 2 D_kv))
  //     key              (K)       : (B, S, D_kv) or nullptr
  //     value            (V)       : (B, S, D_kv) or nullptr
  AttentionQkvFormat qkv_format = Q_K_V_BSNH;
  AttentionQkvFormat past_kv_format = is_past_bsnh ? Q_K_V_BSNH : Q_K_V_BNSH;
  const bool is_packed_qkv = key == nullptr;
  const auto& query_dims = query->Shape().GetDims();

  if (query_dims.size() != 3) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "Input 'query' is expected to have 3 dimensions, got ",
                           query_dims.size());
  }

  int batch_size = static_cast<int>(query_dims[0]);
  int sequence_length = static_cast<int>(query_dims[1]);
  int q_hidden_size = static_cast<int>(query_dims[2]);
  int head_size = 0;

  if (num_heads % kv_num_heads != 0) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                           "num_heads must be a multiple of kv_num_heads. Got num_heads % kv_num_heads == ",
                           num_heads % kv_num_heads);
  }

  int kv_hidden_size = 0;
  // Check key and value when not packed
  if (!is_packed_qkv) {
    head_size = static_cast<int>(q_hidden_size) / num_heads;
    if (head_size % 8 != 0) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "head_size must be a multiple of 8. Got head_size % 8 == ",
                             head_size % 8);
    }
    if (value == nullptr) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'key' and 'value' shall be both present, or both absent in the case of packed qkv.");
    }
    const auto& key_dims = key->Shape().GetDims();
    if (key_dims.size() != 3) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "Input 'key' is expected to have 3 dimensions, got ",
                             key_dims.size());
    } else if (query_dims[0] != key_dims[0]) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'query' and 'key' shall have same dim 0 (batch size)");
    } else if (query_dims[1] != key_dims[1]) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'query' and 'key' shall have same dim 1 (sequence length)");
    }
    kv_hidden_size = static_cast<int>(key_dims[2]);
    const auto& value_dims = value->Shape().GetDims();
    if (value_dims.size() != 3) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "Input 'value' is expected to have 3 dimensions, got ",
                             value_dims.size());
    } else if (query_dims[0] != value_dims[0]) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'query' and 'value' shall have same dim 0 (batch size)");
    } else if (query_dims[1] != value_dims[1]) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'query' and 'value' shall have same dim 1 (sequence length)");
    } else if (value_dims[2] != kv_hidden_size) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "Input 'value' is expected to have same hidden size as key.");
    }
  } else {
    // Check packed qkv
    head_size = static_cast<int>(q_hidden_size) / (num_heads + 2 * kv_num_heads);
    if (head_size % 8 != 0) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "head_size must be a multiple of 8. Got head_size % 8 == ",
                             head_size % 8);
    }
    if (value != nullptr) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'key' and 'value' shall be both present, or both absent in the case of packed qkv.");
    }
    q_hidden_size = head_size * num_heads;
    kv_hidden_size = head_size * kv_num_heads;
  }

  // Check past-present KV
  int32_t past_sequence_length = 0;
  if (past_key != nullptr && past_value != nullptr) {
    const auto& past_key_dims = past_key->Shape().GetDims();
    const auto& past_value_dims = past_value->Shape().GetDims();

    if (past_key_dims.size() != 4) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'past_key' is expected to have 4 dimensions, got ",
                             past_key_dims.size());
    }
    if (past_value_dims.size() != 4) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'past_value' is expected to have 4 dimensions, got ",
                             past_value_dims.size());
    }

    if (past_key_dims[0] != batch_size) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'past_key' dimension 0 should be batch_size, got ",
                             past_key_dims[0]);
    }
    if (past_value_dims[0] != batch_size) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'past_value' dimension 0 should be batch_size, got ",
                             past_value_dims[0]);
    }

    // BNSH
    if (!is_past_bsnh) {
      if (past_key_dims[2] != past_value_dims[2]) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                               "BNSH Input 'past_key' and 'past_value' should have same dimension 2 (max sequence"
                               "length or past sequence length), got ",
                               past_key_dims[1]);
      }
      if (past_key_dims[1] != kv_num_heads) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                               "Input 'past_key' shall have kv_num_heads");
      }
      if (past_value_dims[1] != kv_num_heads) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                               "Input 'past_value' shall have kv_num_heads");
      }
      // We assume all sequence in past kv are right-padded to max or past sequence length
      past_sequence_length = static_cast<int>(past_key_dims[2]);
      // BSNH
    } else {
      if (past_key_dims[1] != past_value_dims[1]) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                               "BNSH Input 'past_key' and 'past_value' should have same dimension 1 (max sequence"
                               "length or past sequence length), got ",
                               past_key_dims[1]);
      }
      if (past_key_dims[2] != kv_num_heads) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                               "Input 'past_key' shall have kv_num_heads");
      }
      if (past_value_dims[2] != kv_num_heads) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                               "Input 'past_value' shall have kv_num_heads");
      }
      // We assume all sequence in past kv are right-padded to max or past sequence length
      past_sequence_length = static_cast<int>(past_key_dims[1]);
    }

    if (past_key_dims[3] != head_size) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'past_key' dimension 3 should be same as head_size, got ",
                             past_key_dims[3]);
    }
    if (past_value_dims[3] != head_size) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Input 'past_value' dimension 3 should be same as head_size, got ",
                             past_value_dims[3]);
    }
  } else if (past_key != nullptr || past_value != nullptr) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                           "Input 'past_key' and 'past_value' shall be both present or both absent.");
  }

  const auto& seqlens_k_dim = seqlens_k->Shape().GetDims();
  if (seqlens_k_dim[0] != batch_size) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                           "seqlens_k must be shape (batch_size).");
  }

  // Set present sequence length from input total_seqlen tensor
  if (!onnxruntime::IsScalarOr1ElementVector(total_seqlen)) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                           "total_sequence_length tensor must be of one element.");
  }
  int total_sequence_length = *((*total_seqlen).template Data<int32_t>());
  int present_sequence_length = std::max(total_sequence_length, past_sequence_length);

  int rotary_dim = 0;
  if (cos_cache != nullptr && sin_cache != nullptr) {
    const auto& cos_dims = cos_cache->Shape().GetDims();
    const auto& sin_dims = sin_cache->Shape().GetDims();

    if (head_size % 16 != 0) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "head_size shall be a multiple of 16. Got head_size % 16 == ",
                             head_size % 16);
    }
    if (cos_dims[0] < total_sequence_length) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "cos_cache dimension 0 shall not be less than total_sequence_length.");
    }
    if (sin_dims[0] < total_sequence_length) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "sin_cache dimension 0 shall not be less than total_sequence_length.");
    }
    if (cos_dims[1] > (head_size / 16) * 8 || cos_dims[1] % 8 != 0) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "cos_cache dimension 1 must be <= head_size / 2 and a multiple of 8.");
    }
    if (sin_dims[1] > (head_size / 16) * 8 || sin_dims[1] % 8 != 0) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "sin_cache dimension 1 must be <= head_size / 2 and a multiple of 8.");
    }
    if (cos_dims[1] != sin_dims[1]) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "cos_cache and sin_cache dimension 1 must be the same.");
    }
    rotary_dim = static_cast<int>(cos_dims[1] * 2);
  } else if (cos_cache != nullptr || sin_cache != nullptr) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                           "Input 'cos_cache' and 'sin_cache' shall be both present or both absent.");
  }

  bool is_interactive = false;
  if (sequence_length > 1 && sequence_length != total_sequence_length) {
    if (batch_size != 1) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "batch_size must be 1 when sequence_length > 1 and past context is given.");
    }
    is_interactive = true;
  }

  bool is_prompt;
  if (is_interactive) {
    is_prompt = false;  // irrelevant for interactive decoding
  } else {
    // If not interactive, sequence_length is 1 for token gen and arbitrarily large for prompt
    is_prompt = (sequence_length == total_sequence_length);
    if (!is_prompt && sequence_length != 1) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "sequence_length shall be 1 when it is not prompt.");
    }
  }

  if (parameters != nullptr) {
    GroupQueryAttentionParameters* output_parameters = reinterpret_cast<GroupQueryAttentionParameters*>(parameters);
    output_parameters->batch_size = batch_size;
    output_parameters->sequence_length = sequence_length;                  // sequence length of Q
    output_parameters->seqlen_past_kv_cache = past_sequence_length;        // max sequence length of past kv tensors
    output_parameters->seqlen_present_kv_cache = present_sequence_length;  // max sequence length of present kv tensors
    output_parameters->hidden_size = q_hidden_size;
    output_parameters->num_heads = num_heads;
    output_parameters->head_size = head_size;
    output_parameters->kv_hidden_size = kv_hidden_size;
    output_parameters->kv_num_heads = kv_num_heads;
    output_parameters->rotary_dim = rotary_dim;
    output_parameters->is_packed_qkv = is_packed_qkv;
    output_parameters->is_interactive = is_interactive;
    output_parameters->is_prompt = is_prompt;
    output_parameters->scale = scale;
    output_parameters->qkv_format = qkv_format;
    output_parameters->past_kv_format = past_kv_format;
  }

  return Status::OK();
}

Status CheckInputs(const Tensor* query,
                   const Tensor* key,
                   const Tensor* value,
                   const Tensor* past_key,
                   const Tensor* past_value,
                   const Tensor* cos_cache,
                   const Tensor* sin_cache,
                   void* parameters,
                   int num_heads,
                   int kv_num_heads,
                   const Tensor* seqlens_k,
                   const Tensor* total_seqlen,
                   bool is_past_bsnh,
                   float scale,
                   int max_threads_per_block) {
  if (max_threads_per_block > 0 && num_heads > max_threads_per_block) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "num_heads should be no larger than ", max_threads_per_block);
  }

  return CheckInputs(query, key, value, past_key, past_value, cos_cache, sin_cache, parameters, num_heads, kv_num_heads, seqlens_k, total_seqlen, is_past_bsnh, scale);
}

}  // namespace group_query_attention_helper
}  // namespace contrib
}  // namespace onnxruntime
