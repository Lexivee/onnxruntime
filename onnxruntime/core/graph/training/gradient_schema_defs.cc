#include "core/graph/op.h"
#include "core/graph/contrib_ops/contrib_defs.h"
#include "gradient_schema_defs.h"
#include "gradient_op_schema.h"

namespace onnxruntime {
namespace training {

using namespace ONNX_NAMESPACE;

void AddRepeatedInputs(
    OpSchema& op_schema,
    const int start,
    const int count,
    const std::vector<std::string>& names,
    const std::vector<std::string>& descriptions,
    const std::vector<std::string>& type_strs,
    const OpSchema::FormalParameterOption param_option) {

  ORT_ENFORCE(names.size() == descriptions.size(),
    "Names and descriptions must be equal-length.");
  ORT_ENFORCE(names.size() == type_strs.size(),
    "Names and type_strs must be equal-length.");
  ORT_ENFORCE(param_option != OpSchema::Variadic,
    "param_option cannot be variadic.");
  ORT_ENFORCE(count > 0, "Count must be positive.");

  for (int i = 0; i < count; ++i) {
    const int input_index_start = start + i * static_cast<int>(names.size());
    // Repeat one group of names once.
    for (size_t j = 0; j < names.size(); ++j) {
      const int input_index = input_index_start + static_cast<int>(j);
      std::string modified_input_name = "__group_" + std::to_string(i) + "__" + names[j];
      ORT_ENFORCE(input_index >= static_cast<int>(op_schema.inputs().size()),
                  "Invalid redefinition of input ", input_index, " for OpSchema ", op_schema.Name());
      op_schema.Input(input_index, modified_input_name, descriptions[j], type_strs[j], param_option, false);
    }
  }
}

void AddRepeatedOutputs(
    OpSchema& op_schema,
    const int start,
    const int count,
    const std::vector<std::string>& names,
    const std::vector<std::string>& descriptions,
    const std::vector<std::string>& type_strs,
    const OpSchema::FormalParameterOption param_option) {

  ORT_ENFORCE(names.size() == descriptions.size(),
    "Names and descriptions must be equal-length.");
  ORT_ENFORCE(names.size() == type_strs.size(),
    "Names and type_strs must be equal-length.");
  ORT_ENFORCE(param_option != OpSchema::Variadic,
    "param_option cannot be variadic.");
  ORT_ENFORCE(count > 0, "Count must be positive.");

  for (int i = 0; i < count; ++i) {
    const int output_index_start = start + i * static_cast<int>(names.size());
    // Repeat one group of names once.
    for (int j = 0; j < static_cast<int>(names.size()); ++j) {
      const int output_index = output_index_start + j;
      std::string modified_output_name = "__group_" + std::to_string(i) + "__" + names[j];
      ORT_ENFORCE(output_index >= static_cast<int>(op_schema.outputs().size()),
                  "Invalid redefinition of output ", output_index, " for OpSchema ", op_schema.Name());
      op_schema.Output(output_index, modified_output_name, descriptions[j], type_strs[j], param_option, false);
    }
  }
}

// TODO: This is copied from onnx schemas. When the change is in and we update this can be removed.
// For Brevity documentation was not copied
OpSchema& RegisterLambOpSchema(OpSchema&& op_schema) {
  op_schema
      .SinceVersion(9)
      .Attr(
          "alpha",
          "Coefficient of previous gradient in running average.",
          AttributeProto::FLOATS,
          std::vector<float>(1024, 0.9f))
      .Attr(
          "beta",
          "Coefficient of previous squared gradient in running average."
          "The effective learning rate is computed by r = R / (1 + T * decay_factor). "
          "Default to 0 so that increasing update counts doesn't reduce the learning rate.",
          AttributeProto::FLOATS,
          std::vector<float>(1024, 0.999f))
      .Attr(
          "lambda",
          "Regularization coefficient of 0.5 * lambda * ||X||_2^2. Default to 0, "
          "which means no regularization.",
          AttributeProto::FLOATS,
          std::vector<float>(1024, 0.0f))
      .Attr(
          "epsilon",
          "Small scalar to avoid dividing by zero.",
          AttributeProto::FLOATS,
          std::vector<float>(1024, 1e-6f))
      .Attr(
          "threshold",
          "The max ratio of tensor norm and its gradient.",
          AttributeProto::FLOATS,
          std::vector<float>(1024, 1.0f))
      .TypeConstraint(
          "T1",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input types to float scalars.")
      .TypeConstraint(
          "T2",
          {"tensor(float)", "tensor(double)"},
          "Constrain input types to float tensors.")
      .TypeConstraint(
          "T3",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input types to float tensors.")
      .TypeConstraint(
          "T4",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input types to float tensors.")
      .TypeConstraint(
          "T_FP16",
          {"tensor(float16)"},
          "Constrain input types to float16 tensors.")
      .TypeConstraint(
          "T_GRAD_NORM",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input types to float tensors.")
      .TypeConstraint(
          "T_BOOL",
          {"tensor(bool)"},
          "Constrain types to boolean tensors.");

  op_schema
      .Input(
          0,
          "update_signal",
          "This signal indicates if weight tensors should be updated.",
          "T_BOOL",
          OpSchema::Optional)
      .Input(
          1,
          "loss_scale",
          "Loss scale for mixed precision training.",
          "T2",
          OpSchema::Optional)
      .Input(
          2,
          "gradient_norm",
          "Norm of global gradient.",
          "T_GRAD_NORM",
          OpSchema::Optional)
      .Input(
          3,
          "R",
          "The initial learning rate.",
          "T1",
          OpSchema::Optional)
      .Input(
          4,
          "weights",
          "weights to optimize.",
          "T2",
          OpSchema::Optional)
      .Input(
          5,
          "gradients",
          "gradients computed in this iteration.",
          "T3",
          OpSchema::Optional)
      .Input(
          6,
          "moment_1",
          "exponentially averaged historical gradients.",
          "T4",
          OpSchema::Optional)
      .Input(
          7,
          "moment_2",
          "exponentially averaged historical squared gradients.",
          "T4",
          OpSchema::Optional)
      .Input(
          8,
          "fp16_weights",
          "FP16 weights to optimize.",
          "T_FP16",
          OpSchema::Optional);

  AddRepeatedInputs(
      op_schema,
      9,
      1023,
      {"extra_weights", "extra_gradients", "extra_moment1", "extra_moment2", "fp16_weights"},
      {"extra_weights", "extra_gradients", "extra_moment1", "extra_moment2", "fp16_weights"},
      {"T2", "T3", "T4", "T4", "T_FP16"},
      OpSchema::Optional);

  op_schema
      .Output(
          0,
          "new_weights",
          "New weights",
          "T2",
          OpSchema::Optional)
      .Output(
          1,
          "output_moment_1",
          "New averaged Gradients",
          "T4",
          OpSchema::Optional)
      .Output(
          2,
          "output_moment_2",
          "New averaged squared gradients",
          "T4",
          OpSchema::Optional)
      .Output(
          3,
          "new_fp16_weights",
          "New FP16 weights",
          "T_FP16",
          OpSchema::Optional);

  AddRepeatedOutputs(
      op_schema,
      4,
      1023,
      {"extra_new_weights", "output_moment_1", "output_moment_2", "new_fp16_weights"},
      {"extra_new_weights", "output_moment_1", "output_moment_2", "new_fp16_weights"},
      {"T2", "T4", "T4", "T_FP16"},
      OpSchema::Optional);

  return op_schema;
}

void RegisterGradientSchemas() {
  ONNX_GRADIENT_OPERATOR_SCHEMA(SinGrad)
      .NumInputs(2)
      .NumOutputs(1)
      .Reference("Sin");

  ONNX_GRADIENT_OPERATOR_SCHEMA(ReluGrad)
      .NumInputs(2)
      .NumOutputs(1)
      .Reference("Relu");

  ONNX_GRADIENT_OPERATOR_SCHEMA(PowGrad)
      .NumInputs(3)
      .NumOutputs(1, 2)
      .Reference("Pow");

  ONNX_GRADIENT_OPERATOR_SCHEMA(SigmoidGrad)
      .NumInputs(2)
      .NumOutputs(1)
      .Reference("Sigmoid");

  ONNX_GRADIENT_OPERATOR_SCHEMA(SoftmaxGrad)
      .NumInputs(2)
      .NumOutputs(1)
      .Reference("Softmax");

  ONNX_GRADIENT_OPERATOR_SCHEMA(AveragePoolGrad)
      .NumInputs(3)
      .NumOutputs(1)
      .Reference("AveragePool");

  ONNX_GRADIENT_OPERATOR_SCHEMA(MaxPoolGrad)
      .Input(0, "dY", "Gradient of output, Y", "T")
      .Input(1, "Indices", "Indices tensor from max pooling across the input tensor.", "I")
      .Output(0, "dX", "Gradient of input, X", "T")
      .TypeConstraint(
          "T",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input and output types to float tensors.")
      .TypeConstraint(
          "I",
          {"tensor(int64)"},
          "Constrain index tensor to int64")
      .ReferenceAttributes("MaxPool");

  ONNX_GRADIENT_OPERATOR_SCHEMA(ConvGrad)
      .NumInputs(2, 3)
      .NumOutputs(1, 3)
      .Reference("Conv");

  ONNX_GRADIENT_OPERATOR_SCHEMA(LRNGrad)
      .NumInputs(3)
      .NumOutputs(1)
      .Reference("LRN");

  ONNX_GRADIENT_OPERATOR_SCHEMA(DropoutGrad)
      .NumInputs(1, 2)
      .NumOutputs(1)
      .Reference("Dropout");

  ONNX_GRADIENT_OPERATOR_SCHEMA(GatherGrad)
      .Input(0, "shape", "Shape of the Gather input X.", "I")
      .Input(1, "indices", "Tensor of int32/int64 indices, of any rank q.", "Tind")
      .Input(2, "dY", "Gradient of output", "T")
      .Output(0, "dX", "Gradient of input", "T")
      .TypeConstraint(
          "I",
          {"tensor(int64)"},
          "Constrain input shape to integer tensors.")
      .TypeConstraint(
          "T",
          OpSchema::all_tensor_types(),
          "Constrain input and output types to float tensors.")
      .TypeConstraint(
          "Tind",
          {"tensor(int32)", "tensor(int64)"},
          "Constrain indices to integer types")
      .ReferenceAttributes("Gather");

  ONNX_GRADIENT_OPERATOR_SCHEMA(DivGrad)
      .Input(0, "dY", "Gradient of output", "T")
      .Input(1, "A", "dividend", "T")
      .Input(2, "B", "divisor", "T")
      .Output(0, "dA", "Gradient of dividend", "T", OpSchema::Optional)
      .Output(1, "dB", "Gradient of divisor", "T", OpSchema::Optional)
      .TypeConstraint(
          "T",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input and output types to numeric tensors.");

  //TODO: Move this to the right location. Its only here for quick experimentation.
  //TODO: Use the mutli weight / grad version.
  ONNX_CONTRIB_OPERATOR_SCHEMA(SGDOptimizer)
      .SinceVersion(9)
      .Input(0, "ETA", "Learning Rate", "L")
      .Input(1, "W", "Original weight(s)", "T")
      .Input(2, "G", "Gradient of Weight(s)", "T")
      .Output(0, "NW", "Updated weight(s)", "T")
      .TypeConstraint(
          "T",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input and output types to float tensors.")
      .TypeConstraint(
          "L",
          {"float"},
          "Constrain learning rate to float");

  // TODO: This is copied from onnx schemas. When the change is in and we update this can be removed.
  // For Brevity documentation was not copied
  ONNX_CONTRIB_OPERATOR_SCHEMA(AdamOptimizer)
      .SinceVersion(9)
      .Input(0, "R", "The initial learning rate.", "T1")
      .Input(1, "T", "The update count of \"X\". It should be a scalar.", "T2")
      .Input(
          2,
          "weights",
          "weights to optimize.",
          "T3")
      .Input(
          3,
          "gradients",
          "gradients computed in this iteration.",
          "T_GRAD")
      .Input(
          4,
          "moment_1",
          "exponentially averaged historical gradients.",
          "T4")
      .Input(
          5,
          "moment_2",
          "exponentially averaged historical squared gradients.",
          "T4")
      .Input(
          6,
          "fp16_weights",
          "FP16 weights to optimize.",
          "T_FP16",
          OpSchema::Optional)
      .Input(
          7,
          "loss_scale",
          "loss scale for mixed precision training",
          "T3",
          OpSchema::Optional)
      .Input(
          8,
          "global_gradient_norm",
          "Global gradient norm.",
          "T_GRAD_NORM",
          OpSchema::Optional)
      .Input(
          9,
          "update_signal",
          "This signal indicates if weight tensors should be updated.",
          "T_BOOL",
          OpSchema::Optional)
      .Output(
          0,
          "new_weights",
          "New weights.",
          "T3")
      .Output(
          1,
          "output_moment_1",
          "New averaged gradients.",
          "T4")
      .Output(
          2,
          "output_moment_2",
          "New averaged squared gradients.",
          "T4")
      .Output(
          3,
          "output_T",
          "New update count.",
          "T2")
      .Output(
          4,
          "new_fp16_weights",
          "New FP16 weights",
          "T_FP16",
          OpSchema::Optional)
      .Attr(
          "alpha",
          "Coefficient of previous gradient in running average.",
          AttributeProto::FLOAT,
          0.9f)
      .Attr(
          "beta",
          "Coefficient of previous squared gradient in running average."
          "The effective learning rate is computed by r = R / (1 + T * decay_factor). "
          "Default to 0 so that increasing update counts doesn't reduce the learning rate.",
          AttributeProto::FLOAT,
          0.999f)
      .Attr(
          "lambda",
          "Regularization coefficient of 0.5 * lambda * ||X||_2^2. Default to 0, "
          "which means no regularization.",
          AttributeProto::FLOAT,
          0.0f)
      .Attr(
          "epsilon",
          "Small scalar to avoid dividing by zero.",
          AttributeProto::FLOAT,
          1e-6f)
      .TypeConstraint(
          "T1",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain learning rate to float")
      .TypeConstraint(
          "T2",
          {"int64"},
          "Constrain step count to 64-bit integer")
      .TypeConstraint(
          "T3",
          {"tensor(float)", "tensor(double)"},
          "Constrain input types to float tensors.")
      .TypeConstraint(
          "T4",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input types to float tensors.")
      .TypeConstraint(
          "T_GRAD",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input types to float tensors.")
      .TypeConstraint(
          "T_FP16",
          {"tensor(float16)"},
          "Constrain input types to float16 tensors.")
      .TypeConstraint(
          "T_GRAD_NORM",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input types to float tensors.")
      .TypeConstraint(
          "T_BOOL",
          {"tensor(bool)"},
          "Constrain types to boolean tensors.");


  ONNX_CONTRIB_OPERATOR_SCHEMA_ELSEWHERE(LambOptimizer, RegisterLambOpSchema);

  ONNX_CONTRIB_OPERATOR_SCHEMA(GradientAccumulator)
      .SinceVersion(9)
      .SetDoc("accumulator for gradient")
      .Input(0, "old_sum", "historical result of accumulator", "T")
      .Input(1, "value", "the value that will be added to the accumulator", "T_GRAD")
      .Output(0, "new_sum", "updated result of accumulator", "T")
      .TypeConstraint(
          "T",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input and output types to float tensors.")
      .TypeConstraint(
          "T_GRAD",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input and output types to float tensors.")
      .TypeAndShapeInferenceFunction([](ONNX_NAMESPACE::InferenceContext& ctx) {
        propagateShapeAndTypeFromFirstInput(ctx);
      });

  ONNX_CONTRIB_OPERATOR_SCHEMA(ZeroGradient)
      .SinceVersion(9)
      .SetDoc("reset the accumulator for gradient")
      .Input(0, "old_gradient", "historical result of accumulated gradient", "T1")
      .Input(1, "reset_signal", "if this input is available, it is ready to reset the accumulator", "T2")
      .Output(0, "zero_gradient", "reset the gradient", "T1")
      .TypeConstraint(
          "T1",
          {"tensor(float16)", "tensor(float)", "tensor(double)"},
          "Constrain input and output gradient types to float tensors.")
      .TypeConstraint(
          "T2",
          OpSchema::all_tensor_types(),
          "reset_signal can be of any tensor type.")
      .TypeAndShapeInferenceFunction([](ONNX_NAMESPACE::InferenceContext& ctx) {
        propagateShapeAndTypeFromFirstInput(ctx);
      });
}
}  // namespace training
}  // namespace onnxruntime
