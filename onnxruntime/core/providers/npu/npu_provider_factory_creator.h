

#pragma once

#include <memory>

#include "core/providers/providers.h"

namespace onnxruntime {
struct NPUProviderFactoryCreator {
  static std::shared_ptr<IExecutionProviderFactory> Create(int use_arena);
};
}  // namespace onnxruntime
