// Copyright (c) Microsoft Corporation. All rights reserved.
// Copyright (c) Intel Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/safeint.h"
#include "core/optimizer/initializer.h"
#include "core/providers/common.h"
#include "core/providers/shared/utils/utils.h"
#include "core/providers/webnn/builders/helper.h"
#include "core/providers/webnn/builders/model_builder.h"
#include "core/providers/webnn/builders/op_builder_factory.h"

#include "base_op_builder.h"

namespace onnxruntime {
namespace webnn {

class NormalizationOpBuilder : public BaseOpBuilder {
  // Add operator related.
 private:
  Status AddToModelBuilderImpl(ModelBuilder& model_builder, const Node& node,
                               const logging::Logger& logger) const override ORT_MUST_USE_RESULT;

  // Operator support related.
 private:
  bool IsOpSupportedImpl(const InitializedTensorSet& initializers, const Node& node,
                         const WebnnDeviceType /* device_type */, const logging::Logger& logger) const override;
  bool HasSupportedInputsImpl(const Node& node, const emscripten::val& wnn_limits,
                              const logging::Logger& logger) const override;
};

Status NormalizationOpBuilder::AddToModelBuilderImpl(ModelBuilder& model_builder,
                                                     const Node& node,
                                                     const logging::Logger& logger) const {
  const auto& op_type = node.OpType();
  const auto& input_defs = node.InputDefs();
  ORT_RETURN_IF_NOT(input_defs.size() >= 2, op_type, " requires at least two inputs.");

  emscripten::val input = model_builder.GetOperand(input_defs[0]->Name());
  std::vector<int64_t> input_shape;
  ORT_RETURN_IF_NOT(GetShape(*input_defs[0], input_shape, logger), "Cannot get input shape");
  const auto rank = input_shape.size();

  emscripten::val options = emscripten::val::object();
  options.set("label", node.Name());

  std::vector<int64_t> scale_shape;
  ORT_RETURN_IF_NOT(GetShape(*input_defs[1], scale_shape, logger), "Cannot get scale shape");
  const auto scale_size = scale_shape.size();
  // Except LayerNormalization, other normalization ops' scale input should be 1-D.
  if (op_type == "LayerNormalization") {
    ORT_RETURN_IF_NOT(scale_size >= 1 && scale_size <= rank,
                      "The scale size should be less than or equal to input size.");
  } else {
    ORT_RETURN_IF_NOT(scale_size == 1, "The scale size should be one.");
  }

  if (input_defs.size() >= 3 && !input_defs[2]->Name().empty()) {
    // Bias input exists, and bias's shape should be the same as scale's shape.
    std::vector<int64_t> bias_shape;
    ORT_RETURN_IF_NOT(GetShape(*input_defs[2], bias_shape, logger), "Cannot get bias shape");
    ORT_RETURN_IF_NOT(bias_shape == scale_shape, "The bias' shape should be equal to scale's shape.");
  }

  emscripten::val scale = model_builder.GetOperand(input_defs[1]->Name());
  options.set("scale", scale);

  if (input_defs.size() >= 3 && !input_defs[2]->Name().empty()) {
    // Bias input exists, and bias's shape is the same as scale's shape.
    emscripten::val bias = model_builder.GetOperand(input_defs[2]->Name());
    options.set("bias", bias);
  }

  NodeAttrHelper helper(node);
  const auto epsilon = helper.Get("epsilon", 1e-05f);
  options.set("epsilon", epsilon);

  emscripten::val output = emscripten::val::undefined();
  if (op_type == "BatchNormalization") {
    ORT_RETURN_IF_NOT(input_defs.size() == 5, "BatchNormalization requires five inputs.");
    emscripten::val mean = model_builder.GetOperand(input_defs[3]->Name());
    emscripten::val variance = model_builder.GetOperand(input_defs[4]->Name());
    if (model_builder.GetPreferredLayout() == DataLayout::NHWC) {
      options.set("axis", rank - 1);
    }

    output = model_builder.GetBuilder().call<emscripten::val>("batchNormalization", input, mean, variance, options);
  } else if (op_type == "LayerNormalization" || op_type == "SimplifiedLayerNormalization") {
    int64_t axis = helper.Get("axis", -1);
    axis = HandleNegativeAxis(axis, rank);
    std::vector<uint32_t> axes(rank - SafeInt<uint32_t>(axis));
    std::iota(axes.begin(), axes.end(), axis);

    if (op_type == "LayerNormalization") {
      options.set("axes", emscripten::val::array(axes));
      output = model_builder.GetBuilder().call<emscripten::val>("layerNormalization", input, options);
    } else {  // SimplifiedLayerNormalization
      /**
      WebNN doesn't support SimplifiedLayerNormalization, decompose it into a series of ops as follows:
      X --> Pow --> ReduceMean --> Add --> Sqrt --> Div -> Mul
            ^          ^           ^                ^      ^
            |          |           |                |      |
            Y:2        axis     B:epsilon           A:X  A:scale
      */

      int32_t input_type;
      ORT_RETURN_IF_NOT(GetType(*input_defs[0], input_type, logger), "cannot get input type");
      emscripten::val common_options = emscripten::val::object();

      // Pow
      emscripten::val pow_constant_desc = emscripten::val::object();
      ORT_RETURN_IF_NOT(SetWebnnDataType(pow_constant_desc, input_type), "Unsupported data type");
      pow_constant_desc.set("shape", emscripten::val::array());
      emscripten::val pow_buffer = emscripten::val::global("Float32Array").new_(1);
      pow_buffer.set(0, 2);
      emscripten::val pow_constant =
          model_builder.GetBuilder().call<emscripten::val>("constant", pow_constant_desc, pow_buffer);
      common_options.set("label", node.Name() + "_pow");
      emscripten::val pow =
          model_builder.GetBuilder().call<emscripten::val>("pow", input, pow_constant, common_options);

      // ReduceMean
      emscripten::val reduce_options = emscripten::val::object();
      reduce_options.set("axes", emscripten::val::array(axes));
      reduce_options.set("keepDimensions", true);
      reduce_options.set("label", node.Name() + "_reduceMean");
      emscripten::val reduce_mean = model_builder.GetBuilder().call<emscripten::val>("reduceMean", pow, reduce_options);

      // Add
      emscripten::val add_constant_desc = emscripten::val::object();
      ORT_RETURN_IF_NOT(SetWebnnDataType(add_constant_desc, input_type), "Unsupported data type");
      add_constant_desc.set("shape", emscripten::val::array());
      emscripten::val add_buffer = emscripten::val::global("Float32Array").new_(1);
      add_buffer.set(0, epsilon);
      emscripten::val add_constant =
          model_builder.GetBuilder().call<emscripten::val>("constant", add_constant_desc, add_buffer);
      common_options.set("label", node.Name() + "_add");
      emscripten::val add =
          model_builder.GetBuilder().call<emscripten::val>("add", reduce_mean, add_constant, common_options);

      // Sqrt
      common_options.set("label", node.Name() + "_sqrt");
      emscripten::val sqrt = model_builder.GetBuilder().call<emscripten::val>("sqrt", add, common_options);

      // Div
      common_options.set("label", node.Name() + "_div");
      emscripten::val div = model_builder.GetBuilder().call<emscripten::val>("div", input, sqrt, common_options);

      // Mul
      common_options.set("label", node.Name() + "_div");
      output = model_builder.GetBuilder().call<emscripten::val>("mul", scale, div, common_options);
    }
  } else if (op_type == "InstanceNormalization") {
    // WebNN spec only supports 4D input for instanceNormalization.
    // Supports 3D input by prepending 1 size dimension.
    // For models with dimensions greater than 4, they will be reshaped into 4D.
    constexpr size_t webnn_shape_rank = 4;
    if (input_shape.size() != webnn_shape_rank) {
      std::vector<uint32_t> new_shape;
      new_shape.reserve(std::max(input_shape.size(), webnn_shape_rank));
      std::transform(input_shape.begin(), input_shape.end(),
                     std::back_inserter(new_shape),
                     [](int64_t dim) -> uint32_t { return SafeInt<uint32_t>(dim); });

      size_t insertion_offset = (model_builder.GetPreferredLayout() == DataLayout::NHWC) ? 2 : 3;
      ptrdiff_t excess_rank = new_shape.size() - webnn_shape_rank;
      auto insertion_point = new_shape.begin() + insertion_offset;
      if (input_shape.size() < webnn_shape_rank) {
        // Pad the shape with extra 1's to satisfy WebNN v1's rank requirements.
        new_shape.insert(insertion_point, -excess_rank, 1);
      } else {
        // Fold the extra range to fit within WebNN v1's rank requirements.
        uint32_t sum = std::accumulate(
            insertion_point, insertion_point + excess_rank + 1, 1, std::multiplies<uint32_t>());
        new_shape.erase(insertion_point, insertion_point + excess_rank);
        *insertion_point = sum;
      }
      emscripten::val reshape_input_options = emscripten::val::object();
      reshape_input_options.set("label", node.Name() + "_reshape_input");
      input = model_builder.GetBuilder().call<emscripten::val>("reshape",
                                                               input,
                                                               emscripten::val::array(new_shape),
                                                               reshape_input_options);
    }

    if (model_builder.GetPreferredLayout() == DataLayout::NHWC) {
      options.set("layout", emscripten::val("nhwc"));
    }
    output = model_builder.GetBuilder().call<emscripten::val>("instanceNormalization", input, options);
    // Reshape back to the original output shape for 3D input.
    if (input_shape.size() != 4) {
      std::vector<uint32_t> output_shape = GetVecUint32FromVecInt64(input_shape);
      emscripten::val reshape_output_options = emscripten::val::object();
      reshape_output_options.set("label", node.Name() + "reshape_output");
      output = model_builder.GetBuilder().call<emscripten::val>("reshape",
                                                                output,
                                                                emscripten::val::array(output_shape),
                                                                reshape_output_options);
    }
  } else {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "Unsupported normalization op: ", op_type);
  }
  model_builder.AddOperand(node.OutputDefs()[0]->Name(), std::move(output));

  return Status::OK();
}

// Operator support related.

bool NormalizationOpBuilder::IsOpSupportedImpl(const InitializedTensorSet& initializers,
                                               const Node& node,
                                               const WebnnDeviceType /* device_type */,
                                               const logging::Logger& logger) const {
  const auto& input_defs = node.InputDefs();
  const auto& op_type = node.OpType();
  NodeAttrHelper helper(node);

  if (input_defs.size() < 2) {
    LOGS(logger, VERBOSE) << op_type << " requires at least two inputs.";
    return false;
  }

  std::vector<int64_t> input_shape;
  if (!GetShape(*input_defs[0], input_shape, logger)) {
    LOGS(logger, VERBOSE) << "Cannot get input shape.";
    return false;
  }

  const auto& output_defs = node.OutputDefs();
  if (output_defs.size() != 1) {
    LOGS(logger, VERBOSE) << op_type << " output count must be one.";
    return false;
  }

  if (op_type == "BatchNormalization" && helper.Get("training_mode", 0)) {
    LOGS(logger, VERBOSE) << "BatchNormalization with training_mode set to true is not supported.";
    return false;
  }

  return true;
}

bool NormalizationOpBuilder::HasSupportedInputsImpl(const Node& node, const emscripten::val& wnn_limits,
                                                    const logging::Logger& logger) const {
  const auto& input_defs = node.InputDefs();
  const auto& op_type = node.OpType();
  int32_t input0_type;  // input data type
  int32_t input1_type;  // scale data type
  int32_t input2_type;  // B data type
  int32_t input3_type;  // mean data type
  int32_t input4_type;  // var data type
  bool has_input2 = input_defs.size() > 2 && input_defs[2]->Exists();
  bool has_input3 = input_defs.size() > 3 && input_defs[3]->Exists();
  bool has_input4 = input_defs.size() > 3 && input_defs[4]->Exists();

  if (!GetType(*input_defs[0], input0_type, logger) ||
      !GetType(*input_defs[1], input1_type, logger) ||
      (has_input2 && !GetType(*input_defs[2], input2_type, logger)) ||
      (has_input3 && !GetType(*input_defs[3], input3_type, logger)) ||
      (has_input4 && !GetType(*input_defs[4], input4_type, logger))) {
    return false;
  }

  std::vector<int32_t> input_types = {input0_type, input1_type};
  if (has_input2) {
    input_types.push_back(input2_type);
  }
  if (has_input3) {
    input_types.push_back(input3_type);
  }
  if (has_input4) {
    input_types.push_back(input4_type);
  }
  if (!AreInputDataTypesSame(op_type, input_types, logger)) {
    return false;
  }

  return IsDataTypeSupportedByOp(op_type, input0_type, wnn_limits, "input", "X", logger);
}

void CreateNormalizationOpBuilder(const std::string& op_type, OpBuilderRegistrations& op_registrations) {
  if (op_registrations.op_builder_map.count(op_type) > 0)
    return;

  constexpr static std::string_view op_types[] =
      {
          "BatchNormalization",
          "InstanceNormalization",
          "LayerNormalization",
          "SimplifiedLayerNormalization",
      };

  op_registrations.builders.push_back(std::make_unique<NormalizationOpBuilder>());
  for (const auto& op_type : op_types) {
    op_registrations.op_builder_map.emplace(op_type, op_registrations.builders.back().get());
  }
}

}  // namespace webnn
}  // namespace onnxruntime
