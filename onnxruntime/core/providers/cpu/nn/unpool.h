// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <cmath>
#include "core/common/common.h"
#include "core/framework/op_kernel.h"
#include "core/providers/cpu/nn/autopad_type.h"

namespace onnxruntime {

class MaxUnpool : public OpKernel {
 public:
  MaxUnpool(const OpKernelInfo& info) : OpKernel(info) {
    ORT_ENFORCE(info.GetAttrs<int64_t>("kernel_shape", kernel_shape_).IsOK(),
                "No kernel shape is set.");

    num_inputs_ = OpKernel::Node().InputDefs().size();

    if (num_inputs_ == 3 && !pads_.empty()) {
      // ignore pads attribute value
    }

    // setup defaults.
    if (!info.GetAttrs<int64_t>("pads", pads_).IsOK() || pads_.empty()) {
      pads_.resize(kernel_shape_.size() * 2, 0);
    }

    if (!info.GetAttrs<int64_t>("strides", strides_).IsOK() || strides_.empty()) {
      strides_.resize(kernel_shape_.size(), 1);
    }

    for (size_t dim = 0; dim < kernel_shape_.size(); ++dim) {
      ORT_ENFORCE(kernel_shape_[dim] > 0);
      ORT_ENFORCE(pads_[dim] < kernel_shape_[dim] && pads_[dim + kernel_shape_.size()] < kernel_shape_[dim],
                  "Pad should be smaller than kernel.");
    }

    ORT_ENFORCE(strides_.size() == kernel_shape_.size());

    // Add 4 pad values (0) for batch and channel dimensions
    pads_.insert(pads_.begin(), {0, 0});
    pads_.insert(pads_.begin() + 2 + kernel_shape_.size(), {0, 0});

    // Separate out any negative pads_ into the slices_ array
    slices_.resize(pads_.size(), 0);
    for (size_t index = 0; index < pads_.size(); index++) {
      if (pads_[index] < 0) {
        slices_[index] = pads_[index];
        pads_[index] = 0;
      }
    }
  }

  ~MaxUnpool() override = default;
  ;

  Status Compute(OpKernelContext* context) const override;

 private:
  std::vector<int64_t> kernel_shape_;
  std::vector<int64_t> pads_;
  std::vector<int64_t> strides_;
  std::vector<int64_t> slices_;  // All of the negative padding values are separated out into slices_
  int64_t num_inputs_;
};

}  // namespace onnxruntime
