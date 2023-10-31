import argparse
import logging
import os
import time
from typing import List

import numpy as np
import torch
from benchmark_helper import setup_logger
from llama_inputs import (
    add_io_bindings,
    convert_inputs_for_ort,
    get_merged_sample_with_past_kv_inputs,
    get_sample_inputs,
    get_sample_with_past_kv_inputs,
)
from transformers import LlamaConfig, LlamaForCausalLM

import onnxruntime as ort

logger = logging.getLogger("")


def get_sequence_lengths(args: argparse.Namespace):
    past_sequence_length, curr_sequence_length = (8, 1) if args.use_past_kv else (0, 8)
    max_sequence_length = 2048
    return past_sequence_length, curr_sequence_length, max_sequence_length


def get_inputs(args: argparse.Namespace, config: LlamaConfig):
    # Dummy values for parity
    batch_size = 2
    past_sequence_length, sequence_length, max_sequence_length = get_sequence_lengths(args)

    if args.merged:
        inputs = get_merged_sample_with_past_kv_inputs(
            config,
            args.device,
            batch_size,
            seq_len=sequence_length,
            past_seq_len=past_sequence_length,
            max_seq_len=max_sequence_length,
            use_fp16=args.use_fp16,
            return_dict=True,
        )
    elif args.use_past_kv:
        inputs = get_sample_with_past_kv_inputs(
            config, args.device, batch_size, sequence_length, use_fp16=args.use_fp16, return_dict=True
        )
    else:
        inputs = get_sample_inputs(config, args.device, batch_size, sequence_length, return_dict=True)

    return inputs


def verify_parity(args: argparse.Namespace, config: LlamaConfig, pt_model: LlamaForCausalLM, kv_cache_ortvalues: dict):
    inputs = get_inputs(args, config)

    # Run inference with PyTorch
    if args.execution_provider != "cpu":
        torch.cuda.synchronize()
    start_time = time.time()
    pt_outputs = pt_model(**inputs).logits.detach().cpu().numpy()
    if args.execution_provider != "cpu":
        torch.cuda.synchronize()
    end_time = time.time()
    logger.info(f"PyTorch took {end_time - start_time} s")

    # Run inference with ORT
    past_sequence_length, _, max_sequence_length = get_sequence_lengths(args)
    inputs = convert_inputs_for_ort(
        inputs,
        use_fp16=args.use_fp16,
        use_buffer_share=args.use_fp16,
        past_seq_len=past_sequence_length,
        max_seq_len=max_sequence_length,
        device=args.execution_provider,
        device_id=int(args.device_id),
    )

    ep = f"{args.execution_provider.upper()}ExecutionProvider"
    if ep == "CUDAExecutionProvider":
        ep = (ep, {"device_id": args.device_id})
    ort_model = ort.InferenceSession(
        args.onnx_model_path,
        sess_options=ort.SessionOptions(),
        providers=[ep],
    )

    # Add IO bindings for non-CPU execution providers
    if args.execution_provider != "cpu":
        io_binding, kv_cache_ortvalues = add_io_bindings(
            ort_model, inputs, args.execution_provider, int(args.device_id), kv_cache_ortvalues
        )

        io_binding.synchronize_inputs()
        start_time = time.time()
        ort_model.run_with_iobinding(io_binding)
        io_binding.synchronize_outputs()
        end_time = time.time()

        ort_outputs = io_binding.copy_outputs_to_cpu()[0]  # Get logits

    else:
        start_time = time.time()
        ort_outputs = ort_model.run(None, inputs)
        end_time = time.time()

        ort_outputs = ort_outputs[0]  # Get logits

    logger.info(f"ONNX Runtime took {end_time - start_time} s")

    # Compare PyTorch and ONNX Runtime accuracy
    tol = 2e1 if "int4" in args.onnx_model_path or "int8" in args.onnx_model_path else 5e-1
    parity = np.allclose(pt_outputs, ort_outputs, rtol=tol, atol=tol)
    logger.warning(f"Are PyTorch and ONNX Runtime results close? {parity}")
    if not parity:
        logger.warning(f"Max diff: {np.max(pt_outputs - ort_outputs)}")
    return kv_cache_ortvalues


def get_args(argv: List[str]):
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "-m",
        "--model_name",
        required=True,
        help="Model name in Hugging Face",
    )

    parser.add_argument(
        "-t",
        "--torch_model_directory",
        required=False,
        default=os.path.join("."),
        help="Path to folder containing PyTorch model and associated files if saved on disk",
    )

    parser.add_argument(
        "-o",
        "--onnx_model_path",
        required=True,
        default=os.path.join("."),
        help="Path to ONNX model (with external data files saved in the same folder as the model)",
    )

    parser.add_argument(
        "-ep",
        "--execution_provider",
        required=False,
        default="cpu",
        choices=["cpu", "cuda", "rocm"],
        help="Execution provider to verify parity with",
    )

    parser.add_argument(
        "-id",
        "--device-id",
        required=False,
        type=str,
        default="0",
        help="Device ID for GPUs",
    )

    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Print verbose logs",
    )
    parser.set_defaults(verbose=False)

    parser.add_argument(
        "-p",
        "--use_past_kv",
        action="store_true",
        help="Use past key and past value as inputs to the model. Necessary for decoder_with_past_model.onnx models.",
    )
    parser.set_defaults(use_past_kv=False)

    parser.add_argument(
        "--merged",
        action="store_true",
        help="Use merged model (i.e. decoder_merged_model.onnx).",
    )
    parser.set_defaults(merged=False)

    parser.add_argument(
        "-fp",
        "--precision",
        required=True,
        choices=["int4", "int8", "fp16", "fp32"],
        help="Precision of model",
    )

    args = parser.parse_args() if argv == [] else parser.parse_args(argv)

    # Use FP32 precision for FP32, INT8, INT4 CPU models, use FP16 precision for FP16 and INT4 GPU models
    args.precision = (
        "fp32"
        if args.precision in {"int8", "fp32"} or (args.precision == "int4" and args.execution_provider == "cpu")
        else "fp16"
    )
    return args


def main(argv: List[str] = []):  # noqa: B006
    args = get_args(argv)
    setup_logger(args.verbose)
    logger.info(f"Arguments: {args}")

    # Load model and config
    setattr(args, "use_fp16", args.precision == "fp16")  # noqa: B010
    setattr(args, "device_name", "cpu" if args.execution_provider == "cpu" else f"cuda:{args.device_id}")  # noqa: B010
    setattr(args, "device", torch.device(args.device_name))  # noqa: B010
    use_auth_token = args.torch_model_directory == os.path.join(".")
    location = args.model_name if use_auth_token else args.torch_model_directory

    config = LlamaConfig.from_pretrained(location, use_auth_token=use_auth_token)
    llama = LlamaForCausalLM.from_pretrained(
        location,
        torch_dtype=(torch.float16 if args.use_fp16 else torch.float32),
        use_auth_token=use_auth_token,
        use_cache=True,
    ).to(args.device)

    if not args.merged:
        verify_parity(args, config, llama)
    else:
        kv_cache_ortvalues = {}

        # Verify prompt generation in merged model (decoder_model.onnx)
        args.use_past_kv = False
        kv_cache_ortvalues = verify_parity(args, config, llama, kv_cache_ortvalues)

        # Verify token generation in merged model (decoder_with_past_model.onnx)
        args.use_past_kv = True
        verify_parity(args, config, llama, kv_cache_ortvalues)


if __name__ == "__main__":
    seed = 2
    np.random.seed(seed)
    torch.manual_seed(seed)
    main()
