// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/inlined_containers.h"
#include "core/providers/webgpu/tensor/transpose.h"
#include "core/providers/cpu/tensor/utils.h"
#include "core/providers/webgpu/shader_variable.h"
#include "core/providers/webgpu/shader_helper.h"
#include "core/providers/webgpu/webgpu_supported_types.h"

namespace onnxruntime {
namespace webgpu {

ONNX_OPERATOR_VERSIONED_KERNEL_EX(
    Transpose,
    kOnnxDomain,
    1, 12,
    kWebGpuExecutionProvider,
    (*KernelDefBuilder::Create())
        .TypeConstraint("T", WebGpuSupportedNumberTypes()),
    Transpose);

ONNX_OPERATOR_VERSIONED_KERNEL_EX(
    Transpose,
    kOnnxDomain,
    13, 20,
    kWebGpuExecutionProvider,
    (*KernelDefBuilder::Create())
        .TypeConstraint("T", WebGpuSupportedNumberTypes()),
    Transpose);

ONNX_OPERATOR_VERSIONED_KERNEL_EX(
    Transpose,
    kOnnxDomain,
    21, 22,
    kWebGpuExecutionProvider,
    (*KernelDefBuilder::Create())
        .TypeConstraint("T", WebGpuSupportedNumberTypes()),
    Transpose);

ONNX_OPERATOR_KERNEL_EX(
    Transpose,
    kOnnxDomain,
    23,
    kWebGpuExecutionProvider,
    (*KernelDefBuilder::Create())
        .TypeConstraint("T", WebGpuSupportedNumberTypes()),
    Transpose);

const std::string AppendPermFunction(gsl::span<const int64_t> perm) {
  std::ostringstream ss;
  ss.imbue(std::locale::classic());
  ss << "fn perm(i: output_indices_t)->a_indices_t {\n"
        "  var a: a_indices_t;\n";
  for (auto i = 0; i < perm.size(); ++i) {
    ss << "  a[" << perm[i] << "] = i[" << i << "];\n";
  }
  ss << "  return a;\n"
        "}\n";
  return ss.str();
}

auto SqueezeShape(const gsl::span<const int64_t>& shape, const gsl::span<const size_t>& adjusted_perm, InlinedVector<int64_t>& new_shape, InlinedVector<int64_t>& new_perm) {
  for (auto i = 0; i < shape.size(); ++i) {
    if (shape[i] != 1) {
      new_shape.push_back(shape[i]);
    }
    if (shape[adjusted_perm[i]] != 1) {
      new_perm.push_back(adjusted_perm[i]);
    }
  }
};

Status TransposeProgram::GenerateShaderCode(ShaderHelper& shader) const {
  const auto& input = shader.AddInput("a", ShaderUsage::UseUniform | ShaderUsage::UseIndicesTypeAlias);
  const auto& output = shader.AddOutput("output", ShaderUsage::UseUniform | ShaderUsage::UseIndicesTypeAlias | ShaderUsage::UseValueTypeAlias);

  if (use_shared_) {
    const auto tile_size = std::to_string(tile_size_);
    shader.AppendImplementation("var<workgroup> tile : array<array<output_value_t, " + tile_size + " + 1>, " + tile_size + ">;\n");
    shader.SetMainFunctionBody(
        "  let stride = (uniforms.output_shape[1] - 1) / " + tile_size +
        " + 1;\n"
        "  let workgroup_id_x = workgroup_idx % stride;\n"
        "  let workgroup_id_y = workgroup_idx / stride;\n"
        "  let input_col = workgroup_id_y * " +
        tile_size +
        "u + local_id.x;\n"
        "  let input_row = workgroup_id_x * " +
        tile_size +
        "u + local_id.y;\n"
        "  if (input_row < uniforms.a_shape[0] && input_col < uniforms.a_shape[1]) {\n"
        "    tile[local_id.y][local_id.x] = " +
        input.GetByIndices("a_indices_t(input_row, input_col)") +
        ";\n"
        "  }\n"
        "  workgroupBarrier();\n"
        "  let output_col = workgroup_id_x * " +
        tile_size +
        "u + local_id.x;\n"
        "  let output_row = workgroup_id_y * " +
        tile_size +
        "u + local_id.y;\n"
        "  if (output_row < uniforms.output_shape[0] && output_col < uniforms.output_shape[1]) {\n    " +
        output.SetByIndices("output_indices_t(output_row, output_col)", "tile[local_id.x][local_id.y]") + "\n  }");
  } else {
    shader.AppendImplementation(AppendPermFunction(this->perm_));
    shader.SetMainFunctionBody(shader.GuardAgainstOutOfBoundsWorkgroupSizes("uniforms.output_size"),
                               "  let indices = ", output.OffsetToIndices("global_idx"),
                               ";\n"
                               "  let x_indices = perm(indices);\n",
                               "  ",
                               output.SetByOffset("global_idx", input.GetByIndices("x_indices")));
  }
  return Status::OK();
}

Status Transpose::ComputeInternal(ComputeContext& context) const {
  const auto* input_tensor = context.Input(0);
  const TensorShape& input_shape = input_tensor->Shape();
  int32_t rank = gsl::narrow_cast<int32_t>(input_shape.NumDimensions());

  TensorShapeVector output_dims(rank);
  InlinedVector<size_t> default_perm(rank);
  const InlinedVector<size_t>* p_perm = nullptr;
  ORT_RETURN_IF_ERROR(ComputeOutputShape(*input_tensor, output_dims, default_perm, p_perm));
  TensorShape output_shape(output_dims);
  auto* output_tensor = context.Output(0, output_shape);

  InlinedVector<int64_t> new_shape{};
  InlinedVector<int64_t> new_perm{};
  SqueezeShape(input_shape.GetDims(), *p_perm, new_shape, new_perm);
  const auto channels_last = new_perm == InlinedVector<int64_t>({2, 3, 1});
  const auto channels_first = new_perm == InlinedVector<int64_t>({3, 1, 2});
  const auto use_shared = (new_shape.size() == 2 && new_perm[0] > new_perm[1]) || channels_last || channels_first;
  auto new_input_shape = use_shared ? new_shape : input_shape;
  auto new_output_shape = output_dims;
  if (use_shared) {
    new_input_shape = channels_last
                          ? InlinedVector<int64_t>({new_shape[0], new_shape[1] * new_shape[2]})
                      : channels_first
                          ? InlinedVector<int64_t>({new_shape[0] * new_shape[1], new_shape[2]})
                          : new_shape;
    new_output_shape = InlinedVector<int64_t>({new_input_shape[1], new_input_shape[0]});
  }

  uint32_t output_size = gsl::narrow_cast<int32_t>(input_tensor->Shape().Size());
  const auto tile_size = 16;
  TransposeProgram program{*p_perm, use_shared, tile_size};
  if (use_shared) {
    program.SetWorkgroupSize(tile_size, tile_size, 1);
  }

  program
      .CacheHint(absl::StrJoin(*p_perm, "-"))
      .AddInputs({{input_tensor, ProgramTensorMetadataDependency::TypeAndRank, new_input_shape, 1}})
      .AddOutputs({{output_tensor, ProgramTensorMetadataDependency::TypeAndRank, new_output_shape, 1}})
      .SetDispatchGroupSize(static_cast<uint32_t>((new_output_shape[1] + tile_size - 1) / tile_size),
                            static_cast<uint32_t>(((new_output_shape[0] + tile_size - 1) / tile_size)))
      .AddUniformVariables({
          {static_cast<uint32_t>(output_size)},
      });

  use_shared ? program.SetDispatchGroupSize(static_cast<uint32_t>((new_output_shape[1] + tile_size - 1) / tile_size),
                                            static_cast<uint32_t>(((new_output_shape[0] + tile_size - 1) / tile_size)))
             : program.SetDispatchGroupSize((output_size + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE);
  return context.RunProgram(program);
}

}  // namespace webgpu
}  // namespace onnxruntime
