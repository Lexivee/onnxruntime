// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/providers/js/js_kernel.h"

namespace onnxruntime {
namespace js {

template <typename T>
class Gemm : public JsKernel {
 public:
  Gemm(const OpKernelInfo& info) : JsKernel(info) {
    float alpha = info.GetAttrOrDefault<float>("alpha", 1.0f);
    float beta = info.GetAttrOrDefault<float>("beta", 1.0f);
    int64_t transA = info.GetAttrOrDefault<int64_t>("transA", 0);
    int64_t transB = info.GetAttrOrDefault<int64_t>("transB", 0);

    // currently only support Conv2D. TODO: support other
    JSEP_INIT_KERNEL_ATTRIBUTE(Gemm, ({
                                 "alpha" : $1,
                                 "beta" : $2,
                                 "transA" : Number($3),
                                 "transB" : Number($4),
                               }),
                               static_cast<float>(alpha),
                               static_cast<float>(beta),
                               static_cast<int64_t>(transA),
                               static_cast<int64_t>(transB));
  }
};

}  // namespace js
}  // namespace onnxruntime
