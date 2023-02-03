# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT License.
# --------------------------------------------------------------------------
import logging
from typing import Union

from fusion_attention import AttentionMask, FusionAttention
from fusion_utils import NumpyHelper
from onnx import NodeProto, TensorProto, helper, numpy_helper
from onnx_model import OnnxModel
from onnx_model_bert import BertOnnxModel
from fusion_base import Fusion
import numpy as np

logger = logging.getLogger(__name__)

#python optimizer.py --input /home/wy/Turing/tulr/model.onnx --output /home/wy/Turing/tulr/opt/model.onnx --model_type tulr --num_heads 16 --hidden_size 1024 --use_external_data_format

class FusionTulrAttention(FusionAttention):
    """
    Fuse TULR Attention subgraph into one Attention node.
    """

    def __init__(
        self,
        model: OnnxModel,
        hidden_size: int,
        num_heads: int,
        attention_mask: AttentionMask,
    ):
        super().__init__(model, hidden_size, num_heads, attention_mask)

    def create_attention_node(
        self,
        mask_index: str,
        q_matmul: NodeProto,
        k_matmul: NodeProto,
        v_matmul: NodeProto,
        q_add: NodeProto,
        k_add: NodeProto,
        v_add: NodeProto,
        num_heads: int,
        hidden_size: int,
        input: str,
        output: str,
        add_qk_str: str,
    ) -> Union[NodeProto, None]:
        """Create an Attention node.

        Args:
            mask_index (str): mask input
            q_matmul (NodeProto): MatMul node in fully connection for Q
            k_matmul (NodeProto): MatMul node in fully connection for  K
            v_matmul (NodeProto): MatMul node in fully connection for  V
            q_add (NodeProto): Add bias node in fully connection for Q
            k_add (NodeProto): Add bias node in fully connection for K
            v_add (NodeProto): Add bias node in fully connection for V
            num_heads (int): number of attention heads. If a model is pruned, it is the number of heads after pruning.
            hidden_size (int): hidden dimension. If a model is pruned, it is the hidden dimension after pruning.
            input (str): input name
            output (str): output name

        Returns:
            Union[NodeProto, None]: the node created or None if failed.
        """
        assert num_heads > 0

        if hidden_size > 0 and (hidden_size % num_heads) != 0:
            logger.debug(f"input hidden size {hidden_size} is not a multiple of num of heads {num_heads}")
            return None

        q_weight = self.model.get_initializer(q_matmul.input[1])
        k_weight = self.model.get_initializer(k_matmul.input[1])
        v_weight = self.model.get_initializer(v_matmul.input[1])
        q_bias = self.model.get_initializer(q_add.input[1]) or self.model.get_initializer(q_add.input[0])
        #k_bias = self.model.get_initializer(k_add.input[1]) or self.model.get_initializer(k_add.input[0])
        v_bias = self.model.get_initializer(v_add.input[1]) or self.model.get_initializer(v_add.input[0])

        if q_weight is None:
            print(
                f"{q_matmul.input[1]} is not an initializer. "
                "Please set do_constant_folding=True in torch.onnx.export to unblock attention fusion"
            )
            return None
        if not (k_weight and v_weight and q_bias):
            return None

        qw = NumpyHelper.to_array(q_weight)
        kw = NumpyHelper.to_array(k_weight)
        vw = NumpyHelper.to_array(v_weight)

        # assert q and k have same shape as expected
        assert qw.shape == kw.shape

        qw_in_size = qw.shape[0]
        kw_in_size = kw.shape[0]
        vw_in_size = vw.shape[0]

        assert qw_in_size == kw_in_size == vw_in_size

        if hidden_size > 0 and hidden_size != qw_in_size:
            logger.warning(
                f"Input hidden size ({hidden_size}) is not same as weight matrix dimension of q,k,v ({qw_in_size}). "
                "Please provide a correct input hidden size or pass in 0"
            )

        is_qkv_diff_dims = False
        if qw.shape != vw.shape:
            is_qkv_diff_dims = True

        # All the matrices can have the same shape or q, k matrics can have the same shape with v being different
        # For 2d weights, the shapes would be [in_size, out_size].
        # For 3d weights, shape would be [in_size, a, b] where a*b = out_size
        qw_out_size = np.prod(qw.shape[1:])
        kw_out_size = np.prod(kw.shape[1:])
        vw_out_size = np.prod(vw.shape[1:])

        qkv_weight_dim = 0
        if is_qkv_diff_dims:
            qkv_weight = np.concatenate((qw, kw, vw), axis=1)
            qkv_weight_dim = qw_out_size + kw_out_size + vw_out_size
        else:
            qkv_weight = np.stack((qw, kw, vw), axis=1)
            qkv_weight_dim = 3 * qw_out_size

        qb = NumpyHelper.to_array(q_bias)
        #kb = NumpyHelper.to_array(k_bias)
        kb = np.zeros_like(qb)
        vb = NumpyHelper.to_array(v_bias)

        q_bias_shape = np.prod(qb.shape)
        k_bias_shape = q_bias_shape
        v_bias_shape = np.prod(vb.shape)

        assert q_bias_shape == k_bias_shape == qw_out_size
        assert v_bias_shape == vw_out_size

        qkv_bias_dim = 0
        if is_qkv_diff_dims:
            qkv_bias = np.concatenate((qb, kb, vb), axis=0)
            qkv_bias_dim = q_bias_shape + k_bias_shape + v_bias_shape
        else:
            qkv_bias = np.stack((qb, kb, vb), axis=0)
            qkv_bias_dim = 3 * q_bias_shape

        attention_node_name = self.model.create_node_name("Attention")

        use_multi_head_attention = False
        if not use_multi_head_attention:
            weight = helper.make_tensor(
                name=attention_node_name + "_qkv_weight",
                data_type=TensorProto.FLOAT,
                dims=[qw_in_size, qkv_weight_dim],
                vals=qkv_weight.flatten().tolist(),
            )

            # Sometimes weights and bias are stored in fp16
            if q_weight.data_type == 10:
                weight.CopyFrom(numpy_helper.from_array(NumpyHelper.to_array(weight).astype(np.float16), weight.name))
            self.model.add_initializer(weight, self.this_graph_name)

        bias = helper.make_tensor(
            name=attention_node_name + "_qkv_bias",
            data_type=TensorProto.FLOAT,
            dims=[qkv_bias_dim],
            vals=qkv_bias.flatten().tolist(),
        )
        if q_bias.data_type == 10:
            bias.CopyFrom(numpy_helper.from_array(NumpyHelper.to_array(bias).astype(np.float16), bias.name))
        self.model.add_initializer(bias, self.this_graph_name)

        if use_multi_head_attention:
            attention_inputs = [
                q_matmul.output[0],
                k_matmul.output[0],
                v_matmul.output[0],
                attention_node_name + "_qkv_bias",
            ]
            if mask_index is not None:
                attention_inputs.append(mask_index)
            if add_qk_str is not None:
                attention_inputs.append(add_qk_str)

            attention_node = helper.make_node(
                "MultiHeadAttention",
                inputs=attention_inputs,
                outputs=[output],
                name=attention_node_name,
            )
        else:
            attention_inputs = [
                input,
                attention_node_name + "_qkv_weight",
                attention_node_name + "_qkv_bias",
            ]
            if mask_index is not None:
                attention_inputs.append(mask_index)
            else:
                attention_inputs.append("")

            if add_qk_str is not None:
                attention_inputs.append("")  # no past
                attention_inputs.append(add_qk_str)

            attention_node = helper.make_node(
                "Attention",
                inputs=attention_inputs,
                outputs=[output],
                name=attention_node_name,
            )
        attention_node.domain = "com.microsoft"
        attention_node.attribute.extend([helper.make_attribute("num_heads", num_heads)])

        if is_qkv_diff_dims:
            attention_node.attribute.extend(
                [helper.make_attribute("qkv_hidden_sizes", [qw_out_size, kw_out_size, vw_out_size])]
            )

        if self.mask_filter_value is not None:
            attention_node.attribute.extend([helper.make_attribute("mask_filter_value", float(self.mask_filter_value))])

        return attention_node

    def fuse(self, normalize_node, input_name_to_nodes, output_name_to_node):
        # Sometimes we can not fuse skiplayernormalization since the add before layernorm has an output that used by nodes outside skiplayernorm
        # Conceptually we treat add before layernorm as skiplayernorm node since they share the same pattern
        start_node = normalize_node
        if normalize_node.op_type != "SkipLayerNormalization":
            return

        # SkipLayerNormalization has two inputs, and one of them is the root input for attention.
        qkv_nodes = self.model.match_parent_path(
            start_node,
            ["Add", "MatMul", "Reshape", "Transpose", "MatMul"],
        )
        if qkv_nodes is not None:
            (_, _, reshape_qkv, transpose_qkv, matmul_qkv) = qkv_nodes
        else:
            return

        other_inputs = []
        for i, input in enumerate(start_node.input):
            if input not in output_name_to_node:
                continue

            if input == qkv_nodes[0].output[0]:
                continue
            other_inputs.append(input)
        if len(other_inputs) != 1:
            return

        root_input = other_inputs[0]

        v_nodes = self.model.match_parent_path(
            matmul_qkv,
            ["Transpose", "Reshape", "Add", "MatMul"],
        )
        if v_nodes is None:
            return
        (_, _, add_v, matmul_v) = v_nodes

        qk_nodes = self.model.match_parent_path(matmul_qkv, ["Softmax", "Add", "Add", "Div", "MatMul"])
        if qk_nodes is None:
            return
        (_, add_qk, _, _, matmul_qk) = qk_nodes

        q_nodes = self.model.match_parent_path(
            matmul_qk,
            ["Transpose", "Reshape", "Add", "MatMul"],
            [0, 0, 0, 1],
        )
        if q_nodes is None:
            return
        add_q = q_nodes[-2]
        matmul_q = q_nodes[-1]

        k_nodes = self.model.match_parent_path(
            matmul_qk,
            ["Transpose", "Reshape", "MatMul"],
            [1, 0, 0],
        )
        if k_nodes is None:
            return

        add_k = None
        matmul_k = k_nodes[-1]

        extra_add_qk_nodes = self.model.match_parent_path(add_qk, ["Mul"])
        if extra_add_qk_nodes is None:
            return

        mask_nodes = self.model.match_parent_path(
            add_qk,
            ["Add", "Mul", "Sub", "Cast", "Unsqueeze", "Unsqueeze"],
        )
        if len(mask_nodes) > 1 and mask_nodes[1].op_type == "Mul":
            _, mul_val = self.model.get_constant_input(mask_nodes[0])
            if mul_val != -10000:
                self.mask_filter_value = mul_val

        if matmul_q.input[0] == root_input:
            mask_index = self.attention_mask.process_mask(mask_nodes[-1].input[0])
            # number of heads are same for all the paths, hence to create attention node, we pass the q_num_heads
            # the input_hidden_size represents the input hidden size, this is used as needed but hidden sizes for Q, K are extracted appropriately
            new_node = self.create_attention_node(
                mask_index,
                matmul_q,
                matmul_k,
                matmul_v,
                add_q,
                add_k,
                add_v,
                self.num_heads,
                self.hidden_size,
                root_input,
                reshape_qkv.output[0],
                add_qk.input[1],
            )
            if new_node is None:
                return
            self.nodes_to_add.append(new_node)
            self.node_name_to_graph_name[new_node.name] = self.this_graph_name

            self.nodes_to_remove.extend([reshape_qkv, transpose_qkv, matmul_qkv])
            self.nodes_to_remove.extend(qk_nodes)
            self.nodes_to_remove.extend(k_nodes[:-1])
            self.nodes_to_remove.extend(v_nodes[:-1])

            # Use prune graph to remove mask nodes since they are shared by all attention nodes.
            self.nodes_to_remove.extend(mask_nodes)
            self.prune_graph = True

# Attr("max_distance", "Max distance", AttributeProto::INT)
# Attr("is_bidirectional", "Default value is 0.", AttributeProto::INT, static_cast<int64_t>(0))
# Input(0, "bias_table", "2D input tensor with shape (num_buckets, num_heads), COL-major(See UT for example)", "T")
# Input(1, "query_length", "The length of query. Self Attention requires query_length = key_length", "U")
# Input(2, "key_length", "The length of key.", "U")
# Output(0, "output", "4D output tensor with shape (1, num_heads, sequence_length, sequence_length)", "T")
class FusionRelativePositionBiasBlock(Fusion):
    def __init__(self, model: OnnxModel, max_distance: int, is_bidirectional: bool):
        super().__init__(model, "RelativePositionBias", "GatherElements")
        self.max_distance = 128
        self.is_bidirectional = 1

    def fuse(self, node, input_name_to_nodes, output_name_to_node):
        stem_nodes = self.model.match_parent_path(
            node,
            ["Expand", "Where", "Equal", "Concat", "Unsqueeze", "Gather", "Shape", "Sub", "Unsqueeze", "Expand", "Unsqueeze", "Range"],
        )
        if stem_nodes is None:
            return

        expand = stem_nodes[0]
        range = stem_nodes[-1]
        rpb_nodes = self.model.match_parent_path(
            expand,
            ["Unsqueeze", "Unsqueeze", "Gemm"]
        )
        if rpb_nodes is None:
            return

        gemm = rpb_nodes[-1]

        self.nodes_to_remove.extend(stem_nodes)
        self.nodes_to_remove.extend(rpb_nodes)

        table_weight = self.model.get_initializer(gemm.input[0])
        table_weight_np = NumpyHelper.to_array(table_weight)
        bias_table = helper.make_tensor(
            name="bias_table_weight",
            data_type=TensorProto.FLOAT,
            dims=[np.shape(table_weight_np)[1], np.shape(table_weight_np)[0]],
            vals=table_weight_np.flatten().tolist(),
        )
        self.model.add_initializer(bias_table, self.this_graph_name)
        inputs = [bias_table.name, range.input[1], range.input[1]]
        outputs = [node.output[0]]
        rpb_node = helper.make_node(
            "RelativePositionBias",
            inputs=inputs,
            outputs=outputs,
            name=self.model.create_node_name("RelativePositionBias", name_prefix="RPB"),
        )
        rpb_node.domain = "com.microsoft"
        rpb_node.attribute.extend([helper.make_attribute("max_distance", self.max_distance)])
        rpb_node.attribute.extend([helper.make_attribute("is_bidirectional", self.is_bidirectional)])

        self.nodes_to_add.append(rpb_node)
        self.node_name_to_graph_name[rpb_node.name] = self.this_graph_name


class TulrOnnxModel(BertOnnxModel):
    def __init__(self, model, num_heads, hidden_size):
        super().__init__(model, num_heads, hidden_size)
        self.attention_mask = AttentionMask(self)
        self.attention_fusion = FusionTulrAttention(self, self.hidden_size, self.num_heads, self.attention_mask)
        self.rpb_fusion = FusionRelativePositionBiasBlock(self, 32, True)

    def fuse_attention(self):
        self.attention_fusion.apply()

    def postprocess(self):
        self.rpb_fusion.apply()
        self.clean_graph()
        self.prune_graph()
