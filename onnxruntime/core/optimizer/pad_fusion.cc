// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/optimizer/pad_fusion.h"
#include "core/graph/graph_utils.h"
#include "core/optimizer/initializer.h"
#include "core/optimizer/utils.h"

namespace onnxruntime {

/*
 * It matches following pattern:
 *     Pad
 *      |
 *   Conv/MaxPool
 */
bool PadFusion::SatisfyCondition(const Graph& graph, const Node& node, const logging::Logger&) const {
  // if Pad has input axis, don't fuse it.
  if (!graph_utils::IsSupportedOptypeVersionAndDomain(node, "Pad", {1, 2, 11, 13, 18, 19}) ||
      node.GetOutputEdgesCount() != 1 ||
      node.InputDefs().size() > 3) {
    return false;
  }

  if (graph.NodeProducesGraphOutput(node)) {
    return false;
  }

  const NodeAttributes& pad_attributes = node.GetAttributes();
  if (pad_attributes.find("mode") != pad_attributes.end() &&
      pad_attributes.at("mode").s() != "constant") {
    return false;
  }

  // constant_value should be an initializer because we have to verify the constant_value should be zero.
  // It is because Conv and MaxPool allow only 0 as padding value.
  if (node.InputDefs().size() > 2 && !graph_utils::NodeArgIsConstant(graph, *node.InputDefs()[2])) {
    return false;
  }

  // Since opset 11, pad constant value becomes part of input instead of attribute.
  if (node.InputDefs().size() > 2) {
    const auto* pad_constant_value_proto = graph_utils::GetConstantInitializer(graph, node.InputDefs()[2]->Name());
    Initializer pad_constant_value{*pad_constant_value_proto, graph.ModelPath()};
    if (std::any_of(pad_constant_value.DataAsByteSpan().begin(), pad_constant_value.DataAsByteSpan().end(), [](const uint8_t byte) { return byte != 0; })) {
      return false;
    }
  } else {
    if (pad_attributes.find("value") != pad_attributes.end() &&
        pad_attributes.at("value").f() != 0.0) {
      return false;
    }
  }

  const Node& child_node = *node.OutputNodesBegin();
  if (!graph_utils::IsSupportedOptypeVersionAndDomain(child_node, "Conv", {1, 11}) &&
      !graph_utils::IsSupportedOptypeVersionAndDomain(child_node, "MaxPool", {1, 8, 10, 11, 12})) {
    return false;
  }
  return true;
}

/*
 * - For 1st two dimension Pads array's value should be zero and for rest of them values should >= 0
 */
Status PadFusion::Apply(Graph& graph, Node& pad_node, RewriteRuleEffect& rule_effect, const logging::Logger&) const {
  std::vector<int64_t> pads_values;

  if (pad_node.SinceVersion() >= 11) {
    const auto* pads_proto = graph_utils::GetConstantInitializer(graph, pad_node.InputDefs()[1]->Name());
    Initializer pads{*pads_proto, graph.ModelPath()};
    pads_values.assign(pads.DataAsSpan<int64_t>().begin(), pads.DataAsSpan<int64_t>().end());
  } else {
    pads_values.assign(pad_node.GetAttributes().at("pads").ints().begin(), pad_node.GetAttributes().at("pads").ints().end());
  }

  uint32_t pads_size = static_cast<uint32_t>(pads_values.size());
  // check if padding is applied only on feature dims
  if (pads_values[0] != 0 || pads_values[1] != 0 || pads_values[pads_size / 2] != 0 ||
      pads_values[pads_size / 2 + 1] != 0) {
    return Status::OK();
  }

  // check if padding is only positive
  if (std::any_of(pads_values.begin(), pads_values.end(), [](int64_t value) { return value < 0; })) {
    return Status::OK();
  }

  Node& child_node = *graph.GetNode(pad_node.OutputNodesBegin()->Index());
  auto child_pads = child_node.GetMutableAttributes()["pads"].mutable_ints();
  uint32_t child_pads_size = static_cast<uint32_t>(child_pads->size());

  for (uint32_t pads_index = 2, child_index = 0; pads_index < pads_size / 2; pads_index++, child_index++) {
    child_pads->Set(child_index, child_pads->Get(child_index) + pads_values[pads_index]);
    child_pads->Set(child_index + (child_pads_size / 2), child_pads->Get(child_index + (child_pads_size / 2)) + pads_values[pads_index + (pads_size / 2)]);
  }

  graph_utils::RemoveNodeOutputEdges(graph, pad_node);
  graph_utils::ReplaceNodeInput(child_node, 0, *pad_node.MutableInputDefs()[0]);
  graph.RemoveNode(pad_node.Index());
  rule_effect = RewriteRuleEffect::kRemovedCurrentNode;
  return Status::OK();
}
}  // namespace onnxruntime