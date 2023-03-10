// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/safeint.h"
#include "core/providers/common.h"
#include "core/framework/tensorprotoutils.h"
#include "core/providers/coreml/builders/helper.h"
#include "core/providers/shared/utils/utils.h"
#include "core/optimizer/initializer.h"

#ifdef __APPLE__
#include "core/providers/coreml/builders/model_builder.h"
#endif
#include "core/providers/coreml/builders/op_builder_factory.h"

#include "base_op_builder.h"

namespace onnxruntime {
namespace coreml {

class PadOpBuilder : public BaseOpBuilder {
  // Add operator related
#ifdef __APPLE__
 public:
  void AddInitializersToSkip(ModelBuilder& model_builder, const Node& node) const override;

 private:
  Status AddToModelBuilderImpl(ModelBuilder& model_builder, const Node& node,
                               const logging::Logger& logger) const override;
#endif

  // Operator support related
 private:
  bool IsOpSupportedImpl(const Node& node, const OpBuilderInputParams& input_params,
                         const logging::Logger& logger) const override;

  int GetMinSupportedOpSet(const Node& /* node */) const override {
    // Note: before Pad-11, inputs `pads` and `constant_value` were attributes
    return 11;
  }
};

// Add operator related

#ifdef __APPLE__
void PadOpBuilder::AddInitializersToSkip(ModelBuilder& model_builder, const Node& node) const {
  model_builder.AddInitializerToSkip(node.InputDefs()[1]->Name());  //  pads
  model_builder.AddInitializerToSkip(node.InputDefs()[2]->Name());  //  constant_value
  if (node.InputDefs().size() > 3) {
    model_builder.AddInitializerToSkip(node.InputDefs()[3]->Name());  // axes
  }
}

Status PadOpBuilder::AddToModelBuilderImpl(ModelBuilder& model_builder,
                                           const Node& node,
                                           const logging::Logger& logger) const {
  std::unique_ptr<COREML_SPEC::NeuralNetworkLayer> layer = CreateNNLayer(model_builder, node);

  auto* coreml_pad = layer->mutable_padding();
  auto* constant_padding_type = coreml_pad->mutable_constant();  // CoreML::Specification::PaddingLayerParams_PaddingConstant

  const auto& input_defs = node.InputDefs();
  std::vector<int64_t> input_shape;
  GetShape(*input_defs[0], input_shape, logger);
  const auto input_rank = input_shape.size();

  const auto& pads_tensor = *model_builder.GetInitializerTensors().at(input_defs[1]->Name());            // pads
  const auto& constant_value_tensor = *model_builder.GetInitializerTensors().at(input_defs[2]->Name());  // constant_value

  Initializer constant_value_raw_data_init(constant_value_tensor);
  float constant_value = constant_value_raw_data_init.DataAsSpan<float>()[0];
  constant_padding_type->set_value(constant_value);

  Initializer pads_initializer_raw_data(pads_tensor);
  auto pads_span = pads_initializer_raw_data.DataAsSpan<int64_t>();

  // if not provided, make a default axes as [0, 1, ..., input_rank - 1]
  std::vector<int64_t> default_axes(input_rank);
  std::iota(std::begin(default_axes), std::end(default_axes), 0);
  auto axes_tensor_data = gsl::span<const int64_t>(default_axes);
  if (input_defs.size() > 3) {
    // optional input axes is provided
    const ONNX_NAMESPACE::TensorProto& axes_tensor = *model_builder.GetInitializerTensors().at(input_defs[3]->Name());
    Initializer axes_initializer_raw_data(axes_tensor);
    axes_tensor_data = axes_initializer_raw_data.DataAsSpan<int64_t>();
  }
  int64_t num_axes = axes_tensor_data.size();

  // Add padding
  auto* height_border = coreml_pad->mutable_paddingamounts()->add_borderamounts();
  auto* width_border = coreml_pad->mutable_paddingamounts()->add_borderamounts();
  for (int64_t i = 0; i < num_axes; i++) {
    if (SafeInt<int64_t>(axes_tensor_data[i]) == input_rank - 2) {
      height_border->set_startedgesize(pads_span[i]);
      height_border->set_endedgesize(pads_span[i + num_axes]);
    }
    if (SafeInt<int64_t>(axes_tensor_data[i]) == input_rank - 1) {
      width_border->set_startedgesize(pads_span[i]);
      width_border->set_endedgesize(pads_span[i + num_axes]);
    }
  }

  *layer->mutable_input()->Add() = input_defs[0]->Name();
  *layer->mutable_output()->Add() = node.OutputDefs()[0]->Name();

  model_builder.AddLayer(std::move(layer));

  return Status::OK();
}
#endif

// Operator support related

bool PadOpBuilder::IsOpSupportedImpl(const Node& node, const OpBuilderInputParams& input_params,
                                     const logging::Logger& logger) const {
  const auto& input_defs = node.InputDefs();
  const auto& initializers = input_params.graph_viewer.GetAllInitializedTensors();

  std::vector<int64_t> input_shape;
  if (!GetShape(*input_defs[0], input_shape, logger))
    return false;

  if (input_shape.empty() || input_shape.size() < 2) {
    LOGS(logger, VERBOSE) << "Pad requires input shape to be at least 2, input is "
                          << input_shape.size() << "d shape";
    return false;
  }

  if (std::find(input_shape.begin(), input_shape.end(), int64_t{0}) != input_shape.end()) {
    LOGS(logger, VERBOSE) << "Input with a 0 for a dimension is not supported";
    return false;
  }

  {
    NodeAttrHelper helper(node);
    const auto mode = helper.Get("mode", "constant");
    if (mode != "constant") {
      LOGS(logger, VERBOSE) << "Only `constant` mode Pad is currently supported for now, mode: " << mode;
      return false;
    }

    if (input_defs.size() < 3) {
      LOGS(logger, VERBOSE) << "`constant_value` input is required for constant mode Pad op.";
      return false;
    }
  }

  // only support if `pads` input is known and does not contain negative values and only applies padding values
  // for last two dimensions.
  {
    const auto pads_initializer_it = initializers.find(input_defs[1]->Name());
    if (pads_initializer_it == initializers.end()) {
      LOGS(logger, VERBOSE) << "pads must be a constant intializer.";
      return false;
    }

    const ONNX_NAMESPACE::TensorProto& pads_initializer = *pads_initializer_it->second;
    Initializer unpacked_tensor(pads_initializer);

    auto pads_tensor_data = unpacked_tensor.DataAsSpan<int64_t>();
    for (int64_t i = 0; i < unpacked_tensor.size(); i++) {
      if (pads_tensor_data[i] < 0) {
        LOGS(logger, VERBOSE) << "Negative pad value is not supported: pads["
                              << i << "] = " << pads_tensor_data[i];
        return false;
      }
    }

    // Check that only supports padding on last two dimensions - [H,W].
    // CoreML PaddinglayerParams: https://apple.github.io/coremltools/mlmodel/Format/NeuralNetwork.html#paddinglayerparams
    const auto input_rank = input_shape.size();
    // if not provided, make a default axes input as [0, 1, ..., input_rank - 1]
    std::vector<int64_t> default_axes(input_rank);
    std::iota(std::begin(default_axes), std::end(default_axes), 0);
    auto axes_tensor_data = gsl::span<const int64_t>(default_axes);
    if (input_defs.size() > 3) {
      // optional axes input is provided
      const auto axes_initializer_it = initializers.find(input_defs[3]->Name());
      if (axes_initializer_it == initializers.end()) {
        LOGS(logger, VERBOSE) << "If provided, axes must be a constant intializer.";
        return false;
      }
      const ONNX_NAMESPACE::TensorProto& axes_initializer = *axes_initializer_it->second;
      Initializer unpacked_tensor(axes_initializer);
      axes_tensor_data = unpacked_tensor.DataAsSpan<int64_t>();
    }
    int64_t num_axes = axes_tensor_data.size();
    for (int64_t i = 0; i < num_axes; i++) {
      if (SafeInt<int64_t>(axes_tensor_data[i]) < input_rank - 2) {
        if (pads_tensor_data[i] != 0 || pads_tensor_data[i + num_axes] != 0) {
          // for axis specified that is not the last two dimension, padding is not supported. i.e.
          // non-zero value appears in `pads` input for corresponding non-last two dimensions.
          LOGS(logger, VERBOSE) << "CoreML only supports padding on last two dimensions.";
          return false;
        }
      }
    }
  }

  // only support if `constant_value` input is known
  if (!Contains(initializers, input_defs[2]->Name())) {
    LOGS(logger, VERBOSE) << "constant_value must be a constant initializer.";
    return false;
  }

  return true;
}

void CreatePadOpBuilder(const std::string& op_type, OpBuilderRegistrations& op_registrations) {
  op_registrations.builders.push_back(std::make_unique<PadOpBuilder>());
  op_registrations.op_builder_map.emplace(op_type, op_registrations.builders.back().get());
}

}  // namespace coreml
}  // namespace onnxruntime
