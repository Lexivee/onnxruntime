// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include <Python.h>
#include "core/platform/env.h"
#include "core/framework/ml_value.h"
#include "core/framework/op_kernel_context_internal.h"
#include "core/language_interop_ops/torch/object_pointer.h"

namespace onnxruntime {
namespace language_interop_ops {
namespace torch {

using OnnxAttrs = std::unordered_map<std::string, std::string>;

// PyObject RAII wrapper
using PythonObjectPtr = ObjectPointer<PyObject>;
template class ObjectPointer<PyObject>;

class TorchProxy {
 public:
  static TorchProxy& GetInstance();

  void Forward(
      void* callback,
      const std::vector<int64_t>& requires_grads,
      const std::vector<OrtValue*>& tensor_args,
      const std::vector<int64_t>& tensor_indices,
      const std::vector<void*>& obj_args,
      const std::vector<int64_t>& obj_indices,
      std::vector<void*>& outputs,
      bool is_training_mode);

  void Backward(
      void* callback,
      const std::vector<int64_t>& requires_grads,
      const std::vector<OrtValue*>& tensor_args,
      const std::vector<int64_t>& tensor_indices,
      const std::vector<void*>& obj_args,
      const std::vector<int64_t>& obj_indices,
      std::vector<void*>& outputs);

  int32_t GetGil() const;
  void PutGil(int32_t) const;

 private:
  TorchProxy(){};
  ~TorchProxy(){};
};
}  // namespace torch
}  // namespace language_interop_ops
}  // namespace onnxruntime
