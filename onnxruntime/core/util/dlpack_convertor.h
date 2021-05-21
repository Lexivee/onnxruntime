// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#ifdef ENABLE_TRAINING
#include "core/framework/ml_value.h"
#include <dlpack/dlpack.h>
#include <Python.h>

// This convertor will take an OrtValue and wrap it as a DLPack tensor

namespace onnxruntime {
namespace python {

// This may create a new ownership to the underlying tensor in OrtValue,
// so we do pass-by-value here. We don't use pass-by-reference because
// it implies no new ownership.
DLManagedTensor* OrtValueToDlpack(OrtValue ort_value);

// DLPack uses same config for both bool and unit8. Parameter is_bool_tensor is to
// tell ORT the data type when creating OrtValue.
OrtValue DlpackToOrtValue(DLManagedTensor* dlpack, bool is_bool_tensor = false);

void DlpackCapsuleDestructor(PyObject* data); 
}  // namespace python
}  // namespace onnxruntime
#endif