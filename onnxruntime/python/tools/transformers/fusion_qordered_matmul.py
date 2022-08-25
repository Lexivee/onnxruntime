#-------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT License.
#--------------------------------------------------------------------------

from logging import getLogger
from typing import Dict

from fusion_base import Fusion
from fusion_utils import FusionUtils
from onnx import helper
from onnx_model import OnnxModel

logger = getLogger(__name__)


class FusionQOrderedMatMul(Fusion):
    def __init__(self, model: OnnxModel):
        super().__init__(model, "QOrderedMatMul", "MatMul")

    def fuse(self, node, input_name_to_nodes: Dict, output_name_to_node: Dict):
        matmul_children = self.model.get_children(node, input_name_to_nodes)

        # Should only have 1 child - Bias Add
        if len(matmul_children) != 1 or matmul_children[0].op_type != "Add":
            return

        bias_add_node = matmul_children[0]

        # Atleast one of the inputs to Bias Add node must be a constant
        bias_add_node_index = 0     
        if self.model.get_constant_value(bias_add_node.input[0]) is None and self.model.get_constant_value(bias_add_node.input[1]) is None:
            return

        if self.model.get_constant_value(bias_add_node.input[0]) is None:
            bias_add_node_index = 1

        bias_add_children = self.model.get_children(bias_add_node, input_name_to_nodes)

        if len(bias_add_children) != 1:
            return

        bias_add_child = bias_add_children[0]        

        # Bias Add can have another Add downstream (residual Add layer)
        has_residual_add = False
        residual_add_node = None

        downstream_quantize_node = None

        if bias_add_child.op_type == "Add":
            has_residual_add = True
            residual_add_node = bias_add_child

            residual_add_children = self.model.get_children(residual_add_node, 
                                                            input_name_to_nodes)

            if len(residual_add_children) != 1 or residual_add_children[0].op_type != "QuantizeLinear":
                return

            downstream_quantize_node = residual_add_children[0]

        elif bias_add_child.op_type == "QuantizeLinear":
            downstream_quantize_node = bias_add_child

        else:
            return

        # Make sure the downstream QuantizeLinear has the proper zero points and scales      
        y_scale = self.model.get_constant_value(downstream_quantize_node.input[1])
        if y_scale is None:
            return

        y_zero_point = self.model.get_constant_value(downstream_quantize_node.input[2])
        if y_zero_point is None or y_zero_point != 0:
            return

        # The first and second inputs to MatMul should flow through DequantizeLinear nodes
        first_path_id, first_input_parent_nodes, _ = self.model.match_parent_paths(
            node,
            [
                (["DequantizeLinear"], [0])
            ],
            output_name_to_node,
        )

        # TODO: Adjust this once QOrderedAttention is ready
        reshape_node_0 = None
        transpose_node_0 = None
        if first_path_id < 0:
            first_path_id, first_input_parent_nodes, _ = self.model.match_parent_paths(
                node,
                [
                    (["Reshape", "Transpose", "DequantizeLinear", "QuantizeLinear"], [0, 0, 0, 0])
                ],
                output_name_to_node,
            )

            if first_path_id < 0:
                return

            reshape_node_0 = first_input_parent_nodes[0]
            transpose_node_0 = first_input_parent_nodes[1]
            dequantize_node_0 = first_input_parent_nodes[2]               
        else:
            dequantize_node_0 = first_input_parent_nodes[0]

        # Make sure the upstream DequantizeLinear-0 has the proper zero points and scales        
        x_scale_0 = self.model.get_constant_value(dequantize_node_0.input[1])
        if x_scale_0 is None:
            return

        x_zero_point_0 = self.model.get_constant_value(dequantize_node_0.input[2])
        if x_zero_point_0 is None or x_zero_point_0 != 0:
            return

        second_path_id, second_input_parent_nodes, _ = self.model.match_parent_paths(
            node,
            [
                (["DequantizeLinear"], [1]),
            ],
            output_name_to_node,
        )

        if second_path_id < 0:
            return

        dequantize_node_1 = second_input_parent_nodes[0]

        # Check if weight 'B' is a constant
        if self.model.get_constant_value(dequantize_node_1.input[0]) is None:
            return

        # Make sure the upstream DequantizeLinear-1 has the proper zero points and scales       
        x_scale_1 = self.model.get_constant_value(dequantize_node_1.input[1])
        if x_scale_1 is None:
            return

        x_zero_point_1 = self.model.get_constant_value(dequantize_node_1.input[2])
        if x_zero_point_1 is None or x_zero_point_1 != 0:
            return

        # Make sure the upstream flow into the Residual Add node flows through a DQ node
        residual_add_dequantize_node = None

        if has_residual_add:
            residual_path_id, residual_input_parent_nodes, _ = self.model.match_parent_paths(
                residual_add_node, [(["DequantizeLinear"], [1]),], output_name_to_node
                )

            if residual_path_id < 0:
                residual_path_id, residual_input_parent_nodes, _ = self.model.match_parent_paths(
                    residual_add_node, [(["DequantizeLinear"], [0]),], output_name_to_node
                )               

                if residual_path_id < 0:
                    return

            residual_add_dequantize_node = residual_input_parent_nodes[0]

            # Make sure the upstream DequantizeLinear to the Residual Add has the proper zero points and scales       
            x_scale_1 = self.model.get_constant_value(residual_add_dequantize_node.input[1])
            if x_scale_1 is None:
                return

            x_zero_point_1 = self.model.get_constant_value(residual_add_dequantize_node.input[2])
            if x_zero_point_1 is None or x_zero_point_1 != 0:
                return

        # Subgraph nodes to be fused
        subgraph_nodes = [node, bias_add_node]  # MatMul + Bias Add

        if has_residual_add:
            subgraph_nodes.extend([residual_add_node]) # Residual Add

        subgraph_nodes.extend([dequantize_node_1, downstream_quantize_node])  #Relevant Q, DQ nodes

        if not self.model.is_safe_to_fuse_nodes(
            subgraph_nodes,
            downstream_quantize_node.output,
            input_name_to_nodes,
            output_name_to_node
        ):
            logger.debug(f"It is not safe to fuse QOrderedMatMul node. Skip")
            return     

        # TODO: Adjust after QOrderedAttention
        if transpose_node_0 is not None:
            self.model.replace_node_input(transpose_node_0, transpose_node_0.input[0], dequantize_node_0.input[0])

        # Make inputs
        fused_node_inputs=[reshape_node_0.output[0] if reshape_node_0 is not None else dequantize_node_0.input[0], 
                dequantize_node_0.input[1], 
                dequantize_node_1.input[0], dequantize_node_1.input[1], 
                downstream_quantize_node.input[1], 
                bias_add_node.input[bias_add_node_index]]

        if has_residual_add:
            fused_node_inputs.append(residual_add_dequantize_node.input[0])
            fused_node_inputs.append(residual_add_dequantize_node.input[1])

        # The MatMul weight 'B' and 'bias' need some post-processing

        # Transpose weight 'B' from ROW to COL
        # This offline transpose is needed only while using the CUDA EP
        # TODO: Make this fusion logic EP-agnostic ?
        weight_tensor = self.model.get_initializer(dequantize_node_1.input[0])
        self.model.transpose_2d_tensor(weight_tensor)

        bias_tensor = self.model.get_initializer(bias_add_node.input[bias_add_node_index])
        self.model.scale_1d_tensor(bias_tensor, y_scale)

        fused_node = helper.make_node(
            "QOrderedMatMul",
            inputs=fused_node_inputs,
            outputs=[downstream_quantize_node.output[0]],
            name=self.model.create_node_name("QOrderedMatMul", name_prefix="QOrderedMatMul"),
        )

        fused_node.attribute.extend([helper.make_attribute("order_A", 1)])
        fused_node.attribute.extend([helper.make_attribute("order_B", 0)])
        fused_node.attribute.extend([helper.make_attribute("order_Y", 1)])

        fused_node.domain = "com.microsoft"

        self.nodes_to_remove.extend(subgraph_nodes)
        self.nodes_to_add.append(fused_node)
        self.node_name_to_graph_name[fused_node.name] = self.this_graph_name

class FusionQOrderedMatMulFromGemm(Fusion):
    def __init__(self, model: OnnxModel):
        super().__init__(model, "QOrderedMatMul", "Reshape")

    def fuse(self, node, input_name_to_nodes: Dict, output_name_to_node: Dict):
        # Two patterns are allowed
        # -> Reshape -> Gemm -> Reshape (matched reshape node) -> Add -> Q -> DQ ->
        # (or)
        # -> Reshape -> Gemm -> Q -> DQ -> Reshape (matched reshape node) ->

        qordered_matmul_paths = {
            "path1": (["Gemm", "Reshape", "DequantizeLinear"], [0, 0, 0]),
            "path2": (["DequantizeLinear", "QuantizeLinear", "Gemm", "Reshape", "DequantizeLinear"], [0, 0, 0, 0, 0]),
        }

        qordered_matmul_nodes = None
        is_residual_add_pattern = False

        upstream_dequantize_node = None
        reshape_before_gemm_node = None
        gemm_node = None
        residual_add_node = None
        residual_add_dequantize_node = None
        downstream_quantize_node = None

        for k, v in qordered_matmul_paths.items():
            qordered_matmul_nodes = self.model.match_parent_path(node, v[0], v[1])
            if qordered_matmul_nodes is None:
                continue
            if k == "path1":
                is_residual_add_pattern = True
            break

        if qordered_matmul_nodes is None:
            return

        if is_residual_add_pattern:
            # Reshape -> Add ->
            reshape_children = self.model.get_children(node, input_name_to_nodes)

            if len(reshape_children) != 1 or reshape_children[0].op_type != "Add":
                return

            residual_add_node = reshape_children[0]

            # Reshape -> Add -> Q ->
            residual_add_children = self.model.get_children(residual_add_node, input_name_to_nodes)

            if len(residual_add_children) != 1 or residual_add_children[0].op_type != "QuantizeLinear":
                return

            downstream_quantize_node = residual_add_children[0]

            # DQ (input 1) -> Add ->
            residual_path_id, residual_input_parent_nodes, _ = self.model.match_parent_paths(
                residual_add_node, [(["DequantizeLinear"], [0]),], output_name_to_node
            )               

            if residual_path_id < 0:
                return

            residual_add_dequantize_node = residual_input_parent_nodes[0]

            (gemm_node, reshape_before_gemm_node, upstream_dequantize_node) = qordered_matmul_nodes
        else:
            reshape_children = self.model.get_children(node, input_name_to_nodes)

            if len(reshape_children) != 1 or reshape_children[0].op_type != "QuantizeLinear":
                return

            downstream_quantize_node = reshape_children[0]

            (_, _, gemm_node, reshape_before_gemm_node, upstream_dequantize_node) = qordered_matmul_nodes

        # Make sure the Reshape before the Gemm has the right pattern
        # feeding into the shape input
        reshape_before_gemm_shape_nodes = self.model.match_parent_path(
            reshape_before_gemm_node, ["Concat", "Unsqueeze", "Squeeze", "Slice", "Shape"], [1, 1, 0, 0, 0])

        if reshape_before_gemm_shape_nodes is None:
            return

        # Make sure the second input to the Gemm is fed
        # through a DQ node and is a constant
        second_path_id, second_input_parent_nodes, _ = self.model.match_parent_paths(
            gemm_node, [(["DequantizeLinear"], [1]),], output_name_to_node,
        )

        if second_path_id < 0:
            return

        matmul_weight_dequantize_node = second_input_parent_nodes[0]

        # Check if weight 'B' is a constant
        if self.model.get_constant_value(matmul_weight_dequantize_node.input[0]) is None:
            return

        if not FusionUtils.check_qdq_node_for_fusion(upstream_dequantize_node, self.model):
            return

        if not FusionUtils.check_qdq_node_for_fusion(downstream_quantize_node, self.model):
            return

        # TODO: Uncomment once per-tensor quant is supported
        #if not FusionUtils.check_qdq_node_for_fusion(matmul_weight_dequantize_node, self.model):
        #    return

        if residual_add_dequantize_node is not None and not FusionUtils.check_qdq_node_for_fusion(residual_add_dequantize_node, self.model):
            return

        # Subgraph nodes to be fused
        subgraph_nodes = [node]  # Downstream Reshape
        subgraph_nodes.extend(qordered_matmul_nodes) # DQ + Reshape + Gemm + [Q + DQ+]
        subgraph_nodes.extend(reshape_before_gemm_shape_nodes) # Reshape shape nodes

        if residual_add_node is not None:
            subgraph_nodes.extend([residual_add_node]) # Residual Add

        subgraph_nodes.extend([downstream_quantize_node]) # Q

        if not self.model.is_safe_to_fuse_nodes(
            subgraph_nodes,
            downstream_quantize_node.output,
            input_name_to_nodes,
            output_name_to_node
        ):
            logger.debug(f"It is not safe to fuse QOrderedMatMul node. Skip")
            return     

        # Make inputs
        fused_node_inputs=[upstream_dequantize_node.input[0], upstream_dequantize_node.input[1], 
                matmul_weight_dequantize_node.input[0], matmul_weight_dequantize_node.input[1], 
                downstream_quantize_node.input[1], 
                gemm_node.input[2]]

        if is_residual_add_pattern:
            fused_node_inputs.append(residual_add_dequantize_node.input[0])
            fused_node_inputs.append(residual_add_dequantize_node.input[1])

        # The MatMul weight 'B' and 'bias' need some post-processing

        # Transpose weight 'B' from ROW to COL
        # This offline transpose is needed only while using the CUDA EP
        # TODO: Make this fusion logic EP-agnostic ?
        '''
        weight_tensor = self.model.get_initializer(dequantize_node_1.input[0])
        self.model.transpose_2d_tensor(weight_tensor)

        bias_tensor = self.model.get_initializer(bias_add_node.input[bias_add_node_index])
        self.model.scale_1d_tensor(bias_tensor, y_scale)
        '''

        fused_node = helper.make_node(
            "QOrderedMatMul",
            inputs=fused_node_inputs,
            outputs=[downstream_quantize_node.output[0]],
            name=self.model.create_node_name("QOrderedMatMul", name_prefix="QOrderedMatMul"),
        )

        fused_node.attribute.extend([helper.make_attribute("order_A", 1)])
        fused_node.attribute.extend([helper.make_attribute("order_B", 0)])
        fused_node.attribute.extend([helper.make_attribute("order_Y", 1)])

        fused_node.domain = "com.microsoft"

        self.nodes_to_remove.extend(subgraph_nodes)
        self.nodes_to_add.append(fused_node)
        self.node_name_to_graph_name[fused_node.name] = self.this_graph_name