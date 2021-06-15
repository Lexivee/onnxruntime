// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <cfenv>
#include <cmath>
#include <vector>

namespace onnxruntime {
namespace test {

//
// Rounds a float to the nearest representable value and returns the nearest integer value as a float.
//
static float RoundHalfToEven(float input) {
  std::fesetround(FE_TONEAREST);
  auto result = std::nearbyintf(input);
  return result;
}

//
// Performs linear quantization on a given float vector.
//
template <typename Integer, bool symmetric, typename = typename std::enable_if<std::is_integral<Integer>::value, Integer>::type>
inline std::vector<Integer> QuantizeLinear(const std::vector<float>& data, float& scale, Integer& zp) {
  std::vector<Integer> result;
  result.reserve(data.size());

  // find quantization range min and max
  float qmax = std::numeric_limits<Integer>::max();
  float qmin = std::numeric_limits<Integer>::min();
  // Adjust the int8 range to -127 to 127 so that zero point can be 0
  if (qmin == -128) {
    qmin = -127;
  }

  const auto minmax = std::minmax_element(data.begin(), data.end());
  float min = std::min(*minmax.first, 0.0f);  // ensure the input range includes zero
  float max = std::max(*minmax.second, 0.0f);
  if (symmetric) {
    scale = std::max(std::abs(max), std::abs(min)) / 127;
    zp = 0;
  } else {
    scale = (max - min) / (qmax - qmin);
    zp = static_cast<Integer>(RoundHalfToEven(std::max(qmin, std::min(qmax, qmin - min / scale))));
  }

  for (size_t i = 0; i < data.size(); i++) {
    result.push_back(static_cast<Integer>(RoundHalfToEven(std::max(qmin, std::min(qmax, data[i] / scale + zp)))));
  }
  return result;
}

//
// Converts a given float vector to a quantized representation with a pre-calculated scale and zero point.
//
template <typename Integer, typename = typename std::enable_if<std::is_integral<Integer>::value, Integer>::type>
inline std::vector<Integer> ToInteger(const std::vector<float>& data, float scale, Integer zero_point = 0) {
  std::vector<Integer> result;
  result.reserve(data.size());
  for (size_t i = 0; i < data.size(); i++) {
    result.push_back(static_cast<Integer>(std::round(data[i] / scale) + zero_point));
  }
  return result;
}

}  // namespace test
}  // namespace onnxruntime

