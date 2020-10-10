// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "orttraining/training_ops/cuda/collective/nccl_common.h"

#include "orttraining/core/framework/adasum/adasum_interface.h"
#ifdef ORT_USE_MPI
#include "orttraining/core/framework/adasum/adasum_mpi.h"
#endif // ORT_USE_MPI
namespace onnxruntime {
namespace cuda {

class AdasumAllReduce final : public NcclKernel {
 public:
  explicit AdasumAllReduce(const OpKernelInfo& info) : NcclKernel(info) {
   int64_t adasum_reduce_algo;
   info.GetAttrOrDefault("reduce_algo", &adasum_reduce_algo, static_cast<int64_t>(0));
   adasum_reduce_algo_ = training::GetAdasumAlgo(adasum_reduce_algo);
   if (adasum_reduce_algo_ == training::AdasumReductionType::GpuHierarchical ||
       adasum_reduce_algo_ == training::AdasumReductionType::CpuReduction) {
     adasum_reducer_ = std::make_unique<training::AdasumMPI>();
   }
   if(!adasum_reducer_->IsAdasumInitialized()) {
     adasum_reducer_->InitializeVHDDReductionComms();
   }
  }

  Status ComputeInternal(OpKernelContext* context) const override;

 private:
  training::AdasumReductionType adasum_reduce_algo_ = training::AdasumReductionType::GpuHierarchical;
  std::unique_ptr<training::AdasumMPI> adasum_reducer_;};

}  // namespace cuda
}  // namespace onnxruntime
