// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "orttraining/core/graph/gradient_builder_base.h"
#include "orttraining/core/graph/optimizer/lamb_optimizer_builder.h"
#include "orttraining/core/graph/optimizer_builder.h"
#include "orttraining/core/graph/graph_augmenter.h"
#include "core/framework/ml_value.h"
#include "core/framework/tensorprotoutils.h"
#include "core/util/math.h"
#include "onnx/defs/attr_proto_util.h"

namespace onnxruntime {
namespace training {
Status LambOptimizerBuilder::Build(
    const std::vector<ArgDef>& weight_argdefs,
    const std::vector<ArgDef>& gradient_argdefs,
    const ArgDef* gradient_norm_argdef,
    const ArgDef* gradient_norm_finite_argdef,
    const std::vector<OptimizerNodeConfig>& opt_configs,
    GraphAugmenter::GraphDefs& graph_defs,
    std::vector<ONNX_NAMESPACE::TensorProto>& new_external_initializers,
    std::vector<ArgDef>& output_weight_argdefs,
    std::vector<ArgDef>& output_gradient_argdefs) const {
  return Build(weight_argdefs, gradient_argdefs,
               gradient_norm_argdef, gradient_norm_finite_argdef,
               opt_configs, graph_defs,
               new_external_initializers, output_weight_argdefs,
               output_gradient_argdefs,
               // gradient clipping is enabled by default for Lamb.
               true, /*enable_grad_clipping*/
               {} /* shared_optim_state */);
}

Status LambOptimizerBuilder::Build(
    const std::vector<ArgDef>& weight_argdefs,
    const std::vector<ArgDef>& gradient_argdefs,
    const ArgDef* gradient_norm_argdef,
    const ArgDef* gradient_norm_finite_argdef,
    const std::vector<OptimizerNodeConfig>& opt_configs,
    GraphAugmenter::GraphDefs& graph_defs,
    std::vector<ONNX_NAMESPACE::TensorProto>& new_external_initializers,
    std::vector<ArgDef>& output_weight_argdefs,
    std::vector<ArgDef>& output_gradient_argdefs,
    bool enable_grad_clipping) const {
  return Build(weight_argdefs, gradient_argdefs,
               gradient_norm_argdef, gradient_norm_finite_argdef,
               opt_configs, graph_defs,
               new_external_initializers, output_weight_argdefs,
               output_gradient_argdefs, enable_grad_clipping,
               {} /* shared_optim_state */);
}

Status LambOptimizerBuilder::Build(
    const std::vector<ArgDef>& weight_argdefs,
    const std::vector<ArgDef>& gradient_argdefs,
    const ArgDef* gradient_norm_argdef,
    const ArgDef* gradient_norm_finite_argdef,
    const std::vector<OptimizerNodeConfig>& opt_configs,
    GraphAugmenter::GraphDefs& graph_defs,
    std::vector<TensorProto>& new_external_initializers,
    std::vector<ArgDef>& output_weight_argdefs,
    std::vector<ArgDef>& output_gradient_argdefs,
    bool enable_grad_clipping,
    const NameMLValMap& shared_optim_state) const {
  ORT_ENFORCE(weight_argdefs.size() <= size_t(1024),
              "The current LambOptimizer can only update up to 1024 weight tensors, but",
              "the actual number of weight tensors is ", weight_argdefs.size());
  // We add optimizer's states such as momentums as initializers.

  // Lamb optimizer node's inputs and outputs.
  std::vector<ArgDef> input_argdefs;
  std::vector<ArgDef> output_argdefs;

  // Indicator of finite gradient norm ArgDef.
  if (gradient_norm_finite_argdef) {
    input_argdefs.push_back(*gradient_norm_finite_argdef);
  } else {
    input_argdefs.emplace_back(ArgDef());
  }

  // Loss scale ArgDef.
  if (!opt_configs[0].loss_scale_input_name.empty()) {
    input_argdefs.emplace_back(ArgDef(opt_configs[0].loss_scale_input_name, graph_defs.CreateTypeProto({1}, ONNX_NAMESPACE::TensorProto_DataType_FLOAT)));
  } else {
    input_argdefs.emplace_back(ArgDef());
  }

  // Gradient norm
  if (gradient_norm_argdef && enable_grad_clipping) {
    input_argdefs.push_back(*gradient_norm_argdef);
  } else if (gradient_norm_argdef == nullptr && enable_grad_clipping) {
    ORT_THROW("Gradient clipping is enabled but gradient norm is not given.");
  } else {
    input_argdefs.push_back(ArgDef());
  }

  // Learning rate ArgDef.
  input_argdefs.emplace_back(ArgDef(opt_configs[0].lr_feed_name, CreateLearningRateTypeProto(graph_defs)));
  graph_defs.AddGraphInputs({opt_configs[0].lr_feed_name});

  // Update count, which should be 1 at the first training iteration.
  // At the end of each Lamb call, the update count may be increased by one.
  const std::string step_tensor_name = "Step";  // per weight optimizer requires a per weight update count
  // Add step as an initializer.
  TensorProto step_tensor_proto;
  const auto step_state_it = shared_optim_state.find(step_tensor_name);
  if (step_state_it != shared_optim_state.end()) {
    const auto& init_tensor = step_state_it->second.Get<Tensor>();
    ORT_THROW_IF_ERROR(IsMatchingTypeAndShape(init_tensor, ONNX_NAMESPACE::TensorProto_DataType_INT64, {1}));
    step_tensor_proto = utils::TensorToTensorProto(init_tensor, step_tensor_name);
  } else {
    step_tensor_proto = CreateTensorProto<int64_t>(step_tensor_name, 1);
  }
  new_external_initializers.emplace_back(step_tensor_proto);
  input_argdefs.emplace_back(ArgDef(step_tensor_name));

  // Add the first output, which is the updated step.
  TypeProto* step_type_proto = graph_defs.CreateTypeProto({}, ONNX_NAMESPACE::TensorProto_DataType_INT64);
  output_argdefs.emplace_back(ArgDef(step_tensor_name + "_Out", step_type_proto));

  // Lamb optimizer's attributes.
  std::vector<float> alpha;
  std::vector<float> beta;
  std::vector<float> lambda;
  std::vector<float> epsilon;
  float ratio_min = -std::numeric_limits<float>::infinity();
  float ratio_max = std::numeric_limits<float>::infinity();
  int64_t do_bias_correction = 0;

  // Set global float attributes.
  {
    // Read the first weight's min and max ratios.
    const auto& attrs = opt_configs.front().attributes;
    auto ratio_min_iter = attrs.find("ratio_min");
    if (ratio_min_iter != attrs.end())
      ratio_min = ratio_min_iter->second;
    auto ratio_max_iter = attrs.find("ratio_max");
    if (ratio_max_iter != attrs.end())
      ratio_max = ratio_max_iter->second;
  }

  // Set global int attributes.
  {
    const auto& int_attrs = opt_configs.front().int_attributes;
    auto do_bias_correction_iter = int_attrs.find("do_bias_correction");
    if (do_bias_correction_iter != int_attrs.end())
      do_bias_correction = do_bias_correction_iter->second;
  }

  // Each iteration handles the associated inputs and outputs of a weight tensor.
  // Associated inputs: [w, g, m1, m2, w_mixed_precision].
  // Associated outputs: [w_new, g_new, m1_new, m2_new, w_mixed_precision_new].
  for (size_t i = 0; i < weight_argdefs.size(); ++i) {
    const std::string& weight_name = weight_argdefs[i].name;
    const std::string& weight_new_name = weight_name + "_Lamb_out";
    const std::string& gradient_name = gradient_argdefs[i].name;
    const std::string& gradient_new_name = gradient_name + "_Lamb_out";

    const auto& attrs = opt_configs[i].attributes;
    const auto& int_attrs = opt_configs[i].int_attributes;

    // Return either the input gradient/weight/mixed-precision-weight or updated gradient/weight/mixed-precision-weight.
    ArgDef output_gradient_argdef = gradient_argdefs[i];
    ArgDef output_weight_argdef = weight_argdefs[i];
    if (opt_configs[i].mixed_precision_weight_arg != nullptr)
      output_weight_argdef = ArgDef(opt_configs[i].mixed_precision_weight_arg->Name(), opt_configs[i].mixed_precision_weight_arg->TypeAsProto());

    // In distributed training, some weights may not be updated by all ranks.
    if (opt_configs[i].enabled) {
      auto alpha_iter = attrs.find("alpha");
      if (alpha_iter != attrs.end())
        alpha.emplace_back(alpha_iter->second);
      else
        alpha.emplace_back(0.9f);

      auto beta_iter = attrs.find("beta");
      if (beta_iter != attrs.end())
        beta.emplace_back(beta_iter->second);
      else
        beta.emplace_back(0.999f);

      auto lambda_iter = attrs.find("lambda");
      if (lambda_iter != attrs.end())
        lambda.emplace_back(lambda_iter->second);
      else
        lambda.emplace_back(0.0f);

      auto epsilon_iter = attrs.find("epsilon");
      if (epsilon_iter != attrs.end())
        epsilon.emplace_back(epsilon_iter->second);
      else
        epsilon.emplace_back(1e-6f);

      auto ratio_min_iter = attrs.find("ratio_min");
      if (ratio_min_iter != attrs.end()) {
        // All weight tensors should have the same min ratio.
        ORT_ENFORCE(ratio_min_iter->second == ratio_min);
      }

      auto ratio_max_iter = attrs.find("ratio_max");
      if (ratio_max_iter != attrs.end()) {
        // All weight tensors should have the same max ratio.
        ORT_ENFORCE(ratio_max_iter->second == ratio_max);
      }

      auto do_bias_correction_iter = int_attrs.find("do_bias_correction");
      if (do_bias_correction_iter != int_attrs.end()) {
        // All weight tensors should have the same bias correction flag.
        ORT_ENFORCE(do_bias_correction_iter->second == do_bias_correction);
      }

      // Extract weight's type and shape information.
      const TypeProto* const weight_type_proto = weight_argdefs[i].type_proto;
      const TypeProto* const gradient_type_proto = gradient_argdefs[i].type_proto;
      std::vector<int64_t> weight_dims;
      ORT_RETURN_IF_NOT(
          weight_argdefs[i].type_proto &&
          weight_argdefs[i].type_proto->has_tensor_type() &&
          weight_argdefs[i].type_proto->tensor_type().has_shape());
      for (const auto& dim : weight_argdefs[i].type_proto->tensor_type().shape().dim()) {
        weight_dims.push_back(dim.dim_value());
      }

      // w & g
      input_argdefs.push_back(weight_argdefs[i]);
      input_argdefs.push_back(gradient_argdefs[i]);

      // Output either w_new or g_new based on config.
      if (opt_configs[i].update_weight) {
        output_weight_argdef = ArgDef(weight_new_name, weight_type_proto);
        output_argdefs.push_back(output_weight_argdef);  // w_new
        output_argdefs.push_back(ArgDef());              // g_new
      } else {
        output_gradient_argdef = ArgDef(gradient_new_name, gradient_type_proto);
        output_argdefs.push_back(ArgDef());                // w_new
        output_argdefs.push_back(output_gradient_argdef);  // g_new
      }

      const auto element_type = opt_configs[i].use_mixed_precision_moments ? ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_FLOAT16 : ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_FLOAT;

      // m1 & m2 & m1_new & m2_new
      const std::vector<std::string> moments_prefixes({"Moment_1", "Moment_2"});
      for (const auto& moment_prefix : moments_prefixes) {
        const std::string gradient_moment_name = moment_prefix + "_" + weight_name;

        // Construct type of momentum tensor.
        TensorProto moment_tensor_proto;
        TypeProto* moment_type_proto = graph_defs.CopyTypeProto(weight_argdefs[i]);

        // Update moment initializer with init value
        const auto& initial_states = opt_configs[i].initial_states;
        const auto moment_state_it = initial_states.find(moment_prefix);
        if (moment_state_it != initial_states.end()) {
          //update moment_tensor_proto
          const auto& init_tensor = moment_state_it->second.Get<Tensor>();

          //TODO: need to support float -> float16 and float16-> float conversion
          ORT_THROW_IF_ERROR(IsMatchingTypeAndShape(init_tensor, element_type, weight_dims));
          moment_tensor_proto = utils::TensorToTensorProto(init_tensor, gradient_moment_name);
        } else if (opt_configs[i].use_mixed_precision_moments) {
          moment_tensor_proto = CreateTensorProto<MLFloat16>(gradient_moment_name, MLFloat16(math::floatToHalf(0.f)), weight_dims);
        } else {
          moment_tensor_proto = CreateTensorProto<float>(gradient_moment_name, 0.f, weight_dims);
        }

        moment_type_proto->mutable_tensor_type()->set_elem_type(element_type);

        // Store momentum tensor to initializer list.
        new_external_initializers.emplace_back(std::move(moment_tensor_proto));

        // Add momentums to the input and output list of the Lamb node.
        input_argdefs.emplace_back(ArgDef(gradient_moment_name, moment_type_proto));
        output_argdefs.emplace_back(ArgDef(gradient_moment_name + "_Out", moment_type_proto));
      }

      // w_mixed_precision & w_mixed_precision_new
      if (opt_configs[i].update_weight && opt_configs[i].mixed_precision_weight_arg != nullptr) {
        input_argdefs.emplace_back(ArgDef(
          opt_configs[i].mixed_precision_weight_arg->Name(),
          opt_configs[i].mixed_precision_weight_arg->TypeAsProto()));
        output_weight_argdef = ArgDef(
          opt_configs[i].mixed_precision_weight_arg->Name() + "_Lamb_out",
          opt_configs[i].mixed_precision_weight_arg->TypeAsProto());
        output_argdefs.push_back(output_weight_argdef);
      } else {
        input_argdefs.emplace_back(ArgDef());
        output_argdefs.emplace_back(ArgDef());
      }
    }

    output_weight_argdefs.push_back(output_weight_argdef);
    output_gradient_argdefs.push_back(output_gradient_argdef);
  }

  std::vector<AttributeProto> attribute_protos;
  attribute_protos.emplace_back(ONNX_NAMESPACE::MakeAttribute("alpha", alpha));
  attribute_protos.emplace_back(ONNX_NAMESPACE::MakeAttribute("beta", beta));
  attribute_protos.emplace_back(ONNX_NAMESPACE::MakeAttribute("lambda", lambda));
  attribute_protos.emplace_back(ONNX_NAMESPACE::MakeAttribute("epsilon", epsilon));
  attribute_protos.emplace_back(ONNX_NAMESPACE::MakeAttribute("ratio_min", ratio_min));
  attribute_protos.emplace_back(ONNX_NAMESPACE::MakeAttribute("ratio_max", ratio_max));
  attribute_protos.emplace_back(ONNX_NAMESPACE::MakeAttribute("do_bias_correction", do_bias_correction));

  graph_defs.AddNodeDefs({NodeDef(OpDefinition(),
                                  input_argdefs,
                                  output_argdefs,
                                  attribute_protos,
                                  OptimizerNodeName("AllWeights"))});

  return Status::OK();
}

}  // namespace training
}  // namespace onnxruntime
