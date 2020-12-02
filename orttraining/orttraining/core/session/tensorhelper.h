// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/framework/ml_value.h"
#include "core/session/inference_session.h"

namespace onnxruntime {
OrtValue SliceTensor(const OrtValue& orig_value, const size_t slice_id,
                     const size_t slice_axis, const size_t num_slices, onnxruntime::InferenceSession& session_state);
}  // namespace onnxruntime