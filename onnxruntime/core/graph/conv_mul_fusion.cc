// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/graph/initializer.h"
#include "core/graph/conv_mul_fusion.h"

using namespace onnx;
using namespace ::onnxruntime::common;
namespace onnxruntime {

Status ConvMulFusion::Apply(onnxruntime::Graph& graph, bool& modified) const {
  std::vector<onnxruntime::NodeIndex> removed_nodes;
  for (auto& node : graph.Nodes()) {
    if (node.OpType() != "Conv" || node.GetOutputEdgesCount() != 1) {
      continue;
    }

    const Node& next_node = *node.OutputNodesBegin();
    if (next_node.OpType() != "Mul" ||
        next_node.GetInputEdgesCount() != 1 ||
        graph.IsNodeOutputsInGraphOutputs(next_node)) {
      continue;
    }

    auto& conv_node = node;
    const Node& mul_node = next_node;

    const auto& conv_inputs = conv_node.InputDefs();
    const auto& mul_inputs = mul_node.InputDefs();

    const ONNX_NAMESPACE::TensorProto* conv_W_tensor_proto = nullptr;
    graph.GetInitializedTensor(conv_inputs[1]->Name(), conv_W_tensor_proto);

    const ONNX_NAMESPACE::TensorProto* mul_B_tensor_proto = nullptr;
    graph.GetInitializedTensor(mul_inputs[1]->Name(), mul_B_tensor_proto);

    if (!Initializer::IsSupportedDataType(conv_W_tensor_proto) ||
        !Initializer::IsSupportedDataType(mul_B_tensor_proto) ||
        conv_W_tensor_proto->data_type() != mul_B_tensor_proto->data_type() ||
        !(conv_W_tensor_proto->dims_size() > 2 && conv_W_tensor_proto->dims(0) == mul_B_tensor_proto->dims(0))) {
      continue;
    }

    auto conv_W = std::make_unique<Initializer>(conv_W_tensor_proto);
    auto mul_B = std::make_unique<Initializer>(mul_B_tensor_proto);

    if (conv_W->data_type() != mul_B->data_type() ||
        !(conv_W->dims().size() > 2 && conv_W->dims()[0] == mul_B->dims()[0])) {
      continue;
    }

    const ONNX_NAMESPACE::TensorProto* conv_B_tensor_proto = nullptr;
    std::unique_ptr<Initializer> conv_B = nullptr;
    if (conv_inputs.size() == 3) {
      if (!graph.GetInitializedTensor(conv_inputs[2]->Name(), conv_B_tensor_proto))
        continue;

      if (!Initializer::IsSupportedDataType(conv_B_tensor_proto) ||
          conv_B_tensor_proto->data_type() != mul_B_tensor_proto->data_type() ||
          conv_B_tensor_proto->dims_size() != 1 || mul_B_tensor_proto->dims_size() != 3 ||
          conv_B_tensor_proto->dims(0) != mul_B_tensor_proto->dims(0)) {
        continue;
      }
      conv_B = std::make_unique<Initializer>(conv_B_tensor_proto);
    }

    // Calculate new value of initializers of conv node
    conv_W->scale_by_axis(*mul_B, 1);
    if (conv_inputs.size() == 3) {
      conv_B->mul(*mul_B);
    }

    // Create new initializers of conv
    ONNX_NAMESPACE::TensorProto new_conv_W_tensor_proto(*conv_W_tensor_proto);
    conv_W->ToProto(&new_conv_W_tensor_proto);

    // Replace initializers of conv node
    graph.RemoveInitializedTensor(conv_inputs[1]->Name());
    graph.AddInitializedTensor(new_conv_W_tensor_proto);

    if (conv_inputs.size() == 3) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6011)  // Not deferencing null pointer. conv_B_tensor_proto is set on line 55
#endif
      ONNX_NAMESPACE::TensorProto new_conv_B_tensor_proto(*conv_B_tensor_proto);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
      conv_B->ToProto(&new_conv_B_tensor_proto);
      graph.RemoveInitializedTensor(conv_inputs[2]->Name());
      graph.AddInitializedTensor(new_conv_B_tensor_proto);
    }

    // Replace the input of the node following mul node
    const NodeArg* mul_output_def = mul_node.OutputDefs()[0];
    NodeArg* conv_output_def = conv_node.MutableOutputDefs()[0];
    for (auto it = mul_node.OutputNodesBegin(); it != mul_node.OutputNodesEnd(); ++it) {
      auto output_node = graph.GetNode((*it).Index());
      if (!output_node) {
        return Status(ONNXRUNTIME, INVALID_ARGUMENT);
      }

      auto& input_defs = output_node->MutableInputDefs();
      for (auto& def : input_defs) {
        if (def == mul_output_def) {
          def = conv_output_def;
        }
      }
    }

    removed_nodes.push_back(mul_node.Index());
  }

  for (auto i : removed_nodes) {
    graph.RemoveNode(i);
  }

  if (!removed_nodes.empty()) {
    modified = true;
    ONNXRUNTIME_RETURN_IF_ERROR(graph.Resolve());
  }
  return Status::OK();
}

}  // namespace onnxruntime
