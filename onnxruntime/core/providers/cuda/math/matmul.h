// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/providers/cuda/cuda_kernel.h"
#include "core/providers/cpu/math/matmul_helper.h"
#include "core/session/onnxruntime_session_options_config_keys.h"

namespace onnxruntime {
namespace cuda {
template <typename T>
class MatMul final : public CudaKernel {
  using Base = CudaKernel;

 public:
  MatMul(const OpKernelInfo& info)
      : CudaKernel(info),
        alpha_{info.GetAttrOrDefault<float>("alpha", 1.0f)},
        trans_A_{info.GetAttrOrDefault<int64_t>("transA", 0) != 0},
        trans_B_{info.GetAttrOrDefault<int64_t>("transB", 0) != 0},
        trans_batch_a_{info.GetAttrOrDefault<int64_t>("transBatchA", 0) != 0},
        trans_batch_b_{info.GetAttrOrDefault<int64_t>("transBatchB", 0) != 0},
        use_fp8_("1" == info.GetConfigOptions().GetConfigEntry(kOrtSessionOptionsGemmCudaFloat8E4M3FN)),
        allocator_(info.GetAllocator(OrtMemType::OrtMemTypeDefault)) {
          if (use_fp8_) {
            std::vector<float> quant_float(256);
            for (int i = 0; i < 256; i ++) {
              quant_float[i] = Float8e4m3ToFloat32(i);
            }
            std_quant_ = ComputeStandardDeviation(quant_float);

            std::string activation = info.GetAttrOrDefault<std::string>("activation", "NONE");
            if (activation == "NONE") {
              epilogue_ = CUBLASLT_EPILOGUE_DEFAULT;
            } else if (activation == "RELU") {
              epilogue_ = CUBLASLT_EPILOGUE_RELU;
            } else if (activation == "GELU") {
              epilogue_ = CUBLASLT_EPILOGUE_GELU;
            } else {
              ORT_THROW("Unexpected value for activation: '", activation, "'.");
            }
          }
        }

  Status ComputeInternal(OpKernelContext* context) const override;
  Status ComputeDefault(OpKernelContext* context, MatMulComputeHelper& helper) const;

 private:
  const float alpha_;
  const bool trans_A_;
  const bool trans_B_;
  const bool trans_batch_a_;
  const bool trans_batch_b_;
  const bool use_fp8_;
  float std_quant_;
  AllocatorPtr allocator_;
  cublasLtEpilogue_t epilogue_;

  float ComputeStandardDeviation(const std::vector<float>& v) const;
  float ComputeScale(const Tensor* tensor) const;
  float Float8e4m3ToFloat32(int i)
  {
    // TODO implement
    return float(i);
  }
};

template <typename T>
Status FuncMatMul(
    // Use OpKernel and do a pointer cast to unify functional calls with other eps.
    // TODO: remove CudaKernel and OpKernelContext.
    const CudaKernel* cuda_kernel,
    // Do NOT use ctx to access inputs and outputs.
    // Inputs and outputs are passed in as function arguments.
    OpKernelContext* ctx,
    const Tensor* A,
    const Tensor* B,
    float alpha,
    bool trans_A,
    bool trans_B,
    bool trans_batch_A,
    bool trans_batch_B,
    Tensor* Y);

}  // namespace cuda
}  // namespace onnxruntime
