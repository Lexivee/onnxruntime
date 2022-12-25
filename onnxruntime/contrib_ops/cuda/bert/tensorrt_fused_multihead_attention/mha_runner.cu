/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
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

#include "contrib_ops/cuda/bert/tensorrt_fused_multihead_attention/mha_runner.h"
#include "contrib_ops/cuda/bert/tensorrt_fused_multihead_attention/fused_multihead_attention_v2.h"

namespace onnxruntime {
namespace contrib {
namespace cuda {

union __half2_uint32_t_union {
  half2 fp162;
  uint32_t u32;
};

void set_alpha_fp16(uint32_t& alpha, float norm) {
  __half2_uint32_t_union temp;
  temp.fp162 = __float2half2_rn(norm);
  alpha = temp.u32;
}

class FusedMHARunnerFP16v2::mhaImpl {
 public:
  mhaImpl(FusedMHARunnerFP16v2* interface)
      : interface(interface), sm(interface->mSm), xmmaKernel(getXMMAKernelsV2(DATA_TYPE_FP16, sm)) {
    ORT_ENFORCE((sm == kSM_70 || sm == kSM_75 || sm == kSM_80 || sm == kSM_86),
                "Unsupported architecture");
    params.clear();
  }

  ~mhaImpl() {}

  void setup(const int S, const int B) {
    size_t warps_m = 2;
    size_t warps_n = 2;
    size_t warps_k = 1;

    // For bert and vit, use flash attention when sequence length is larger than the threshold.
    use_flash_attention = is_flash_attention(S);

    if (use_flash_attention) {
      warps_m = 4;
      warps_n = 1;
    } else {
      if (sm == 70) {
        if (S == 64 || S == 96) {
          warps_m = 2;
          warps_n = 2;
        } else if (S == 128) {
          warps_m = 1;
          warps_n = 4;
        } else if (S == 256 || S == 384) {
          warps_m = 1;
          warps_n = 8;
        } else {
          ORT_ENFORCE(false, "Unsupporte sequence length");
        }
      } else {
        if (S == 32 || S == 64 || S == 96 || S == 128) {
          warps_m = 2;
          warps_n = 2;
        } else if (S == 192 || S == 256) {
          warps_m = 1;
          warps_n = 4;
        } else if (S == 384 || S == 512) {
          warps_m = 1;
          warps_n = 8;
        } else {
          ORT_ENFORCE(false, "Unsupporte sequence length");
        }
      }
    }

    // The number of threads per CTA.
    threads_per_cta = warps_m * warps_n * warps_k * 32;
    // The number of xmmas in the M dimension. We use one uint32_t per XMMA in the M dimension.
    xmmas_m = (S + 16 * warps_m - 1) / (16 * warps_m);
    // The number of xmmas in the N dimension.
    xmmas_n = (S + 16 * warps_n - 1) / (16 * warps_n);

    const float scale_bmm1 = interface->mRsqrtHeadSize;
    const float scale_softmax = 1.f;  // Seems to be only required for int8
    const float scale_bmm2 = 1.f;

    set_alpha_fp16(params.scale_bmm1, scale_bmm1);
    set_alpha_fp16(params.scale_softmax, scale_softmax);
    set_alpha_fp16(params.scale_bmm2, scale_bmm2);

    params.b = B;
    params.h = interface->mNumHeads;
    params.s = S;
    params.d = interface->mHeadSize;

    // For now we set window_num = 0, to avoid using fused multi-head window-attention kernel
    // params.window_num = 0;

    params.qkv_stride_in_bytes = 3 * interface->mNumHeads * interface->mHeadSize * sizeof(half);
    params.packed_mask_stride_in_bytes = xmmas_m * threads_per_cta * sizeof(uint32_t);
    params.o_stride_in_bytes = interface->mNumHeads * interface->mHeadSize * sizeof(half);

    has_causal_mask = false;
  }

  void setup_causal_masked_fmha(const int S, const int B) {
    const float scale_bmm1 = interface->mRsqrtHeadSize;
    const float scale_softmax = 1.f;  // Seems to be only required for int8
    const float scale_bmm2 = 1.f;

    set_alpha_fp16(params.scale_bmm1, scale_bmm1);
    set_alpha_fp16(params.scale_softmax, scale_softmax);
    set_alpha_fp16(params.scale_bmm2, scale_bmm2);

    params.b = B;
    params.h = interface->mNumHeads;
    params.s = S;
    params.d = interface->mHeadSize;

    // mLdQKV = 3 * B * mNumHeads * mHeadSize;
    // mLdOut = B * mNumHeads * mHeadSize;

    params.qkv_stride_in_bytes = 3 * interface->mNumHeads * interface->mHeadSize * sizeof(half);
    params.o_stride_in_bytes = interface->mNumHeads * interface->mHeadSize * sizeof(half);

    // fallback to original fmha_v2 when head_size <= 64 and seq_len <- 128
    use_flash_attention = interface->mEnableFlashAttention;
    if (params.d <= 64 && params.s <= 128) {
      use_flash_attention = false;
      // get max sequence length
      if (params.s > 64) {
        params.s = 128;
      } else {
        params.s = 64;
      }
    }

    // set flags
    params.force_unroll = use_flash_attention;
    has_causal_mask = true;
  }

  void run(const void* input, const void* cu_seqlens, void* output, cudaStream_t stream) {
    params.qkv_ptr = const_cast<void*>(input);
    params.o_ptr = output;
    params.cu_seqlens = static_cast<int*>(const_cast<void*>(cu_seqlens));
    xmmaKernel->run(params, stream, use_flash_attention, has_causal_mask);
    CUDA_CALL_THROW(cudaPeekAtLastError());
  }

  bool isValid(int s) const {
    return xmmaKernel->isValid(s) || is_flash_attention(s);
  }

  int getSFromMaxSeqLen(const int max_seq_len) const {
    if (is_flash_attention(max_seq_len)) {
      return max_seq_len;
    }

    int S = max_seq_len;
    if (max_seq_len <= 32) {
      S = 32;
    } else if (max_seq_len <= 64) {
      S = 64;
    } else if (max_seq_len <= 96) {
      S = 96;
    } else if (max_seq_len <= 128) {
      S = 128;
    } else if (max_seq_len <= 192) {
      S = 192;
    } else if (max_seq_len <= 256) {
      S = 256;
    } else if (max_seq_len <= 384) {
      S = 384;
    } else if (max_seq_len <= 512) {
      S = 512;
    }

    return S;
  }

 protected:
  bool is_flash_attention(const int S) const {
    ORT_ENFORCE(interface->mHasCausalMask == false);
    return interface->mEnableFlashAttention && S >= kMinSequenceLengthFlashAttention;
  }

 private:
  FusedMHARunnerFP16v2* interface;
  Fused_multihead_attention_params_v2 params;
  int sm;
  const FusedMultiHeadAttentionXMMAKernelV2* xmmaKernel;
  size_t xmmas_m;
  size_t xmmas_n;
  size_t threads_per_cta;
  bool use_flash_attention = false;
  bool has_causal_mask = false;
};

FusedMHARunnerFP16v2::FusedMHARunnerFP16v2(const int numHeads, const int headSize, const int sm, bool causal_mask, bool enable_flash_attention)
    : MHARunner(numHeads, headSize, 2, causal_mask), mSm(sm), mEnableFlashAttention(enable_flash_attention), pimpl(new mhaImpl(this)) {
}

void FusedMHARunnerFP16v2::setup(const int S, const int B) {
  MHARunner::setup(S, B);
  if (mHasCausalMask) {
    pimpl->setup_causal_masked_fmha(S, B);
  } else {
    pimpl->setup(S, B);
  }
}

bool FusedMHARunnerFP16v2::is_supported(int sm, int head_size, int sequence_length,
                                        bool enable_flash_attention, bool causal) {
  if (causal) {
    if (!(sm == kSM_70 || sm == kSM_75 || sm == kSM_80 || sm == kSM_86)) {
      return false;
    }

    if (enable_flash_attention) {
      return head_size == 64 ||
             head_size == 32 ||
             head_size == 40 ||
             head_size == 80 ||
             head_size == 128 ||
             head_size == 144 ||
             head_size == 160 ||
             head_size == 256;
    }

    return (head_size == 64 || head_size == 32 || head_size == 40) && sequence_length <= 128;
  }

  if (!(sm == kSM_70 || sm == kSM_75 || sm == kSM_80 || sm == kSM_86)) {
    return false;
  }

  if (head_size != 64 && head_size != 32) {
    return false;
  }

  if (sm == kSM_70 && head_size == 32) {
    return false;
  }

  // Use flash attention when sequence_length >= 512 for BERT
  if (enable_flash_attention && sequence_length >= kMinSequenceLengthFlashAttention) {
    return true;
  }

  // TODO: enable flash attention when sequence_length > 384.
  const int max_sequence_length = ((sm >= kSM_80 || head_size == 32) ? 512 : 384);
  return sequence_length <= max_sequence_length;
}

size_t FusedMHARunnerFP16v2::getWorkspaceSize() const {
  return 0;
}

void FusedMHARunnerFP16v2::run(const void* input, const void* cu_seqlens, void* output, cudaStream_t stream) {
  pimpl->run(input, cu_seqlens, output, stream);
}

bool FusedMHARunnerFP16v2::isValid(int s) const {
  return pimpl->isValid(s);
}

int FusedMHARunnerFP16v2::getSFromMaxSeqLen(const int max_seq_len) const {
  return pimpl->getSFromMaxSeqLen(max_seq_len);
}

}  // namespace cuda
}  // namespace contrib
}  // namespace onnxruntime
