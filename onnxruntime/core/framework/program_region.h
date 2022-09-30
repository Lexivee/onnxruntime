#pragma once
#include "core/common/inlined_containers.h"

namespace onnxruntime {

struct ProgramRegion {
  size_t start_pc;
  size_t end_pc;

  InlinedVector<std::pair<size_t, size_t> > stream_pc_range;
};

}  // namespace onnxruntime
