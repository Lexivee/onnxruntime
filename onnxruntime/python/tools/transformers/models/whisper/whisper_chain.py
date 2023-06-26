import logging
import os
import sys

import onnx
from onnx import TensorProto, helper
from transformers import WhisperConfig

sys.path.append(os.path.join(os.path.dirname(__file__), "..", ".."))
from benchmark_helper import Precision  # noqa: E402
from convert_generation import (  # noqa: E402
    get_shared_initializers,
    update_decoder_subgraph_share_buffer_and_use_decoder_masked_mha,
)
import time

logger = logging.getLogger(__name__)


def chain_model(args):
    # Load encoder/decoder and insert necessary (but unused) graph inputs expected by BeamSearch op
    encoder_model = onnx.load_model(args.encoder_path, load_external_data=True)
    encoder_model.graph.name = "encoderdecoderinit subgraph"

    decoder_model = onnx.load_model(args.decoder_path, load_external_data=True)
    decoder_model.graph.name = "decoder subgraph"

    config = WhisperConfig.from_pretrained(args.model_name_or_path)

    beam_inputs = [
        "input_features_fp16" if args.precision == Precision.FLOAT16 else "input_features",
        "max_length",
        "min_length",
        "num_beams",
        "num_return_sequences",
        "length_penalty_fp16" if args.precision == Precision.FLOAT16 else "length_penalty",
        "repetition_penalty_fp16" if args.precision == Precision.FLOAT16 else "input_features",
        "vocab_mask" if args.use_prefix_vocab_mask else "",
        "prefix_vocab_mask" if args.use_prefix_vocab_mask else "",
        "",  # attention mask
        "decoder_input_ids" if args.use_forced_decoder_ids else "",
        "logits_processor" if args.use_logits_processor else "",
    ]
    beam_outputs = ["sequences"]

    input_features_cast_node, len_pen_cast_node, rep_pen_cast_node = None, None, None
    if args.precision == Precision.FLOAT16:
        input_features_cast_node = helper.make_node(
            "Cast",
            inputs=["input_features"],
            outputs=["input_features_fp16"],
            name="CastInputFeaturesToFp16",
            to=TensorProto.FLOAT16,
        )
        len_pen_cast_node = helper.make_node(
            "Cast",
            inputs=["length_penalty"],
            outputs=["length_penalty_fp16"],
            name="CastLengthPenaltyToFp16",
            to=TensorProto.FLOAT16,
        )
        rep_pen_cast_node = helper.make_node(
            "Cast",
            inputs=["repetition_penalty"],
            outputs=["repetition_penalty_fp16"],
            name="CastRepetitionPenaltyToFp16",
            to=TensorProto.FLOAT16,
        )

    node = helper.make_node("BeamSearch", inputs=beam_inputs, outputs=beam_outputs, name="BeamSearch_zcode")
    node.domain = "com.microsoft"
    node.attribute.extend(
        [
            helper.make_attribute("eos_token_id", config.eos_token_id),
            helper.make_attribute("pad_token_id", config.pad_token_id),
            helper.make_attribute("decoder_start_token_id", config.decoder_start_token_id),
            helper.make_attribute("no_repeat_ngram_size", args.no_repeat_ngram_size),
            helper.make_attribute("early_stopping", True),
            helper.make_attribute("model_type", 2),
        ]
    )

    input_features = helper.make_tensor_value_info(
        "input_features", TensorProto.FLOAT, ["batch_size", "feature_size", "sequence_length"]
    )
    max_length = helper.make_tensor_value_info("max_length", TensorProto.INT32, [1])
    min_length = helper.make_tensor_value_info("min_length", TensorProto.INT32, [1])
    num_beams = helper.make_tensor_value_info("num_beams", TensorProto.INT32, [1])
    num_return_sequences = helper.make_tensor_value_info("num_return_sequences", TensorProto.INT32, [1])
    length_penalty = helper.make_tensor_value_info("length_penalty", TensorProto.FLOAT, [1])
    repetition_penalty = helper.make_tensor_value_info("repetition_penalty", TensorProto.FLOAT, [1])

    graph_inputs = [
        input_features,
        max_length,
        min_length,
        num_beams,
        num_return_sequences,
        length_penalty,
        repetition_penalty,
    ]
    if args.use_forced_decoder_ids:
        decoder_input_ids = helper.make_tensor_value_info(
            "decoder_input_ids", TensorProto.INT32, ["batch_size", "initial_sequence_length"]
        )
        graph_inputs.append(decoder_input_ids)

    if args.use_logits_processor:
        logits_processor = helper.make_tensor_value_info("logits_processor", TensorProto.INT32, [1])
        graph_inputs.append(logits_processor)

    if args.use_vocab_mask:
        vocab_mask = helper.make_tensor_value_info("vocab_mask", TensorProto.INT32, [config.vocab_size])
        graph_inputs.append(vocab_mask)

    if args.use_prefix_vocab_mask:
        prefix_vocab_mask = helper.make_tensor_value_info(
            "prefix_vocab_mask", TensorProto.INT32, ["batch_size", config.vocab_size]
        )
        graph_inputs.append(prefix_vocab_mask)

    # graph outputs
    sequences = helper.make_tensor_value_info(
        "sequences", TensorProto.INT32, ["batch_size", "num_return_sequences", "max_length"]
    )
    graph_outputs = [sequences]

    if hasattr(args, "use_gpu") and args.use_gpu:
        if update_decoder_subgraph_share_buffer_and_use_decoder_masked_mha(decoder_model.graph):
            logger.info("Updated whisper decoder subgraph to use DecoderMaskedMultiHeadAttention successfully!")
        else:
            logger.warning("DecoderMaskedMultiHeadAttention could not be applied to whisper decoder subgraph")

    # Initializers/opsets
    # Delete shared data between decoder/encoder and move to larger graph initializers
    '''
    print("Timing diff between shared_initializers attempts:")
    time_total_old = 0.0
    time_total_new = 0.0
    import copy
    num_iterations = 1
    for _ in range(num_iterations):
        encoder_copy = copy.deepcopy(encoder_model)
        decoder_copy = copy.deepcopy(decoder_model)
        start_time = time.time()
        initializers = get_shared_initializers(decoder_copy, encoder_copy, only_raw_data = False)
        time_to_dedup = time.time()-start_time
        print("Time to get shared initializes: ", time_to_dedup)
        time_total_old += time_to_dedup
    for _ in range(num_iterations):
        encoder_copy = copy.deepcopy(encoder_model)
        decoder_copy = copy.deepcopy(decoder_model)
        start_time = time.time()
        initializers = get_shared_initializers(decoder_copy, encoder_copy, only_raw_data = True)
        time_to_dedup = time.time()-start_time
        print("Time to get shared initializes: ", time_to_dedup)
        time_total_new += time_to_dedup
    print("Avg time with old: ", time_total_old / num_iterations)
    print("Avg time with new: ", time_total_new / num_iterations)
    import pdb
    pdb.set_trace()
    '''
    initializers = get_shared_initializers(decoder_model, encoder_model, greedy = True)
    #initializers = []
    node.attribute.extend(
        [
            helper.make_attribute("decoder", decoder_model.graph),
            helper.make_attribute("encoder", encoder_model.graph),
        ]
    )

    opset_import = [helper.make_opsetid(domain="com.microsoft", version=1), helper.make_opsetid(domain="", version=14)]

    graph_nodes = (
        [input_features_cast_node, len_pen_cast_node, rep_pen_cast_node, node]
        if args.precision == Precision.FLOAT16
        else [node]
    )
    beam_graph = helper.make_graph(graph_nodes, "beam-search-test", graph_inputs, graph_outputs, initializers)
    assert decoder_model.ir_version == encoder_model.ir_version
    logger.info(f"Using IR version {decoder_model.ir_version} for chained model")

    # Set IR version of chained model to IR version of subgraphs in order to generate a working E2E model
    beam_model = helper.make_model_gen_version(
        beam_graph,
        producer_name="onnxruntime.transformers",
        opset_imports=opset_import,
        ir_version=decoder_model.ir_version,
    )

    if os.path.isfile(args.beam_model_output_dir):
        logger.info(f"Overwriting {args.beam_model_output_dir} and {args.beam_model_output_dir + '.data'}")
        os.remove(args.beam_model_output_dir)
        os.remove(args.beam_model_output_dir + ".data")
    onnx.save(
        beam_model,
        args.beam_model_output_dir,
        save_as_external_data=True,
        all_tensors_to_one_file=True,
        convert_attribute=True,
        location=f"{os.path.basename(args.beam_model_output_dir)}.data",
    )
    #onnx.checker.check_model(args.beam_model_output_dir, full_check=True)
