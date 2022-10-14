// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/providers/cuda/cuda_kernel.h"
#include "core/platform/env_var_utils.h"

namespace onnxruntime {
namespace cuda {

namespace matmul_detail {
// Environment variable to disable CublasLtMatmul and use CublasGemm instead. Default is false.
constexpr const char* kDisableCublasLtMatmul = "ORT_DISABLE_CUBLASLT_MATMUL";

}  // namespace matmul_detail

template <typename T>
class Gemm final : public CudaKernel {
  using Base = CudaKernel;

 public:
  Gemm(const OpKernelInfo& info) : CudaKernel(info) {
    int64_t temp;
    ORT_ENFORCE(info.GetAttr<int64_t>("transA", &temp).IsOK());
    trans_A_ = (temp != 0);

    ORT_ENFORCE(info.GetAttr<int64_t>("transB", &temp).IsOK());
    trans_B_ = (temp != 0);

    ORT_ENFORCE(info.GetAttr<float>("alpha", &alpha_).IsOK());
    ORT_ENFORCE(info.GetAttr<float>("beta", &beta_).IsOK());

    // We will support CublasLtMatmul only for half type for now
    disable_cublaslt_matmul_ = !std::is_same<T, MLFloat16>::value ||
                               ParseEnvironmentVariableWithDefault<bool>(matmul_detail::kDisableCublasLtMatmul, false);
  }

  Status ComputeInternal(OpKernelContext* context) const override;

 private:
  bool trans_A_;
  bool trans_B_;
  float alpha_;
  float beta_;
  bool disable_cublaslt_matmul_;
};
}  // namespace cuda
}  // namespace onnxruntime
