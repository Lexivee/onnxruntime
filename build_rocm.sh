#!/bin/bash

BUILD_DIR=build_rocm

#./build.sh --build_dir=${BUILD_DIR} --config=RelWithDebInfo --build_wheel --enable_training --use_rocm --rocm_home=/opt/rocm --nccl_home=/opt/rocm --enable_rocm_profiling --skip_submodule_sync --skip_tests --parallel --mpi_home=/usr/local/mpi 

ROCM_HOME=/opt/rocm
RocmVersion='5.4.0'
CONFIG='Debug'
#CONFIG='RelWithDebInfo'

python tools/ci_build/build.py \
  --config ${CONFIG} \
  --enable_training \
  --enable_rocm_profiling \
  --mpi_home /opt/ompi \
  --cmake_extra_defines \
      CMAKE_HIP_COMPILER=${ROCM_HOME}/llvm/bin/clang++ \
      onnxruntime_BUILD_KERNEL_EXPLORER=ON \
  --use_rocm \
  --use_mpi \
  --enable_nccl \
  --rocm_version=${RocmVersion} \
  --rocm_home ${ROCM_HOME} \
  --nccl_home ${ROCM_HOME}\
  --update \
  --build_dir ${BUILD_DIR} \
  --build \
  --parallel \
  --build_wheel \
  --skip_tests \
  --skip_submodule_sync \
  --allow_running_as_root
  #--enable_training_torch_interop \


cmake --build ${BUILD_DIR}/${CONFIG} --target kernel_explorer --parallel
