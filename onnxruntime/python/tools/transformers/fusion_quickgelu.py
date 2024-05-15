# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT License.
# --------------------------------------------------------------------------

from fusion_base import Fusion
from onnx import helper
from onnx_model import OnnxModel

import logging

logger = logging.getLogger(__name__)


class FusionQuickGelu(Fusion):
    def __init__(self, model: OnnxModel):
        super().__init__(model, "QuickGelu", ["MatMul"])

    def fuse(self, node, input_name_to_nodes, output_name_to_node):
        # Fuse the following subgraph to `QuickGelu`
        #
        #       root_input
        #      /          \
        #     |           Mul       ----+
        #     |       (B = ~1.702)      |
        #      \           |            |
        #       \       Sigmoid         |---- `QuickGelu`
        #        \     /                |
        #         \   /                 |
        #          Mul              ----+
        #           |
        #         MatMul            [node]

        second_mul_node = self.model.match_parent_path(node, ["Mul"], [0])
        if second_mul_node is None:
            logger.debug("fuse_quickgelu: failed to match second Mul node")
            return
        second_mul_node = second_mul_node[0]
        
        root_input = None
        root_input_1 = self.model.match_parent_path(second_mul_node, ["Add"], [0])
        root_input_2 = self.model.match_parent_path(second_mul_node, ["MatMul"], [0])
        if root_input_1 is not None:
            root_input = root_input_1[0].output[0]
        elif root_input_2 is not None:
            root_input = root_input_2[0].output[0]
        else:
            logger.debug("fuse_quickgelu: failed to match root input")
            return

        sigmoid_node = self.model.match_parent_path(second_mul_node, ["Sigmoid"], [1])
        if sigmoid_node is None:
            logger.debug("fuse_quickgelu: failed to match Sigmoid node")
            return
        sigmoid_node = sigmoid_node[0]
        
        first_mul_node = self.model.match_parent_path(sigmoid_node, ["Mul"], [0])
        if first_mul_node is None:
            logger.debug("fuse_quickgelu: failed to match first Mul node")
            return
        first_mul_node = first_mul_node[0]

        approximation_value = self.model.get_constant_value(first_mul_node.input[1]).item()
        if approximation_value != 1.7021484375:
            logger.debug("fuse_quickgelu: failed to match approximation value")
            return

        if first_mul_node.input[0] != root_input:
            logger.debug("fuse_quickgelu: failed to match root input with first Mul node's input")
            return

        new_node = helper.make_node(
            "QuickGelu",
            inputs=[root_input],
            outputs=[second_mul_node.output[0]],
            name=self.model.create_node_name("QuickGelu"),
        )
        new_node.domain = "com.microsoft"
        new_node.attribute.extend([helper.make_attribute("alpha", approximation_value)])

        self.nodes_to_remove.extend([first_mul_node, sigmoid_node, second_mul_node])
        self.nodes_to_add.append(new_node)
        self.node_name_to_graph_name[new_node.name] = self.this_graph_name
        self.increase_counter("QuickGelu")
