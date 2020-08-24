#include "pch.h"
#include "inc/NominalRangeConverter.h"

namespace _winml {
  NominalRangeConverter::NominalRangeConverter(ImageNominalPixelRange pixelRange) {
    // For Normalization: the formula is input_range[min, max] / scale - shift
    // For DeNormalization: the formula is (input_range[min, max] + shift) * scale
    if (pixelRange == ImageNominalPixelRange::kNominalRange_0_255) {
      scale = 1.f;
      shift = 0;
    }
    else if (pixelRange == ImageNominalPixelRange::kNormalized_0_1) {
      scale = 255.f;
      shift = 0;
    }
    else if (pixelRange == ImageNominalPixelRange::kNormalized_1_1) {
      scale = (255.f / 2.f);
      shift = 1;
    }
  };

  // [0, 255] --> [0, 255]
  // [0, 255] / 255 --> [0, 1]
  // [0, 255] * 2 / 255 - 1 --> [-1, 1]
  float NominalRangeConverter::Normalize(float val) const {
    return val / scale - shift;
  }

  DirectX::PackedVector::HALF NominalRangeConverter::Normalize(DirectX::PackedVector::HALF val) const {
    return val / scale - shift;
  }

  __m128 NominalRangeConverter::Normalize(__m128 sse_data) const {
    __m128 sse_shift = _mm_set1_ps(shift);
    __m128 sse_scale = _mm_set1_ps(scale);

    auto sse_dived = _mm_div_ps(sse_data, sse_scale);
    return _mm_sub_ps(sse_dived, sse_shift);
  }

  // [0, 255] --> [0, 255]
  // ([0, 1] + 0 ) * 255 -> [0, 1]
  // ([-1, 1] + 1) * 255 / 2 --> [-1, 1]
  float NominalRangeConverter::Denormalize(float val) const {
    return scale * (val + shift);
  }

  DirectX::PackedVector::HALF NominalRangeConverter::Denormalize(DirectX::PackedVector::HALF val) const {
    return scale * (val + shift);
  }

  __m128 NominalRangeConverter::Denormalize(__m128 sse_data) const {
    __m128 sse_shift = _mm_set1_ps(shift);
    __m128 sse_scale = _mm_set1_ps(scale);

    auto sse_added = _mm_add_ps(sse_data, sse_shift);
    return _mm_mul_ps(sse_added, sse_scale);
  }
} // namespace _winml