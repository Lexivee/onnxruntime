// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/resource.h"

#define ORT_ROCM_RESOURCE_VERSION 1

enum RocmResource : int {
  hip_stream_t = rocm_resource_offset,
  miopen_handle_t,
  hipblas_handle_t
};
