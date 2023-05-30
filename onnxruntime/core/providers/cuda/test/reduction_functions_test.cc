// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifndef NDEBUG

#include <memory>

#include "core/common/optional.h"
#include "core/providers/cuda/reduction/reduction_functions.h"
#include "core/providers/cuda/shared_inc/cuda_utils.h"
#include "core/common/type_utils.h"
#include "test/util/test_random_seed.cc"
#include "test/common/random_generator_base.h"

using onnxruntime::test::RandomValueGeneratorBase;

namespace onnxruntime {

namespace cuda {
namespace test {

namespace {
struct DeviceMemoryDeleter {
  template <typename T>
  void operator()(T* p) {
    cudaFree(p);
  }
};

template <typename T>
std::unique_ptr<T, DeviceMemoryDeleter> AllocateDeviceMemory(size_t n = 1) {
  T* p{};
  cudaMalloc(&p, n * sizeof(T));
  return std::unique_ptr<T, DeviceMemoryDeleter>(p);
}

template <typename T>
void CheckDeviceValues(size_t n, const T* d_actual, const T* expected, float relative_error_tolerance) {
  std::vector<T> actual(n);
  cudaMemcpy(actual.data(), d_actual, n * sizeof(T), cudaMemcpyDeviceToHost);

  for (size_t i = 0; i < n; ++i) {
    ORT_ENFORCE(std::abs(actual[i] - expected[i]) / expected[i] < relative_error_tolerance,
                "i: ", i, ", actual[i]: ", actual[i], ", expected[i]: ", expected[i]);
  }
}

void TestReduceRowToScalarApis(int size, float relative_error_tolerance = 1e-4f) {
  auto debug_info = MakeString("size: ", size);
  float expected_output_sum = 0;
  float expected_output_square_sum = 0;
  float expected_output_mean = 0;
  const std::vector<int64_t> shape = {size};
  RandomValueGeneratorBase random_value_generator{};
  const auto input = random_value_generator.Uniform<float>(shape, 0.1f, 1.0f);
  for (const auto input_value : input) {
    expected_output_sum += input_value;
    expected_output_square_sum += input_value * input_value;
    expected_output_mean += input_value / float(size);
  }
  const auto buffer_size_in_bytes =
      compute_reduction_buffer_size<float>(size);

  auto device_input = AllocateDeviceMemory<float>(size);
  auto device_output_sum = AllocateDeviceMemory<float>();
  auto device_output_square_sum = AllocateDeviceMemory<float>();
  auto device_output_mean = AllocateDeviceMemory<float>();
  auto buffer = AllocateDeviceMemory<char>(buffer_size_in_bytes);

  cudaMemcpy(device_input.get(), input.data(), size * sizeof(float), cudaMemcpyHostToDevice);

  ORT_ENFORCE(reduce_sum(
                  0,
                  device_input.get(),
                  device_output_sum.get(),
                  size,
                  buffer.get(),
                  buffer_size_in_bytes)
                  .IsOK(),
              debug_info);
  ORT_ENFORCE(reduce_square_sum(
                  0,
                  device_input.get(),
                  device_output_square_sum.get(),
                  size,
                  buffer.get(),
                  buffer_size_in_bytes)
                  .IsOK(),
              debug_info);
  ORT_ENFORCE(reduce_mean(
                  0,
                  device_input.get(),
                  device_output_mean.get(),
                  size,
                  buffer.get(),
                  buffer_size_in_bytes)
                  .IsOK(),
              debug_info);

  ORT_ENFORCE(CUDA_CALL(cudaDeviceSynchronize()).IsOK(), debug_info);

  CheckDeviceValues(1, device_output_sum.get(), &expected_output_sum, relative_error_tolerance);
  CheckDeviceValues(1, device_output_square_sum.get(), &expected_output_square_sum, relative_error_tolerance);
  CheckDeviceValues(1, device_output_mean.get(), &expected_output_mean, relative_error_tolerance);
}

void TestReduceRowsToRow(int m, int n, bool reset_initial_output, float relative_error_tolerance = 1e-4f) {
  auto debug_info = MakeString("m: ", m, ", n:", n, ", reset_initial_output: ", reset_initial_output);

  const TensorShape shape{m, n};
  RandomValueGeneratorBase random{};
  const auto values = random.Uniform<float>(shape.GetDims(), 1.0f, 10.0f);
  const auto initial_value = reset_initial_output ? 0.0f : 5.0f;
  const std::vector<float> expected_row =
      [m, n, &values, initial_value]() {
        std::vector<float> row(n, initial_value);
        for (int i = 0; i < m; ++i) {
          for (int j = 0; j < n; ++j) {
            row[j] += values[i * n + j];
          }
        }
        return row;
      }();

  auto d_in = AllocateDeviceMemory<float>(m * n);
  auto d_out = AllocateDeviceMemory<float>(n);

  cudaMemcpy(d_in.get(), values.data(), m * n * sizeof(float), cudaMemcpyHostToDevice);

  if (!reset_initial_output) {
    // manually initialize output data
    Fill(0, d_out.get(), initial_value, n);
  }

  ORT_ENFORCE(reduce_matrix_rows(
                  0, d_in.get(), d_out.get(),
                  m, n,
                  reset_initial_output)
                  .IsOK(),
              debug_info);

  ORT_ENFORCE(CUDA_CALL(cudaDeviceSynchronize()).IsOK(), debug_info);

  CheckDeviceValues(n, d_out.get(), expected_row.data(), relative_error_tolerance);
}

template <typename T>
std::vector<T> ExpectedReduceMatrixColumnsOutput(
    int m, int n, const std::vector<T>& values) {
  std::vector<T> column(m);
  for (int i = 0; i < m; ++i) {
    for (int j = 0; j < n; ++j) {
      column[i] += values[i * n + j];
    }
  }
  return column;
}

void TestReduceColumnsToColumn(int m, int n, float relative_error_tolerance = 1e-4f) {
  auto debug_info = MakeString("m: ", m, ", n:", n);

  const TensorShape shape{m, n};
  RandomValueGeneratorBase random{};
  const auto values = random.Uniform<float>(shape.GetDims(), 1.0f, 10.0f);
  const auto expected_column = ExpectedReduceMatrixColumnsOutput(m, n, values);

  auto d_in = AllocateDeviceMemory<float>(m * n);
  auto d_out = AllocateDeviceMemory<float>(m);

  cudaMemcpy(d_in.get(), values.data(), m * n * sizeof(float), cudaMemcpyHostToDevice);

  size_t buffer_size_in_bytes =
      compute_reduce_matrix_columns_buffer_size<float>(m, n);
  auto d_buffer = AllocateDeviceMemory<char>(buffer_size_in_bytes);

  ORT_ENFORCE(reduce_matrix_columns(
                  0,
                  d_in.get(), d_out.get(),
                  m, n,
                  d_buffer.get(), buffer_size_in_bytes)
                  .IsOK(),
              debug_info);

  ORT_ENFORCE(CUDA_CALL(cudaDeviceSynchronize()).IsOK(), debug_info);

  CheckDeviceValues(m, d_out.get(), expected_column.data(), relative_error_tolerance);
}
}  // namespace

void ReductionFunctionsTest_ReduceRowToScalar() {
  TestReduceRowToScalarApis(3);
  TestReduceRowToScalarApis(19);
  TestReduceRowToScalarApis(123);
  TestReduceRowToScalarApis(1128);
  TestReduceRowToScalarApis(5566);
  TestReduceRowToScalarApis(941736, 2e-4f);
}

void ReductionFunctionsTest_ReduceRowsToRow() {
  for (int m : {3, 193, 2945}) {
    for (int n : {3, 193, 2945}) {
      TestReduceRowsToRow(m, n, true);
      TestReduceRowsToRow(m, n, false);
    }
  }
}

void ReductionFunctionsTest_ReduceColumnsToColumn() {
  for (int m : {3, 193, 2945}) {
    for (int n : {3, 193, 2945}) {
      TestReduceColumnsToColumn(m, n);
    }
  }
}

void ReductionFunctionsTest_BufferOffsets() {
  const int m = 2048;
  const int n = 1024;
  const TensorShape shape{m, n};

  const size_t max_buffer_offset = 15;

  const size_t buffer_size_in_bytes =
      compute_reduce_matrix_columns_buffer_size<double>(m, n) + max_buffer_offset;

  auto d_input = AllocateDeviceMemory<double>(m * n);
  auto d_output = AllocateDeviceMemory<double>(m);
  auto d_buffer = AllocateDeviceMemory<char>(buffer_size_in_bytes);

  RandomValueGeneratorBase random{};
  const float relative_error_tolerance = 1e-4f;

  for (size_t buffer_offset = 1; buffer_offset <= max_buffer_offset; ++buffer_offset) {
    auto debug_info = MakeString("buffer offset: ", buffer_offset);

    const auto input = random.Uniform<double>(shape.GetDims(), 1.0, 10.0);
    cudaMemcpy(d_input.get(), input.data(), m * n * sizeof(double), cudaMemcpyHostToDevice);

    ORT_ENFORCE(reduce_matrix_columns(
                    0,
                    d_input.get(), d_output.get(),
                    m, n,
                    d_buffer.get() + buffer_offset,
                    buffer_size_in_bytes - buffer_offset)
                    .IsOK(),
                debug_info);

    const auto expected_column = ExpectedReduceMatrixColumnsOutput(m, n, input);
    CheckDeviceValues(m, d_output.get(), expected_column.data(), relative_error_tolerance);
  }
}

void ReductionFunctionsTest_InvalidBufferSize() {
  const int m = 2048;
  const int n = 1024;
  const TensorShape shape{m, n};

  // this should be too small
  const size_t buffer_size_in_bytes =
      compute_reduce_matrix_columns_buffer_size<float>(m, n) / 10;

  auto d_input = AllocateDeviceMemory<float>(m * n);
  auto d_output = AllocateDeviceMemory<float>(m);
  auto d_buffer = AllocateDeviceMemory<char>(buffer_size_in_bytes);

  RandomValueGeneratorBase random{};
  const auto input = random.Uniform<float>(shape.GetDims(), 1.0, 10.0);
  cudaMemcpy(d_input.get(), input.data(), m * n * sizeof(float), cudaMemcpyHostToDevice);

  const auto status =
      reduce_matrix_columns(0, d_input.get(), d_output.get(), m, n, d_buffer.get(), buffer_size_in_bytes);
  ORT_ENFORCE(!status.IsOK());
}

void ReductionFunctionsTest_GetApplicableMatrixReduction() {
  auto test_get_applicable_matrix_reduction =
      [](cudnnReduceTensorOp_t cudnn_op,
         const std::vector<int64_t>& dims, const std::vector<int64_t>& axes,
         ApplicableMatrixReduction expected_reduction,
         const optional<int>& expected_m = nullopt,
         const optional<int>& expected_n = nullopt) {
        auto debug_info = MakeString(
            "cudnn_op: ", cudnn_op,
            ", dims: ", TensorShape::FromExistingBuffer(dims),
            ", axes: ", TensorShape::FromExistingBuffer(axes));
        int m{}, n{};
        get_applicable_matrix_reduction(cudnn_op, dims, axes, m, n);
        ORT_ENFORCE(
            static_cast<int>(get_applicable_matrix_reduction(cudnn_op, dims, axes, m, n)) ==
                static_cast<int>(expected_reduction),
            debug_info);
        if (expected_m) {
          ORT_ENFORCE(m == *expected_m, debug_info, ":", m, " vs ", *expected_m);
        }
        if (expected_n) {
          ORT_ENFORCE(n == *expected_n, debug_info, ":", n, " vs ", *expected_n);
        }
      };

  const cudnnReduceTensorOp_t valid_op_type = CUDNN_REDUCE_TENSOR_ADD;

  // contiguous axes from beginning
  test_get_applicable_matrix_reduction(
      valid_op_type, {2, 4, 8, 16}, {0, 1},
      ApplicableMatrixReduction::Rows, 2 * 4, 8 * 16);

  // contiguous axes to end
  test_get_applicable_matrix_reduction(
      valid_op_type, {2, 4, 8, 16}, {1, 2, 3},
      ApplicableMatrixReduction::Columns, 2, 4 * 8 * 16);

  // single axis
  test_get_applicable_matrix_reduction(
      valid_op_type, {2, 4, 8, 16}, {3},
      ApplicableMatrixReduction::Columns, 2 * 4 * 8, 16);

  // empty axes
  test_get_applicable_matrix_reduction(
      valid_op_type, {2, 4, 8, 16}, {},
      ApplicableMatrixReduction::Rows, 2 * 4 * 8 * 16, 1);

  // all axes
  test_get_applicable_matrix_reduction(
      valid_op_type, {2, 4, 8, 16}, {0, 1, 2, 3},
      ApplicableMatrixReduction::Rows, 2 * 4 * 8 * 16, 1);

  // handle ones
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {0},
      ApplicableMatrixReduction::Columns, 2 * 4 * 8, 1);
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {1},
      ApplicableMatrixReduction::Rows, 2, 4 * 8);
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {1, 3},
      ApplicableMatrixReduction::Rows, 2, 4 * 8);
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {1, 3, 4},
      ApplicableMatrixReduction::Rows, 2 * 4, 8);
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {1, 3, 4, 6},
      ApplicableMatrixReduction::Rows, 2 * 4 * 8, 1);
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {3, 4, 6},
      ApplicableMatrixReduction::Columns, 2, 4 * 8);
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {4, 6},
      ApplicableMatrixReduction::Columns, 2, 4 * 8);
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {6},
      ApplicableMatrixReduction::Columns, 2 * 4, 8);
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {7},
      ApplicableMatrixReduction::Columns, 2 * 4 * 8, 1);

  // unsupported axes
  test_get_applicable_matrix_reduction(
      valid_op_type, {2, 4, 8, 16, 32, 64}, {0, 1, 3, 4},
      ApplicableMatrixReduction::None);
  test_get_applicable_matrix_reduction(
      valid_op_type, {2, 4, 8, 16}, {1, 2},
      ApplicableMatrixReduction::None);
  test_get_applicable_matrix_reduction(
      valid_op_type, {1, 2, 1, 1, 4, 1, 8, 1}, {3, 6},
      ApplicableMatrixReduction::Columns, 2 * 4, 8);

  // invalid op type
  test_get_applicable_matrix_reduction(
      CUDNN_REDUCE_TENSOR_MAX, {2, 4, 8, 16}, {0, 1},
      ApplicableMatrixReduction::None);
}

}  // namespace test
}  // namespace cuda
}  // namespace onnxruntime

#endif
