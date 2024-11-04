// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/safeint.h"
#include "core/framework/tensorprotoutils.h"
#include "core/providers/common.h"
#include "core/providers/coreml/builders/impl/base_op_builder.h"
#include "core/providers/coreml/builders/impl/builder_utils.h"
#include "core/providers/coreml/builders/model_builder.h"
#include "core/providers/coreml/builders/op_builder_factory.h"
#include "core/providers/coreml/shape_utils.h"
#include "core/providers/shared/utils/utils.h"
#include "core/optimizer/initializer.h"

namespace onnxruntime {
namespace coreml {

class SqueezeOpBuilder : public BaseOpBuilder {
  void AddInitializersToSkip(ModelBuilder& model_builder, const Node& node) const override;

  Status AddToModelBuilderImpl(ModelBuilder& model_builder, const Node& node,
                               const logging::Logger& logger) const override;

  bool IsOpSupportedImpl(const Node& node, const OpBuilderInputParams& input_params,
                         const logging::Logger& logger) const override;
  bool SupportsMLProgram() const override { return true; }
};

namespace {
void GetAxes(ModelBuilder& model_builder, const Node& node, std::vector<int64_t>& axes) {
  // Squeeze opset 13 use input as axes
  if (node.SinceVersion() > 12) {
    // If axes is not provided, return an empty axes as default to squeeze all
    if (node.InputDefs().size() > 1) {
      const auto& axes_tensor = *model_builder.GetConstantInitializer(node.InputDefs()[1]->Name());
      Initializer unpacked_tensor(axes_tensor);
      auto raw_axes = unpacked_tensor.DataAsSpan<int64_t>();
      const auto size = SafeInt<size_t>(axes_tensor.dims()[0]);
      axes.reserve(size);
      axes.insert(axes.end(), raw_axes.begin(), raw_axes.end());
    }
  } else {
    NodeAttrHelper helper(node);
    axes = helper.Get("axes", std::vector<int64_t>());
  }
}
}  // namespace

void SqueezeOpBuilder::AddInitializersToSkip(ModelBuilder& model_builder, const Node& node) const {
  if (node.SinceVersion() > 12 && node.InputDefs().size() > 1) {
    model_builder.AddInitializerToSkip(node.InputDefs()[1]->Name());
  }
}

Status SqueezeOpBuilder::AddToModelBuilderImpl(ModelBuilder& model_builder,
                                               const Node& node,
                                               [[maybe_unused]] const logging::Logger& logger) const {
  std::unique_ptr<COREML_SPEC::NeuralNetworkLayer> layer = model_builder.CreateNNLayer(node);
  const auto& input_defs(node.InputDefs());
  auto* coreml_squeeze = layer->mutable_squeeze();
  std::vector<int64_t> axes;
  GetAxes(model_builder, node, axes);
  std::vector<int64_t> input_shape;
  GetShape(*input_defs[0], input_shape, logger);
#if defined(COREML_ENABLE_MLPROGRAM)
  if (model_builder.CreateMLProgram()) {
    using namespace CoreML::Specification::MILSpec;

    std::string_view coreml_op_type = node.OpType() == "Squeeze" ? "squeeze" : "reshape";
    std::unique_ptr<Operation> op = model_builder.CreateOperation(node, coreml_op_type);
    AddOperationInput(*op, "x", input_defs[0]->Name());

    if (coreml_op_type == "squeeze") {
      if (!axes.empty()) {
        AddOperationInput(*op, "axes", model_builder.AddConstant(op->type(), "axes", axes));
      }
    } else {
      for (auto& axis : axes) {
        axis = HandleNegativeAxis(axis, input_shape.size() + axes.size());
      }
      std::vector<int64_t> new_shape(axes.size() + input_shape.size(), 1);
      std::sort(axes.begin(), axes.end());
      // For example: Given an input tensor (data) of shape [3, 4, 5],
      // then Unsqueeze(data, axes=[0, 4]) outputs a tensor (expanded) containing same data as data but with shape [1, 3, 4, 5, 1].
      for (size_t i = 0, ori_i = 0, axes_i = 0; i < new_shape.size(); i++) {
        if ((axes_i >= axes.size() || static_cast<int64_t>(i) != axes[axes_i]) && input_shape.size() >= ori_i) {
          new_shape[i] = input_shape[ori_i++];
        } else {
          axes_i++;
        }
      }
      AddOperationInput(*op, "shape", model_builder.AddConstant(op->type(), "shape", new_shape));
    }
    AddOperationOutput(*op, *node.OutputDefs()[0]);
    model_builder.AddOperation(std::move(op));
  } else  // NOLINT
#endif
  {
    if (axes.empty()) {
      coreml_squeeze->set_squeezeall(true);
    } else {
      *coreml_squeeze->mutable_axes() = {axes.cbegin(), axes.cend()};
      coreml_squeeze->set_squeezeall(false);
    }

    *layer->mutable_input()->Add() = node.InputDefs()[0]->Name();
    *layer->mutable_output()->Add() = node.OutputDefs()[0]->Name();

    model_builder.AddLayer(std::move(layer));
  }
  return Status::OK();
}

bool SqueezeOpBuilder::IsOpSupportedImpl(const Node& node, const OpBuilderInputParams& input_params,
                                         const logging::Logger& logger) const {
  // Squeeze opset 13 uses input 1 as axes, if we have input 1 then it needs to be an initializer
  const auto& input_defs = node.InputDefs();
  if (node.SinceVersion() > 12 && input_defs.size() > 1) {
    const auto& axes_name = input_defs[1]->Name();
    if (!input_params.graph_viewer.GetConstantInitializer(axes_name)) {
      LOGS(logger, VERBOSE) << "Input axes of Squeeze must be known";
      return false;
    }
  }

  if (node.OpType() == "Unsqueeze") {
    if (!input_params.create_mlprogram) {
      return false;
    }
    int64_t rank = -1;
    if (node.SinceVersion() > 12) {
      const auto& axes_tensor = *input_params.graph_viewer.GetConstantInitializer(input_defs[1]->Name());
      Initializer unpacked_tensor(axes_tensor);
      rank = unpacked_tensor.size();
    } else {
      NodeAttrHelper helper(node);
      auto axes = helper.Get("axes", std::vector<int64_t>());
      rank = static_cast<int64_t>(axes.size());
    }
    std::vector<int64_t> input_shape;
    if (!GetShape(*input_defs[0], input_shape, logger) || input_shape.size() + rank > 5) {
      LOGS(logger, VERBOSE) << "Unsqueeze with rank > 5 is not supported";
      return false;
    }
  }
  return true;
}

void CreateSqueezeOpBuilder(const std::string& op_type, OpBuilderRegistrations& op_registrations) {
  op_registrations.builders.push_back(std::make_unique<SqueezeOpBuilder>());
  op_registrations.op_builder_map.emplace(op_type, op_registrations.builders.back().get());
}

}  // namespace coreml
}  // namespace onnxruntime
