// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/providers/webgpu/program.h"
#include "core/providers/webgpu/webgpu_kernel.h"

namespace onnxruntime {
namespace contrib {
namespace webgpu {

using namespace onnxruntime::webgpu;

class FastGeluProgram final : public Program<FastGeluProgram> {
 public:
  FastGeluProgram(const std::string& kernel_name, int bias_components) : Program{kernel_name}, bias_components_{bias_components} {
  }

  Status GenerateShaderCode(ShaderHelper& sh) const override;

  WEBGPU_PROGRAM_DEFINE_UNIFORM_VARIABLES({"vec_size", ProgramUniformVariableDataType::Uint32});

 private:
  int bias_components_;
};

class FastGelu final : public WebGpuKernel {
 public:
  FastGelu(const OpKernelInfo& info) : WebGpuKernel(info) {}

  Status ComputeInternal(onnxruntime::webgpu::ComputeContext& context) const override;
};

}  // namespace webgpu
}  // namespace contrib
}  // namespace onnxruntime
