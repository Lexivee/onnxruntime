#include "core/providers/qnn/builder/qnn_node_group/hardsigmoid_mul_fusion.h"

#include <algorithm>
#include <cassert>
#include <gsl/gsl>
#include <limits>
#include <optional>
#include "core/graph/graph_utils.h"
#include "core/optimizer/qdq_transformer/qdq_util.h"
#include "core/framework/node_unit.h"
#include "core/providers/shared/utils/utils.h"
#include "core/providers/qnn/builder/qnn_utils.h"
#include "core/providers/qnn/builder/op_builder_factory.h"
#include "core/providers/qnn/builder/qnn_node_group/utils.h"

namespace onnxruntime {
namespace qnn {

static Status QnnHardSigmoidMulFusionAdd(QnnModelWrapper& qnn_model_wrapper,
                                         const NodeUnit& hardsigmoid_node_unit,
                                         const NodeUnit& mul_node_unit,
                                         const logging::Logger& logger,
                                         bool validate = false) {
  ORT_UNUSED_PARAMETER(logger);
  assert(hardsigmoid_node_unit.OpType() == "HardSigmoid" && mul_node_unit.OpType() == "Mul");
  const auto& node_name = utils::GetNodeName(hardsigmoid_node_unit);
  const NodeUnitIODef& input_def = hardsigmoid_node_unit.Inputs()[0];
  const NodeUnitIODef& output_def = mul_node_unit.Outputs()[0];

  QnnTensorWrapper input_tensor;
  QnnTensorWrapper output_tensor;

  ORT_RETURN_IF_ERROR(qnn_model_wrapper.MakeTensorWrapper(input_def, input_tensor));
  ORT_RETURN_IF_ERROR(qnn_model_wrapper.MakeTensorWrapper(output_def, output_tensor));

  if (validate) {
    ORT_RETURN_IF_ERROR(qnn_model_wrapper.ValidateQnnNode(node_name,
                                                          QNN_OP_PACKAGE_NAME_QTI_AISW,
                                                          QNN_OP_HARD_SWISH,
                                                          {input_tensor.GetQnnTensor()},
                                                          {output_tensor.GetQnnTensor()},
                                                          {}));
  } else {
    LOGS(logger, VERBOSE) << " Adding QNN HardSwish via fusion. HardSigmoid name: [" << hardsigmoid_node_unit.Name()
                          << "] Mul name: [" << mul_node_unit.Name() << "]";

    ORT_RETURN_IF_NOT(qnn_model_wrapper.AddTensorWrapper(std::move(input_tensor)), "Failed to add input");
    ORT_RETURN_IF_NOT(qnn_model_wrapper.AddTensorWrapper(std::move(output_tensor)), "Failed to add output");
    ORT_RETURN_IF_NOT(qnn_model_wrapper.CreateQnnNode(node_name,
                                                      QNN_OP_PACKAGE_NAME_QTI_AISW,
                                                      QNN_OP_HARD_SWISH,
                                                      {input_def.node_arg.Name()},
                                                      {output_def.node_arg.Name()},
                                                      {},
                                                      validate),
                      "Failed to add fused HardSwish node.");
  }

  return Status::OK();
}

std::unique_ptr<IQnnNodeGroup> TryHardSigmoidMulFusion(
    QnnModelWrapper& qnn_model_wrapper,
    const NodeUnit& hardsigmoid_node_unit,
    const std::unordered_map<const Node*, const NodeUnit*>& node_to_node_unit,
    const std::unordered_map<const NodeUnit*, const IQnnNodeGroup*>& node_unit_to_qnn_node_group,
    const logging::Logger& logger) {
  // Looking for a standalone HardSigmoid to start the sequence.
  if (hardsigmoid_node_unit.OpType() != "HardSigmoid" ||
      hardsigmoid_node_unit.UnitType() != NodeUnit::Type::SingleNode) {
    return nullptr;
  }

  NodeAttrHelper hs_attr_helper(hardsigmoid_node_unit);
  float alpha = hs_attr_helper.Get("alpha", 0.2f);
  float beta = hs_attr_helper.Get("beta", 0.5f);
  constexpr float req_alpha = 1.0f / 6.0f;
  constexpr float req_beta = 0.5f;
  constexpr float alpha_eps = std::numeric_limits<float>::epsilon() * req_alpha;
  constexpr float beta_eps = std::numeric_limits<float>::epsilon() * req_beta;

  // Check for explicit values of alpha and beta.
  if (std::abs(alpha - req_alpha) > alpha_eps || std::abs(beta - req_beta) > beta_eps) {
    return nullptr;
  }

  // HardSigmoid must have a single Mul child (1 output edge) and must not produce a graph output.
  const GraphViewer& graph_viewer = qnn_model_wrapper.GetGraphViewer();
  const std::array<std::string_view, 1> child_types = {"Mul"};
  const NodeUnit* mul_node_unit = GetOnlyChildOfType(graph_viewer, hardsigmoid_node_unit, child_types,
                                                     node_to_node_unit, node_unit_to_qnn_node_group);

  if (mul_node_unit == nullptr) {
    return nullptr;
  }

  // Input to HardSigmoid must also be the other input to the Mul.
  const Node& mul_node = mul_node_unit->GetNode();
  auto& hs_input_name = hardsigmoid_node_unit.Inputs()[0].node_arg.Name();
  const bool same_root_input = mul_node.InputDefs()[0]->Name() == hs_input_name ||
                               mul_node.InputDefs()[1]->Name() == hs_input_name;

  if (!same_root_input) {
    return nullptr;
  }

  if (Status status = QnnHardSigmoidMulFusionAdd(qnn_model_wrapper, hardsigmoid_node_unit, *mul_node_unit,
                                                 logger, /*validate*/ true);
      !status.IsOK()) {
    return nullptr;
  }

  return std::make_unique<hs_mul_fusion::QnnNodeGroup>(hardsigmoid_node_unit, *mul_node_unit);
}

namespace hs_mul_fusion {

QnnNodeGroup::QnnNodeGroup(const NodeUnit& hardsigmoid_node_unit, const NodeUnit& mul_node_unit)
    : hardsigmoid_node_unit_(hardsigmoid_node_unit), mul_node_unit_(mul_node_unit) {
}

Status QnnNodeGroup::IsSupported(QnnModelWrapper& qmw, const logging::Logger& logger) const {
  return QnnHardSigmoidMulFusionAdd(qmw, hardsigmoid_node_unit_, mul_node_unit_, logger, /*validate*/ true);
}

Status QnnNodeGroup::AddToModelBuilder(QnnModelWrapper& qmw, const logging::Logger& logger) const {
  return QnnHardSigmoidMulFusionAdd(qmw, hardsigmoid_node_unit_, mul_node_unit_, logger, /*validate*/ false);
}

std::vector<const NodeUnit*> QnnNodeGroup::GetNodeUnits() const {
  return std::vector<const NodeUnit*>{&hardsigmoid_node_unit_, &mul_node_unit_};
}

const NodeUnit* QnnNodeGroup::GetTargetNodeUnit() const {
  return &hardsigmoid_node_unit_;
}

}  // namespace hs_mul_fusion
}  // namespace qnn
}  // namespace onnxruntime
