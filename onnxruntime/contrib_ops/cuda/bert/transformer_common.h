#pragma once

#include "core/providers/shared_library/provider_api.h"
#include "core/common/status.h"
#include "core/framework/float16.h"
#include "core/providers/cuda/cuda_pch.h"
#include "core/providers/cuda/shared_inc/cuda_call.h"

namespace onnxruntime {
namespace contrib {
namespace cuda {

class TransformerOptions {
 public:
  static const TransformerOptions* GetInstance();

  bool IsPrecisionMode() const { return is_precision_mode_; }

  bool DisablePersistentSoftmax() const { return disable_persistent_softmax_; }

  void Initialize(int value) {
    is_precision_mode_ = (value & 0x01) > 0;
    disable_persistent_softmax_ = (value & 0x02) > 0;
    initialized_ = true;
  }

 private:
  // Default is false. If the mode is on, prefer precision than speed.
  bool is_precision_mode_{false};

  // Disable persistent softmax.
  bool disable_persistent_softmax_{false};

  bool initialized_{false};

  static TransformerOptions instance;
};

}  // namespace cuda
}  // namespace contrib
}  // namespace onnxruntime