// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "vitisai_profiler.h"

namespace onnxruntime {
namespace profiling {

#if defined(USE_VITISAI)

bool VitisaiProfiler::StartProfiling(TimePoint tp) {
  return true;
}

void VitisaiProfiler::EndProfiling(TimePoint tp, Events& events) {

  auto time_point = \
         std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();

  std::vector<std::tuple<std::string, int, int, long long, long long>> api_events;
  std::vector<std::tuple<std::string, int, int, long long, long long>> kernel_events;
  profiler_collect(api_events, kernel_events);

  std::unordered_map<std::string, std::string> event_args;

  for (auto& a : api_events) {
    events.emplace_back(EventCategory::API_EVENT,
                        std::get<1>(a),
                        std::get<2>(a),
                        std::get<0>(a),
                        std::get<3>(a) - time_point,
                        std::get<4>(a),
                        event_args);
  }

  for (auto& k : kernel_events) {
    events.emplace_back(EventCategory::KERNEL_EVENT,
                        std::get<1>(k),
                        std::get<2>(k),
                        std::get<0>(k),
                        std::get<3>(k) - time_point,
                        std::get<4>(k),
                        event_args);
  }
}

#endif

}  // namespace profiling
}  // namespace onnxruntime
