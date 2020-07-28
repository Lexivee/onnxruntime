﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "LearningModelSessionOptions.g.h"
#include <thread>
namespace WINMLP {

struct LearningModelSessionOptions : LearningModelSessionOptionsT<LearningModelSessionOptions, ILearningModelSessionOptionsNative> {
  LearningModelSessionOptions() = default;

  LearningModelSessionOptions(const LearningModelSessionOptions& options);

  uint32_t BatchSizeOverride();
  void BatchSizeOverride(uint32_t value);

  bool CloseModelOnSessionCreation();
  void CloseModelOnSessionCreation(bool value);

  STDMETHOD(OverrideIntraOpNumThreads)
  (uint32_t intraOpNumThreads);

  STDMETHOD(GetIntraOpNumThreads)
  (uint32_t* numThreads);

 private:
  // The batch size override property is used to inform the engine when the developer
  // wants to explicitly set the batch size of a model to a fixed batch size.
  //
  // 0     : dont override the model batch definitions
  // 1...n : override the model with the given batch size
  //
  // This value is a unsigned value, and users are not allowed to override models with a free batch size.
  // If the model supports free dimensional batch sizes, the caller should provide 0, to not override.
  //
  // The default value here is 1 so that models with free dimension batch sizes (which is very common)
  // can be optimized to fixed sizes.
  uint32_t batch_size_override_ = 1;

  // The close model on session creation property is used to inform the engine when the developer
  // no longer needs the learningmodelsession after session creation.
  // The engine can use the learning model during session creation to move resources rather than make copies.
  //
  // True     : Move resources in the LearningModel in to the LearningModelSession
  // False    : Copy resources in the LearningModel to the LearningModelSession
  //
  // The default value here is False so that models are not automatically closed on session creation.
  bool close_model_on_session_creation_ = false;

  // The intra operator num threads property is used to control the number of threads used in the threadpool for intra operator calculations.
  // The default value here is the maximum number of logical cores to ensure that the default behavior of WinML always runs the fastest.
  // WARNING: Setting a number higher than the maximum number of logical cores may result in an inefficient threadpool
  uint32_t intra_op_num_threads_override_ = std::thread::hardware_concurrency();
};

}  // namespace WINMLP

namespace WINML::factory_implementation {
struct LearningModelSessionOptions : LearningModelSessionOptionsT<LearningModelSessionOptions, implementation::LearningModelSessionOptions> {
};
}  // namespace WINML::factory_implementation
