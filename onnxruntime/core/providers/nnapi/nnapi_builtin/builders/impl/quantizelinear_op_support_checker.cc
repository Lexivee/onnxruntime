// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/logging/logging.h"
#include "core/providers/common.h"
#include "core/providers/shared/utils/utils.h"
#include "core/providers/nnapi/nnapi_builtin/builders/helper.h"
#include "core/providers/nnapi/nnapi_builtin/builders/op_support_helpers.h"
#include "core/providers/nnapi/nnapi_builtin/builders/op_support_checker_factory.h"
#include "core/providers/nnapi/nnapi_builtin/builders/impl/base_op_support_checker.h"

namespace onnxruntime {
namespace nnapi {

class QuantizeLinearOpSupportChecker : public BaseOpSupportChecker {
 private:
  int32_t GetMinSupportedNNAPIFeatureLevel(const NodeUnit& /* node_unit */,
                                           const OpSupportCheckParams& /* params */) const override {
    return ANEURALNETWORKS_FEATURE_LEVEL_3;
  }

  bool HasSupportedInputOutputsImpl(
      const InitializedTensorSet& initializers, const NodeUnit& node_unit,
      const OpSupportCheckParams& params) const override {
    return op_support_helpers::IsQuantizedIOSupported(initializers, node_unit, {0}, params, ArgType::kOutput);
  }
};

void CreateQuantizeLinearOpSupportChecker(
    const std::string& op_type, OpSupportCheckerRegistrations& op_registrations) {
  op_registrations.support_checkers.push_back(std::make_unique<QuantizeLinearOpSupportChecker>());
  op_registrations.op_support_checker_map.emplace(op_type, op_registrations.support_checkers.back().get());
}

}  // namespace nnapi
}  // namespace onnxruntime
