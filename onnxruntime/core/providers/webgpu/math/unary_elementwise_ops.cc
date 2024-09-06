// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/webgpu/math/unary_elementwise_ops.h"
#include "core/providers/webgpu/webgpu_supported_types.h"

namespace onnxruntime {
namespace webgpu {
Status UnaryElementwiseProgram::GenerateShaderCode(ShaderHelper& shader) const {
  const auto& input = shader.AddInput("x",
                                      ToProgramVariableDataType(Inputs()[0].tensor->GetElementType(), 4),
                                      ShaderVariable::UseUniform | additional_usage_);
  const auto& output = shader.AddOutput("y",
                                        ToProgramVariableDataType(Outputs()[0].tensor->GetElementType(), 4),
                                        ShaderVariable::UseUniform);
  shader.AppendImplementation(additional_impl_);
  shader.MainFunctionBody(shader.GuardAgainstOutOfBoundsWorkgroupSizes("uniforms.vec_size"),
                          "  let a = ", input.GetByOffset("global_idx"), ";\n  ",
                          output.SetByOffset("global_idx", expression_));

  return Status::OK();
}

Status UnaryElementwise::ComputeInternal(ComputeContext& context) const {
  const auto* input_tensor = context.Input(0);
  auto* output_tensor = context.Output(0, input_tensor->Shape());
  int64_t size = input_tensor->Shape().Size();
  if (size == 0) {
    return Status::OK();
  }
  SafeInt<uint32_t> vec_size = (size + 3) / 4;
  UnaryElementwiseProgram program{kernel_name_, expression_, additional_impl_, additional_usage_};
  program
      .Inputs({{input_tensor, ProgramTensorMetadataDependency::Type, {vec_size}}})
      .Outputs({{output_tensor, ProgramTensorMetadataDependency::None, {vec_size}}})
      .DispatchGroupSize((vec_size + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE)
      .UniformVariables({
          {static_cast<uint32_t>(vec_size)},
      });
  if (!cache_hint.empty()) {
    program.CacheHint(cache_hint);
  }
  ORT_RETURN_IF_ERROR(ConfigureProgram(program));
  return context.RunProgram(program);
}

#define WEBGPU_ELEMENTWISE_IMPL(OP_TYPE, ...)                                            \
  class OP_TYPE final : public UnaryElementwise {                                        \
   public:                                                                               \
    OP_TYPE(const OpKernelInfo& info) : UnaryElementwise{info, #OP_TYPE, __VA_ARGS__} {} \
  };

#define WEBGPU_ELEMENTWISE_KERNEL(OP_TYPE_AND_CLASS_NAME, VERSION, TYPE)      \
  ONNX_OPERATOR_KERNEL_EX(                                                    \
      OP_TYPE_AND_CLASS_NAME, kOnnxDomain, VERSION, kWebGpuExecutionProvider, \
      KernelDefBuilder().TypeConstraint("T", TYPE),                           \
      OP_TYPE_AND_CLASS_NAME);

#define WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(OP_TYPE_AND_CLASS_NAME, VERSION_FROM, VERSION_TO, TYPE) \
  ONNX_OPERATOR_VERSIONED_KERNEL_EX(                                                                \
      OP_TYPE_AND_CLASS_NAME, kOnnxDomain, VERSION_FROM, VERSION_TO, kWebGpuExecutionProvider,      \
      KernelDefBuilder().TypeConstraint("T", TYPE),                                                 \
      OP_TYPE_AND_CLASS_NAME);

//
// math
//

WEBGPU_ELEMENTWISE_IMPL(Abs, "abs(a)")
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Abs, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Abs, 13, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Neg, "-a")
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Neg, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Neg, 13, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Floor, "floor(a)")
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Floor, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Floor, 13, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Ceil, "ceil(a)")
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Ceil, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Ceil, 13, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Reciprocal, "1.0/a")
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Reciprocal, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Reciprocal, 13, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Sqrt, "sqrt(a)")
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Sqrt, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Sqrt, 13, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Exp, "exp(a)")
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Exp, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Exp, 13, WebGpuSupportedFloatTypes())

constexpr char ErfImpl[] = R"(
const r0 = 0.3275911;
const r1 = 0.254829592;
const r2 = -0.284496736;
const r3 = 1.421413741;
const r4 = -1.453152027;
const r5 = 1.061405429;

fn erf_v(v: x_value_t) -> x_value_t {
  let absv = abs(v);
  let x = 1.0 / (1.0 + r0 * absv);
  return sign(v) * (1.0 - ((((r5 * x + r4) * x + r3) * x + r2) * x + r1) * x * exp(-absv * absv));
}
)";

WEBGPU_ELEMENTWISE_IMPL(Erf, "erf_v(a)", ErfImpl, ShaderVariable::UseValueTypeAlias)
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Erf, 9, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Erf, 13, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Log, "log(a)")
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Log, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Log, 13, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Sigmoid, "1.0 / (1.0 + exp(-a))")
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Sigmoid, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Sigmoid, 13, WebGpuSupportedFloatTypes())

constexpr char HardSigmoidImpl[] = R"(
fn hard_sigmoid_v(v: vec4<x_element_t>) -> vec4<x_element_t> {
  let alpha = x_element_t(uniforms.f32_attr[0]);
  let beta_v = vec4<x_element_t>(uniforms.f32_attr[1]);
  return max(vec4<x_element_t>(0.0),
             min(vec4<x_element_t>(1.0), alpha * v + beta_v));
}
)";
class HardSigmoid final : public UnaryElementwise {
 public:
  HardSigmoid(const OpKernelInfo& info)
      : UnaryElementwise{info, "HardSigmoid", "hard_sigmoid_v(a)", HardSigmoidImpl, ShaderVariable::UseElementTypeAlias} {
    // attr[0] is alpha, attr[1] is beta
    info.GetAttrOrDefault("alpha", attr, 0.2f);
    info.GetAttrOrDefault("beta", attr + 1, 0.5f);
  }

  Status ConfigureProgram(UnaryElementwiseProgram& program) const override {
    program.UniformVariables({gsl::make_span(attr, 2), {}});
    return Status::OK();
  }

 protected:
  float attr[2];
};

WEBGPU_ELEMENTWISE_KERNEL(HardSigmoid, 6, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Sin, "sin(a)")
WEBGPU_ELEMENTWISE_KERNEL(Sin, 7, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Cos, "cos(a)")
WEBGPU_ELEMENTWISE_KERNEL(Cos, 7, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Tan, "tan(a)")
WEBGPU_ELEMENTWISE_KERNEL(Tan, 7, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Asin, "asin(a)")
WEBGPU_ELEMENTWISE_KERNEL(Asin, 7, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Acos, "acos(a)")
WEBGPU_ELEMENTWISE_KERNEL(Acos, 7, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Atan, "atan(a)")
WEBGPU_ELEMENTWISE_KERNEL(Atan, 7, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Sinh, "sinh(a)")
WEBGPU_ELEMENTWISE_KERNEL(Sinh, 9, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Cosh, "cosh(a)")
WEBGPU_ELEMENTWISE_KERNEL(Cosh, 9, WebGpuSupportedFloatTypes())

// built-in function tanh() does not work with large input (f32 88.7 or f16 11.09)
// https://github.com/gpuweb/gpuweb/issues/4458
constexpr char TanhImpl[] = R"(
fn tanh_v(a: x_value_t) -> x_value_t {
  let expr = exp(-2 * abs(a));
  return sign(a) * (1 - expr) / (1 + expr);
}
)";
WEBGPU_ELEMENTWISE_IMPL(Tanh, "tanh_v(a)", TanhImpl, ShaderVariable::UseValueTypeAlias)
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Tanh, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Tanh, 13, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Asinh, "asinh(a)")
WEBGPU_ELEMENTWISE_KERNEL(Asinh, 9, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Acosh, "acosh(a)")
WEBGPU_ELEMENTWISE_KERNEL(Acosh, 9, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Atanh, "atanh(a)")
WEBGPU_ELEMENTWISE_KERNEL(Atanh, 9, WebGpuSupportedFloatTypes())

// todo: logical ops

//
// activation
//

// todo: clip

class LinearUnit : public UnaryElementwise {
 public:
  LinearUnit(const OpKernelInfo& info,
             const std::string& kernel_name,
             const std::string& expression,
             const std::string& additional_impl,
             float default_alpha)
      : UnaryElementwise{info, kernel_name, expression, additional_impl, ShaderVariable::UseElementTypeAlias} {
    info.GetAttrOrDefault("alpha", &alpha_, default_alpha);
  }

  Status ConfigureProgram(UnaryElementwiseProgram& program) const override {
    program.UniformVariables({alpha_, {}});
    return Status::OK();
  }

 protected:
  float alpha_;
};

#define WEBGPU_LU_IMPL(OP_TYPE, ...)                                               \
  class OP_TYPE final : public LinearUnit {                                        \
   public:                                                                         \
    OP_TYPE(const OpKernelInfo& info) : LinearUnit{info, #OP_TYPE, __VA_ARGS__} {} \
  };

constexpr char EluImpl[] = R"(
fn elu(a: x_element_t) -> x_element_t {
  let alpha = x_element_t(uniforms.f32_attr);
  return select((exp(a) - 1.0) * alpha, a, a >= 0.0);
}

fn elu_v(v: vec4<x_element_t>) -> vec4<x_element_t> {
  return vec4(elu(v.x), elu(v.y), elu(v.z), elu(v.w));
}
)";

WEBGPU_LU_IMPL(Elu, "elu_v(a)", EluImpl, 1.0)
WEBGPU_ELEMENTWISE_KERNEL(Elu, 6, WebGpuSupportedFloatTypes())

class Gelu : public UnaryElementwise {
 public:
  Gelu(const OpKernelInfo& info)
      : UnaryElementwise{info,
                         "Gelu",
                         info.GetAttrOrDefault<std::string>("approximate", "none") == "tanh" ? TanhBasedImpl : DefaultImpl,
                         info.GetAttrOrDefault<std::string>("approximate", "none") == "tanh" ? TanhImpl : ErfImpl,
                         ShaderVariable::UseValueTypeAlias} {
    cache_hint = info.GetAttrOrDefault<std::string>("approximate", "none");
  }

  constexpr static const char DefaultImpl[] = "0.5 * a * (1.0 + erf_v(a * 0.7071067811865475))";
  constexpr static const char TanhBasedImpl[] = "0.5 * a * (1 + tanh_v(0.7978845608028654 * (a + 0.044715 * a * a * a)))";
};

WEBGPU_ELEMENTWISE_KERNEL(Gelu, 20, WebGpuSupportedFloatTypes())

WEBGPU_ELEMENTWISE_IMPL(Relu, "select(x_value_t(0), a, a > x_value_t(0))", "", ShaderVariable::UseValueTypeAlias)
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Relu, 6, 12, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(Relu, 13, 13, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(Relu, 14, WebGpuSupportedFloatTypes())

WEBGPU_LU_IMPL(LeakyRelu, "select(x_element_t(uniforms.f32_attr) * a, a, a >= vec4<x_element_t>(0))", "", 0.01f)
WEBGPU_ELEMENTWISE_VERSIONED_KERNEL(LeakyRelu, 6, 15, WebGpuSupportedFloatTypes())
WEBGPU_ELEMENTWISE_KERNEL(LeakyRelu, 16, WebGpuSupportedFloatTypes())

WEBGPU_LU_IMPL(ThresholdedRelu, "select(vec4<x_element_t>(0), a, a > vec4<x_element_t>(uniforms.f32_attr))", "", 1.0f)
WEBGPU_ELEMENTWISE_KERNEL(ThresholdedRelu, 10, WebGpuSupportedFloatTypes())

// TODO: add other unary elementwise ops

}  // namespace webgpu
}  // namespace onnxruntime
