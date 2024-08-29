# How to Write WebGPU EP Kernel

This document describes how to write a WebGPU EP kernel for ONNX Runtime.

The following document will assume the operator name is `Example`, and you will see class `ExampleProgram` and `ExampleOpKernel` in the examples. Replace `Example` with the actual operator name you are implementing.

Follow the following steps to create a WebGPU kernel:

## 1. Decide _filename_ and _cateogory_, and create a new file at:

`onnxruntime/core/providers/webgpu/{category}/{filename}.cc`

- filename is usually a snake_case_name of the operator name, or a descriptive name if it includes multiple operators (eg. binary_elementwise_ops.cc)
- category is the subfolder representing the operator category (eg. math/nn/controlflow)

  see folder structure under onnxruntime/core/providers/cpu/ or onnxruntime/core/providers/cuda/ for examples

## 2. Declare a new Program class

### 2.1. The Program class should inherit from Program<YourProgramName>:

```c++
class ExampleProgram : public Program<ExampleProgram> {
// ...
}
```

### 2.2. The Program class can define the following information:

There are 3 types of definitions described as below. All of them are optional. If not specified, it is treated as empty. Those definitions are defined as static const members to ensure they don't depend on any runtime information.

#### **constants**

constants are declaration of values that are never changes in the shader code. They are inserted into the WGSL source code like this:

```wgsl
const A : u32 = 64;
```

Use macro `WEBGPU_PROGRAM_DEFINE_CONSTANTS` to define constants in your Program class.

#### **overridable constants**

overridable constants are similar to constants, but they can be overridden before the compute pipeline is created. Overridable constants may or may not have a default value. They are inserted into the WGSL source code like this:

```wgsl
override B : u32 = 64;
override C : f32;
```

Use macro `WEBGPU_PROGRAM_DEFINE_OVERRIDABLE_CONSTANTS` to define overridable constants in your Program class.

#### **uniform definitions**

uniform definitions are declaration of uniform varables. Their names and type must be defined and cannot be changed. Their values(including length) can be set at runtime.

Use macro `WEBGPU_PROGRAM_DEFINE_UNIFORMS` to define uniform definitions in your Program class.

### 2.3. The Program class should override the `GenerateShaderCode` method:

```c++
Status GenerateShaderCode(ShaderHelper& sh) const override;
```

In the function implementation, `sh` is an instance of `ShaderHelper` which provides a set of helper functions to generate shader code.

Example:

```c++
Status UnaryElementwiseProgram::GenerateShaderCode(ShaderHelper& shader) const {
  const auto& input = shader.AddVariable(ProgramVariableScope::Input,
                                         "x",
                                         ToProgramVariableDataType(Inputs()[0].tensor->GetElementType(), 4),
                                         1);
  const auto& output = shader.AddVariable(ProgramVariableScope::Output,
                                          "y",
                                          ToProgramVariableDataType(Outputs()[0]->GetElementType(), 4),
                                          1);
  shader.AppendImplementation(additional_impl_);
  shader.MainFunctionBody(shader.GuardAgainstOutOfBoundsWorkgroupSizes("uniforms.vec_size"),
                          "let a = ", input.GetByOffset("global_idx"), ";\n",
                          output.SetByOffset("global_idx", expression_));

  return Status::OK();
}
```

`ShaderHelper::AddVariable` creates an instace of `ShaderVariable`. The class `ShaderVariable` is similar to `IndicesHelper` in onnxruntime-web. It provides a set of helper functions as value/indices/offset getter/setter.

`ShaderHelper::AppendImplementation` inserts additional implementation code into the shader code. It will be put before the main function.

`ShaderHelper::MainFunctionBody` generates the main function body. It accepts arbitrary number of arguments and concatenates them into the main function body.

### 2.3. Lifecycle of the Program class

For each calls into the `ExampleOpKernel::ComputeInternal()` method, a new instance of the `ExampleProgram` class should be created as local variable (The detail will be explained in `ExampleOpKernel` as below). The Program instance is destroyed when reaching the end of scope.

A few functions can be called on the Program instance:

- call `ProgramBase::Inputs` and `ProgramBase::Outputs` to set input/output tensor info.
- call `ProgramBase::CacheHint` to set the cache hint.
- call `ProgramBase::UniformsVariables`(optional) and `ProgramBase::OverridableConstants`(optional) to set runtime info of uniforms and overridable constants. They need to match the corresponding definitions described above.
- call `ProgramBase::DispatchGroupSize` and `ProgramBase::WorkgroupSize`(optional) to set the dispatch group size and workgroup size.

## 3. Declare a new OpKernel class

### 3.1. The OpKernel class should inherit from WebGpuKernel:

```c++
class ExampleOpKernel : public WebGpuKernel {
// ...
}
```

### 3.2. The OpKernel class should override the `ComputeInternal` method:

```c++
Status ComputeInternal(ComputeContext& context) const override;
```

Usually, in the implementation, we do 3 things:
- Create a local variable of the Program class.
- Set a few runtime info of the Program instance.
- Call `context.RunProgram(program)` to run the program and return the status.

Complicated operators may do more things. Check header files and existing implementations for more details.

## 4. Register the operator

Register the operator just like any EP does. Check existing implementations for more details.

Please note that registration is composed of 2 parts:
- Use macros like `ONNX_OPERATOR_KERNEL_EX` or `ONNX_OPERATOR_VERSIONED_KERNEL_EX` (or wrap a new macro as what we usually do) to register the operator in kernel source code file.
- Add the operator to onnxruntime/core/providers/webgpu/webgpu_execution_provider.cc

## 5. Write tests

This section is WIP.

## 6. Build and test

use `build.bat --use_webgpu --skip_tests` to build the WebGPU EP. For Release build, append `--config Release` or `--config RelWithDebInfo` to the command line.

to test, find the "onnx_test_runner.exe" in your build folder. run it like:
```
onnx_test_runner.exe -v -e webgpu -C "session.disable_cpu_ep_fallback|1" C:\code\onnxruntime\js\test\data\node\opset17\test_abs
```

> Assume C:\code\onnxruntime is the root of your onnxruntime repo
>
> if it does not exist, run the following in your onnxruntime repo root:
> ```
> cd js
> npm ci
> npm run prepare-node-tests
> ```
