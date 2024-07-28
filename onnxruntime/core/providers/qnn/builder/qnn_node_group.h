// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "core/framework/node_unit.h"
#include "core/providers/qnn/builder/qnn_model_wrapper.h"

namespace onnxruntime {
namespace qnn {

class IQnnNodeGroup {
 public:
  virtual ~IQnnNodeGroup() = default;
  virtual Status IsSupported(QnnModelWrapper& qmw, const logging::Logger& logger) const = 0;
  virtual Status AddToModelBuilder(QnnModelWrapper& qmw, const logging::Logger& logger) const = 0;
  virtual std::vector<const NodeUnit*> GetNodeUnits() const = 0;
  virtual const NodeUnit* GetTargetNodeUnit() const = 0;
  virtual std::string_view Type() const = 0;

  size_t index_ = 0;
};

Status GetQnnNodeGroups(/*out*/ std::vector<std::unique_ptr<IQnnNodeGroup>>& qnn_node_groups,
                        QnnModelWrapper& qnn_model_wrapper,
                        const std::unordered_map<const Node*, const NodeUnit*>& node_to_node_unit,
                        size_t num_node_units,
                        const logging::Logger& logger);
}  // namespace qnn
}  // namespace onnxruntime
