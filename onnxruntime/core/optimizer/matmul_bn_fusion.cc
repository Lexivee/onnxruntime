// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/optimizer/matmul_bn_fusion.h"
#include "core/graph/graph_utils.h"
#include "core/optimizer/initializer.h"
#include "core/optimizer/utils.h"

namespace onnxruntime {
bool MatchPath(const Node& parent_node,
               const gsl::span<std::pair<std::string, InlinedVector<ONNX_NAMESPACE::OperatorSetVersion>>>& path,
               const Node& child_node) {
  if (path.size() == 0) {
    return true;
  }

  if (!graph_utils::IsSupportedOptypeVersionAndDomain(child_node, path[0].first, path[0].second) ||
      child_node.GetExecutionProviderType() != parent_node.GetExecutionProviderType()) {
    return false;
  }

  /*
   * last node in the path can have more than one output
   * because all those outputs will be preserved by the addition of new Gemm node
   */
  if (path.size() > 1 && child_node.GetOutputEdgesCount() != 1) {
    return false;
  }

  return MatchPath(child_node, path.subspan(1), *child_node.OutputNodesBegin());
}

/*
 *   Given a MatMul node, it will verify the following pattern.
 *                MatMul
 *                  |
 *               Reshape
 *                  |
 *             Transpose
 *                  |
 *        BatchNormalization
 * Other Conditions:
 *   - B tensor of MatMul should be constant.
 *   - scale, B, mean, var tensors of BatchNormalization should be constant.
 *   - Every node in the path except first and last node, should have only 1 output edge.
 */
bool MatmulBNFusion::SatisfyCondition(const Graph& graph, const Node& node, const logging::Logger&) const {
  if (!graph_utils::IsSupportedOptypeVersionAndDomain(node, "MatMul", {1, 9, 13}) ||
      node.GetOutputEdgesCount() != 1) {
    return false;
  }

  const Node& child_node = *node.OutputNodesBegin();

  std::vector<std::pair<std::string, InlinedVector<ONNX_NAMESPACE::OperatorSetVersion>>> path{
      {"Reshape", {1, 5}},
      {"Transpose", {1}},
      {"BatchNormalization", {1, 6, 7}}};

  if (!MatchPath(node, path, child_node)) {
    return false;
  }

  const auto& batch_norm_node = *child_node.OutputNodesBegin()->OutputNodesBegin();

  // Check that the appropriate inputs to the Matmul and BN nodes are constants.
  if (!graph_utils::NodeArgIsConstant(graph, *node.InputDefs()[1]) ||
      !graph_utils::NodeArgIsConstant(graph, *batch_norm_node.InputDefs()[1]) ||
      !graph_utils::NodeArgIsConstant(graph, *batch_norm_node.InputDefs()[2]) ||
      !graph_utils::NodeArgIsConstant(graph, *batch_norm_node.InputDefs()[3]) ||
      !graph_utils::NodeArgIsConstant(graph, *batch_norm_node.InputDefs()[4])) {
    return false;
  }

  // First output from BN is required. Others are optional. If any optional outputs exist we can't fuse.
  const auto& output_defs = batch_norm_node.OutputDefs();
  if (output_defs.size() > 1) {
    for (size_t i = 1, end = output_defs.size(); i < end; ++i) {
      if (output_defs[i] != nullptr && output_defs[i]->Exists()) {
        return false;
      }
    }
  }

  if (graph.NodeProducesGraphOutput(node)) {
    return false;
  }

  return true;
}

/*
 * BatchNormalization: [https://learn.microsoft.com/en-us/windows/win32/api/directml/ns-directml-dml_batch_normalization_operator_desc]
 *   Scale * ((Input - Mean) / sqrt(Variance + Epsilon)) + Bias // ignore the FusedActivation in the above definition, that's very specific to DML
 * Expanding out the terms:
 *   Output = (Scale / sqrt(Variance + Epsilon)) * Input + (Scale / sqrt(Variance + Epsilon)) * -Mean + Bias
 * Here,
 *   [Scale/sqrt(Variance + Epsilon)] is constant, and let's call it `alpha`
 *   [(Scale / sqrt(Variance + Epsilon)) * -Mean + Bias] is also constant, and let's call it `beta`
 * Output = alpha * Input + beta, Input = B tensor of MatMul.
 *
 */
Status MatmulBNFusion::Apply(Graph& graph, Node& matmul_node, RewriteRuleEffect& rule_effect, const logging::Logger&) const {
  const Node& child_node = *matmul_node.OutputNodesBegin();
  NodeIndex batch_norm_node_index = child_node.OutputNodesBegin()->OutputNodesBegin()->Index();
  Node& batch_norm_node = *graph.GetNode(batch_norm_node_index);

  // only perform fusion if epsilon is present and is of float_32 type
  auto epsilon_attribute = batch_norm_node.GetAttributes().find("epsilon");
  if (epsilon_attribute == batch_norm_node.GetAttributes().end() ||
      epsilon_attribute->second.type() != ONNX_NAMESPACE::AttributeProto_AttributeType_FLOAT) {
    return Status::OK();
  }
  const float epsilon = epsilon_attribute->second.f();

  const onnx::TensorProto* scale_tensor = graph_utils::GetConstantInitializer(graph, batch_norm_node.InputDefs()[1]->Name());
  ORT_ENFORCE(scale_tensor);
  const onnx::TensorProto* bias_tensor = graph_utils::GetConstantInitializer(graph, batch_norm_node.InputDefs()[2]->Name());
  ORT_ENFORCE(bias_tensor);
  const onnx::TensorProto* mean_tensor = graph_utils::GetConstantInitializer(graph, batch_norm_node.InputDefs()[3]->Name());
  ORT_ENFORCE(mean_tensor);
  const onnx::TensorProto* var_tensor = graph_utils::GetConstantInitializer(graph, batch_norm_node.InputDefs()[4]->Name());
  ORT_ENFORCE(var_tensor);
  const onnx::TensorProto* matmul_b_tensor = graph_utils::GetConstantInitializer(graph, matmul_node.InputDefs()[1]->Name());
  ORT_ENFORCE(matmul_b_tensor);

  if (!optimizer_utils::IsFloatingPointDataType(*matmul_b_tensor) ||
      !optimizer_utils::IsFloatingPointDataType(*scale_tensor) ||
      !optimizer_utils::IsFloatingPointDataType(*bias_tensor) ||
      !optimizer_utils::IsFloatingPointDataType(*mean_tensor) ||
      !optimizer_utils::IsFloatingPointDataType(*var_tensor) ||
      scale_tensor->dims_size() != 1 ||
      bias_tensor->dims_size() != 1 ||
      mean_tensor->dims_size() != 1 ||
      var_tensor->dims_size() != 1 ||
      scale_tensor->dims(0) != matmul_b_tensor->dims(1) ||
      bias_tensor->dims(0) != matmul_b_tensor->dims(1) ||
      mean_tensor->dims(0) != matmul_b_tensor->dims(1) ||
      var_tensor->dims(0) != matmul_b_tensor->dims(1)) {
    return Status::OK();
  }

  /*
   * temp = scale / sqrt(var + epsilon)
   * output = (temp * Input) - ((temp * mean) + bias)
   */
  Initializer scale(*scale_tensor, graph.ModelPath());
  Initializer bias(*bias_tensor, graph.ModelPath());
  Initializer mean(*mean_tensor, graph.ModelPath());
  Initializer var(*var_tensor, graph.ModelPath());
  Initializer matmul_b(*matmul_b_tensor, graph.ModelPath());

  var.add(epsilon);
  var.sqrt();
  scale.div(var);  // this is the temp
  matmul_b.scale_by_axis(scale, 1, true);

  mean.mul(scale);
  bias.sub(mean);

  // create B tensorProto for new Gemm node from <matmulB> initializer.
  ONNX_NAMESPACE::TensorProto new_gemm_b_tensor(*matmul_b_tensor);
  matmul_b.ToProto(new_gemm_b_tensor);
  const std::string new_gemm_b_name = graph.GenerateNodeArgName("MatMulBnFusion_GemmB_" + matmul_b_tensor->name());
  new_gemm_b_tensor.set_name(new_gemm_b_name);
  NodeArg& new_gemm_b_node_arg = graph_utils::AddInitializer(graph, new_gemm_b_tensor);

  // create bias tensorProto for new Gemm node from <bias> initializer.
  ONNX_NAMESPACE::TensorProto new_gemm_bias_tensor(*bias_tensor);
  bias.ToProto(new_gemm_bias_tensor);
  const std::string new_gemm_bias_name = graph.GenerateNodeArgName("MatMulBnFusion_GemmBias");
  new_gemm_bias_tensor.set_name(new_gemm_bias_name);
  NodeArg& new_gemm_bias_node_arg = graph_utils::AddInitializer(graph, new_gemm_bias_tensor);

  graph.AddNode(
      graph.GenerateNodeArgName("MatMulBnFusion_Gemm"),
      "Gemm",
      "Generated from Matmul BatchNormalization fusion",
      {matmul_node.MutableInputDefs()[0], &new_gemm_b_node_arg, &new_gemm_bias_node_arg},
      matmul_node.MutableOutputDefs(),
      nullptr,
      kOnnxDomain);

  // Remove MatMul node.
  Node* node = graph.GetNode(matmul_node.Index());
  graph_utils::RemoveNodeOutputEdges(graph, *node);
  graph.RemoveNode(matmul_node.Index());

  // Delete BatchNormalization node and update the input of the child of BatchNormalization
  graph_utils::FinalizeNodeFusion(graph, *graph.GetNode(child_node.OutputNodesBegin()->Index()), batch_norm_node);

  rule_effect = RewriteRuleEffect::kRemovedCurrentNode;
  return Status::OK();
}
}  // namespace onnxruntime