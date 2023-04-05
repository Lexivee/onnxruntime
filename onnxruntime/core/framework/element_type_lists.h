// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <string>

#include "boost/mp11.hpp"

#include "core/common/type_list.h"
#include "core/framework/float8.h"
#include "core/framework/float16.h"

namespace onnxruntime {

// Contains type lists grouping various ORT element types.
// Element type refers to the element type of a Tensor, Sequence, etc.
namespace element_type_lists {

using AllFixedSizeExceptHalf =
    TypeList<
        float,
        double,
        int64_t,
        uint64_t,
        int32_t,
        uint32_t,
        int16_t,
        uint16_t,
        int8_t,
        uint8_t,
        bool>;

using AllFixedSize =
    TypeList<
        float,
        double,
        int64_t,
        uint64_t,
        int32_t,
        uint32_t,
        int16_t,
        uint16_t,
        int8_t,
        uint8_t,
        MLFloat16,
        BFloat16,
        bool>;

using All_float8 =
    TypeList<
        Float8E4M3FN,
        Float8E4M3FNUZ,
        Float8E5M2,
        Float8E5M2FNUZ>;

using All_with_float8 =
    boost::mp11::mp_push_back<
        AllFixedSize,
        Float8E4M3FN,
        Float8E4M3FNUZ,
        Float8E5M2,
        Float8E5M2FNUZ>;

using AllIeeeFloatExceptHalf =
    TypeList<
        float,
        double>;

using AllIeeeFloat =
    TypeList<
        float,
        double,
        MLFloat16,
        Float8E4M3FN,
        Float8E4M3FNUZ,
        Float8E5M2,
        Float8E5M2FNUZ>;

using AllNumeric =
    TypeList<
        float,
        double,
        int64_t,
        uint64_t,
        int32_t,
        uint32_t,
        int16_t,
        uint16_t,
        int8_t,
        uint8_t,
        MLFloat16,
        BFloat16>;

}  // namespace element_type_lists

}  // namespace onnxruntime
