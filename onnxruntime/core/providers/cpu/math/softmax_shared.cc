/**
* Derived from caffe2, need copy right announcement here.
*/

/**
* Copyright (c) 2016-present, Facebook, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/


#include <algorithm>
#include <cmath>
#include "core/providers/cpu/math/softmax_shared.h"
#include "core/util/math.h"
#include "core/util/math_cpuonly.h"

namespace onnxruntime {
template <typename T>
common::Status SoftmaxCPU(const int64_t N,
                          const int64_t D,
                          const T* Xdata,
                          T* Ydata,
                          T* scale,
                          const T* sum_multiplier,
                          bool logarithmic,
                          T* rowmax, onnxruntime::concurrency::ThreadPool* tp) {
  // the Math functions SoftmaxCPU uses only support int32_t as input, so enforce that
  if (N * D > INT32_MAX || N > INT32_MAX || D > INT32_MAX) {
    std::ostringstream ss;
    ss << "SoftmaxCPU inputs N, D and N * D must be < " << INT32_MAX << ". N=" << N << ", D=" << D;
    std::string msg = ss.str();

    return Status(common::ONNXRUNTIME, common::INVALID_ARGUMENT, msg);
  }

  const int n = gsl::narrow_cast<int>(N);
  const int d = gsl::narrow_cast<int>(D);
  const int nd = gsl::narrow_cast<int>(N * D);

  math::RowwiseMax<T, CPUMathUtil>(n, d, Xdata, rowmax, nullptr);

  // Put the intermediate result X - max(X) into Y by first copying X to Y, and then subtracting max from each entry
  gsl::copy(gsl::make_span(Xdata, nd), gsl::make_span(Ydata, nd));

  math::Gemm<T>(CblasNoTrans, CblasNoTrans, n, d, 1, -1, rowmax, sum_multiplier, 1, Ydata, tp);

  // Exponentiation
  math::Exp<T, CPUMathUtil>(nd, Ydata, Ydata, nullptr);
  math::Gemv<T, CPUMathUtil>(CblasNoTrans, n, d, 1, Ydata, sum_multiplier, 0, scale, nullptr);

  // Do division
  if (!logarithmic) {
    for (int i = 0; i < N; ++i) {
      for (int j = 0; j < D; ++j) {
        Ydata[i * D + j] /= scale[i];
      }
    }
  } else {
    for (int i = 0; i < N; ++i) {
      auto log_fmaxf_scale_i = std::log(std::fmax(scale[i], 1e-20f));
      for (int j = 0; j < D; ++j) {
        Ydata[i * D + j] = Xdata[i * D + j] - rowmax[i] - log_fmaxf_scale_i;
      }
    }
  }

  return Status::OK();
}

template 
common::Status SoftmaxCPU<float>(const int64_t N,
                          const int64_t D,
                          const float* Xdata,
                          float* Ydata,
                          float* scale,
                          const float* sum_multiplier,
                          bool logarithmic,
                          float* rowmax, onnxruntime::concurrency::ThreadPool* tp);
template 
common::Status SoftmaxCPU<double>(const int64_t N,
                          const int64_t D,
                          const double* Xdata,
                          double* Ydata,
                          double* scale,
                          const double* sum_multiplier,
                          bool logarithmic,
                          double* rowmax, onnxruntime::concurrency::ThreadPool* tp);
}  // namespace onnxruntime
