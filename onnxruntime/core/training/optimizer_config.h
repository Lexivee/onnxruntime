// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <unordered_map>

#include "core/graph/node_arg.h"

namespace onnxruntime {
namespace training {
// configuration per optimizer node
struct OptimizerNodeConfig {
  std::string name{};
  const NodeArg* fp16_weight_arg{};
  std::string lr_feed_name{};
  std::unordered_map<std::string, float> attributes{};
  bool use_fp16_moments{false};
};

// configuration for optimizer portion of graph
struct OptimizerGraphConfig {
  int world_rank{0};
  int world_size{1};
  bool use_mixed_precision{false};
  bool always_do_update{false};
  bool allreduce_in_fp16{false};
  int gradient_accumulation_steps{1};
  std::string loss_scale_input_name{};  // empty string means no loss scaling factor is applied
};
}  // namespace training
}  // namespace onnxruntime
