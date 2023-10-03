// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cpu/tensor/affine_grid.h"

#include "core/common/common.h"
#include "core/providers/op_kernel_type_control.h"
#include "core/util/math_cpuonly.h"
#include <iostream>
#include "Eigen/src/Core/Map.h"
#include <Eigen/Dense>
#include "core/common/eigen_common_wrapper.h"

namespace onnxruntime {

#define REGISTER_KERNEL_TYPED(T)                                          \
  ONNX_CPU_OPERATOR_TYPED_KERNEL(                                         \
      AffineGrid,                                                         \
      20,                                                                 \
      T,                                                                  \
      KernelDefBuilder()                                                  \
          .TypeConstraint("T1", DataTypeImpl::GetTensorType<T>())         \
          .TypeConstraint("T2", DataTypeImpl::GetTensorType<int64_t>()),  \
      AffineGrid<T>);

REGISTER_KERNEL_TYPED(float)

void generate_base_grid_2d(int64_t H, int64_t W, bool align_corners, Eigen::Matrix<float, Eigen::Dynamic, 2>& base_grid) {
  Eigen::VectorXf row_vec = Eigen::VectorXf::LinSpaced(W, -1, 1);
  if (!align_corners) {
    row_vec = row_vec * (W - 1) / W;
  }
  Eigen::VectorXf col_vec = Eigen::VectorXf::LinSpaced(H, -1, 1);
  if (!align_corners) {
    col_vec = col_vec * (H - 1) / H;
  }

  base_grid.resize(H * W, 2);
  for (int j = 0; j < H; j++) {
    for (int i = 0; i < W; i++) {
      base_grid.row(j * W + i) << row_vec(i), col_vec(j);
    }
  }
}

void generate_base_grid_3d(int64_t D, int64_t H, int64_t W, bool align_corners, Eigen::Matrix<float, Eigen::Dynamic, 3>& base_grid) {
  Eigen::VectorXf row_vec = Eigen::VectorXf::LinSpaced(W, -1, 1);
  if (!align_corners) {
    row_vec = row_vec * (W - 1) / W;
  }
  Eigen::VectorXf col_vec = Eigen::VectorXf::LinSpaced(H, -1, 1);
  if (!align_corners) {
    col_vec = col_vec * (H - 1) / H;
  }

  Eigen::VectorXf slice_vec = Eigen::VectorXf::LinSpaced(D, -1, 1);
  if (!align_corners) {
    slice_vec = slice_vec * (D - 1) / D;
  }

  base_grid.resize(D * H * W, 3);
  for (int k = 0; k < D; k++) {
    for (int j = 0; j < H; j++) {
      for (int i = 0; i < W; i++) {
        base_grid.row(k * H * W + j * W + i) << row_vec(i), col_vec(j), slice_vec(k);
      }
    }
  }
}

void affine_grid_generator_2d(const Tensor* theta, const Eigen::Matrix<float, 2, Eigen::Dynamic>& base_grid_transposed, int64_t batch_num, int64_t H, int64_t W, Tensor* grid) {
  const Eigen::StorageOptions option = Eigen::RowMajor;
  auto theta_batch_offset = batch_num * 2 * 3;
  const float* theta_data = theta->Data<float>() + theta_batch_offset;
  const Eigen::Matrix<float, 2, 2, option> theta_R{{theta_data[0], theta_data[1]}, {theta_data[3], theta_data[4]}};
  const Eigen::Array<float, 2, 1> theta_T(theta_data[2], theta_data[5]);

  auto grid_batch_offset = batch_num * H * W * 2;
  float* grid_data = grid->MutableData<float>() + grid_batch_offset;
  Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, 2, option>> grid_matrix(grid_data, narrow<size_t>(H * W), 2);
  grid_matrix = ((theta_R * base_grid_transposed).array().colwise() + theta_T).matrix().transpose();
}

void affine_grid_generator_3d(const Tensor* theta, const Eigen::Matrix<float, 3, Eigen::Dynamic>& base_grid_transposed, int64_t batch_num, int64_t D, int64_t H, int64_t W, Tensor* grid) {
  const Eigen::StorageOptions option = Eigen::RowMajor;
  auto theta_batch_offset = batch_num * 3 * 4;
  const float* theta_data = theta->Data<float>() + theta_batch_offset;
  const Eigen::Matrix<float, 3, 3, option> theta_R{
    {theta_data[0], theta_data[1], theta_data[2]},
    {theta_data[4], theta_data[5], theta_data[6]},
    {theta_data[8], theta_data[9], theta_data[10]}
  };
  const Eigen::Array<float, 3, 1> theta_T(theta_data[3], theta_data[7], theta_data[11]);

  auto grid_batch_offset = batch_num * D * H * W * 3;
  float* grid_data = grid->MutableData<float>() + grid_batch_offset;
  Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, 3, option>> grid_matrix(grid_data, narrow<size_t>(D * H * W), 3);
  grid_matrix = ((theta_R * base_grid_transposed).array().colwise() + theta_T).matrix().transpose();
}

template <typename T>
Status AffineGrid<T>::Compute(OpKernelContext* context) const {
  const Tensor* theta = context->Input<Tensor>(0);
  //const auto elem_type = theta.GetElementType();
  const TensorShape& theta_shape = theta->Shape();
  if (theta_shape.NumDimensions() != 3) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "AffineGrid : Input theta tensor dimension is not 3");
  }

  const Tensor* size = context->Input<Tensor>(1);
  const TensorShape& size_shape = size->Shape();
  const int64_t* size_data = size->Data<int64_t>();

  if (size_shape.GetDims()[0] == 4 /*&& get_check_2d_grid_sample_consistency(theta_shape, size_shape, N, C, H, W)*/) {
    int64_t N = size_data[0], H = size_data[2], W = size_data[3];
    
    TensorShape grid_shape{N, H, W, 2};
    auto grid = context->Output(0, grid_shape);
    
    Eigen::Matrix<float, Eigen::Dynamic, 2> base_grid;
    generate_base_grid_2d(H, W, align_corners_, base_grid);
    Eigen::Matrix<float, 2, Eigen::Dynamic> base_grid_transposed = base_grid.transpose();

    for (int64_t batch_num = 0; batch_num < N; batch_num++) {
      affine_grid_generator_2d(theta, base_grid_transposed, batch_num, H, W, grid);
    }
  } else if (size_shape.GetDims()[0] == 5 /*&& get_check_2d_grid_sample_consistency(theta_shape, size_shape, N, C, H, W)*/) {
    int64_t N = size_data[0], D = size_data[2], H = size_data[3], W = size_data[4];

    TensorShape grid_shape{N, D, H, W, 3};
    auto grid = context->Output(0, grid_shape);

    Eigen::Matrix<float, Eigen::Dynamic, 3> base_grid;
    generate_base_grid_3d(D, H, W, align_corners_, base_grid);
    Eigen::Matrix<float, 3, Eigen::Dynamic> base_grid_transposed = base_grid.transpose();

    for (int64_t batch_num = 0; batch_num < N; batch_num++) {
      affine_grid_generator_3d(theta, base_grid_transposed, batch_num, D, H, W, grid);
    }
  } else {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "AffineGrid : Invalidate size - length of size shall be 4 or 5.");
  }
  return Status::OK();
}
}  // namespace onnxruntime
