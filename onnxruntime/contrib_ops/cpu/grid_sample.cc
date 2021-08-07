// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "grid_sample.h"

#include <cmath>
#include "core/util/math_cpuonly.h"
#include "core/common/common.h"
#include "core/framework/tensor.h"
#include "core/platform/threadpool.h"

namespace onnxruntime {
namespace contrib {

#define REGISTER_KERNEL_TYPED(T)                                  \
  ONNX_OPERATOR_TYPED_KERNEL_EX(                                  \
      GridSample,                                                 \
      kMSDomain,                                                  \
      1,                                                          \
      T,                                                          \
      kCpuExecutionProvider,                                      \
      KernelDefBuilder()                                          \
          .TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      GridSample<T>);


REGISTER_KERNEL_TYPED(float)

template <typename T>
T GsDenormalize(T n, int64_t length, bool align_corners) {
  T x = {};
  if (align_corners) { // align_corners: true => [-1, 1] to [0, length - 1]
    x = (n + static_cast<T>(1)) / static_cast<T>(2) * (length - 1);
  }
  else { // align_corners: false => [-1, 1] to [-0.5, length - 0.5]
    x = ((n + static_cast<T>(1)) * length - static_cast<T>(1)) / static_cast<T>(2);
  }
  return x;
}

template <typename T>
T GsReflect(T x, T x_min, T x_max) {
  T dx = {};
  T range = x_max - x_min;
  if (x < x_min) {
    dx = x_min - x;
    int n = static_cast<int>(dx / range);
    T r = dx - n * range;
    if (n % 2 == 0) {
      x = x_min + r;
    }
    else {
      x = x_max - r;
    }
  }
  else if (x > x_max) {
    dx = x - x_max;
    int n = static_cast<int>(dx / range);
    T r = dx - n * range;
    if (n % 2 == 0) {
      x = x_max - r;
    }
    else {
      x = x_min + r;
    }
  }
  // else fallthrough
  return x;
}

// Calcuate cubic convolution interpolation coefficients 
// ROBERT G. KEYS https://ieeexplore.ieee.org/document/1163711
template<typename T>
void GsGetCubicCoeffs(T x, T coeffs[4]) {
  constexpr T cubic_alpha = -0.75f;
  x = std::abs(x);
  coeffs[0] = static_cast<T>(((cubic_alpha * (x + 1) - 5 * cubic_alpha) * (x + 1) + 8 * cubic_alpha) * (x + 1) - 4 * cubic_alpha);
  coeffs[1] = static_cast<T>(((cubic_alpha + 2) * x - (cubic_alpha + 3)) * x * x + 1);
  coeffs[2] = static_cast<T>(((cubic_alpha + 2) * (1 - x) - (cubic_alpha + 3)) * (1 - x) * (1 - x) + 1);
  coeffs[3] = static_cast<T>(((cubic_alpha * (2 - x) - 5 * cubic_alpha) * (2 - x) + 8 * cubic_alpha) * (2 - x) - 4 * cubic_alpha);
}

template<typename T>
T GsBicubicInterpolate(T p[4][4], T x, T y) {
	T v[4] = {};
  T coeffs[4] = {};
  GsGetCubicCoeffs(x, coeffs);
  for (int64_t i = 0; i < 4; i++) {
    v[i] = coeffs[0] * p[i][0] + coeffs[1] * p[i][1] + coeffs[2] * p[i][2]  + coeffs[3] * p[i][3];
  }
  GsGetCubicCoeffs(y, coeffs);
  T pixel = coeffs[0] * v[0] + coeffs[1] * v[1] + coeffs[2] * v[2]  + coeffs[3] * v[3];
	return pixel;
}

template <typename T>
T GridSample<T>::PixelAtGrid(const T* image, int64_t r, int64_t c, int64_t H, int64_t W, T border[/* 4 */]) const {
  T pixel = {}; // default 0
  if (padding_mode_ == Zeros) {
    if (c >= 0 && c < W && r >=0 && r < H) {
      pixel = image[r * W + c];
    }
  }
  else if (padding_mode_ == Border) {
    c = std::clamp<int64_t>(c, 0,  W - 1);
    r = std::clamp<int64_t>(r, 0,  H - 1);
    pixel = image[r * W + c];
  }
  else { // (padding_mode_ == Reflection)
    c = static_cast<int64_t>(GsReflect(static_cast<T>(c), border[0], border[2]));
    r = static_cast<int64_t>(GsReflect(static_cast<T>(r), border[1], border[3]));
    pixel = image[r * W + c];
  }
  return pixel;
}

template <typename T>
Status GridSample<T>::Compute(OpKernelContext* context) const {
  const auto* input = context->Input<Tensor>(0);
  const auto* grid = context->Input<Tensor>(1);
  const auto& input_dims = input->Shape();
  const auto& grid_dims = grid->Shape();

  if (input_dims.NumDimensions() != 4 || grid_dims.NumDimensions() != 4) {
    return Status(common::ONNXRUNTIME, common::INVALID_ARGUMENT, "Only 4-D tensor is supported");
  }

  auto N = input_dims[0];
  auto C = input_dims[1];
  auto H_in = input_dims[2];
  auto W_in = input_dims[3];
  auto H_out = grid_dims[1];
  auto W_out = grid_dims[2];
  ORT_ENFORCE(grid_dims[0] == N, "grid batch size ", grid_dims[0], " does not match input batch size", N);
  ORT_ENFORCE(grid_dims[3] == 2, "last dimension of grid: ", grid_dims[3], ", expect 2");

  TensorShape Y_shape = {N, C, H_out, W_out};
  auto& Y = *context->Output(0, Y_shape);

  T x_min = static_cast<T>(-0.5f);;
  T x_max = W_in - static_cast<T>(0.5f);
  T y_min = static_cast<T>(-0.5f);;
  T y_max = H_in - static_cast<T>(0.5f);

  if (align_corners_) {
    x_min = 0;
    x_max = static_cast<T>(W_in - 1);
    y_min = 0;
    y_max = static_cast<T>(H_in - 1);
  }
  T border[] = {x_min, y_min, x_max, y_max}; // l-t-r-b

  concurrency::ThreadPool* tp = H_out * W_out > 64 ? context->GetOperatorThreadPool() : nullptr;
  for (int64_t n = 0; n < N; n++) {
    const T* grid_data = grid->Data<T>() + n *  (H_out * W_out) * 2;
    concurrency::ThreadPool::TrySimpleParallelFor(
        tp, C,
        [&](std::ptrdiff_t c) {
          const T* X_data = input->Data<T>() + (n * C + c) * (H_in * W_in);
          T* Y_data = Y.MutableData<T>() + (n * C + c) * (H_out * W_out);

          for (int64_t oy = 0; oy < H_out; oy++) {
            for (int64_t ox = 0; ox < W_out; ox++) {
              const T* gridpoint = grid_data + (oy * W_out + ox) * 2;
              T* Y_gridpoint = Y_data + oy * W_out + ox;
              auto nx = gridpoint[0]; // normalized location
              auto ny = gridpoint[1];
              auto x = GsDenormalize<T>(nx, W_in, align_corners_); // actual location
              auto y = GsDenormalize<T>(ny, H_in, align_corners_);

              if (mode_ == Nearest) {
                x = std::nearbyint(x);
                y = std::nearbyint(y);
              }
              
              if (x < x_min || x > x_max || y < y_min || y > y_max) { // out of bound
                if (padding_mode_ == Border) {
                  x = std::clamp(x, static_cast<T>(0), static_cast<T>(W_in - 1)); // use original border in both align_corner cases
                  y = std::clamp(y, static_cast<T>(0), static_cast<T>(H_in - 1));
                }
                else if (padding_mode_== Reflection) {
                  x = GsReflect(x, x_min, x_max);
                  y = GsReflect(y, y_min, y_max);
                }
              } // out of bound

              if (mode_ == Nearest) {
                // x, y are integers in all padding modes
                *Y_gridpoint = PixelAtGrid(X_data, static_cast<int64_t>(y), static_cast<int64_t>(x), H_in, W_in, border);
                continue;
              }

              if (mode_ == Bilinear) {
                int64_t x1 = static_cast<int64_t>(std::floor(x));
                int64_t y1 = static_cast<int64_t>(std::floor(y));
                int64_t x2 = x1 + 1;
                int64_t y2 = y1 + 1;

                T p11 = PixelAtGrid(X_data, y1, x1, H_in, W_in, border);
                T p12 = PixelAtGrid(X_data, y1, x2, H_in, W_in, border);
                T p21 = PixelAtGrid(X_data, y2, x1, H_in, W_in, border);
                T p22 = PixelAtGrid(X_data, y2, x2, H_in, W_in, border);

                T dx2 = static_cast<T>(x2) - x;
                T dx1 = x - static_cast<T>(x1);
                T dy2 = static_cast<T>(y2) - y;
                T dy1 = y - static_cast<T>(y1);
                *Y_gridpoint = dy2 * (dx2 * p11 + dx1 * p12) + dy1 * (dx2 * p21 + dx1 * p22);
              }
              if (mode_ == Bicubic) {
                int64_t x0 = static_cast<int64_t>(std::floor(x)) - 1; // top-left corner of the bbox
                int64_t y0 = static_cast<int64_t>(std::floor(y)) - 1;
                T p[4][4] = {}; // [H][W]
                for (int64_t h = 0; h < 4; h++) {
                  for (int64_t w = 0; w < 4; w++) {
                    p[h][w] = PixelAtGrid(X_data, h + y0, w + x0, H_in, W_in, border);
                  }
                }
                T dx = x - x0 - 1.f;
                T dy = y - y0 - 1.f;
                *Y_gridpoint = GsBicubicInterpolate(p, dx, dy);
              }
            }
          }
        });
  }
  return Status::OK();
}

}  // namespace contrib
}  // namespace onnxruntime
