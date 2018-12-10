/**
* Copyright (c) 2016-present, Facebook, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
/* Modifications Copyright (c) Microsoft. */

#pragma once

#include "core/common/common.h"
#include "core/common/exceptions.h"
#include "core/framework/op_kernel.h"
#include "core/providers/cpu/nn/autopad_type.h"
#include "core/framework/tensor.h"
#include "core/util/math_cpuonly.h"

namespace onnxruntime {

template <typename T>
class BatchNorm : public OpKernel {
 public:
  BatchNorm(const OpKernelInfo& op_kernel_info) : OpKernel(op_kernel_info) {
    float tmp_eplison;
    if (op_kernel_info.GetAttr<float>("epsilon", &tmp_eplison).IsOK()) {
      epsilon_ = tmp_eplison;
    }
  }

  Status Compute(OpKernelContext* p_op_kernel_context) const override;

 protected:
  float epsilon_ = 1e-5f;
  int64_t is_test_;  // ignored in this implementation since we're doing inferencing only.
};
}  // namespace onnxruntime
