# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT License.  See License.txt in the project root for
# license information.
# --------------------------------------------------------------------------

import os
import unittest

import onnx
from whisper_model_generator import *
from model_loader import get_test_data_path
from parity_utilities import find_transformers_source

if find_transformers_source():
    from fusion_options import FusionOptions
    from onnx_model import OnnxModel
    from optimizer import optimize_by_fusion, optimize_model
else:
    from onnxruntime.transformers.fusion_options import FusionOptions
    from onnxruntime.transformers.onnx_model import OnnxModel
    from onnxruntime.transformers.optimizer import optimize_by_fusion, optimize_model


class TestFusion(unittest.TestCase):
    def verify_fusion(self, optimized_model, expected_model_filename):
        optimized_model.topological_sort(is_deterministic=True)

        expected_model_path = os.path.join(os.path.dirname(__file__), "test_data", "whisper", expected_model_filename)
        expected_model = OnnxModel(onnx.load(expected_model_path))
        expected_model.topological_sort(is_deterministic=True)

        if "sln" in expected_model_filename or "mha" in expected_model_filename:
            with open("optimized_model.txt", "w") as f:
                f.write(str(optimized_model.model.graph))
                f.close()
            with open("expected_model.txt", "w") as f:
                f.write(str(expected_model.model.graph))
                f.close()
        self.assertEqual(str(optimized_model.model.graph), str(expected_model.model.graph))

    # Note: The test cases where the initial LayerNorm is (Add + LayerNorm) have been temporarily
    # skipped since they only occur once at the beginning of the model. The initial LayerNorm is
    # always SkipLayerNorm thereafter. The test case below is an example for how to write the 
    # (Add + LayerNorm) test cases.
    #
    # The (Add + LayerNorm) case happens for attention types #1, #2, and #3 in onnx_models_bart.py

    # def test_encoder_attention_fusion_with_layernorm_(self):
    #     num_heads = 4
    #     hidden_size = 64
    #     model = create_whisper_encoder_attention(num_heads=num_heads, hidden_size=hidden_size)
    #     dir = "."
    #     model_path = os.path.join(dir, "whisper_encoder_attention_ln.onnx")
    #     onnx.save(model, model_path)
    #     options = FusionOptions("bart")
    #     optimized_model = optimize_model(model_path, model_type="bart", num_heads=num_heads, hidden_size=hidden_size, 
    #                                      optimization_options=options)
    #     optimized_model.save_model_to_file("encoder_attention_with_ln_fused_actual.onnx", use_external_data_format=True)
    #     # os.remove(model_path)
    #     self.verify_fusion(optimized_model, "encoder_attention_with_ln_fused.onnx")

    # Attention type #1 in onnx_model_bart.py
    def test_encoder_attention_fusion_with_skiplayernorm(self):
        num_heads = 4
        hidden_size = 64
        model = create_whisper_encoder_attention(num_heads=num_heads, hidden_size=hidden_size, add_before_layernorm=False)
        dir = "."
        model_path = os.path.join(dir, "whisper_encoder_attention_sln.onnx")
        onnx.save(model, model_path)
        options = FusionOptions("bart")
        optimized_model = optimize_model(model_path, model_type="bart", num_heads=num_heads, hidden_size=hidden_size, 
                                         optimization_options=options)
        # optimized_model.save_model_to_file("encoder_attention_with_sln_fused_actual.onnx", use_external_data_format=True)
        os.remove(model_path)
        self.verify_fusion(optimized_model, "encoder_attention_with_sln_fused.onnx")

    # Attention type #2 in onnx_model_bart.py
    def test_decoder_attention_fusion_with_skiplayernorm(self):
        num_heads = 4
        hidden_size = 64
        model = create_whisper_decoder_attention(num_heads=num_heads, hidden_size=hidden_size, add_before_layernorm=False)
        dir = "."
        model_path = os.path.join(dir, "whisper_decoder_attention_sln.onnx")
        onnx.save(model, model_path)
        options = FusionOptions("bart")
        optimized_model = optimize_model(model_path, model_type="bart", num_heads=num_heads, hidden_size=hidden_size, 
                                         optimization_options=options)
        # optimized_model.save_model_to_file("decoder_attention_with_sln_fused_actual.onnx", use_external_data_format=True)
        os.remove(model_path)
        self.verify_fusion(optimized_model, "decoder_attention_with_sln_fused.onnx")

    # Attention type #4 in onnx_model_bart.py
    def test_decoder_multihead_attention_fusion(self):
        num_heads = 4
        hidden_size = 64
        model = create_whisper_decoder_multihead_attention(num_heads=num_heads, hidden_size=hidden_size)
        dir = "."
        model_path = os.path.join(dir, "whisper_decoder_mha.onnx")
        onnx.save(model, model_path)
        options = FusionOptions("bart")
        options.use_multi_head_attention = True
        optimized_model = optimize_model(model_path, model_type="bart", num_heads=num_heads, hidden_size=hidden_size,
                                         optimization_options=options)
        # optimized_model.save_model_to_file("decoder_mha_fused_actual.onnx", use_external_data_format=True)
        os.remove(model_path)
        self.verify_fusion(optimized_model, "decoder_mha_fused.onnx")

    # Attention type #3 in onnx_model_bart.py
    def test_decoder_with_past_multihead_self_attention_fusion_with_skiplayernorm(self):
        num_heads = 4
        hidden_size = 64
        model = create_whisper_decoder_with_past_multihead_self_attention(num_heads=num_heads, hidden_size=hidden_size, add_before_layernorm=False)
        dir = "."
        model_path = os.path.join(dir, "whisper_decoder_with_past_self_mha.onnx")
        onnx.save(model, model_path)
        options = FusionOptions("bart")
        options.use_multi_head_attention = True
        optimized_model = optimize_model(model_path, model_type="bart", num_heads=num_heads, hidden_size=hidden_size,
                                         optimization_options=options)
        # optimized_model.save_model_to_file("decoder_self_mha_fused_actual.onnx", use_external_data_format=True)
        os.remove(model_path)
        self.verify_fusion(optimized_model, "decoder_with_past_self_mha_fused.onnx")

    # Attention type #5 in onnx_model_bart.py
    def test_decoder_with_past_multihead_cross_attention_fusion(self):
        num_heads = 4
        hidden_size = 64
        model = create_whisper_decoder_with_past_multihead_cross_attention(num_heads=num_heads, hidden_size=hidden_size)
        dir = "."
        model_path = os.path.join(dir, "whisper_decoder_with_past_cross_mha.onnx")
        onnx.save(model, model_path)
        options = FusionOptions("bart")
        options.use_multi_head_attention = True
        optimized_model = optimize_model(model_path, model_type="bart", num_heads=num_heads, hidden_size=hidden_size,
                                         optimization_options=options)
        # optimized_model.save_model_to_file("decoder_cross_mha_fused_actual.onnx", use_external_data_format=True)
        os.remove(model_path)
        self.verify_fusion(optimized_model, "decoder_with_past_cross_mha_fused.onnx")

if __name__ == "__main__":
    unittest.main()
