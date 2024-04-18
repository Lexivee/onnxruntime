# --------------------------------------------------------------------------
# Copyright 2020 The HuggingFace Inc. team
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# --------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT License.  See License.txt in the project root for
# license information.
# -------------------------------------------------------------------------
import math
import random
import unittest
from colorama import Fore, Style, init

import numpy
import torch
from bert_padding import pad_input, unpad_input
from einops import rearrange, repeat
from onnx import TensorProto, helper

from onnxruntime import InferenceSession, OrtValue, SessionOptions

torch.manual_seed(0)

pipeline_mode = True  # Reduces number of tests so pipeline doesn't time out

# Initialize colorama
init(autoreset=True)


class Formats:
    BSNH = 0
    BNSH = 1


class Config:
    batch_size = 0
    sequence_length = 0
    kv_sequence_length = 0
    past_sequence_length = 0
    num_heads = 0
    kv_num_heads = 0
    head_size = 0

    def __init__(self, b, s, s2, sp, n, n2, h):
        self.batch_size = b
        self.sequence_length = s
        self.kv_sequence_length = s2
        self.past_sequence_length = sp
        self.num_heads = n
        self.kv_num_heads = n2
        self.head_size = h


class PromptConfig:
    batch_size = 0
    q_sequence_length = 0
    kv_sequence_length = 0
    buffer_sequence_length = 0
    num_heads = 0
    kv_num_heads = 0
    head_size = 0

    def __init__(self, b, sq, skv, sb, n, n2, h):
        self.batch_size = b
        self.q_sequence_length = sq
        self.kv_sequence_length = skv
        self.buffer_sequence_length = sb
        self.num_heads = n
        self.kv_num_heads = n2
        self.head_size = h


# LLaMA Microsoft model
class LlamaMSRotaryEmbedding(torch.nn.Module):
    def __init__(self):
        super().__init__()

    def rotate_tensor(
        self,
        x: torch.Tensor,  # BxSxNxH
        cos: torch.Tensor,  # 1xSx1x(H/2)
        sin: torch.Tensor,  # 1xSx1x(H/2)
        pos: torch.Tensor,
        interleaved: bool,
    ):
        # Dimension of x is [batch_size, seq_len, n_heads, head_dim]
        rot_dim = 2 * cos.shape[3]

        # Dolly requires partial rotation
        x_rot = x[:, :, :, :rot_dim]

        if interleaved:
            x1 = x_rot[:, :, :, 0::2]
            x2 = x_rot[:, :, :, 1::2]
        else:
            half = x_rot.shape[-1] // 2
            x1 = x[:, :, :, 0:half]
            x2 = x[:, :, :, half : 2 * half]

        seq_len = x.shape[1]

        # cos_x: (1, S, 1, H/2)
        # sin_x: (1, S, 1, H/2)
        # x1: (B, S, N, H/2)
        # x2: (B, S, N, H/2)
        if seq_len == 1:
            batch_size = x.shape[0]
            pos_i = pos.unsqueeze(1).unsqueeze(2).unsqueeze(3).long()
            cos_x = cos.expand(batch_size, -1, -1, -1)
            sin_x = sin.expand(batch_size, -1, -1, -1)
            cos_x = cos_x.gather(1, pos_i.expand(-1, -1, cos.shape[2], cos.shape[3]))
            sin_x = sin_x.gather(1, pos_i.expand(-1, -1, sin.shape[2], sin.shape[3]))
            real = cos_x * x1 - sin_x * x2
            imag = sin_x * x1 + cos_x * x2
            if interleaved:
                x_rot[:, :, :, 0::2] = real
                x_rot[:, :, :, 1::2] = imag
            else:
                x_rot = torch.cat((real, imag), dim=-1)
        else:
            cos_x = cos[:, 0:seq_len, :, :]
            sin_x = sin[:, 0:seq_len, :, :]
            real = cos_x * x1 - sin_x * x2
            imag = sin_x * x1 + cos_x * x2
            if interleaved:
                x_rot[:, :, :, 0::2] = real
                x_rot[:, :, :, 1::2] = imag
            else:
                x_rot = torch.cat((real, imag), dim=-1)

        return torch.cat((x_rot, x[:, :, :, rot_dim:]), dim=-1)

    def forward(self, x, cos, sin, pos, interleaved):
        return self.rotate_tensor(x, cos, sin, pos, interleaved)


def create_group_query_attention_graph_prompt(
    config,
    past_kv_format=Formats.BSNH,
    share_buffer=True,
    local_window_size=-1,
    rotary=False,
    rotary_interleaved=False,
    packed=False,
):
    past_kv_seqlen = config.buffer_sequence_length if share_buffer else 0
    present_kv_seqlen = config.buffer_sequence_length if share_buffer else config.kv_sequence_length
    nodes = [
        helper.make_node(
            "GroupQueryAttention",
            [
                "query",
                "key" if not packed else "",
                "value" if not packed else "",
                "past_key" if share_buffer else "",
                "past_value" if share_buffer else "",
                "seqlens_k",
                "total_sequence_length",
                "cos_cache" if rotary else "",
                "sin_cache" if rotary else "",
            ],
            ["output", "present_key", "present_value"],
            "GroupQueryAttention_0",
            num_heads=config.num_heads,
            kv_num_heads=config.kv_num_heads,
            local_window_size=local_window_size,
            do_rotary=rotary,
            rotary_interleaved=rotary_interleaved,
            # is_past_bsnh=1 if past_kv_format == Formats.BSNH else 0,
            # kv_share_buffer=1 if share_buffer else 0,
            domain="com.microsoft",
        ),
    ]

    graph_input = [
        helper.make_tensor_value_info(
            "query",
            TensorProto.FLOAT,
            [
                config.batch_size,
                config.q_sequence_length,
                (
                    (config.num_heads * config.head_size)
                    if not packed
                    else (config.num_heads * config.head_size + 2 * config.kv_num_heads * config.head_size)
                ),
            ],
        ),
        helper.make_tensor_value_info(
            "seqlens_k",
            TensorProto.INT32,
            [config.batch_size],
        ),
        helper.make_tensor_value_info(
            "total_sequence_length",
            TensorProto.INT32,
            [1],
        ),
    ]
    if not packed:
        graph_input += [
            helper.make_tensor_value_info(
                "key",
                TensorProto.FLOAT,
                [
                    config.batch_size,
                    config.kv_sequence_length,
                    config.kv_num_heads * config.head_size,
                ],
            ),
            helper.make_tensor_value_info(
                "value",
                TensorProto.FLOAT,
                [
                    config.batch_size,
                    config.kv_sequence_length,
                    config.kv_num_heads * config.head_size,
                ],
            ),
        ]
    if share_buffer:
        graph_input += [
            helper.make_tensor_value_info(
                "past_key",
                TensorProto.FLOAT,
                [
                    config.batch_size,
                    past_kv_seqlen if past_kv_format == Formats.BSNH else config.kv_num_heads,
                    config.kv_num_heads if past_kv_format == Formats.BSNH else past_kv_seqlen,
                    config.head_size,
                ],
            ),
            helper.make_tensor_value_info(
                "past_value",
                TensorProto.FLOAT,
                [
                    config.batch_size,
                    past_kv_seqlen if past_kv_format == Formats.BSNH else config.kv_num_heads,
                    config.kv_num_heads if past_kv_format == Formats.BSNH else past_kv_seqlen,
                    config.head_size,
                ],
            ),
        ]
    if rotary:
        graph_input += [
            helper.make_tensor_value_info(
                "cos_cache",
                TensorProto.FLOAT,
                [
                    config.buffer_sequence_length if share_buffer else config.kv_sequence_length,
                    (math.floor(config.head_size / 16) * 16) // 2,
                ],
            ),
            helper.make_tensor_value_info(
                "sin_cache",
                TensorProto.FLOAT,
                [
                    config.buffer_sequence_length if share_buffer else config.kv_sequence_length,
                    (math.floor(config.head_size / 16) * 16) // 2,
                ],
            ),
        ]

    graph_output = [
        helper.make_tensor_value_info(
            "output",
            TensorProto.FLOAT,
            [config.batch_size, config.q_sequence_length, config.num_heads * config.head_size],
        ),
        helper.make_tensor_value_info(
            "present_key",
            TensorProto.FLOAT,
            [
                config.batch_size,
                present_kv_seqlen if past_kv_format == Formats.BSNH else config.kv_num_heads,
                config.kv_num_heads if past_kv_format == Formats.BSNH else present_kv_seqlen,
                config.head_size,
            ],
        ),
        helper.make_tensor_value_info(
            "present_value",
            TensorProto.FLOAT,
            [
                config.batch_size,
                present_kv_seqlen if past_kv_format == Formats.BSNH else config.kv_num_heads,
                config.kv_num_heads if past_kv_format == Formats.BSNH else present_kv_seqlen,
                config.head_size,
            ],
        ),
        helper.make_tensor_value_info(
            "present_key",
            TensorProto.FLOAT,
            [
                config.batch_size,
                config.kv_sequence_length if past_kv_format == Formats.BSNH else config.kv_num_heads,
                config.kv_num_heads if past_kv_format == Formats.BSNH else config.kv_sequence_length,
                config.head_size,
            ],
        ),
        helper.make_tensor_value_info(
            "present_value",
            TensorProto.FLOAT,
            [
                config.batch_size,
                config.kv_sequence_length if past_kv_format == Formats.BSNH else config.kv_num_heads,
                config.kv_num_heads if past_kv_format == Formats.BSNH else config.kv_sequence_length,
                config.head_size,
            ],
        ),
    ]

    graph = helper.make_graph(
        nodes,
        "GroupQueryAttention_Graph",
        graph_input,
        graph_output,
    )

    model = helper.make_model(graph)
    return model.SerializeToString()


def create_group_query_attention_graph_past(
    config,
    past_kv_format=Formats.BSNH,
    share_buffer=True,
    local_window_size=-1,
    rotary=False,
    rotary_interleaved=False,
    packed=False,
):
    past_kv_seqlen = config.kv_sequence_length
    present_kv_seqlen = (
        config.kv_sequence_length if share_buffer else config.kv_sequence_length + config.sequence_length
    )
    nodes = [
        helper.make_node(
            "GroupQueryAttention",
            [
                "query",
                "key" if not packed else "",
                "value" if not packed else "",
                "past_key",
                "past_value",
                "seqlens_k",
                "total_sequence_length",
                "cos_cache" if rotary else "",
                "sin_cache" if rotary else "",
            ],
            ["output", "present_key", "present_value"],
            "GroupQueryAttention_0",
            num_heads=config.num_heads,
            kv_num_heads=config.kv_num_heads,
            local_window_size=local_window_size,
            do_rotary=rotary,
            rotary_interleaved=rotary_interleaved,
            # is_past_bsnh=1 if past_kv_format == Formats.BSNH else 0,
            # kv_share_buffer=1 if share_buffer else 0,
            domain="com.microsoft",
        ),
    ]

    graph_input = [
        helper.make_tensor_value_info(
            "query",
            TensorProto.FLOAT,
            [
                config.batch_size,
                config.sequence_length,
                (
                    (config.num_heads * config.head_size)
                    if not packed
                    else (config.num_heads * config.head_size + 2 * config.kv_num_heads * config.head_size)
                ),
            ],
        ),
        helper.make_tensor_value_info(
            "past_key",
            TensorProto.FLOAT,
            [
                config.batch_size,
                past_kv_seqlen if past_kv_format == Formats.BSNH else config.kv_num_heads,
                config.kv_num_heads if past_kv_format == Formats.BSNH else past_kv_seqlen,
                config.head_size,
            ],
        ),
        helper.make_tensor_value_info(
            "past_value",
            TensorProto.FLOAT,
            [
                config.batch_size,
                past_kv_seqlen if past_kv_format == Formats.BSNH else config.kv_num_heads,
                config.kv_num_heads if past_kv_format == Formats.BSNH else past_kv_seqlen,
                config.head_size,
            ],
        ),
        helper.make_tensor_value_info(
            "seqlens_k",
            TensorProto.INT32,
            [config.batch_size],
        ),
        helper.make_tensor_value_info(
            "total_sequence_length",
            TensorProto.INT32,
            [1],
        ),
    ]
    if not packed:
        graph_input += [
            helper.make_tensor_value_info(
                "key",
                TensorProto.FLOAT,
                [
                    config.batch_size,
                    config.sequence_length,
                    config.kv_num_heads * config.head_size,
                ],
            ),
            helper.make_tensor_value_info(
                "value",
                TensorProto.FLOAT,
                [
                    config.batch_size,
                    config.sequence_length,
                    config.kv_num_heads * config.head_size,
                ],
            ),
        ]
    if rotary:
        graph_input += [
            helper.make_tensor_value_info(
                "cos_cache",
                TensorProto.FLOAT,
                [
                    config.kv_sequence_length + (0 if share_buffer else config.sequence_length),
                    (math.floor(config.head_size / 16) * 16) // 2,
                ],
            ),
            helper.make_tensor_value_info(
                "sin_cache",
                TensorProto.FLOAT,
                [
                    config.kv_sequence_length + (0 if share_buffer else config.sequence_length),
                    (math.floor(config.head_size / 16) * 16) // 2,
                ],
            ),
        ]

    graph_output = [
        helper.make_tensor_value_info(
            "output",
            TensorProto.FLOAT,
            [config.batch_size, config.sequence_length, config.num_heads * config.head_size],
        ),
        helper.make_tensor_value_info(
            "present_key",
            TensorProto.FLOAT,
            [
                config.batch_size,
                present_kv_seqlen if past_kv_format == Formats.BSNH else config.kv_num_heads,
                config.kv_num_heads if past_kv_format == Formats.BSNH else present_kv_seqlen,
                config.head_size,
            ],
        ),
        helper.make_tensor_value_info(
            "present_value",
            TensorProto.FLOAT,
            [
                config.batch_size,
                present_kv_seqlen if past_kv_format == Formats.BSNH else config.kv_num_heads,
                config.kv_num_heads if past_kv_format == Formats.BSNH else present_kv_seqlen,
                config.head_size,
            ],
        ),
    ]

    graph = helper.make_graph(
        nodes,
        "GroupQueryAttention_Graph",
        graph_input,
        graph_output,
    )

    model = helper.make_model(graph)
    return model.SerializeToString()


def generate_random_padding_mask(max_seqlen, batch_size, device, mode="random"):
    assert mode in ["full", "random", "third"]
    if mode == "full":
        lengths = torch.full((batch_size, 1), max_seqlen, device=device, dtype=torch.int32)
    elif mode == "random":
        lengths = torch.randint(max(1, max_seqlen - 20), max_seqlen, (batch_size, 1), device=device)
    else:
        lengths = torch.randint(max_seqlen // 3, max_seqlen, (batch_size, 1), device=device)
    padding_mask = repeat(torch.arange(max_seqlen, device=device), "s -> b s", b=batch_size) < lengths
    return padding_mask


def generate_qkv(q, k, v, query_padding_mask=None, key_padding_mask=None, kvpacked=False, qkvpacked=False):
    """
    Arguments:
        q: (batch_size, seqlen_q, nheads, d)
        k: (batch_size, seqlen_k, nheads_k, d)
        v: (batch_size, seqlen_k, nheads_k, d)
        query_padding_mask: (batch_size, seqlen), bool
        key_padding_mask: (batch_size, seqlen), bool
    """
    assert not (kvpacked and qkvpacked)
    batch_size, seqlen_q, nheads, d = q.shape
    _, seqlen_k, nheads_k, _ = k.shape
    assert k.shape == (batch_size, seqlen_k, nheads_k, d)
    assert v.shape == (batch_size, seqlen_k, nheads_k, d)

    if query_padding_mask is not None:
        q_unpad, indices_q, cu_seqlens_q, max_seqlen_q = unpad_input(q, query_padding_mask)

        def output_pad_fn(output_unpad):
            return pad_input(output_unpad, indices_q, batch_size, seqlen_q)

    else:
        q_unpad = rearrange(q, "b s h d -> (b s) h d")
        cu_seqlens_q = torch.arange(
            0, (batch_size + 1) * seqlen_q, step=seqlen_q, dtype=torch.int32, device=q_unpad.device
        )
        max_seqlen_q = seqlen_q

        def output_pad_fn(output_unpad):
            return rearrange(output_unpad, "(b s) h d -> b s h d", b=batch_size)

    if key_padding_mask is not None:
        k_unpad, indices_k, cu_seqlens_k, max_seqlen_k = unpad_input(k, key_padding_mask)
        v_unpad, _, _, _ = unpad_input(v, key_padding_mask)
    else:
        k_unpad = rearrange(k, "b s h d -> (b s) h d")
        v_unpad = rearrange(v, "b s h d -> (b s) h d")
        cu_seqlens_k = torch.arange(
            0, (batch_size + 1) * seqlen_k, step=seqlen_k, dtype=torch.int32, device=k_unpad.device
        )
        max_seqlen_k = seqlen_k

    if qkvpacked:
        assert (query_padding_mask == key_padding_mask).all()
        assert nheads == nheads_k
        qkv_unpad = torch.stack([q_unpad, k_unpad, v_unpad], dim=1)
        qkv = torch.stack([q, k, v], dim=2)
        if query_padding_mask is not None:

            def dqkv_pad_fn(dqkv_unpad):
                return pad_input(dqkv_unpad, indices_q, batch_size, seqlen_q)

        else:

            def dqkv_pad_fn(dqkv_unpad):
                return rearrange(dqkv_unpad, "(b s) t h d -> b s t h d", b=batch_size)

        return (
            qkv_unpad.detach().requires_grad_(),
            cu_seqlens_q,
            max_seqlen_q,
            qkv.detach().requires_grad_(),
            output_pad_fn,
            dqkv_pad_fn,
        )
    elif kvpacked:
        kv_unpad = torch.stack([k_unpad, v_unpad], dim=1)
        kv = torch.stack([k, v], dim=2)
        dq_pad_fn = output_pad_fn
        if key_padding_mask is not None:

            def dkv_pad_fn(dkv_unpad):
                return pad_input(dkv_unpad, indices_k, batch_size, seqlen_k)

        else:

            def dkv_pad_fn(dkv_unpad):
                return rearrange(dkv_unpad, "(b s) t h d -> b s t h d", b=batch_size)

        return (
            q_unpad.detach().requires_grad_(),
            kv_unpad.detach().requires_grad_(),
            cu_seqlens_q,
            cu_seqlens_k,
            max_seqlen_q,
            max_seqlen_k,
            q.detach().requires_grad_(),
            kv.detach().requires_grad_(),
            output_pad_fn,
            dq_pad_fn,
            dkv_pad_fn,
        )
    else:
        dq_pad_fn = output_pad_fn
        if key_padding_mask is not None:

            def dk_pad_fn(dk_unpad):
                return pad_input(dk_unpad, indices_k, batch_size, seqlen_k)

        else:

            def dk_pad_fn(dk_unpad):
                return rearrange(dk_unpad, "(b s) h d -> b s h d", b=batch_size)

        return (
            q_unpad.detach().requires_grad_(),
            k_unpad.detach().requires_grad_(),
            v_unpad.detach().requires_grad_(),
            cu_seqlens_q,
            cu_seqlens_k,
            max_seqlen_q,
            max_seqlen_k,
            q.detach().requires_grad_(),
            k.detach().requires_grad_(),
            v.detach().requires_grad_(),
            output_pad_fn,
            dq_pad_fn,
            dk_pad_fn,
        )


def create_inputs(config: Config, kv_packed=False, qkv_packed=True):
    qkv = torch.randn(
        config.batch_size,
        config.sequence_length,
        3,
        config.num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    key_padding_mask = generate_random_padding_mask(
        config.sequence_length, config.batch_size, device="cpu", mode="random"
    )
    qkv_unpad, cu_seqlens, max_seqlen, qkv, output_pad_fn, dqkv_pad_fn = generate_qkv(
        *qkv.unbind(dim=2), key_padding_mask, key_padding_mask, kv_packed, qkv_packed
    )
    return qkv_unpad, cu_seqlens, max_seqlen, qkv, output_pad_fn, dqkv_pad_fn, key_padding_mask


def generate_token_offset(cu_seqlens, max_seqlen):
    token_offset = []
    token_padset = []  # These are the indices that contain padding tokens
    for i in range(1, len(cu_seqlens)):
        start = i - 1
        pre_seqlen = cu_seqlens[i - 1]
        seqlen = cu_seqlens[i]
        token_offset += range(start * max_seqlen, (start * max_seqlen) + (seqlen - pre_seqlen))
        token_padset += range((start * max_seqlen) + (seqlen - pre_seqlen), i * max_seqlen)
    return numpy.asarray(token_offset + token_padset, dtype=numpy.int32)


def gqa_prompt_func(
    q,
    k,
    v,
    config,
    new_k,
    new_v,
    cos=None,
    sin=None,
    seqlens_k=None,
    window_size=-1,
    past_kv_format=Formats.BSNH,
    share_buffer=True,
    rotary_interleaved=False,
):
    onnx_model_str = create_group_query_attention_graph_prompt(
        config,
        past_kv_format,
        share_buffer,
        local_window_size=window_size,
        rotary=cos is not None,
        rotary_interleaved=rotary_interleaved,
        packed=new_k is None,
    )
    q = torch.reshape(q, (config.batch_size, config.q_sequence_length, -1))
    past_k = k.clone() if share_buffer else None
    past_v = v.clone() if share_buffer else None
    if new_k is not None:
        new_k = torch.reshape(new_k, (config.batch_size, config.kv_sequence_length, -1))
        new_v = torch.reshape(new_v, (config.batch_size, config.kv_sequence_length, -1))
    if share_buffer:
        ort_inputs = {
            "query": q.detach().cpu().numpy(),
            "past_key": OrtValue.ortvalue_from_numpy(past_k.detach().cpu().numpy(), "cpu", 0),
            "past_value": OrtValue.ortvalue_from_numpy(past_v.detach().cpu().numpy(), "cpu", 0),
            "seqlens_k": seqlens_k.detach().cpu().numpy().astype(numpy.int32),
            "total_sequence_length": torch.tensor([config.q_sequence_length], dtype=torch.int32).detach().cpu().numpy(),
        }
        sess_options = SessionOptions()
        ort_session = InferenceSession(onnx_model_str, sess_options, providers=["CPUExecutionProvider"])
        io_binding = ort_session.io_binding()
        if new_k is not None:
            ort_inputs["key"] = new_k.detach().cpu().numpy()
            ort_inputs["value"] = new_v.detach().cpu().numpy()
            io_binding.bind_cpu_input("key", ort_inputs["key"])
            io_binding.bind_cpu_input("value", ort_inputs["value"])
        if cos is not None:
            ort_inputs["cos_cache"] = cos.detach().cpu().numpy()
            ort_inputs["sin_cache"] = sin.detach().cpu().numpy()
            io_binding.bind_cpu_input("cos_cache", ort_inputs["cos_cache"])
            io_binding.bind_cpu_input("sin_cache", ort_inputs["sin_cache"])
        # TODO: do we need io binding for cpu input?
        io_binding.bind_cpu_input("query", ort_inputs["query"])
        io_binding.bind_input(
            "past_key", "cpu", 0, numpy.float32, ort_inputs["past_key"].shape(), ort_inputs["past_key"].data_ptr()
        )
        io_binding.bind_input(
            "past_value",
            "cpu",
            0,
            numpy.float32,
            ort_inputs["past_value"].shape(),
            ort_inputs["past_value"].data_ptr(),
        )
        io_binding.bind_cpu_input("seqlens_k", ort_inputs["seqlens_k"])
        io_binding.bind_cpu_input("total_sequence_length", ort_inputs["total_sequence_length"])
        io_binding.bind_output("output")
        io_binding.bind_ortvalue_output("present_key", ort_inputs["past_key"])
        io_binding.bind_ortvalue_output("present_value", ort_inputs["past_value"])
        ort_session.run_with_iobinding(io_binding)
        ort_output, present_k, present_v = io_binding.copy_outputs_to_cpu()
        ort_output = numpy.array(ort_output)
        output = torch.tensor(ort_output)
        return output, present_k, present_v
    else:
        ort_inputs = {
            "query": q.detach().cpu().numpy(),
            "seqlens_k": seqlens_k.detach().cpu().numpy().astype(numpy.int32),
            "total_sequence_length": torch.tensor([config.q_sequence_length], dtype=torch.int32).detach().cpu().numpy(),
        }
        sess_options = SessionOptions()
        ort_session = InferenceSession(onnx_model_str, sess_options, providers=["CPUExecutionProvider"])
        io_binding = ort_session.io_binding()
        if new_k is not None:
            ort_inputs["key"] = new_k.detach().cpu().numpy()
            ort_inputs["value"] = new_v.detach().cpu().numpy()
            io_binding.bind_cpu_input("key", ort_inputs["key"])
            io_binding.bind_cpu_input("value", ort_inputs["value"])
        if cos is not None:
            ort_inputs["cos_cache"] = cos.detach().cpu().numpy()
            ort_inputs["sin_cache"] = sin.detach().cpu().numpy()
            io_binding.bind_cpu_input("cos_cache", ort_inputs["cos_cache"])
            io_binding.bind_cpu_input("sin_cache", ort_inputs["sin_cache"])
        io_binding.bind_cpu_input("query", ort_inputs["query"])
        io_binding.bind_cpu_input("seqlens_k", ort_inputs["seqlens_k"])
        io_binding.bind_cpu_input("total_sequence_length", ort_inputs["total_sequence_length"])
        io_binding.bind_output("output")
        io_binding.bind_output("present_key")
        io_binding.bind_output("present_value")
        ort_session.run_with_iobinding(io_binding)
        ort_output, present_k, present_v = io_binding.copy_outputs_to_cpu()
        ort_output = numpy.array(ort_output)
        output = torch.tensor(ort_output)
        return output, present_k, present_v


def gqa_past_func(
    q,
    k,
    v,
    config,
    new_k,
    new_v,
    cos=None,
    sin=None,
    seqlens_k=None,
    past_kv_format=Formats.BSNH,
    share_buffer=True,
    window_size=-1,
    rotary_interleaved=False,
):
    onnx_model_str = create_group_query_attention_graph_past(
        config,
        past_kv_format,
        share_buffer,
        local_window_size=window_size,
        rotary=cos is not None,
        rotary_interleaved=rotary_interleaved,
        packed=new_k is None,
    )
    q = torch.reshape(q, (config.batch_size, config.sequence_length, -1))
    past_k = k.clone()
    past_v = v.clone()
    if new_k is not None:
        new_k = torch.reshape(new_k, (config.batch_size, config.sequence_length, -1))
        new_v = torch.reshape(new_v, (config.batch_size, config.sequence_length, -1))
    if share_buffer:
        ort_inputs = {
            "query": q.detach().cpu().numpy(),
            "past_key": OrtValue.ortvalue_from_numpy(past_k.detach().cpu().numpy(), "cpu", 0),
            "past_value": OrtValue.ortvalue_from_numpy(past_v.detach().cpu().numpy(), "cpu", 0),
            "seqlens_k": seqlens_k.detach().cpu().numpy().astype(numpy.int32),
            "total_sequence_length": torch.tensor([config.kv_sequence_length], dtype=torch.int32)
            .detach()
            .cpu()
            .numpy(),
        }
        sess_options = SessionOptions()
        ort_session = InferenceSession(onnx_model_str, sess_options, providers=["CPUExecutionProvider"])
        io_binding = ort_session.io_binding()
        if new_k is not None:
            ort_inputs["key"] = new_k.detach().cpu().numpy()
            ort_inputs["value"] = new_v.detach().cpu().numpy()
            io_binding.bind_cpu_input("key", ort_inputs["key"])
            io_binding.bind_cpu_input("value", ort_inputs["value"])
        if cos is not None:
            ort_inputs["cos_cache"] = cos.detach().cpu().numpy()
            ort_inputs["sin_cache"] = sin.detach().cpu().numpy()
            io_binding.bind_cpu_input("cos_cache", ort_inputs["cos_cache"])
            io_binding.bind_cpu_input("sin_cache", ort_inputs["sin_cache"])
        io_binding.bind_cpu_input("query", ort_inputs["query"])
        io_binding.bind_input(
            "past_key", "cpu", 0, numpy.float32, ort_inputs["past_key"].shape(), ort_inputs["past_key"].data_ptr()
        )
        io_binding.bind_input(
            "past_value",
            "cpu",
            0,
            numpy.float32,
            ort_inputs["past_value"].shape(),
            ort_inputs["past_value"].data_ptr(),
        )
        io_binding.bind_cpu_input("seqlens_k", ort_inputs["seqlens_k"])
        io_binding.bind_cpu_input("total_sequence_length", ort_inputs["total_sequence_length"])
        io_binding.bind_output("output")
        io_binding.bind_ortvalue_output("present_key", ort_inputs["past_key"])
        io_binding.bind_ortvalue_output("present_value", ort_inputs["past_value"])
        ort_session.run_with_iobinding(io_binding)
        ort_output, present_k, present_v = io_binding.copy_outputs_to_cpu()
        ort_output = numpy.array(ort_output)
        output = torch.tensor(ort_output)
        return output, present_k, present_v
    else:
        ort_inputs = {
            "query": q.detach().cpu().numpy(),
            "past_key": past_k.detach().cpu().numpy(),
            "past_value": past_v.detach().cpu().numpy(),
            "seqlens_k": seqlens_k.detach().cpu().numpy().astype(numpy.int32),
            "total_sequence_length": torch.tensor(
                [config.kv_sequence_length + config.sequence_length], dtype=torch.int32
            )
            .detach()
            .cpu()
            .numpy(),
        }
        sess_options = SessionOptions()
        ort_session = InferenceSession(onnx_model_str, sess_options, providers=["CPUExecutionProvider"])
        io_binding = ort_session.io_binding()
        if new_k is not None:
            ort_inputs["key"] = new_k.detach().cpu().numpy()
            ort_inputs["value"] = new_v.detach().cpu().numpy()
            io_binding.bind_cpu_input("key", ort_inputs["key"])
            io_binding.bind_cpu_input("value", ort_inputs["value"])
        if cos is not None:
            ort_inputs["cos_cache"] = cos.detach().cpu().numpy()
            ort_inputs["sin_cache"] = sin.detach().cpu().numpy()
            io_binding.bind_cpu_input("cos_cache", ort_inputs["cos_cache"])
            io_binding.bind_cpu_input("sin_cache", ort_inputs["sin_cache"])
        io_binding.bind_cpu_input("query", ort_inputs["query"])
        io_binding.bind_cpu_input("past_key", ort_inputs["past_key"])
        io_binding.bind_cpu_input("past_value", ort_inputs["past_value"])
        io_binding.bind_cpu_input("seqlens_k", ort_inputs["seqlens_k"])
        io_binding.bind_cpu_input("total_sequence_length", ort_inputs["total_sequence_length"])
        io_binding.bind_output("output")
        io_binding.bind_output("present_key")
        io_binding.bind_output("present_value")
        ort_session.run_with_iobinding(io_binding)
        ort_output, present_k, present_v = io_binding.copy_outputs_to_cpu()
        ort_output = numpy.array(ort_output)
        output = torch.tensor(ort_output)
        return output, present_k, present_v


def construct_causal_mask(seqlen_q, seqlen_k, query_padding_mask=None, key_padding_mask=None, device=None):
    row_idx = rearrange(torch.arange(seqlen_q, device=device, dtype=torch.long), "s -> s 1")
    col_idx = torch.arange(seqlen_k, device=device, dtype=torch.long)
    sk = seqlen_k if key_padding_mask is None else rearrange(key_padding_mask.sum(-1), "b -> b 1 1 1")
    sq = seqlen_q if query_padding_mask is None else rearrange(query_padding_mask.sum(-1), "b -> b 1 1 1")
    return col_idx > row_idx + sk - sq


def construct_local_mask(
    seqlen_q,
    seqlen_k,
    window_size=(-1, -1),  # -1 means infinite window size
    query_padding_mask=None,
    key_padding_mask=None,
    device=None,
):
    row_idx = rearrange(torch.arange(seqlen_q, device=device, dtype=torch.long), "s -> s 1")
    col_idx = torch.arange(seqlen_k, device=device, dtype=torch.long)
    sk = seqlen_k if key_padding_mask is None else rearrange(key_padding_mask.sum(-1), "b -> b 1 1 1")
    sq = seqlen_q if query_padding_mask is None else rearrange(query_padding_mask.sum(-1), "b -> b 1 1 1")
    if window_size[0] < 0:
        return col_idx > row_idx + sk - sq + window_size[1]
    else:
        sk = torch.full_like(col_idx, seqlen_k) if key_padding_mask is None else sk
        return torch.logical_or(
            col_idx > torch.minimum(row_idx + sk - sq + window_size[1], sk),
            col_idx < row_idx + sk - sq - window_size[0],
        )


def attention_ref(
    q,
    k,
    v,
    query_padding_mask=None,
    key_padding_mask=None,
    dropout_p=0.0,
    dropout_mask=None,
    causal=False,
    window_size=(-1, -1),  # -1 means infinite window size
    upcast=True,
    reorder_ops=False,
):
    """
    Arguments:
        q: (batch_size, seqlen_q, nheads, head_dim)
        k: (batch_size, seqlen_k, nheads_k, head_dim)
        v: (batch_size, seqlen_k, nheads_k, head_dim)
        query_padding_mask: (batch_size, seqlen_q)
        key_padding_mask: (batch_size, seqlen_k)
        dropout_p: float
        dropout_mask: (batch_size, nheads, seqlen_q, seqlen_k)
        causal: whether to apply causal masking
        window_size: (int, int), left and right window size
        upcast: whether to cast all inputs to fp32, do all computation in fp32, then cast
            output back to fp16/bf16.
        reorder_ops: whether to change the order of operations (scaling k instead of scaling k, etc.)
            without changing the math. This is to estimate the numerical error from operation
            reordering.
    Output:
        output: (batch_size, seqlen_q, nheads, head_dim)
        attention: (batch_size, nheads, seqlen_q, seqlen_k), softmax after dropout
    """
    if causal:
        window_size = (window_size[0], 0)
    dtype_og = q.dtype
    if upcast:
        q, k, v = q.float(), k.float(), v.float()
    seqlen_q, seqlen_k = q.shape[1], k.shape[1]
    k = repeat(k, "b s h d -> b s (h g) d", g=q.shape[2] // k.shape[2])
    v = repeat(v, "b s h d -> b s (h g) d", g=q.shape[2] // v.shape[2])
    d = q.shape[-1]
    if not reorder_ops:
        scores = torch.einsum("bthd,bshd->bhts", q / math.sqrt(d), k)
    else:
        scores = torch.einsum("bthd,bshd->bhts", q, k / math.sqrt(d))
    if key_padding_mask is not None:
        scores.masked_fill_(rearrange(~key_padding_mask, "b s -> b 1 1 s"), float("-inf"))
    if window_size[0] >= 0 or window_size[1] >= 0:
        local_mask = construct_local_mask(
            seqlen_q,
            seqlen_k,
            window_size,
            query_padding_mask,
            key_padding_mask,
            q.device,
        )
        scores.masked_fill_(local_mask, float("-inf"))
    attention = torch.softmax(scores, dim=-1)
    # Some rows might be completely masked out so we fill them with zero instead of NaN
    if window_size[0] >= 0 or window_size[1] >= 0:
        attention = attention.masked_fill(torch.all(local_mask, dim=-1, keepdim=True), 0.0)
    # We want to mask here so that the attention matrix doesn't have any NaNs
    # Otherwise we'll get NaN in dV
    if query_padding_mask is not None:
        attention = attention.masked_fill(rearrange(~query_padding_mask, "b s -> b 1 s 1"), 0.0)
    dropout_scaling = 1.0 / (1 - dropout_p)
    if dropout_mask is not None:
        attention_drop = attention.masked_fill(~dropout_mask, 0.0)
    else:
        attention_drop = attention
    output = torch.einsum("bhts,bshd->bthd", attention_drop, v * dropout_scaling)
    if query_padding_mask is not None:
        output.masked_fill_(rearrange(~query_padding_mask, "b s -> b s 1 1"), 0.0)
    return output.to(dtype=dtype_og), attention.to(dtype=dtype_og)


def attention_qkvpacked_ref(
    qkv, key_padding_mask=None, dropout_p=0.0, dropout_mask=None, causal=False, upcast=True, reorder_ops=False
):
    return attention_ref(
        qkv[:, :, 0],
        qkv[:, :, 1],
        qkv[:, :, 2],
        key_padding_mask,
        key_padding_mask,
        dropout_p,
        dropout_mask,
        upcast=upcast,
        causal=causal,
        reorder_ops=reorder_ops,
    )


def parity_check_gqa_prompt(
    config,
    causal=True,
    local=False,
    past_format=Formats.BSNH,
    rotary=False,
    rotary_interleaved=False,
    packed=False,
    rtol=1e-3,
    atol=1e-3,
):
    q = torch.randn(
        config.batch_size,
        config.q_sequence_length,
        config.num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    k = torch.randn(
        config.batch_size,
        config.buffer_sequence_length if past_format == Formats.BSNH else config.kv_num_heads,
        config.kv_num_heads if past_format == Formats.BSNH else config.buffer_sequence_length,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    v = torch.randn(
        config.batch_size,
        config.buffer_sequence_length if past_format == Formats.BSNH else config.kv_num_heads,
        config.kv_num_heads if past_format == Formats.BSNH else config.buffer_sequence_length,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    new_k = torch.randn(
        config.batch_size,
        config.kv_sequence_length,
        config.kv_num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    new_v = torch.randn(
        config.batch_size,
        config.kv_sequence_length,
        config.kv_num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )

    window_size = (-1, -1)
    left_window_size = -1
    if local:
        left_window_size = random.randint(0, config.kv_sequence_length)
        window_size = (left_window_size, 0)
    elif causal:
        left_window_size = -1
        window_size = (-1, 0)

    # Pytorch to compare
    k_cache_ref = k.clone()
    v_cache_ref = v.clone()
    if past_format == Formats.BNSH:
        k_cache_ref = k_cache_ref.transpose(1, 2)
        v_cache_ref = v_cache_ref.transpose(1, 2)
    cache_seqlens = torch.tensor([config.kv_sequence_length], device="cpu").repeat(config.batch_size)
    rotary_seqlens = torch.tensor([0], device="cpu").repeat(config.batch_size)

    if rotary:
        rotary_fraction = 1.0
        rotary_dim = math.floor(int(rotary_fraction * config.head_size) / 16) * 16
        angle = torch.rand(config.buffer_sequence_length, rotary_dim // 2, device="cpu") * 2 * math.pi
        cos = torch.cos(angle).to(dtype=torch.float32)
        sin = torch.sin(angle).to(dtype=torch.float32)
        rot = LlamaMSRotaryEmbedding()
        q_ro = rot(
            q.clone(), cos.unsqueeze(0).unsqueeze(2), sin.unsqueeze(0).unsqueeze(2), rotary_seqlens, rotary_interleaved
        )
        k_ro = rot(
            new_k.clone(),
            cos.unsqueeze(0).unsqueeze(2),
            sin.unsqueeze(0).unsqueeze(2),
            rotary_seqlens,
            rotary_interleaved,
        )
    else:
        cos, sin = None, None
        q_ro, k_ro = q, new_k

    rearrange(torch.arange(config.kv_sequence_length, device="cpu"), "s -> 1 s")
    arange = rearrange(torch.arange(config.buffer_sequence_length, device="cpu"), "s -> 1 s")
    cache_seqlens_expanded = rearrange(cache_seqlens, "b -> b 1")
    kv_seqlens = torch.tensor([config.kv_sequence_length], device="cpu").repeat(config.batch_size)
    kv_seqlens_expanded = rearrange(kv_seqlens, "b -> b 1")
    update_mask = arange < kv_seqlens_expanded
    k_cache_ref[update_mask] = rearrange(k_ro, "b s ... -> (b s) ...")
    v_cache_ref[update_mask] = rearrange(new_v, "b s ... -> (b s) ...")
    k_cache_rep = repeat(k_cache_ref, "b s h d -> b s (h g) d", g=config.num_heads // config.kv_num_heads)
    v_cache_rep = repeat(v_cache_ref, "b s h d -> b s (h g) d", g=config.num_heads // config.kv_num_heads)
    key_padding_mask = arange < cache_seqlens_expanded
    out_ref, _ = attention_ref(
        q_ro, k_cache_rep, v_cache_rep, None, key_padding_mask, 0.0, None, causal=True, window_size=window_size
    )
    out_ref = out_ref.detach().cpu().numpy()
    if past_format == Formats.BNSH:
        k_cache_ref = k_cache_ref.transpose(1, 2)
        v_cache_ref = v_cache_ref.transpose(1, 2)

    # Flash function
    if packed:
        packed_qkv = torch.concatenate([q, new_k, new_v], dim=2)
        out, present_k, present_v = gqa_prompt_func(
            packed_qkv,
            k,
            v,
            config,
            None,
            None,
            cos,
            sin,
            cache_seqlens,
            left_window_size,
            past_format,
            True,
            rotary_interleaved,
        )
    else:
        out, present_k, present_v = gqa_prompt_func(
            q,
            k,
            v,
            config,
            new_k,
            new_v,
            cos,
            sin,
            cache_seqlens,
            left_window_size,
            past_format,
            True,
            rotary_interleaved,
        )
    out = torch.squeeze(out, 0)
    out = torch.reshape(out, (config.batch_size, config.q_sequence_length, config.num_heads, config.head_size))
    out = out.detach().cpu().numpy()

    # Make sure past-present buffer updating correctly
    assert numpy.allclose(present_k, k_cache_ref.detach().cpu().numpy(), rtol=rtol, atol=atol, equal_nan=True)
    assert numpy.allclose(present_v, v_cache_ref.detach().cpu().numpy(), rtol=rtol, atol=atol, equal_nan=True)

    # Compare results
    all_close = numpy.allclose(out, out_ref, rtol=rtol, atol=atol, equal_nan=True)
    correct = Fore.GREEN + "True" if all_close else Fore.RED + "False"
    print(
        "KV-buffer",
        " packed:",
        packed,
        " causal:",
        causal,
        " local:",
        local,
        " rotary:",
        rotary,
        " rotary_interleaved:",
        rotary_interleaved,
        "past kv format:",
        "BSNH" if past_format == Formats.BSNH else "BNSH",
        " B:",
        config.batch_size,
        " S:",
        config.q_sequence_length,
        " kv S:",
        config.kv_sequence_length,
        " N:",
        config.num_heads,
        " kv N:",
        config.kv_num_heads,
        " h:",
        config.head_size,
        " Mean Error:",
        numpy.mean(numpy.abs(out - out_ref)),
        correct
    )


def parity_check_gqa_prompt_no_buff(
    config,
    causal=True,
    local=False,
    past_format=Formats.BSNH,
    rotary=False,
    rotary_interleaved=False,
    packed=False,
    rtol=1e-3,
    atol=1e-3,
):
    q = torch.randn(
        config.batch_size,
        config.q_sequence_length,
        config.num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    new_k = torch.randn(
        config.batch_size,
        config.kv_sequence_length,
        config.kv_num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    new_v = torch.randn(
        config.batch_size,
        config.kv_sequence_length,
        config.kv_num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )

    window_size = (-1, -1)
    left_window_size = -1
    if local:
        left_window_size = random.randint(0, config.kv_sequence_length)
        window_size = (left_window_size, 0)
    elif causal:
        left_window_size = -1
        window_size = (-1, 0)

    # Pytorch to compare
    k_cache_ref = new_k.clone()
    v_cache_ref = new_v.clone()
    cache_seqlens = torch.tensor([config.kv_sequence_length], device="cpu").repeat(config.batch_size)
    rotary_seqlens = torch.tensor([0], device="cpu").repeat(config.batch_size)

    if rotary:
        rotary_fraction = 1.0
        rotary_dim = math.floor(int(rotary_fraction * config.head_size) / 16) * 16
        angle = torch.rand(config.kv_sequence_length, rotary_dim // 2, device="cpu") * 2 * math.pi
        cos = torch.cos(angle).to(dtype=torch.float32)
        sin = torch.sin(angle).to(dtype=torch.float32)
        rot = LlamaMSRotaryEmbedding()
        q_ro = rot(
            q.clone(), cos.unsqueeze(0).unsqueeze(2), sin.unsqueeze(0).unsqueeze(2), rotary_seqlens, rotary_interleaved
        )
        k_ro = rot(
            k_cache_ref.clone(),
            cos.unsqueeze(0).unsqueeze(2),
            sin.unsqueeze(0).unsqueeze(2),
            rotary_seqlens,
            rotary_interleaved,
        )
    else:
        cos, sin = None, None
        q_ro, k_ro = q, k_cache_ref
    k_cache_ref = k_ro

    brange = rearrange(torch.arange(config.kv_sequence_length, device="cpu"), "s -> 1 s")
    cache_seqlens_expanded = rearrange(cache_seqlens, "b -> b 1")
    new_mask = brange < cache_seqlens_expanded
    k_cache_rep = repeat(k_cache_ref, "b s h d -> b s (h g) d", g=config.num_heads // config.kv_num_heads)
    v_cache_rep = repeat(v_cache_ref, "b s h d -> b s (h g) d", g=config.num_heads // config.kv_num_heads)
    out_ref, _ = attention_ref(
        q_ro, k_cache_rep, v_cache_rep, None, new_mask, 0.0, None, causal=True, window_size=window_size
    )
    out_ref = out_ref.detach().cpu().numpy()
    if past_format == Formats.BNSH:
        k_cache_ref = k_cache_ref.transpose(1, 2)
        v_cache_ref = v_cache_ref.transpose(1, 2)

    # Flash function
    if packed:
        packed_qkv = torch.concatenate([q, new_k, new_v], dim=2)
        out, present_k, present_v = gqa_prompt_func(
            packed_qkv,
            None,
            None,
            config,
            None,
            None,
            cos,
            sin,
            cache_seqlens,
            left_window_size,
            past_format,
            False,
            rotary_interleaved,
        )
    else:
        out, present_k, present_v = gqa_prompt_func(
            q,
            None,
            None,
            config,
            new_k,
            new_v,
            cos,
            sin,
            cache_seqlens,
            left_window_size,
            past_format,
            False,
            rotary_interleaved,
        )
    out = torch.squeeze(out, 0)
    out = torch.reshape(out, (config.batch_size, config.q_sequence_length, config.num_heads, config.head_size))
    out = out.detach().cpu().numpy()

    # Make sure past-present buffer updating correctly
    assert numpy.allclose(present_k, k_cache_ref.detach().cpu().numpy(), rtol=rtol, atol=atol, equal_nan=True)
    assert numpy.allclose(present_v, v_cache_ref.detach().cpu().numpy(), rtol=rtol, atol=atol, equal_nan=True)

    # Compare results
    all_close = numpy.allclose(out, out_ref, rtol=rtol, atol=atol, equal_nan=True)
    correct = Fore.GREEN + "True" if all_close else Fore.RED + "False"
    print(
        "No buff",
        " packed:",
        packed,
        " causal:",
        causal,
        " local:",
        local,
        " rotary:",
        rotary,
        " rotary_interleaved:",
        rotary_interleaved,
        "past kv format:",
        "BSNH" if past_format == Formats.BSNH else "BNSH",
        " B:",
        config.batch_size,
        " S:",
        config.q_sequence_length,
        " kv S:",
        config.kv_sequence_length,
        " N:",
        config.num_heads,
        " kv N:",
        config.kv_num_heads,
        " h:",
        config.head_size,
        " Mean Error:",
        numpy.mean(numpy.abs(out - out_ref)),
        correct
    )


def parity_check_gqa_past(
    config,
    causal=True,
    local=False,
    past_format=Formats.BSNH,
    rotary=False,
    rotary_interleaved=False,
    packed=False,
    rtol=1e-3,
    atol=1e-3,
):
    q = torch.randn(
        config.batch_size,
        config.sequence_length,
        config.num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    k = torch.randn(
        config.batch_size,
        config.kv_sequence_length if past_format == Formats.BSNH else config.kv_num_heads,
        config.kv_num_heads if past_format == Formats.BSNH else config.kv_sequence_length,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    v = torch.randn(
        config.batch_size,
        config.kv_sequence_length if past_format == Formats.BSNH else config.kv_num_heads,
        config.kv_num_heads if past_format == Formats.BSNH else config.kv_sequence_length,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    new_k = torch.randn(
        config.batch_size,
        config.sequence_length,
        config.kv_num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    new_v = torch.randn(
        config.batch_size,
        config.sequence_length,
        config.kv_num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )

    window_size = (-1, -1)
    left_window_size = -1
    if local:
        left_window_size = random.randint(0, config.kv_sequence_length)
        window_size = (left_window_size, 0)
    elif causal:
        left_window_size = -1
        window_size = (-1, 0)

    # Pytorch to compare
    k_cache_ref = k.clone()
    v_cache_ref = v.clone()
    if past_format == Formats.BNSH:
        k_cache_ref = k_cache_ref.transpose(1, 2)
        v_cache_ref = v_cache_ref.transpose(1, 2)
    # cache_seqlens = torch.tensor([config.past_sequence_length], device="cpu").repeat(config.batch_size)
    cache_seqlens = torch.randint(
        0,
        config.kv_sequence_length - config.sequence_length + 1,
        (config.batch_size,),
        dtype=torch.int32,
        device="cpu",
    )

    if rotary:
        rotary_fraction = 1.0
        rotary_dim = math.floor(int(rotary_fraction * config.head_size) / 16) * 16
        angle = torch.rand(config.kv_sequence_length, rotary_dim // 2, device="cpu") * 2 * math.pi
        cos = torch.cos(angle).to(dtype=torch.float32)
        sin = torch.sin(angle).to(dtype=torch.float32)
        rot = LlamaMSRotaryEmbedding()
        q_ro = rot(
            q.clone(), cos.unsqueeze(0).unsqueeze(2), sin.unsqueeze(0).unsqueeze(2), cache_seqlens, rotary_interleaved
        )
        k_ro = rot(
            new_k.clone(),
            cos.unsqueeze(0).unsqueeze(2),
            sin.unsqueeze(0).unsqueeze(2),
            cache_seqlens,
            rotary_interleaved,
        )
    else:
        cos, sin = None, None
        q_ro, k_ro = q, new_k

    arange = rearrange(torch.arange(config.kv_sequence_length, device="cpu"), "s -> 1 s")
    cache_seqlens_expanded = rearrange(cache_seqlens, "b -> b 1")
    update_mask = torch.logical_and(
        cache_seqlens_expanded <= arange, arange < cache_seqlens_expanded + config.sequence_length
    )
    k_cache_ref[update_mask] = rearrange(k_ro, "b s ... -> (b s) ...")
    v_cache_ref[update_mask] = rearrange(new_v, "b s ... -> (b s) ...")
    k_cache_rep = repeat(k_cache_ref, "b s h d -> b s (h g) d", g=config.num_heads // config.kv_num_heads)
    v_cache_rep = repeat(v_cache_ref, "b s h d -> b s (h g) d", g=config.num_heads // config.kv_num_heads)
    key_padding_mask = arange < cache_seqlens_expanded + config.sequence_length
    out_ref, _ = attention_ref(
        q_ro, k_cache_rep, v_cache_rep, None, key_padding_mask, 0.0, None, causal=True, window_size=window_size
    )
    out_ref = out_ref.detach().cpu().numpy()
    if past_format == Formats.BNSH:
        k_cache_ref = k_cache_ref.transpose(1, 2)
        v_cache_ref = v_cache_ref.transpose(1, 2)

    # ORT function
    if packed:
        packed_qkv = torch.concatenate([q, new_k, new_v], dim=2)
        out, present_k, present_v = gqa_past_func(
            packed_qkv,
            k,
            v,
            config,
            None,
            None,
            cos,
            sin,
            cache_seqlens,
            past_format,
            True,
            left_window_size,
            rotary_interleaved,
        )
    else:
        out, present_k, present_v = gqa_past_func(
            q,
            k,
            v,
            config,
            new_k,
            new_v,
            cos,
            sin,
            cache_seqlens,
            past_format,
            True,
            left_window_size,
            rotary_interleaved,
        )
    out = torch.squeeze(out, 0)
    out = torch.reshape(out, (config.batch_size, config.sequence_length, config.num_heads, config.head_size))
    out = out.detach().cpu().numpy()

    # Make sure past-present buffer updating correctly
    assert numpy.allclose(present_k, k_cache_ref.detach().cpu().numpy(), rtol=rtol, atol=atol, equal_nan=True)
    assert numpy.allclose(present_v, v_cache_ref.detach().cpu().numpy(), rtol=rtol, atol=atol, equal_nan=True)

    # Compare results
    all_close = numpy.allclose(out, out_ref, rtol=rtol, atol=atol, equal_nan=True)
    correct = Fore.GREEN + "True" if all_close else Fore.RED + "False"
    print(
        "KV-buffer",
        "past kv format:",
        "BSNH" if past_format == Formats.BSNH else "BNSH",
        " packed:",
        packed,
        " causal:",
        causal,
        " local:",
        local,
        " rotary:",
        rotary,
        " rotary_interleaved:",
        rotary_interleaved,
        " B:",
        config.batch_size,
        " S:",
        config.sequence_length,
        " kv S:",
        config.kv_sequence_length,
        " N:",
        config.num_heads,
        " kv N:",
        config.kv_num_heads,
        " h:",
        config.head_size,
        " Mean Error:",
        numpy.mean(numpy.abs(out - out_ref)),
        correct
    )


def parity_check_gqa_past_no_buff(
    config,
    causal=True,
    local=False,
    past_format=Formats.BSNH,
    rotary=False,
    rotary_interleaved=False,
    packed=False,
    rtol=1e-3,
    atol=1e-3,
):
    torch.manual_seed(69)
    q = torch.randn(
        config.batch_size,
        config.sequence_length,
        config.num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    k = torch.randn(
        config.batch_size,
        config.kv_sequence_length if past_format == Formats.BSNH else config.kv_num_heads,
        config.kv_num_heads if past_format == Formats.BSNH else config.kv_sequence_length,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    v = torch.randn(
        config.batch_size,
        config.kv_sequence_length if past_format == Formats.BSNH else config.kv_num_heads,
        config.kv_num_heads if past_format == Formats.BSNH else config.kv_sequence_length,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    new_k = torch.randn(
        config.batch_size,
        config.sequence_length,
        config.kv_num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )
    new_v = torch.randn(
        config.batch_size,
        config.sequence_length,
        config.kv_num_heads,
        config.head_size,
        device="cpu",
        dtype=torch.float32,
        requires_grad=False,
    )

    window_size = (-1, -1)
    left_window_size = -1
    if local:
        left_window_size = random.randint(0, config.kv_sequence_length)
        window_size = (left_window_size, 0)
    elif causal:
        left_window_size = -1
        window_size = (-1, 0)

    # Pytorch to compare
    k_cache_ref = k.clone()
    v_cache_ref = v.clone()
    if past_format == Formats.BNSH:
        k_cache_ref = k_cache_ref.transpose(1, 2)
        v_cache_ref = v_cache_ref.transpose(1, 2)
    k_cache_ref = torch.cat((k_cache_ref, new_k), 1)
    v_cache_ref = torch.cat((v_cache_ref, new_v), 1)
    # cache_seqlens = torch.tensor([config.past_sequence_length], device="cpu").repeat(config.batch_size)
    cache_seqlens = torch.randint(
        0,
        config.kv_sequence_length,
        (config.batch_size,),
        dtype=torch.int32,
        device="cpu",
    )
    cache_seqlens[random.randint(0, config.batch_size - 1)] = config.kv_sequence_length

    if rotary:
        rotary_fraction = 1.0
        rotary_dim = math.floor(int(rotary_fraction * config.head_size) / 16) * 16
        angle = (
            torch.rand(config.kv_sequence_length + config.sequence_length, rotary_dim // 2, device="cpu") * 2 * math.pi
        )
        cos = torch.cos(angle).to(dtype=torch.float32)
        sin = torch.sin(angle).to(dtype=torch.float32)
        rot = LlamaMSRotaryEmbedding()
        q_ro = rot(
            q.clone(), cos.unsqueeze(0).unsqueeze(2), sin.unsqueeze(0).unsqueeze(2), cache_seqlens, rotary_interleaved
        )
        k_ro = rot(
            new_k.clone(),
            cos.unsqueeze(0).unsqueeze(2),
            sin.unsqueeze(0).unsqueeze(2),
            cache_seqlens,
            rotary_interleaved,
        )
    else:
        cos, sin = None, None
        q_ro, k_ro = q, new_k

    arange = rearrange(torch.arange(config.kv_sequence_length + config.sequence_length, device="cpu"), "s -> 1 s")
    cache_seqlens_expanded = rearrange(cache_seqlens, "b -> b 1")
    update_mask = torch.logical_and(
        cache_seqlens_expanded <= arange, arange < cache_seqlens_expanded + config.sequence_length
    )
    k_cache_ref[update_mask] = rearrange(k_ro, "b s ... -> (b s) ...")
    v_cache_ref[update_mask] = rearrange(new_v, "b s ... -> (b s) ...")
    k_cache_rep = repeat(k_cache_ref, "b s h d -> b s (h g) d", g=config.num_heads // config.kv_num_heads)
    v_cache_rep = repeat(v_cache_ref, "b s h d -> b s (h g) d", g=config.num_heads // config.kv_num_heads)
    key_padding_mask = arange < cache_seqlens_expanded + config.sequence_length
    out_ref, _ = attention_ref(
        q_ro, k_cache_rep, v_cache_rep, None, key_padding_mask, 0.0, None, causal=True, window_size=window_size
    )
    out_ref = out_ref.detach().cpu().numpy()
    if past_format == Formats.BNSH:
        k_cache_ref = k_cache_ref.transpose(1, 2)
        v_cache_ref = v_cache_ref.transpose(1, 2)

    # Flash function
    if packed:
        packed_qkv = torch.concatenate([q, new_k, new_v], dim=2)
        out, present_k, present_v = gqa_past_func(
            packed_qkv,
            k,
            v,
            config,
            None,
            None,
            cos,
            sin,
            cache_seqlens,
            past_format,
            False,
            window_size=left_window_size,
            rotary_interleaved=rotary_interleaved,
        )
    else:
        out, present_k, present_v = gqa_past_func(
            q,
            k,
            v,
            config,
            new_k,
            new_v,
            cos,
            sin,
            cache_seqlens,
            past_format,
            False,
            window_size=left_window_size,
            rotary_interleaved=rotary_interleaved,
        )
    out = torch.squeeze(out, 0)
    out = torch.reshape(out, (config.batch_size, config.sequence_length, config.num_heads, config.head_size))
    out = out.detach().cpu().numpy()

    # Compare results
    all_close = numpy.allclose(out, out_ref, rtol=rtol, atol=atol, equal_nan=True)
    if not all_close:
        print("seqlens", cache_seqlens)
        print("out", out)
        print("out_ref", out_ref)
        print(out - out_ref)
    correct = Fore.GREEN + "True" if all_close else Fore.RED + "False"
    print(
        "NO buff",
        " packed:",
        packed,
        " causal:",
        causal,
        " local:",
        local,
        " rotary:",
        rotary,
        " rotary_interleaved:",
        rotary_interleaved,
        "past kv format:",
        "BSNH" if past_format == Formats.BSNH else "BNSH",
        " B:",
        config.batch_size,
        " S:",
        config.sequence_length,
        " kv S:",
        config.kv_sequence_length,
        " N:",
        config.num_heads,
        " kv N:",
        config.kv_num_heads,
        " h:",
        config.head_size,
        " Mean Error:",
        numpy.mean(numpy.abs(out - out_ref)),
        correct
    )


class TestGQA(unittest.TestCase):
    # def test_gqa_no_past(self):
    #     torch.manual_seed(69)
    #     print("-------- TEST GQA NO PAST (PROMPT CASE) ---------")
    #     batches = [1, 3] if pipeline_mode else [1, 3, 5]
    #     seqs = (
    #         [
    #             (127, 127),
    #             (35, 35),
    #             (2000, 2000),
    #             (200, 200),
    #             (240, 240),
    #         ]
    #         if pipeline_mode
    #         else [
    #             (127, 127),
    #             (35, 35),
    #             (2000, 2000),
    #             (200, 200),
    #             (240, 240),
    #         ]
    #     )
    #     num_h = [(32, 32), (9, 3), (4, 4)] if pipeline_mode else [(6, 6), (6, 3), (9, 9), (9, 3)]
    #     h_sizes = [16, 128, 256] if pipeline_mode else [32, 40, 64, 80, 96, 128, 160, 192, 224, 256]
    #     for b in batches:
    #         for sq, skv in seqs:
    #             for n, n2 in num_h:
    #                 for h in h_sizes:
    #                     for local in [False, True]:
    #                         for rotary, rotary_interleaved in [(True, False), (True, True), (False, False)]:
    #                             for packed in [False, True]:
    #                                 config = PromptConfig(b, sq, skv, sq + skv + 8, n, n2, h)
    #                                 past_kv_format = Formats.BNSH
    #                                 parity_check_gqa_prompt(
    #                                     config,
    #                                     local=local,
    #                                     past_format=past_kv_format,
    #                                     rotary=rotary,
    #                                     rotary_interleaved=rotary_interleaved,
    #                                     packed=packed,
    #                                 )
    #                                 parity_check_gqa_prompt_no_buff(
    #                                     config,
    #                                     local=local,
    #                                     past_format=past_kv_format,
    #                                     rotary=rotary,
    #                                     rotary_interleaved=rotary_interleaved,
    #                                     packed=packed,
    #                                 )

    def test_gqa_past(self):
        print("-------- TEST GQA PAST (TOKEN GEN) ---------")
        batches = [1, 3] if pipeline_mode else [1, 3, 5]
        seqs = (
            [(1, 128), (1, 1024), (1, 2048)]
            if pipeline_mode
            else [
                (1, 128),
                (1, 339),
                (1, 1024),
                (1, 5000),
                (1, 800),
                (1, 256),
                (1, 799),
                (1, 2048),
                # (1, 128 * 512),
                # (16, 128 * 512),
                # (128, 128),
            ]
        )
        num_h = [(16, 16), (9, 3), (4, 4)] if pipeline_mode else [(6, 6), (6, 3), (9, 9), (9, 3)]
        h_sizes = [16, 64, 256] if pipeline_mode else [32, 40, 64, 80, 96, 128, 160, 192, 224, 256]
        random.seed(69)
        # config = Config(1, 1, 128, 122, 4, 4, 16)
        # past_kv_format = Formats.BSNH
        # local = False
        # rotary = False
        # rotary_interleaved = False
        # packed = False
        # parity_check_gqa_past(
        #     config,
        #     local=local,
        #     past_format=past_kv_format,
        #     rtol=1e-3,
        #     atol=1e-3,
        #     rotary=rotary,
        #     rotary_interleaved=rotary_interleaved,
        #     packed=packed,
        # )
        for b in batches:
            for s, s2 in seqs:
                for n, n2 in num_h:
                    for h in h_sizes:
                        for local in [False, True]:
                            for rotary, rotary_interleaved in [(False, False), (True, True), (False, False)]:
                                for packed in [False, True]:
                                    sp = random.randint(1, s2 - s) if s2 - s > 0 else 0
                                    config = Config(3, s, s2, sp, n, n2, h)
                                    print(b, s, s2, sp, n, n2, h)
                                    past_kv_format = Formats.BNSH
                                    # parity_check_gqa_past(
                                    #     config,
                                    #     local=local,
                                    #     past_format=past_kv_format,
                                    #     rtol=1e-3,
                                    #     atol=1e-3,
                                    #     rotary=rotary,
                                    #     rotary_interleaved=rotary_interleaved,
                                    #     packed=packed,
                                    # )
                                    parity_check_gqa_past_no_buff(
                                        config,
                                        local=local,
                                        past_format=past_kv_format,
                                        rtol=1e-3,
                                        atol=1e-3,
                                        rotary=rotary,
                                        rotary_interleaved=rotary_interleaved,
                                        packed=packed,
                                    )
                                    return


if __name__ == "__main__":
    unittest.main()
