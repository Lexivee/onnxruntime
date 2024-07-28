// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <gsl/gsl>
#include <string_view>
#include <unordered_map>

#include "core/graph/graph_viewer.h"
#include "core/framework/node_unit.h"
#include "core/providers/qnn/builder/qnn_node_group.h"

namespace onnxruntime {
namespace qnn {
const NodeUnit* GetOnlyChildOfType(const GraphViewer& graph_viewer,
                                   const NodeUnit& parent_node_unit,
                                   gsl::span<const std::string_view> child_op_types,
                                   const std::unordered_map<const Node*, const NodeUnit*>& node_unit_map,
                                   const std::unordered_map<const NodeUnit*, QnnNodeGroup::IndexType>& node_unit_to_qnn_node_group);

}  // namespace qnn
}  // namespace onnxruntime
