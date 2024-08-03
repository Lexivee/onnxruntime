# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT License.
# --------------------------------------------------------------------------

"""
Benchmark performance of MultiHeadAttention with ORT or PyTorch.

In Linux, run the the following:
   sh benchmark_mha.sh

In Windows, run the the following:
   benchmark_mha.cmd
"""

import argparse
import csv
import math
import os
import platform
import statistics
import time
from contextlib import nullcontext
from datetime import datetime
from enum import IntEnum
from typing import Callable, Dict, List, Optional, Tuple

import torch
import torch.utils.benchmark as benchmark
from onnx import TensorProto, helper
from packaging.version import Version
from torch.nn.attention import SDPBackend, sdpa_kernel
from torch.nn.functional import scaled_dot_product_attention

from onnxruntime import InferenceSession, SessionOptions, get_available_providers
from onnxruntime.transformers.io_binding_helper import CudaSession


class InputFormats:
    Q_K_V_BSNH_BSNH_BSNH = 0
    QKV_BSN3H = 1
    Q_KV_BSNH_BSN2H = 2
    Q_K_V_BSNH_BNSH_BNSH = 3  # For cross attention

    @staticmethod
    def input_format_str(format: int) -> str:
        names = InputFormats.get_name_list()
        return names[format]

    @staticmethod
    def convert(format_str: str) -> int:
        names = InputFormats.get_name_list()
        return names.index(format_str)

    @staticmethod
    def get_name_list() -> List[str]:
        return ["Q,K,V", "QKV", "Q,KV", "Q,K',V'"]


class SdpaKernel(IntEnum):
    """Bit flags for sdpa_kernel CUDA provider option"""

    DEFAULT = 0
    FLASH_ATTENTION = 1
    EFFICIENT_ATTENTION = 2
    TRT_FUSED_ATTENTION = 4
    CUDNN_FLASH_ATTENTION = 8
    MATH = 16
    TRT_FLASH_ATTENTION = 32
    TRT_CROSS_ATTENTION = 64
    TRT_CAUSAL_ATTENTION = 128


class MultiHeadAttentionConfig:
    def __init__(
        self,
        batch_size: int,
        sequence_length: int,
        num_heads: int,
        head_size: int,
        causal: bool,
        past_sequence_length: int = 0,
        kv_sequence_length=None,
        max_cache_sequence_length=None,
        softmax_scale: float = 0.0,
        provider="CPUExecutionProvider",
        device: Optional[torch.device] = None,
        enable_cuda_graph: bool = False,
        dtype=torch.float,
        use_kv_cache: bool = False,
        has_past_input: bool = False,
        share_past_present_buffer: bool = False,
        input_format: int = InputFormats.Q_K_V_BSNH_BSNH_BSNH,
        verbose: bool = False,
        has_bias: bool = False,
    ):
        self.operator = "MultiHeadAttention"
        self.batch_size = batch_size
        self.sequence_length = sequence_length
        self.kv_sequence_length = kv_sequence_length or sequence_length
        self.max_cache_sequence_length = max_cache_sequence_length
        self.past_sequence_length = past_sequence_length
        self.num_heads = num_heads
        self.head_size = head_size
        self.causal = causal
        self.softmax_scale = softmax_scale or (1.0 / (head_size**0.5))

        # Support the case that there is no past but need present output (for prompt case).
        self.has_past_input = has_past_input
        if has_past_input:
            assert use_kv_cache
        else:  # no past input
            assert past_sequence_length == 0

        self.has_present_output = use_kv_cache

        self.use_kv_cache = use_kv_cache
        if not use_kv_cache:
            assert past_sequence_length == 0
        else:
            assert self.kv_sequence_length == self.sequence_length

        # Only BSNH input format supports past state.
        if input_format != InputFormats.Q_K_V_BSNH_BSNH_BSNH:
            assert not self.has_past_input
            assert not self.has_present_output

        # Derived values
        self.total_sequence_length = self.kv_sequence_length + past_sequence_length
        self.past_buffer_length = self.max_cache_sequence_length if share_past_present_buffer else past_sequence_length
        self.present_buffer_length = (
            self.max_cache_sequence_length if share_past_present_buffer else self.total_sequence_length
        )

        self.provider = provider
        self.device = device
        self.enable_cuda_graph = enable_cuda_graph
        self.dtype = dtype

        self.share_past_present_buffer = share_past_present_buffer
        self.input_format = input_format
        self.is_packed_qkv = input_format == InputFormats.QKV_BSN3H
        self.is_packed_kv = input_format == InputFormats.Q_KV_BSNH_BSN2H
        self.verbose = verbose
        self.has_bias = has_bias

    def __repr__(self):
        return (
            f"MultiHeadAttentionConfig(batch_size={self.batch_size}, sequence_length={self.sequence_length}, "
            f"num_heads={self.num_heads}, head_size={self.head_size}, "
            f"kv_sequence_length={self.kv_sequence_length}, past_sequence_length={self.past_sequence_length}, "
            f"max_cache_sequence_length={self.max_cache_sequence_length},"
            f"causal={self.causal}), softmax_scale={self.softmax_scale}, use_kv_cache={self.use_kv_cache}, "
            f"share_past_present_buffer={self.share_past_present_buffer}, "
            f"provider={self.provider}, device={self.device}, enable_cuda_graph={self.enable_cuda_graph}, "
            f"dtype={self.dtype}, input_format={InputFormats.input_format_str(self.input_format)}, "
            f"has_bias={self.has_bias}"
        )

    def shape_dict(self, input_format=None):
        shapes: Dict[str, Tuple] = {
            "output": (self.batch_size, self.sequence_length, self.num_heads * self.head_size),
        }

        input_format = input_format or self.input_format
        if input_format == InputFormats.QKV_BSN3H:
            shapes = {
                **shapes,
                "query": (self.batch_size, self.sequence_length, self.num_heads, 3, self.head_size),
            }
        elif input_format == InputFormats.Q_KV_BSNH_BSN2H:
            shapes = {
                **shapes,
                "query": (self.batch_size, self.sequence_length, self.num_heads * self.head_size),
                "key": (self.batch_size, self.sequence_length, self.num_heads, 2, self.head_size),
            }
        elif input_format == InputFormats.Q_K_V_BSNH_BSNH_BSNH:
            shapes = {
                **shapes,
                "query": (self.batch_size, self.sequence_length, self.num_heads * self.head_size),
                "key": (self.batch_size, self.sequence_length, self.num_heads * self.head_size),
                "value": (self.batch_size, self.sequence_length, self.num_heads * self.head_size),
            }
        else:
            assert input_format == InputFormats.Q_K_V_BSNH_BNSH_BNSH
            shapes = {
                **shapes,
                "query": (self.batch_size, self.sequence_length, self.num_heads * self.head_size),
                "key": (self.batch_size, self.num_heads, self.sequence_length, self.head_size),
                "value": (self.batch_size, self.num_heads, self.sequence_length, self.head_size),
            }

        if self.has_past_input:
            shapes = {
                **shapes,
                "past_key": (self.batch_size, self.num_heads, self.past_buffer_length, self.head_size),
                "past_value": (self.batch_size, self.num_heads, self.past_buffer_length, self.head_size),
            }

        if self.has_present_output:
            shapes = {
                **shapes,
                "present_key": (self.batch_size, self.num_heads, self.present_buffer_length, self.head_size),
                "present_value": (self.batch_size, self.num_heads, self.present_buffer_length, self.head_size),
            }

        if self.has_bias:
            shapes["bias"] = (3 * self.num_heads * self.head_size,)

        return shapes

    def symbolic_shape_dict(self, input_format=None):
        shapes: Dict[str, Tuple] = {
            "output": ("batch_size", "sequence_length", self.num_heads * self.head_size),
        }

        input_format = input_format or self.input_format
        if input_format == InputFormats.QKV_BSN3H:
            shapes = {
                **shapes,
                "query": ("batch_size", "sequence_length", self.num_heads, 3, self.head_size),
            }
        elif input_format == InputFormats.Q_KV_BSNH_BSN2H:
            shapes = {
                **shapes,
                "query": ("batch_size", "sequence_length", self.num_heads * self.head_size),
                "key": ("batch_size", "sequence_length", self.num_heads, 2, self.head_size),
            }
        elif input_format == InputFormats.Q_K_V_BSNH_BSNH_BSNH:
            shapes = {
                **shapes,
                "query": ("batch_size", "sequence_length", self.num_heads * self.head_size),
                "key": ("batch_size", "sequence_length", self.num_heads * self.head_size),
                "value": ("batch_size", "sequence_length", self.num_heads * self.head_size),
            }
        else:
            assert input_format == InputFormats.Q_K_V_BSNH_BNSH_BNSH
            shapes = {
                **shapes,
                "query": ("batch_size", "sequence_length", self.num_heads * self.head_size),
                "key": ("batch_size", self.num_heads, "sequence_length", self.head_size),
                "value": ("batch_size", self.num_heads, "sequence_length", self.head_size),
            }

        if self.has_past_input:
            shapes = {
                **shapes,
                "past_key": ("batch_size", self.num_heads, "past_buffer_length", self.head_size),
                "past_value": ("batch_size", self.num_heads, "past_buffer_length", self.head_size),
            }

        if self.has_present_output:
            shapes = {
                **shapes,
                "present_key": ("batch_size", self.num_heads, "present_buffer_length", self.head_size),
                "present_value": ("batch_size", self.num_heads, "present_buffer_length", self.head_size),
            }

        if self.has_bias:
            shapes["bias"] = (3 * self.num_heads * self.head_size,)

        return shapes

    def random_inputs(self, seed: int = 123, no_bias_k_v: bool = False):
        device = self.device
        dtype = self.dtype

        shape_dict = self.shape_dict()

        if seed > 0:
            torch.manual_seed(seed)

        shape = (self.batch_size, self.sequence_length, self.num_heads, self.head_size)
        q = torch.empty(shape, device=device, dtype=dtype).normal_(mean=0, std=0.1)
        k = torch.empty(shape, device=device, dtype=dtype).normal_(mean=0, std=0.1)
        v = torch.empty(shape, device=device, dtype=dtype).normal_(mean=0, std=0.1)

        bias_q = torch.empty((self.num_heads * self.head_size,), device=device, dtype=dtype).normal_(mean=0, std=0.1)
        bias_k = torch.empty((self.num_heads * self.head_size,), device=device, dtype=dtype).normal_(mean=0, std=0.1)
        bias_v = torch.empty((self.num_heads * self.head_size,), device=device, dtype=dtype).normal_(mean=0, std=0.1)
        if no_bias_k_v:
            bias_k = torch.zeros_like(bias_k)
            bias_v = torch.zeros_like(bias_v)

        k_bnsh = k.transpose(1, 2)
        v_bnsh = v.transpose(1, 2)

        if self.input_format == InputFormats.Q_K_V_BSNH_BSNH_BSNH:
            feeds = {
                "query": q.reshape(shape_dict["query"]),
                "key": k.reshape(shape_dict["key"]),
                "value": v.reshape(shape_dict["value"]),
            }
        elif self.input_format == InputFormats.QKV_BSN3H:
            query = q.view(self.batch_size * self.sequence_length, self.num_heads, self.head_size)
            key = k.view(self.batch_size * self.sequence_length, self.num_heads, self.head_size)
            value = v.view(self.batch_size * self.sequence_length, self.num_heads, self.head_size)
            feeds = {
                "query": torch.dstack((query, key, value)).reshape(shape_dict["query"]).contiguous(),
            }
        elif self.input_format == InputFormats.Q_KV_BSNH_BSN2H:
            key = k.view(self.batch_size * self.sequence_length, self.num_heads, self.head_size)
            value = v.view(self.batch_size * self.sequence_length, self.num_heads, self.head_size)
            feeds = {
                "query": q.reshape(shape_dict["query"]),
                "key": torch.dstack((key, value)).reshape(shape_dict["key"]).contiguous(),
            }
        else:
            assert self.input_format == InputFormats.Q_K_V_BSNH_BNSH_BNSH
            feeds = {
                "query": q.reshape(shape_dict["query"]),
                "key": k_bnsh.contiguous(),
                "value": v_bnsh.contiguous(),
            }

        if self.has_past_input:
            feeds = {
                **feeds,
                "past_key": torch.empty(shape_dict["past_key"], device=device, dtype=dtype).normal_(mean=0, std=0.1),
                "past_value": torch.empty(shape_dict["past_value"], device=device, dtype=dtype).normal_(
                    mean=0, std=0.1
                ),
            }

        if self.has_bias:
            feeds["bias"] = torch.concat([bias_q, bias_k, bias_v], dim=0).reshape(shape_dict["bias"]).contiguous()

        return feeds

    def get_input_output_names(self):
        if self.input_format == InputFormats.Q_K_V_BSNH_BNSH_BNSH:
            return ["query", "key", "value"], ["output"]

        if self.input_format == InputFormats.QKV_BSN3H:
            inputs, outputs = ["query"], ["output"]
        elif self.input_format == InputFormats.Q_KV_BSNH_BSN2H:
            inputs, outputs = ["query", "key"], ["output"]
        else:
            inputs, outputs = ["query", "key", "value"], ["output"]

        if self.has_bias:
            inputs = [*inputs, "bias"]

        if self.has_past_input:
            inputs = [*inputs, "past_key", "past_value"]

        if self.has_present_output:
            outputs = [*outputs, "present_key", "present_value"]

        return inputs, outputs


def fill_optional_mha_inputs(input_names):
    inputs = ["query", "key", "value", "bias", "key_padding_mask", "relative_position_bias", "past_key", "past_value"]

    # Remove optional inputs that are not in input_names with empty string
    inputs_with_optional = [input if input in input_names else "" for input in inputs]

    # Remove empty string at the end of the list.
    while inputs_with_optional[-1] == "":
        inputs_with_optional.pop(-1)

    return inputs_with_optional


def create_multi_head_attention_onnx_model(config: MultiHeadAttentionConfig, use_symbolic_shape=False):
    input_names, output_names = config.get_input_output_names()

    float_type = TensorProto.FLOAT16 if config.dtype == torch.float16 else TensorProto.FLOAT
    nodes = [
        helper.make_node(
            "MultiHeadAttention",
            fill_optional_mha_inputs(input_names),
            output_names,
            "MultiHeadAttention_0",
            num_heads=config.num_heads,
            unidirectional=int(config.causal),
            scale=config.softmax_scale,
            domain="com.microsoft",
        ),
    ]

    shape_dict = config.symbolic_shape_dict() if use_symbolic_shape else config.shape_dict()
    inputs = [
        helper.make_tensor_value_info(input_name, float_type, list(shape_dict[input_name]))
        for input_name in input_names
        if input_name
    ]

    outputs = [
        helper.make_tensor_value_info(output_name, float_type, list(shape_dict[output_name]))
        for output_name in output_names
        if output_name
    ]

    graph = helper.make_graph(
        nodes,
        "MultiHeadAttention_Graph",
        inputs,
        outputs,
    )

    model = helper.make_model(graph)

    return model.SerializeToString()


def create_ort_session(
    config: MultiHeadAttentionConfig,
    session_options=None,
    attention_kernel=SdpaKernel.DEFAULT,
    use_symbolic_shape: bool = True,
    use_tf32: bool = True,
) -> CudaSession:
    if config.verbose:
        print(f"create session for {vars(config)}")
    onnx_model_str = create_multi_head_attention_onnx_model(config, use_symbolic_shape=use_symbolic_shape)

    if config.provider == "CUDAExecutionProvider":
        device_id = torch.cuda.current_device() if isinstance(config.device, str) else config.device.index
        provider_options = CudaSession.get_cuda_provider_options(device_id, config.enable_cuda_graph)
        provider_options["sdpa_kernel"] = int(attention_kernel)
        provider_options["use_tf32"] = int(use_tf32)
        providers = [(config.provider, provider_options), "CPUExecutionProvider"]
    else:
        providers = ["CPUExecutionProvider"]

    ort_session = InferenceSession(onnx_model_str, session_options, providers=providers)
    return ort_session


def create_session(
    config: MultiHeadAttentionConfig, session_options=None, attention_kernel=SdpaKernel.DEFAULT, use_tf32: bool = True
) -> CudaSession:
    ort_session = create_ort_session(
        config, session_options, attention_kernel, use_symbolic_shape=False, use_tf32=use_tf32
    )
    cuda_session = CudaSession(ort_session, config.device, config.enable_cuda_graph)
    shape_dict = config.shape_dict()
    cuda_session.allocate_buffers(shape_dict)
    return cuda_session


class OrtMultiHeadAttention:
    """A wrapper of ORT MultiHeadAttention to test relevance and performance."""

    def __init__(self, config: MultiHeadAttentionConfig, session_options=None, use_tf32: bool = True):
        self.ort_session = create_session(config, session_options, use_tf32=use_tf32)
        self.feed_dict = config.random_inputs()

    def infer(self):
        return self.ort_session.infer(self.feed_dict)


def measure_latency(cuda_session: CudaSession, input_dict):
    start = time.time()
    _ = cuda_session.infer(input_dict)
    end = time.time()
    return end - start


def flops(batch, sequence_length, head_size, num_heads, causal):
    return 4 * batch * sequence_length**2 * num_heads * head_size // (2 if causal else 1)


def tflops_per_second(flop, time):
    try:
        return (flop / time / 10**12) if not math.isnan(time) else 0.0
    except ZeroDivisionError:
        return None


def get_gpu_kernel_name(attention_kernel: SdpaKernel) -> str:
    kernel_names = {
        SdpaKernel.DEFAULT: "ort:default",
        SdpaKernel.FLASH_ATTENTION: "ort:flash",
        SdpaKernel.EFFICIENT_ATTENTION: "ort:efficient",
        SdpaKernel.CUDNN_FLASH_ATTENTION: "ort:cudnn",
        SdpaKernel.MATH: "ort:math",
    }
    assert attention_kernel in kernel_names
    return kernel_names[attention_kernel]


def get_cpu_kernel_name(config: MultiHeadAttentionConfig) -> str:
    # CPU Flash Attention does not support causal and kv cache etc.
    if not (config.causal or config.use_kv_cache or config.past_sequence_length > 0):
        if os.getenv("ORT_DISABLE_FLASH_ATTENTION") != "1":
            return "ort:flash"

    return "ort:math"


# ------------------------------------------------------------------
# Functions for benchmarking PyTorch SDPA
# ------------------------------------------------------------------
def benchmark_torch_function(func: Callable, *args, **kwargs) -> float:
    warmup = 5
    repeats = 100
    for _ in range(warmup):
        func(*args, **kwargs)

    timer = benchmark.Timer(
        stmt="func(*args, **kwargs)",
        globals={"args": args, "kwargs": kwargs, "func": func},
    )

    return timer.timeit(number=repeats).median


def run_torch_sdpa(
    batch_size: int,
    q_seq_len: int,
    kv_seq_len: int,
    num_heads: int,
    head_size: int,
    causal: bool,
    device,
    dtype,
    has_mask: bool = False,
    mask_dim: int = 2,
    mask_dtype=torch.bool,
    backend: Optional[int] = None,
):
    q_shape = (batch_size, num_heads, q_seq_len, head_size)
    kv_shape = (batch_size, num_heads, kv_seq_len, head_size)
    q = torch.randn(q_shape, device=device, dtype=dtype)
    k = torch.randn(kv_shape, device=device, dtype=dtype)
    v = torch.randn(kv_shape, device=device, dtype=dtype)

    attn_mask = None
    if has_mask:
        mask_shape = (batch_size, num_heads, q_seq_len, kv_seq_len) if mask_dim == 4 else (q_seq_len, kv_seq_len)
        attn_mask = torch.ones(mask_shape, dtype=mask_dtype, device=device)

    context = sdpa_kernel(backend) if backend is not None else nullcontext()

    with context:
        average_latency = benchmark_torch_function(
            scaled_dot_product_attention,
            q,
            k,
            v,
            is_causal=causal,
            attn_mask=attn_mask,
        )
    return average_latency


def get_test_configs(use_gpu: bool = True):
    if use_gpu:
        # (batch_size, sequence_length, past_sequence_length, num_heads, head_size, run_unfused)
        configs = [
            (32, 512, 0, 64, 32, True),
            (32, 512, 0, 128, 16, True),
            (16, 1024, 0, 64, 32, True),
            (16, 1024, 0, 128, 16, True),
            (8, 2048, 0, 64, 32, True),
            (8, 2048, 0, 128, 16, False),
            (4, 4096, 0, 64, 32, False),
            (4, 4096, 0, 128, 16, False),
            (2, 8192, 0, 64, 32, False),
            (2, 8192, 0, 128, 16, False),
            (1, 16384, 0, 64, 32, False),
            (1, 16384, 0, 128, 16, False),
            # stable diffusion
            (1, 4096, 0, 8, 40, False),
            (1, 4096, 0, 8, 80, False),
            (1, 4096, 0, 8, 160, False),
            (4, 4096, 0, 8, 40, False),
            (4, 4096, 0, 8, 80, False),
            (4, 4096, 0, 8, 160, False),
            (1, 16384, 0, 8, 40, False),
            (1, 16384, 0, 8, 80, False),
            (1, 16384, 0, 8, 160, False),
            # bert-base
            (128, 128, 0, 12, 64, True),
            (64, 128, 0, 12, 64, True),
            (128, 384, 0, 12, 64, True),
            (64, 384, 0, 12, 64, True),
            (128, 512, 0, 12, 64, True),
            (64, 512, 0, 12, 64, True),
            # TNLGv4
            (4, 2048, 0, 32, 128, True),
            (4, 4096, 0, 32, 128, False),
            (8, 2048, 0, 32, 128, False),
            (8, 4096, 0, 32, 128, False),
        ]
    else:
        configs = [
            # TNLGv4
            (1, 128, 0, 32, 128, True),
            (1, 256, 0, 32, 128, True),
            (1, 512, 0, 32, 128, True),
            (1, 1024, 0, 32, 128, True),
            # (1, 2048, 0, 32, 128, True),
            # bert-base
            (1, 128, 0, 12, 64, True),
            (1, 384, 0, 12, 64, True),
            (1, 512, 0, 12, 64, True),
            (4, 128, 0, 12, 64, True),
            (4, 384, 0, 12, 64, True),
            (4, 512, 0, 12, 64, True),
            # bert-large
            (1, 128, 0, 16, 64, True),
            (1, 384, 0, 16, 64, True),
            (1, 512, 0, 16, 64, True),
            (4, 128, 0, 16, 64, True),
            (4, 384, 0, 16, 64, True),
            (4, 512, 0, 16, 64, True),
        ]
    return configs


def get_compute_capability():
    assert torch.cuda.is_available()
    major, minor = torch.cuda.get_device_capability()
    sm = major * 10 + minor
    return sm


def run_tflops_test(
    csv_writer: csv.DictWriter,
    use_gpu: bool = True,
    enable_cuda_graph: bool = False,
    causal: bool = False,
    has_past: bool = False,
    intra_op_num_threads: int = 0,
    repeats: int = 100,
):
    print(f"run_tflops_test: causal={causal}")

    if use_gpu:
        device_id = torch.cuda.current_device()
        device = torch.device("cuda", device_id)
        formats = [InputFormats.Q_K_V_BSNH_BSNH_BSNH, InputFormats.Q_KV_BSNH_BSN2H, InputFormats.QKV_BSN3H]
        provider = "CUDAExecutionProvider"
        # flash attention is available for sm >= 80
        sm = get_compute_capability()
        if sm >= 80:
            backends = [SdpaKernel.DEFAULT, SdpaKernel.FLASH_ATTENTION, SdpaKernel.EFFICIENT_ATTENTION]
        else:
            backends = [SdpaKernel.DEFAULT, SdpaKernel.EFFICIENT_ATTENTION]
    else:
        device_id = 0
        device = torch.device("cpu")
        formats = [InputFormats.Q_K_V_BSNH_BSNH_BSNH]
        enable_cuda_graph = False
        provider = "CPUExecutionProvider"
        backends = [SdpaKernel.DEFAULT]

    configs = get_test_configs(use_gpu)

    print("\nformat\tcausal\tprompt\tbatch\tseqlen\theads\th_dim\tthreads\tms\tTFLOPS\tkernel")

    for input_format in formats:
        for batch_size, sequence_length, past_sequence_length, num_heads, head_size, enable_unfused in configs:
            for use_kv_cache in [False]:
                config = MultiHeadAttentionConfig(
                    batch_size=batch_size,
                    sequence_length=sequence_length,
                    num_heads=num_heads,
                    head_size=head_size,
                    causal=causal,
                    use_kv_cache=use_kv_cache,
                    past_sequence_length=past_sequence_length,
                    max_cache_sequence_length=None,
                    kv_sequence_length=None,
                    provider=provider,
                    enable_cuda_graph=enable_cuda_graph,
                    device=device,
                    dtype=torch.float16 if use_gpu else torch.float,
                    share_past_present_buffer=False,
                    input_format=input_format,
                )
            for attention_kernel in backends:
                sess_options = SessionOptions()
                sess_options.intra_op_num_threads = intra_op_num_threads
                session = create_session(config, sess_options, attention_kernel=attention_kernel)

                if use_gpu:
                    kernel = get_gpu_kernel_name(attention_kernel)
                else:
                    kernel = get_cpu_kernel_name(config)

                if "math" in kernel:
                    # Skip large sequence length for Unfused kernel to avoid OOM.
                    if not enable_unfused:
                        if config.verbose:
                            print(f"skip unfused kernel for {vars(config)}")
                        continue

                    # Unfused kernel does not support packed QKV or packed KV formats.
                    if input_format not in [InputFormats.Q_K_V_BSNH_BSNH_BSNH]:
                        if config.verbose:
                            print(f"skip input_format for {vars(config)}")
                        continue

                input_dict = config.random_inputs()

                # warm up session
                _ = measure_latency(session, input_dict)

                latency_list = []
                for _ in range(repeats):
                    latency = measure_latency(session, input_dict)
                    latency_list.append(latency)
                average_latency = statistics.mean(latency_list)

                del session

                format_str = InputFormats.input_format_str(input_format)

                # compute TFLOPS per second
                speed = None
                if past_sequence_length == 0:
                    speed = tflops_per_second(
                        flops(batch_size, sequence_length, head_size, num_heads, causal), average_latency
                    )

                row = {
                    "use_gpu": use_gpu,
                    "enable_cuda_graph": enable_cuda_graph,
                    "format": format_str,
                    "causal": causal,
                    "batch_size": batch_size,
                    "sequence_length": sequence_length,
                    "past_sequence_length": past_sequence_length,
                    "num_heads": num_heads,
                    "head_size": head_size,
                    "intra_op_num_threads": intra_op_num_threads,
                    "average_latency": average_latency,
                    "tflops": speed,
                    "kernel": kernel,
                }
                csv_writer.writerow(row)

                speed = f"{speed:.2f}" if speed is not None else "NA"
                print(
                    f"{format_str}\t{causal}\t{not has_past}\t{batch_size}\t{sequence_length}\t{num_heads}\t{head_size}\t"
                    f"{intra_op_num_threads}\t{average_latency * 1000:.2f}\t{speed}\t{kernel}"
                )


def run_torch_test(
    csv_writer: csv.DictWriter,
    use_gpu: bool = True,
    causal: bool = False,
):
    configs = get_test_configs(use_gpu)

    if use_gpu:
        if not torch.cuda.is_available():
            return
        device_id = torch.cuda.current_device()
        device = torch.device("cuda", device_id)
        dtype = torch.float16
        backends = [
            None,
            SDPBackend.FLASH_ATTENTION,
            SDPBackend.EFFICIENT_ATTENTION,
            SDPBackend.CUDNN_ATTENTION,
            SDPBackend.MATH,
        ]
    else:
        device = torch.device("cpu")
        dtype = torch.float32
        backends = [None]

    backend_names = {
        SDPBackend.FLASH_ATTENTION: "torch:flash",
        SDPBackend.EFFICIENT_ATTENTION: "torch:efficient",
        SDPBackend.CUDNN_ATTENTION: "torch:cudnn",
        SDPBackend.MATH: "torch:math",
        None: "torch:default",
    }

    # Test PyTorch latency
    for batch_size, sequence_length, past_sequence_length, num_heads, head_size, enable_unfused in configs:
        for backend in backends:
            if backend == SDPBackend.MATH and not enable_unfused:
                continue
            if backend == SDPBackend.FLASH_ATTENTION and platform.system() != "Linux":
                continue

            backend_name = backend_names[backend]
            try:
                with torch.no_grad():
                    torch_latency = run_torch_sdpa(
                        batch_size,
                        sequence_length,
                        sequence_length,
                        num_heads,
                        head_size,
                        causal,
                        has_mask=False,
                        mask_dim=2,
                        mask_dtype=torch.bool,
                        device=device,
                        dtype=dtype,
                        backend=backend,
                    )
            except RuntimeError:
                continue

            speed = tflops_per_second(flops(batch_size, sequence_length, head_size, num_heads, causal), torch_latency)
            input_format = "Q,K,V"
            print(
                f"{input_format}\t{causal}\t{batch_size}\t{sequence_length}\t{num_heads}\t{head_size}\t"
                f"{0}\t{torch_latency * 1000:.2f}\t{speed:.2f}\t{backend_name}"
            )
            row = {
                "use_gpu": use_gpu,
                "enable_cuda_graph": False,
                "format": input_format,
                "causal": causal,
                "batch_size": batch_size,
                "sequence_length": sequence_length,
                "past_sequence_length": past_sequence_length,
                "num_heads": num_heads,
                "head_size": head_size,
                "intra_op_num_threads": torch.get_num_threads(),
                "average_latency": torch_latency,
                "tflops": speed,
                "kernel": backend_name,
            }
            csv_writer.writerow(row)


def run_tflops_tests(args):
    features = "gpu" if args.use_gpu else "cpu"
    if args.causal:
        features += "_causal"
    if args.has_past:
        features += "_past"
    csv_filename = "benchmark_mha_{}_{}_{}.csv".format(
        features,
        "torch" if args.torch else "ort",
        datetime.now().strftime("%Y%m%d-%H%M%S"),
    )
    with open(csv_filename, mode="a", newline="") as csv_file:
        column_names = [
            "use_gpu",
            "enable_cuda_graph",
            "format",
            "causal",
            "batch_size",
            "sequence_length",
            "past_sequence_length",
            "num_heads",
            "head_size",
            "intra_op_num_threads",
            "average_latency",
            "tflops",
            "kernel",
        ]
        csv_writer = csv.DictWriter(csv_file, fieldnames=column_names)
        csv_writer.writeheader()

        if args.torch:
            run_torch_test(csv_writer, args.use_gpu, args.causal)
        else:
            run_tflops_test(
                csv_writer,
                use_gpu=args.use_gpu,
                enable_cuda_graph=args.use_cuda_graph,
                causal=args.causal,
                has_past=args.has_past,
                intra_op_num_threads=args.intra_op_num_threads,
            )


def plot_prompt_performance(
    model_name: str,
    batch_size: int,
    num_heads: int,
    head_size: int,
    max_seq_len: int,
):
    import triton

    formats = InputFormats.get_name_list()

    # Exclude cross attention since kernel crashes for some configuration.
    formats = formats[:-1]

    settings = {
        "line_vals": formats,
        "line_names": ["ORT-MHA:" + name for name in formats],
        "styles": [("red", "solid"), ("yellow", "dashdot"), ("blue", "dashed"), ("green", "dotted")][0 : len(formats)],
    }

    sm = get_compute_capability()
    configs = [
        triton.testing.Benchmark(
            x_names=["sequence_length"],
            x_vals=[2**i for i in range(6, 17) if 2**i <= max_seq_len],
            line_arg="input_format",
            ylabel="ms",
            **settings,
            plot_name=f"prompt-sm{sm}-{model_name}-b{batch_size}-h{num_heads}_{head_size}-fp16",
            args={
                "batch_size": batch_size,
                "num_heads": num_heads,
                "head_size": head_size,
            },
        )
    ]

    @triton.testing.perf_report(configs)
    def benchmark(
        input_format: str,
        sequence_length: int,
        batch_size: int,
        num_heads: int,
        head_size: int,
        device="cuda",
    ):
        warmup = 15
        repeat = 100

        config: MultiHeadAttentionConfig = MultiHeadAttentionConfig(
            batch_size=batch_size,
            sequence_length=sequence_length,
            num_heads=num_heads,
            head_size=head_size,
            causal=False,
            past_sequence_length=0,
            kv_sequence_length=sequence_length if input_format == InputFormats.get_name_list()[-1] else None,
            max_cache_sequence_length=max_seq_len,
            provider="CUDAExecutionProvider",
            enable_cuda_graph=False,
            device=device,
            dtype=torch.float16,
            use_kv_cache=False,
            input_format=InputFormats.convert(input_format),
        )

        obj = OrtMultiHeadAttention(config)
        ms = triton.testing.do_bench(obj.infer, warmup=warmup, rep=repeat)
        return ms

    benchmark.run(save_path=".", print_data=True)


def run_bert_performance_test():
    """
    Run performance tests for prompt and token generation.

    """
    configures = [
        # (1, 32, 128, 8192, "TNLGv4"),
        # (4, 32, 128, 8192, "TNLGv4"),
        (1, 12, 64, 1024, "BertBase"),
        (16, 12, 64, 1024, "BertBase"),
        (1, 16, 64, 1024, "BertLarge"),
        (8, 16, 64, 1024, "BertLarge"),
    ]

    for batch_size, num_heads, head_size, max_seq_len, model_name in configures:
        plot_prompt_performance(
            batch_size=batch_size,
            num_heads=num_heads,
            head_size=head_size,
            max_seq_len=max_seq_len,
            model_name=model_name,
        )


def _parse_arguments():
    parser = argparse.ArgumentParser(description="Benchmark MultiHeadAttention for ONNX Runtime and PyTorch.")

    parser.add_argument(
        "--use_gpu",
        required=False,
        action="store_true",
        help="Use GPU for inference.",
    )
    parser.set_defaults(use_gpu=False)

    parser.add_argument(
        "--use_cuda_graph",
        required=False,
        action="store_true",
        help="Use cuda graph in onnxruntime.",
    )
    parser.set_defaults(use_cuda_graph=False)

    parser.add_argument(
        "--intra_op_num_threads",
        required=False,
        type=int,
        choices=[0, 1, 2, 4, 8, 16],
        default=0,
        help="intra_op_num_threads for onnxruntime. ",
    )

    parser.add_argument(
        "--has_past",
        required=False,
        action="store_true",
        help="whether past_sequence_length > 0",
    )
    parser.set_defaults(has_past=False)

    parser.add_argument(
        "--causal",
        required=False,
        action="store_true",
        help="test unidirectional",
    )
    parser.set_defaults(causal=False)

    parser.add_argument(
        "--torch",
        required=False,
        action="store_true",
        help="test pytorch instead of onnxruntime",
    )
    parser.set_defaults(torch=False)

    args = parser.parse_args()

    return args


if __name__ == "__main__":
    args = _parse_arguments()
    print(f"arguments:{args}")

    if args.has_past:
        assert args.causal, "--has_past need --causal specified"

    if args.use_gpu:
        assert args.torch or not args.causal, "no causal cuda kernel in MHA op"
        assert torch.cuda.is_available()
        if not args.torch:
            assert "CUDAExecutionProvider" in get_available_providers()

    if args.torch:
        assert Version(torch.__version__) >= Version("2.3.0")
        assert args.has_past is False

    if args.use_gpu and not args.torch:
        if platform.system() == "Linux":
            s = torch.cuda.Stream()
            with torch.cuda.stream(s), torch.no_grad():
                run_bert_performance_test()

    run_tflops_tests(args)
