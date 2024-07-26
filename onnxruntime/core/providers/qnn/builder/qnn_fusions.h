// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/framework/node_unit.h"
#include "core/providers/qnn/builder/qnn_model_wrapper.h"

namespace onnxruntime {
namespace qnn {

struct QnnNodeGroup {
  using IndexType = size_t;
  enum class Type : uint8_t {
    Undefined = 0,
    NodeUnit,
    ConvActivationFusion,
    DQQFusion,
    HardSigmoidMulFusion,
    COUNT,
  };

  static std::string_view TypeToString(QnnNodeGroup::Type type);

  Status IsSupported(QnnModelWrapper& qmw, const logging::Logger& logger) const;
  Status AddToModelBuilder(QnnModelWrapper& qmw, const logging::Logger& logger) const;
  const std::vector<const NodeUnit*>& GetNodeUnits() const { return node_units_; }
  const NodeUnit* GetTargetNodeUnit(const logging::Logger& logger) const;

  QnnNodeGroup::Type type_ = QnnNodeGroup::Type::Undefined;
  IndexType index_ = 0;
  std::vector<const NodeUnit*> node_units_;
};

Status GetQnnNodeGroups(/*out*/ std::vector<QnnNodeGroup>& qnn_node_groups,
                        QnnModelWrapper& qnn_model_wrapper,
                        const std::unordered_map<const Node*, const NodeUnit*>& node_unit_map,
                        const logging::Logger& logger);

/**
 * Tries to fuse a node sequence starting from the given starting node. Should be called in a topologically ordered
 * walk of node units.
 *
 * \param fused_nodes Output list of node units that were fused. Remains empty if fusion was not applied.
 * \param qnn_model_wrapper The QNN model that is being built.
 * \param starting_node The node unit that could potentially start the sequence.
 * \param node_unit_map Maps a node to its node unit.
 * \param handled_node_units Set of node units that have already been processed. Fusion will not fuse nodes
 *                           in this set.
 * \param logger The logger.
 * \param do_op_validation True if should call QNN operator validation APIs.
 * \return A Status indicating a potential failure.
 */
Status TryFusions(/*out*/ std::vector<const NodeUnit*>& fused_nodes,
                  QnnModelWrapper& qnn_model_wrapper,
                  const NodeUnit& starting_node,
                  const std::unordered_map<const Node*, const NodeUnit*>& node_unit_map,
                  const std::unordered_set<const NodeUnit*>& handled_node_units,
                  const logging::Logger& logger,
                  bool do_op_validation);
}  // namespace qnn
}  // namespace onnxruntime
