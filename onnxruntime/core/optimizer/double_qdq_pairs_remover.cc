// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#include "core/optimizer/double_qdq_pairs_remover.h"

#include "core/graph/graph_utils.h"
#include "core/optimizer/initializer.h"
#include "core/optimizer/utils.h"

namespace onnxruntime {

Status DoubleQDQPairsRemover::ApplyImpl(
    Graph& graph,
    bool& modified,
    int /*graph_level*/,
    const logging::Logger& /*logger*/) const {
  const GraphViewer graph_viewer(graph);
  const auto& node_topology_list = graph_viewer.GetNodesInTopologicalOrder();

  for (const auto& self_index : node_topology_list) {
    NodeIndex parent_index = 0;
    NodeIndex child_index = 0;
    NodeIndex grandchild_index = 0;
    if (IsNodeRemovable(graph, self_index, parent_index, child_index, grandchild_index)) {
      graph.RemoveEdge(self_index, child_index, 0, 0);
      graph.RemoveEdge(child_index, grandchild_index, 0, 0);
      graph_utils::ReplaceDownstreamNodeInput(graph, *graph.GetNode(grandchild_index), 0, *graph.GetNode(self_index), 0);
      graph.RemoveNode(child_index);
      graph.RemoveNode(grandchild_index);

      modified = true;
    }
  }
  return Status::OK();
}

bool DoubleQDQPairsRemover::IsNodeRemovable(
    Graph& graph,
    const NodeIndex& self_index,
    NodeIndex& parent_index,
    NodeIndex& child_index,
    NodeIndex& grandchild_index) {
  // Check if the self is a DQ self
  Node* self = graph.GetNode(self_index);
  if (self == nullptr ||
      self->OpType() != "DequantizeLinear" ||
      self->GetInputEdgesCount() != 1 ||
      self->GetOutputEdgesCount() != 1) {
    return false;
  }

  // parent should be a Q self, and have only one perent
  parent_index = self->InputEdgesBegin()->GetNode().Index();
  Node* parent = graph.GetNode(parent_index);
  if (parent == nullptr ||
      parent->OpType() != "QuantizeLinear") {
    return false;
  }

  // child should be a Q self, and have only one child
  child_index = self->OutputEdgesBegin()->GetNode().Index();
  const Node* child = graph.GetNode(child_index);
  if (child == nullptr ||
      child->OpType() != "QuantizeLinear" ||
      child->GetOutputEdgesCount() != 1 ||
      graph.NodeProducesGraphOutput(*child)) {
    return false;
  }

  // grandchild should be a DQ self, and have only one grandchild
  grandchild_index = child->OutputEdgesBegin()->GetNode().Index();
  Node* grandchild = graph.GetNode(grandchild_index);
  if (grandchild == nullptr ||
      grandchild->OpType() != "DequantizeLinear" ||
      graph.NodeProducesGraphOutput(*grandchild)) {
    return false;
  }

  float new_scale = 0.0f;
  int new_zero_point = 0;
  TensorProto_DataType type = ONNX_NAMESPACE::TensorProto_DataType_INT8;

  if (!FindNewZeroPointAndScale(graph, *self, *grandchild, new_scale, new_zero_point, type)) {
    return false;
  }
  ApplyNewInputValue(graph, *self, InputIndex::SCALE_ID, new_scale);
  ApplyNewInputValue(graph, *parent, InputIndex::SCALE_ID, new_scale);
  if (type == ONNX_NAMESPACE::TensorProto_DataType_INT8) {
    ApplyNewInputValue(graph, *self, InputIndex::ZERO_POINT_ID, gsl::narrow_cast<int8_t>(new_zero_point));
    ApplyNewInputValue(graph, *parent, InputIndex::ZERO_POINT_ID, gsl::narrow_cast<int8_t>(new_zero_point));
  } else {
    ApplyNewInputValue(graph, *self, InputIndex::ZERO_POINT_ID, gsl::narrow_cast<uint8_t>(new_zero_point));
    ApplyNewInputValue(graph, *parent, InputIndex::ZERO_POINT_ID, gsl::narrow_cast<uint8_t>(new_zero_point));
  }
  return true;
}

bool DoubleQDQPairsRemover::FindNewZeroPointAndScale(const Graph& graph, const Node& node1, const Node& node2, float& new_scale, int& new_zero_point, TensorProto_DataType& type) {
  if (node1.InputDefs().size() != InputIndex::TOTAL_COUNT ||
      node2.InputDefs().size() != InputIndex::TOTAL_COUNT ||
      !optimizer_utils::IsScalar(*node1.InputDefs()[InputIndex::SCALE_ID]) ||
      !optimizer_utils::IsScalar(*node1.InputDefs()[InputIndex::ZERO_POINT_ID]) ||
      !optimizer_utils::IsScalar(*node2.InputDefs()[InputIndex::SCALE_ID]) ||
      !optimizer_utils::IsScalar(*node2.InputDefs()[InputIndex::ZERO_POINT_ID])) {
    return false;
  }
  // if Q/DQ scale and zero point are not constant, return false
  const ONNX_NAMESPACE::TensorProto* node1_scale_tensor_proto =
      graph_utils::GetConstantInitializer(graph, node1.InputDefs()[InputIndex::SCALE_ID]->Name());
  const ONNX_NAMESPACE::TensorProto* node2_scale_tensor_proto =
      graph_utils::GetConstantInitializer(graph, node2.InputDefs()[InputIndex::SCALE_ID]->Name());
  const ONNX_NAMESPACE::TensorProto* node1_zp_tensor_proto =
      graph_utils::GetConstantInitializer(graph, node1.InputDefs()[InputIndex::ZERO_POINT_ID]->Name());
  const ONNX_NAMESPACE::TensorProto* node2_zp_tensor_proto =
      graph_utils::GetConstantInitializer(graph, node2.InputDefs()[InputIndex::ZERO_POINT_ID]->Name());
  if (nullptr == node2_zp_tensor_proto ||
      nullptr == node1_zp_tensor_proto ||
      nullptr == node2_scale_tensor_proto ||
      nullptr == node1_scale_tensor_proto) {
    return false;
  }
  Initializer zero_point_init_1{*node1_zp_tensor_proto, graph.ModelPath()};
  Initializer zero_point_init_2{*node2_zp_tensor_proto, graph.ModelPath()};
  if (zero_point_init_1.data_type() != zero_point_init_2.data_type()) {
    return false;
  }

  Initializer scale_init_1{*node1_scale_tensor_proto, graph.ModelPath()};
  Initializer scale_init_2{*node2_scale_tensor_proto, graph.ModelPath()};
  if (scale_init_1.data_type() != ONNX_NAMESPACE::TensorProto_DataType_FLOAT ||
      scale_init_2.data_type() != ONNX_NAMESPACE::TensorProto_DataType_FLOAT) {
    return false;
  }
  const float scale_1 = scale_init_1.data<float>()[0];
  const float scale_2 = scale_init_2.data<float>()[0];

  float real_max1 = 0.0;
  float real_min1 = 0.0;
  float real_max2 = 0.0;
  float real_min2 = 0.0;
  int zero_point_1 = 0;
  int zero_point_2 = 0;
  if (zero_point_init_1.data_type() == ONNX_NAMESPACE::TensorProto_DataType_UINT8) {
    zero_point_1 = zero_point_init_1.data<uint8_t>()[0];
  } else if (zero_point_init_1.data_type() == ONNX_NAMESPACE::TensorProto_DataType_INT8) {
    zero_point_1 = zero_point_init_1.data<int8_t>()[0];
  } else {
    return false;
  }
  if (zero_point_init_2.data_type() == ONNX_NAMESPACE::TensorProto_DataType_UINT8) {
    zero_point_2 = zero_point_init_2.data<uint8_t>()[0];
  } else if (zero_point_init_2.data_type() == ONNX_NAMESPACE::TensorProto_DataType_INT8) {
    zero_point_2 = zero_point_init_2.data<int8_t>()[0];
  } else {
    return false;
  }
  const int q_min_1 = (zero_point_init_1.data_type() == ONNX_NAMESPACE::TensorProto_DataType_UINT8) ? std::numeric_limits<uint8_t>::min() : std::numeric_limits<int8_t>::min();
  const int q_max_1 = (zero_point_init_1.data_type() == ONNX_NAMESPACE::TensorProto_DataType_UINT8) ? std::numeric_limits<uint8_t>::max() : std::numeric_limits<int8_t>::max();
  const int q_min_2 = (zero_point_init_2.data_type() == ONNX_NAMESPACE::TensorProto_DataType_UINT8) ? std::numeric_limits<uint8_t>::min() : std::numeric_limits<int8_t>::min();
  const int q_max_2 = (zero_point_init_2.data_type() == ONNX_NAMESPACE::TensorProto_DataType_UINT8) ? std::numeric_limits<uint8_t>::max() : std::numeric_limits<int8_t>::max();

  real_min1 = q_min_1 - zero_point_1 * scale_1;
  real_min2 = q_min_2 - zero_point_2 * scale_2;
  real_max1 = (q_max_1 - q_min_1 + zero_point_1) * scale_1 - q_min_1;
  real_max2 = (q_max_2 - q_min_2 + zero_point_2) * scale_2 - q_min_1;

  const float real_min = std::max(real_min1, real_min2);
  const float real_max = std::min(real_max1, real_max2);
  new_scale = (real_max - real_min) / (q_max_1 - q_min_1);
  new_zero_point = (q_min_1 - real_min) / new_scale;
  type = static_cast<TensorProto_DataType>(zero_point_init_1.data_type());
  return true;
}

template <typename T>
void DoubleQDQPairsRemover::ApplyNewInputValue(Graph& graph, Node& node, const InputIndex& index, T value) {
  const auto* input_tensor = graph_utils::GetConstantInitializer(graph, node.InputDefs()[index]->Name());
  Initializer input_init{*input_tensor, graph.ModelPath()};
  TensorProto new_input_tensor(*input_tensor);
  input_init.data<T>()[0] = value;
  input_init.ToProto(new_input_tensor);
  auto new_name = graph.GenerateNodeArgName("DoubleQDQRemoved_" + node.InputDefs()[index]->Name());
  new_input_tensor.set_name(new_name);
  NodeArg& new_input = graph_utils::AddInitializer(graph, new_input_tensor);
  graph_utils::ReplaceNodeInput(node, index, new_input);
}
}  // namespace onnxruntime
