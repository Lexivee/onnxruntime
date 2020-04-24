﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "std.h"
#include "winrt_headers.h"

namespace ProtobufHelpers
{
    // LoadTensorFromProtobufFile take a path to a FP32 data file and loads it into a 32bit array or
    // 16bit array based on isFp16
    winml::ITensor LoadTensorFromProtobufFile(const std::wstring& filePath, bool isFp16);
    // LoadTensorFloat16FromProtobufFile takes a path to a FP16 data file and loads it into a 16bit array
    winml::TensorFloat16Bit LoadTensorFloat16FromProtobufFile(const std::wstring& filePath);

    winml::LearningModel CreateModel(
        winml::TensorKind kind,
        const std::vector<int64_t>& shape,
        uint32_t num_elements = 1);
}
