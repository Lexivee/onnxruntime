// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <Python.h>
#include "core/framework/tensorprotoutils.h"
#include "core/language_interop_ops/torch/custom_function_register.h"
#include "core/language_interop_ops/torch/torch_proxy.h"
#include "core/platform/env.h"
#include "core/util/dlpack_convertor.h"

namespace onnxruntime {
namespace language_interop_ops {
namespace torch {

// Holder of GIL state.
// It automatically acquire the state upon creation and
// release the acquired state after being destroyed.
class GilGuard {
 public:
  GilGuard() : state_(PyGILState_Ensure()){};
  ~GilGuard() { PyGILState_Release(state_); };

 private:
  PyGILState_STATE state_;
};

void DlpackCapsuleDestructor(PyObject* data) {
  DLManagedTensor* dlmanged_tensor = (DLManagedTensor*)PyCapsule_GetPointer(data, "dltensor");
  if (dlmanged_tensor) {
    // the dlmanged_tensor has not been consumed, call deleter ourselves.
    dlmanged_tensor->deleter(const_cast<DLManagedTensor*>(dlmanged_tensor));
  } else {
    // the dlmanged_tensor has been consumed,
    // PyCapsule_GetPointer has set an error indicator.
    PyErr_Clear();
  }
}

bool ExtractPointerOutput(PyObject* pyObj, std::vector<void*>& outputs) {
  void* prt = PyLong_AsVoidPtr(pyObj);
  outputs.push_back(prt);
  return true;
}

TorchProxy& TorchProxy::GetInstance() {
  static TorchProxy proxy;
  return proxy;
}

TorchProxy::TorchProxy() {
  if (Py_IsInitialized()) {
    initialized_ = false;
  }
  Py_Initialize();
  initialized_ = true;
}

TorchProxy::~TorchProxy() {
  if (initialized_) {
    Py_Finalize();
  }
}

void CheckArguments(
    const size_t len,
    const std::vector<int64_t>& requires_grads,
    const std::vector<OrtValue*>& tensor_args,
    const std::vector<int64_t>& tensor_indices,
    const std::vector<void*> obj_args,
    const std::vector<int64_t>& obj_indices) {
  ORT_ENFORCE(requires_grads.size() == len);
  ORT_ENFORCE(tensor_args.size() + obj_args.size() == len);
  ORT_ENFORCE(tensor_args.size() == tensor_indices.size());
  ORT_ENFORCE(obj_args.size() == obj_indices.size());

  for (const auto i : requires_grads) {
    ORT_ENFORCE(i == 0 || i == 1, "Flag of requiring gradient must be either 0 (not required) or 1 (required) but got ", i);
  }

  std::vector<int64_t> counts(len, 0);
  for (const auto i : tensor_indices) {
    ORT_ENFORCE(i >= 0 && static_cast<size_t>(i) < len, "Index range is from 0 to ", len - 1, ", but found ", i);
    counts.at(i) += 1;
  }
  for (const auto i : obj_indices) {
    ORT_ENFORCE(i >= 0 && static_cast<size_t>(i) < len, "Index range is from 0 to ", len - 1, ", but found ", i);
    counts.at(i) += 1;
  }
  for (size_t i = 0; i < len; ++i) {
    ORT_ENFORCE(counts.at(i) == 1, "Duplicated or unused argument index detected: ", i);
  }
}

// len: the number of input arguments.
// tensor_indices: if tensor_indices[i] is j,
//                 then the j-th input argument should be a tensor.
PyObject* CreateTensorFlags(
    const size_t len,
    const std::vector<int64_t>& tensor_indices) {
  PyObject* flags = PyList_New(len);

  // First we fill the list with 0. Later we will
  // assign 1's to tensors' corresponding positions.
  for (size_t i = 0; i < len; ++i) {
    PyObject* zero = PyLong_FromLong(0);
    PyList_SetItem(flags, i, zero);
  }

  for (const auto i : tensor_indices) {
    PyObject* one = PyLong_FromLong(1);
    PyList_SetItem(flags, i, one);
  }

  return flags;
}

// flags[i] corresponds to the i-th input of apply/backward.
PyObject* CreateRequiresGradFlags(
    const std::vector<int64_t>& requires_grads) {
  PyObject* flags = PyList_New(requires_grads.size());
  for (size_t i = 0; i < requires_grads.size(); ++i) {
    PyObject* value;
    if (requires_grads.at(i) != 0) {
      value = PyLong_FromLong(1);
    } else {
      value = PyLong_FromLong(0);
    }
    PyList_SetItem(flags, i, value);
  }
  return flags;
}

void InvokeRunner(
    PyObject* callback_runner,
    PyObject* args,
    std::vector<void*>& returned_args) {
  GilGuard guard;
  PyObject* result = PyObject_CallObject(reinterpret_cast<PyObject*>(callback_runner), args);
  if (PyErr_Occurred()) {
    PyErr_Print();
    ORT_THROW("Python function execution fails with the above information.");
  }

  ORT_ENFORCE(PyTuple_Check(result), "Python function must return a tuple.");
  for (int i = 0; i < PyTuple_Size(result); ++i) {
    ORT_ENFORCE(ExtractPointerOutput(PyTuple_GetItem(result, i), returned_args));
  }
}

PyObject* CreateForwardArguments(
    PyObject* callback,
    const size_t len,
    const std::vector<int64_t>& requires_grads,
    const std::vector<OrtValue*>& tensor_args,
    const std::vector<int64_t>& tensor_indices,
    const std::vector<void*>& obj_args,
    const std::vector<int64_t>& obj_indices,
    bool is_training_mode) {
  ORT_ENFORCE(PyCallable_Check(callback), "Forward callback is not callable.");
  PyObject* args = PyTuple_New(4 + len);
  PyObject* tensor_flags = CreateTensorFlags(len, tensor_indices);
  PyObject* requires_grad_flags = CreateRequiresGradFlags(requires_grads);
  PyTuple_SetItem(args, 0, callback);
  PyTuple_SetItem(args, 1, requires_grad_flags);
  PyTuple_SetItem(args, 2, tensor_flags);
  PyTuple_SetItem(args, 3, is_training_mode ? Py_True : Py_False);

  for (size_t i = 0; i < tensor_args.size(); ++i) {
    // Wrap with DLPack, then transfer to Python for its release.
    DLManagedTensor* dlmanaged_tensor = onnxruntime::python::OrtValueToDlpack(*tensor_args[i]);
    PyObject* dltensor = PyCapsule_New(dlmanaged_tensor, "dltensor", DlpackCapsuleDestructor);
    PyTuple_SetItem(args, 4 + tensor_indices[i], dltensor);
  }

  for (size_t i = 0; i < obj_args.size(); ++i) {
    PyTuple_SetItem(args, 4 + obj_indices[i], reinterpret_cast<PyObject*>(obj_args[i]));
  }

  return args;
}

void Invoke(
    PyObject* runner,
    PyObject* callback,
    const std::vector<int64_t>& requires_grads,
    const std::vector<OrtValue*>& tensor_args,
    const std::vector<int64_t>& tensor_indices,
    const std::vector<void*>& obj_args,
    const std::vector<int64_t>& obj_indices,
    std::vector<void*>& returned_args,
    bool is_training_mode) {
  const auto len = tensor_args.size() + obj_args.size();
  CheckArguments(len, requires_grads, tensor_args, tensor_indices, obj_args, obj_indices);
  PyObject* args = CreateForwardArguments(
      callback,
      len,
      requires_grads,
      tensor_args,
      tensor_indices,
      obj_args,
      obj_indices,
      is_training_mode);
  InvokeRunner(runner, args, returned_args);
  // TODO: Free Python objects.
  // DestoryForwardArguments(args);
}

void TorchProxy::Forward(
    void* callback,
    const std::vector<int64_t>& requires_grads,
    const std::vector<OrtValue*>& tensor_args,
    const std::vector<int64_t>& tensor_indices,
    const std::vector<void*>& obj_args,
    const std::vector<int64_t>& obj_indices,
    std::vector<void*>& returned_args,
    bool is_training_mode) {
  auto runner = OrtTorchFunctionPool::GetInstance().GetForwardRunner();
  Invoke(
      runner,
      reinterpret_cast<PyObject*>(callback),
      requires_grads,
      tensor_args,
      tensor_indices,
      obj_args,
      obj_indices,
      returned_args,
      is_training_mode);
}

void TorchProxy::Backward(
    void* callback,
    const std::vector<int64_t>& requires_grads,
    const std::vector<OrtValue*>& tensor_args,
    const std::vector<int64_t>& tensor_indices,
    const std::vector<void*>& obj_args,
    const std::vector<int64_t>& obj_indices,
    std::vector<void*>& returned_args) {
  auto runner = OrtTorchFunctionPool::GetInstance().GetBackwardRunner();
  Invoke(
      runner,
      reinterpret_cast<PyObject*>(callback),
      requires_grads,
      tensor_args,
      tensor_indices,
      obj_args,
      obj_indices,
      returned_args,
      true /*is_training_mode*/);
}
}  // namespace torch
}  // namespace language_interop_ops
}  // namespace onnxruntime