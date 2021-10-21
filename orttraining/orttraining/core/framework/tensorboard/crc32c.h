/* Copyright 2016 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

/* Modifications Copyright (c) Microsoft. */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace onnxruntime {
namespace training {
namespace tensorboard {

uint32_t Crc32cUpdate(uint32_t init_crc, const char* data, size_t size);

// Returns the CRC-32C (Castagnoli) checksum for data[0, size-1] (https://en.wikipedia.org/wiki/Cyclic_redundancy_check)
inline uint32_t Crc32c(const char* data, size_t size) { return Crc32cUpdate(0, data, size); }

}  // namespace tensorboard
}  // namespace training
}  // namespace onnxruntime
