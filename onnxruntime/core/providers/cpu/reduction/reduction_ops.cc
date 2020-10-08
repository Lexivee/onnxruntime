// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cpu/reduction/reduction_ops.h"
#include "core/providers/common.h"

using namespace std;
namespace onnxruntime {

#define REGISTER_UNARY_ELEMENTWISE_KERNEL(x, sinceVersion)                            \
  ONNX_CPU_OPERATOR_TYPED_KERNEL(                                                     \
      x,                                                                              \
      sinceVersion,                                                                   \
      float,                                                                          \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),   \
      x<float>);                                                                      \
                                                                                      \
  ONNX_CPU_OPERATOR_TYPED_KERNEL(                                                     \
      x,                                                                              \
      sinceVersion,                                                                   \
      int32_t,                                                                        \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<int32_t>()), \
      x<int32_t>);

#define REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(x, startVer, endVer)              \
  ONNX_CPU_OPERATOR_VERSIONED_TYPED_KERNEL(                                           \
      x,                                                                              \
      startVer,                                                                       \
      endVer,                                                                         \
      float,                                                                          \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),   \
      x<float>);                                                                      \
                                                                                      \
  ONNX_CPU_OPERATOR_VERSIONED_TYPED_KERNEL(                                           \
      x,                                                                              \
      startVer,                                                                       \
      endVer,                                                                         \
      int32_t,                                                                        \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<int32_t>()), \
      x<int32_t>);

#define REGISTER_UNARY_ELEMENTWISE_KERNEL_DOUBLE_ONLY(x, sinceVersion)               \
  ONNX_CPU_OPERATOR_TYPED_KERNEL(                                                    \
      x,                                                                             \
      sinceVersion,                                                                  \
      double,                                                                        \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<double>()), \
      x<double>);

#define REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_DOUBLE_ONLY(x, startVer, endVer) \
  ONNX_CPU_OPERATOR_VERSIONED_TYPED_KERNEL(                                          \
      x,                                                                             \
      startVer,                                                                      \
      endVer,                                                                        \
      double,                                                                        \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<double>()), \
      x<double>);

#define REGISTER_UNARY_ELEMENTWISE_KERNEL_INT64_ONLY(x, sinceVersion)                 \
  ONNX_CPU_OPERATOR_TYPED_KERNEL(                                                     \
      x,                                                                              \
      sinceVersion,                                                                   \
      int64_t,                                                                        \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<int64_t>()), \
      x<int64_t>);

#define REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_INT64_ONLY(x, startVer, endVer)   \
  ONNX_CPU_OPERATOR_VERSIONED_TYPED_KERNEL(                                           \
      x,                                                                              \
      startVer,                                                                       \
      endVer,                                                                         \
      int64_t,                                                                        \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<int64_t>()), \
      x<int64_t>);

#define REGISTER_UNARY_ELEMENTWISE_KERNEL_INT8_ONLY(x, sinceVersion)                 \
  ONNX_CPU_OPERATOR_TYPED_KERNEL(                                                    \
      x,                                                                             \
      sinceVersion,                                                                  \
      int8_t,                                                                        \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<int8_t>()), \
      x<int8_t>);

#define REGISTER_UNARY_ELEMENTWISE_KERNEL_UINT8_ONLY(x, sinceVersion)                 \
  ONNX_CPU_OPERATOR_TYPED_KERNEL(                                                     \
      x,                                                                              \
      sinceVersion,                                                                   \
      uint8_t,                                                                        \
      KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<uint8_t>()), \
      x<uint8_t>);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceL1, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceL1, 11);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceL2, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceL2, 11);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceLogSum, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceLogSum, 11);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceLogSumExp, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceLogSumExp, 11);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceMax, 1, 10);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_INT64_ONLY(ReduceMax, 1, 10);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceMax, 11, 11);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_INT64_ONLY(ReduceMax, 11, 11);

REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceMax, 12);
REGISTER_UNARY_ELEMENTWISE_KERNEL_INT64_ONLY(ReduceMax, 12);
REGISTER_UNARY_ELEMENTWISE_KERNEL_INT8_ONLY(ReduceMax, 12);
REGISTER_UNARY_ELEMENTWISE_KERNEL_UINT8_ONLY(ReduceMax, 12);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceMean, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceMean, 11);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceMin, 1, 10);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_INT64_ONLY(ReduceMin, 1, 10);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceMin, 11, 11);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_INT64_ONLY(ReduceMin, 11, 11);

REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceMin, 12);
REGISTER_UNARY_ELEMENTWISE_KERNEL_INT64_ONLY(ReduceMin, 12);
REGISTER_UNARY_ELEMENTWISE_KERNEL_INT8_ONLY(ReduceMin, 12);
REGISTER_UNARY_ELEMENTWISE_KERNEL_UINT8_ONLY(ReduceMin, 12);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceProd, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceProd, 11);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_INT64_ONLY(ReduceProd, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL_INT64_ONLY(ReduceProd, 11);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceSum, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceSum, 11);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_INT64_ONLY(ReduceSum, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL_INT64_ONLY(ReduceSum, 11);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_DOUBLE_ONLY(ReduceSum, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL_DOUBLE_ONLY(ReduceSum, 11);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ReduceSumSquare, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL(ReduceSumSquare, 11);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_DOUBLE_ONLY(ReduceSumSquare, 1, 10);
REGISTER_UNARY_ELEMENTWISE_KERNEL_DOUBLE_ONLY(ReduceSumSquare, 11);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ArgMax, 1, 10);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ArgMax, 11, 11);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL_DOUBLE_ONLY(ArgMax, 11, 11)
REGISTER_UNARY_ELEMENTWISE_KERNEL(ArgMax, 12);
REGISTER_UNARY_ELEMENTWISE_KERNEL_DOUBLE_ONLY(ArgMax, 12);

REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ArgMin, 1, 10);
REGISTER_UNARY_ELEMENTWISE_VERSIONED_KERNEL(ArgMin, 11, 11);
REGISTER_UNARY_ELEMENTWISE_KERNEL(ArgMin, 12);

bool NeedsTransposeForReduce(const Tensor* input_tensor_ptr,
                             const std::vector<int64_t>& axes_,
                             std::vector<int64_t>& axes,
                             const TensorShape* input_shape_override) {
  ORT_ENFORCE(input_tensor_ptr != nullptr, "Input to be reduced is null");

  if (input_shape_override) {
    ORT_ENFORCE(input_tensor_ptr->Shape().Size() == input_shape_override->Size(),
                "The input shape override's size does not match the input tensor's shape size");
  }

  const auto& input_shape = input_shape_override ? *input_shape_override : input_tensor_ptr->Shape();
  size_t ndim = input_shape.NumDimensions();
  if (ndim == 0) {
    return false;
  }

  axes.reserve(axes_.size());
  for (int64_t axis : axes_) {
    axes.push_back(HandleNegativeAxis(axis, static_cast<int64_t>(ndim)));
  }

  if (axes.empty()) {
    // This is the default case for non-arg kind reductions. Reduce on all dimensions.
    for (size_t i = 0; i < ndim; i++) {
      axes.push_back(i);
    }
  }

  std::sort(axes.begin(), axes.end());

  // If all reduced axes are located at the tail of the input shape, then copy could be skipped is required
  bool need_copy = true;
  if (axes.size() <= ndim && ndim > 0 &&
      axes.front() == static_cast<int64_t>(ndim - axes.size()) &&
      axes.back() == static_cast<int64_t>(ndim) - 1) {
    need_copy = false;
  }
  return need_copy;
}

// When all reduce axes are located at the tail of the dims, quite general cases, transpose and extra
// copy could be skipped to improve performance. If required by check_no_transpose = true, then
// the calling code will check if the data was transposed and act accordingly.
// return value: true means transposedInputData is not created/copied, input tensor data could
// be directly used as row major matrix [block_size, blocks], where blocks is the
// size of each reduce.
// `input_shape_override` overrides the shape of `input` for compute purposes.
template <typename T>
bool PrepareForReduce(const Tensor* input_tensor_ptr,
                      FastAllocVector<T>& transposed_input_data,
                      int64_t& block_size,
                      int64_t& blocks,
                      const std::vector<int64_t>& axes_,
                      bool keepdims_,
                      /*out*/ std::vector<int64_t>& reduced_dims,
                      bool check_no_transpose,
                      const TensorShape* input_shape_override) {
  ORT_ENFORCE(input_tensor_ptr != nullptr, "Input to be reduced is null");

  if (input_shape_override) {
    ORT_ENFORCE(input_tensor_ptr->Shape().Size() == input_shape_override->Size(),
                "The input shape override's size does not match the input tensor's shape size");
  }

  const Tensor& input = *input_tensor_ptr;
  const auto& input_shape = input_shape_override ? *input_shape_override : input.Shape();

  size_t ndim = input_shape.NumDimensions();

  // Scalar tensor
  if (ndim == 0) {
    if (!check_no_transpose) {
      auto size = input_shape.Size();
      assert(size == 1);
      transposed_input_data.resize(size, 0);
      T* to_data = &transposed_input_data[0];
      *to_data = *input.Data<T>();
    }
    block_size = blocks = 1;
    return true;
  }

  std::vector<int64_t> axes;
  bool need_copy = NeedsTransposeForReduce(input_tensor_ptr, axes_, axes, input_shape_override);

  std::vector<bool> keep_axis(ndim, true);
  for (auto i : axes) {
    keep_axis[i] = false;
  }

  //transpose the input so that all to-be-reduced axes are at the head
  std::vector<int64_t> transposed_axes(axes.begin(), axes.end());
  for (size_t i = 0; i < ndim; ++i) {
    if (keep_axis[i]) {
      transposed_axes.push_back(i);
    }
  }

  std::vector<int64_t> new_dims(transposed_axes.size());
  for (size_t i = 0; i < transposed_axes.size(); ++i) {
    new_dims[i] = input_shape.GetDims().at(transposed_axes[i]);
  }

  int num_axes = static_cast<int>(transposed_axes.size());
  auto in_dims = input_shape.GetDims();

  // Measure amount of contiguous data we can copy at once
  int64_t blocksize = 1;
  int n_shared_idxs = 0;
  for (int i = num_axes - 1; i >= 0; --i) {
    if (transposed_axes[i] == i) {
      blocksize *= new_dims[i];
      ++n_shared_idxs;
    } else {
      break;
    }
  }

  const T* from_data = input.template Data<T>();
  size_t count = input_shape.Size();

  //set to-be-reduced axes to one. squeeze is keepdims_ is false
  int64_t first_dim = 1;
  reduced_dims.reserve(in_dims.size());

  for (size_t i = 0; i < in_dims.size(); i++) {
    const auto in_dim = in_dims[i];
    if (keep_axis[i]) {
      reduced_dims.push_back(in_dim);
    } else {
      first_dim *= in_dim;
      if (keepdims_) {
        reduced_dims.push_back(in_dim == 0 ? 0 : 1);
      } else {
        // as we are reducing on this axis and not keeping a dim for it, we can't drop a dim value of 0.
        // e.g. if input was {3, 0, 2} and we reduced on axis 1 without keeping it, the output shape would be
        // {3, 2} which is invalid given the input was empty.
        // note that if we do keep the dim the output shape will have a 0 in it,
        // which is still valid for an empty tensor, so allow that.
        ORT_ENFORCE(in_dim != 0,
                    "Can't reduce on dim with value of 0 if 'keepdims' is false. "
                    "Invalid output shape would be produced. input_shape:",
                    input_shape);
      }
    }
  }

  auto num_elements = input_shape.Size();

  // edge case. one or more input dims with value of 0.
  if (num_elements == 0) {
    block_size = blocks = 0;
    return true;
  }

  if (0 == first_dim) {
    return false;
  }

  block_size = num_elements / first_dim;
  blocks = first_dim;

  if (!need_copy && check_no_transpose) {
    return true;
  }

  transposed_input_data.resize(input_shape.Size(), 0);
  T* to_data = &transposed_input_data[0];
  if (num_axes < 2 || n_shared_idxs == num_axes) {
    memcpy(to_data, from_data, count * sizeof(T));
    return false;
  }

  int itr_axes = num_axes - n_shared_idxs;

  // Calculate strides
  std::vector<int64_t> stride_x(itr_axes, 0);
  for (size_t i = 0; static_cast<int>(i) < itr_axes; i++) {
    stride_x[i] = 1;
    for (size_t j = transposed_axes[i] + 1; static_cast<int>(j) < itr_axes; j++) {
      stride_x[i] *= in_dims[j];
    }
  }

  std::vector<int64_t> itr_idxs(itr_axes, 0);

  // Branch here to avoid branching within the loop
  if (blocksize > 1) {
    for (size_t index = 0; index < (count / blocksize); index++) {
      int64_t from_index = 0;
      for (int i = 0; i < itr_axes; ++i) {
        from_index += stride_x[i] * itr_idxs[i];
      }

      memcpy(
          to_data + blocksize * index,
          from_data + blocksize * from_index,
          blocksize * sizeof(T));

      ++itr_idxs[itr_axes - 1];
      for (int i = itr_axes - 1; i >= 1; --i) {
        auto expected_dim = new_dims[i];
        if (itr_idxs[i] < expected_dim) {
          break;
        }
        itr_idxs[i] %= expected_dim;
        ++itr_idxs[i - 1];
      }
    }
  } else {
    for (size_t index = 0; index < count; index++) {
      int64_t from_index = 0;
      for (int i = 0; i < itr_axes; ++i) {
        from_index += stride_x[i] * itr_idxs[i];
      }

      *(to_data + index) = *(from_data + from_index);

      ++itr_idxs[itr_axes - 1];
      for (int i = itr_axes - 1; i >= 1; --i) {
        auto expected_dim = new_dims[i];
        if (itr_idxs[i] < expected_dim) {
          break;
        }
        itr_idxs[i] %= expected_dim;
        ++itr_idxs[i - 1];
      }
    }
  }
  return false;
}

void ExperimentalPrepareForReduce(const Tensor& input, const std::vector<int64_t>& reduced_axes,
                                  ResultsExperimentalPrepareForReduce& results) {
  auto input_shape = input.Shape();

  // Common initialisation for the indices.
  std::vector<int64_t> cumulative_shape = input_shape.GetDims();
  cumulative_shape[cumulative_shape.size() - 1] = 1;
  for (int i = static_cast<int>(cumulative_shape.size()) - 2; i >= 0; --i) {
    cumulative_shape[i] = cumulative_shape[i + 1] * input_shape[i + 1];
  }
  int64_t projection_size = 1;
  for (auto a : reduced_axes) {
    projection_size *= input_shape[a];
  }

  int last_reduced_axis = static_cast<int>(reduced_axes.size()) - 1;
  int loop_reduced_axis = 1;
  results.last_loop_red_size = input_shape[reduced_axes[last_reduced_axis]];
  results.last_loop_red_inc = cumulative_shape[reduced_axes[last_reduced_axis]];
  projection_size /= input_shape[reduced_axes[last_reduced_axis]];
  --last_reduced_axis;
  while (last_reduced_axis >= 0) {
    if (reduced_axes[last_reduced_axis] != reduced_axes[last_reduced_axis + 1] - 1)
      break;
    results.last_loop_red_size *= input_shape[reduced_axes[last_reduced_axis]];
    projection_size /= input_shape[reduced_axes[last_reduced_axis]];
    --last_reduced_axis;
    ++loop_reduced_axis;
  }

  // Builds the list of indices projected into the same sum.
  int reduced_axes_size = static_cast<int>(reduced_axes.size()) - loop_reduced_axis;
  if (reduced_axes_size == 0) {
    results.projected_index.resize(1, 0);
  } else {
    results.projected_index.resize(projection_size);
    std::vector<int64_t> projected_indices(reduced_axes_size, 0);
    int64_t current_index = 0;
    size_t current_pos = 0;
    int j;
    for (current_pos = 0; current_pos < results.projected_index.size(); ++current_pos) {
      results.projected_index[current_pos] = current_index;
      ++projected_indices[projected_indices.size() - 1];
      current_index += cumulative_shape[reduced_axes[reduced_axes_size - 1]];
      for (j = reduced_axes_size - 1; j > 0; --j) {
        if (projected_indices[j] < input_shape[reduced_axes[j]])
          break;
        projected_indices[j] -= input_shape[reduced_axes[j]];
        current_index -= input_shape[reduced_axes[j]] * cumulative_shape[reduced_axes[j]];
        ++projected_indices[j - 1];
        current_index += cumulative_shape[reduced_axes[j - 1]];
      }
    }
  }

  // Builds the list of indices for the unprojected sum.
  std::vector<int64_t> unreduced_axes;
  for (int64_t i = 0; i < static_cast<int64_t>(cumulative_shape.size()); ++i) {
    if (std::find(reduced_axes.begin(), reduced_axes.end(), i) != reduced_axes.end())
      continue;
    unreduced_axes.push_back(i);
  }
  int64_t unprojection_size = 1;
  for (auto a : unreduced_axes) {
    unprojection_size *= input_shape[a];
  }
  std::vector<int64_t> unprojected_indices(unreduced_axes.size(), 0);

  // The last index is usually an image size.
  // We differently process the last unprojected dimension.
  results.last_loop_size = input_shape[unreduced_axes[unreduced_axes.size() - 1]];
  int64_t unprojection_size_before_last = unprojection_size / results.last_loop_size;
  results.unprojected_index.reserve(unprojection_size_before_last);
  results.last_loop_inc = cumulative_shape[unreduced_axes[unreduced_axes.size() - 1]];
  if (unprojected_indices.size() <= 1) {
    results.unprojected_index.push_back(0);
  } else {
    int64_t current_index = 0;
    int j;
    for (int64_t pos = 0; pos < unprojection_size_before_last; ++pos) {
      results.unprojected_index.push_back(current_index);
      ++unprojected_indices[unprojected_indices.size() - 2];
      current_index += cumulative_shape[unreduced_axes[unreduced_axes.size() - 2]];
      for (j = static_cast<int>(unreduced_axes.size()) - 2; j > 0; --j) {
        if (unprojected_indices[j] < input_shape[unreduced_axes[j]])
          break;
        unprojected_indices[j] -= input_shape[unreduced_axes[j]];
        current_index -= input_shape[unreduced_axes[j]] * cumulative_shape[unreduced_axes[j]];
        ++unprojected_indices[j - 1];
        current_index += cumulative_shape[unreduced_axes[j - 1]];
      }
    }
  }
}

template <typename T, typename AGG>
void ExperimentalReduce(Tensor* output, const Tensor& input, const std::vector<int64_t>& reduced_axes,
                        OpKernelContext* ctx, ResultsExperimentalPrepareForReduce& last_results) {
  auto output_shape = output->Shape();
  const T* from_data = input.template Data<T>();
  T* to_data = output->template MutableData<T>();
  int64_t count = output_shape.Size();

  if (reduced_axes.size() == 0 || reduced_axes.size() == input.Shape().NumDimensions()) {
    ORT_ENFORCE(count == 1, "Reduction on all axes, output size should be 1.");
    int64_t input_size = input.Shape().Size();
    to_data[0] = AGG(input_size).aggall(from_data);
    return;
  }

  //FastAllocVector<int64_t> projected_index(GetAllocator<int64_t>(*ctx));
  //FastAllocVector<int64_t> unprojected_index(GetAllocator<int64_t>(*ctx));
  if (!last_results.equal(input.Shape().GetDims(), reduced_axes)) {
    ExperimentalPrepareForReduce(input, reduced_axes, last_results);
  }
  int64_t denominator = last_results.last_loop_red_size * last_results.projected_index.size();

  auto fn = [&](std::ptrdiff_t first, std::ptrdiff_t end) {
    int64_t loop;
    const T* loop_red_ptr;
    const T* loop_red_ptr_end;
    int64_t current_index = first * last_results.last_loop_size;
    for (int64_t main_index = first; main_index < end; ++main_index) {
      for (loop = 0; loop < last_results.last_loop_size; ++loop, ++current_index) {
        AGG accumulator(denominator);
        int64_t origin = last_results.unprojected_index[main_index] + loop * last_results.last_loop_inc;
        for (auto it = last_results.projected_index.begin(); it != last_results.projected_index.end(); ++it) {
          loop_red_ptr = from_data + (origin + *it);
          loop_red_ptr_end = loop_red_ptr + last_results.last_loop_red_size * last_results.last_loop_red_inc;
          for (; loop_red_ptr != loop_red_ptr_end; loop_red_ptr += last_results.last_loop_red_inc) {
            accumulator.update(*loop_red_ptr);
          }
        }
        to_data[current_index] = accumulator.get_value();
      }
    }
  };

  auto cost = TensorOpCost{(double)(last_results.projected_index.size() * sizeof(T) * last_results.last_loop_size * last_results.last_loop_red_size),
                           (double)last_results.last_loop_size * last_results.last_loop_red_size,
                           (double)last_results.projected_index.size() * last_results.last_loop_size * last_results.last_loop_red_size};
  concurrency::ThreadPool::TryParallelFor(ctx->GetOperatorThreadPool(), count / last_results.last_loop_size, cost, fn);
}

template <typename T, typename AGG>
void CommonComputeReduce(OpKernelContext* ctx, const std::vector<int64_t> axes_, int64_t keepdims_,
                         ResultsExperimentalPrepareForReduce& last_results) {
  const Tensor* input = ctx->Input<Tensor>(0);

  std::vector<int64_t> axes;
  NeedsTransposeForReduce(input, axes_, axes, nullptr);
  int64_t min_dims = 1;
  auto reduced_dims = input->Shape().GetDims();
  for (auto i : axes) {
    reduced_dims[i] = reduced_dims[i] > 0 ? 1 : 0;
    min_dims = ((min_dims == 0) || (reduced_dims[i] > 0)) ? min_dims : 0;
  }
  if (min_dims == 0) {
    if (keepdims_) {
      ctx->Output(0, reduced_dims);
    } else {
      ctx->Output(0, std::vector<int64_t>());
    }
    return;
  }
  Tensor* output;
  if (keepdims_) {
    output = ctx->Output(0, reduced_dims);
  } else {
    auto dropped_dims = input->Shape().GetDims();
    std::vector<int64_t> dropped_dims2;
    for (auto i : axes) {
      dropped_dims[i] = -1;
    }
    for (auto it = dropped_dims.begin(); it != dropped_dims.end(); ++it) {
      if (*it != -1) {
        dropped_dims2.push_back(*it);
      }
    }
    output = ctx->Output(0, dropped_dims2);
  }
  ExperimentalReduce<T, AGG>(output, *input, axes, ctx, last_results);
}

template <typename T>
Status ReduceL1<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;

  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);

  T* output_data = reduced->template MutableData<T>();

  if (no_transpose) {
    const T* input_data = input->template Data<T>();

    for (int64_t i = 0; i < block_size; ++i) {
      output_data[i] = ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).cwiseAbs().sum();
    }
  } else {
    EigenVectorMap<T> out_vec(output_data, block_size);
    out_vec = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks).cwiseAbs().rowwise().sum();
  }

  return Status::OK();
}

template <typename T>
Status ReduceL2<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;

  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);

  T* output_data = reduced->template MutableData<T>();

  if (no_transpose) {
    const T* input_data = input->template Data<T>();

    for (int64_t i = 0; i < block_size; ++i) {
      output_data[i] = ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).norm();
    }
  } else {
    EigenVectorMap<T> out_vec(output_data, block_size);
    out_vec = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks).rowwise().norm();
  }

  return Status::OK();
}

template <typename T>
Status ReduceLogSum<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;

  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);

  T* output_data = reduced->template MutableData<T>();

  if (no_transpose) {
    const T* input_data = input->template Data<T>();

    for (int64_t i = 0; i < block_size; ++i) {
      output_data[i] = ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).sum();
    }
  } else {
    EigenVectorMap<T> out_vec(output_data, block_size);
    out_vec = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks).rowwise().sum();
  }

  for (int j = 0; j < block_size; ++j) {
    *(output_data) = static_cast<T>(std::log(*(output_data)));
    ++output_data;
  }

  return Status::OK();
}

template <typename T>
Status ReduceLogSumExp<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;
  const Tensor* input = ctx->Input<Tensor>(0);

  PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims);

  Tensor* reduced = ctx->Output(0, reduced_dims);

  T* output_data = reduced->template MutableData<T>();

  for (int j = 0; j < block_size; ++j) {
    T max_value = std::numeric_limits<T>::lowest();
    for (int i = 0; i < blocks; ++i) {
      max_value = std::max(max_value, transposed_input_data[i * block_size + j]);
    }
    T scaled_exp_sum = 0;
    for (int i = 0; i < blocks; ++i) {
      scaled_exp_sum += static_cast<T>(std::exp(transposed_input_data[i * block_size + j] - max_value));
    }
    *(output_data++) = static_cast<T>(std::log(scaled_exp_sum) + max_value);
  }
  return Status::OK();
}

template <typename T>
Status ReduceMax<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;
  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);

  T* output_data = reduced->template MutableData<T>();

  if (no_transpose) {
    const T* input_data = input->template Data<T>();

    for (int64_t i = 0; i < block_size; ++i) {
      output_data[i] = ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).maxCoeff();
    }
  } else {
    EigenVectorMap<T> out_vec(output_data, block_size);
    out_vec = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks).rowwise().maxCoeff();
  }

  return Status::OK();
}

template <typename T>
Status ReduceMean<T>::Compute(OpKernelContext* ctx) const {
  /*
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;
  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);

  T* output_data = reduced->template MutableData<T>();

  if (no_transpose) {
    const T* input_data = ctx->Input<Tensor>(0)->template Data<T>();
    auto lambda = [input_data, blocks, output_data](ptrdiff_t i) {
      output_data[i] = ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).mean();
    };
    concurrency::ThreadPool::TryBatchParallelFor(ctx->GetOperatorThreadPool(), block_size, lambda, 0);
  } else {
    EigenVectorMap<T> out_vec(output_data, block_size);
    out_vec = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks).rowwise().mean();
  }
  */
  CommonComputeReduce<T, ReduceAggregatorMean<T>>(ctx, axes_, keepdims_, last_results_);
  return Status::OK();
}

template <typename T>
Status ReduceMin<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;
  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);

  T* output_data = reduced->template MutableData<T>();

  if (no_transpose) {
    const T* input_data = input->template Data<T>();

    for (int64_t i = 0; i < block_size; ++i) {
      output_data[i] = ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).minCoeff();
    }
  } else {
    EigenVectorMap<T> out_vec(output_data, block_size);
    out_vec = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks).rowwise().minCoeff();
  }

  return Status::OK();
}

template <typename T>
Status ReduceProd<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;
  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);

  T* output_data = reduced->template MutableData<T>();

  if (no_transpose) {
    const T* input_data = input->template Data<T>();

    for (int64_t i = 0; i < block_size; ++i) {
      output_data[i] = ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).prod();
    }
  } else {
    EigenVectorMap<T> out_vec(output_data, block_size);
    out_vec = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks).rowwise().prod();
  }

  return Status::OK();
}

template <typename T>
Status ReduceSum<T>::Compute(OpKernelContext* ctx) const {
  CommonComputeReduce<T, ReduceAggregatorSum<T>>(ctx, axes_, keepdims_, last_results_);
  return Status::OK();
}

template <typename T>
void ReduceSumCore(const T* input_data, T* output_data, bool no_transpose,
                   int64_t blocks, int64_t block_size, FastAllocVector<T>& transposed_input_data,
                   concurrency::ThreadPool* tp) {
  if (no_transpose) {
    auto lambda = [input_data, blocks, output_data](ptrdiff_t i) {
      // The ConstEigenMatrixMap type is expanded to work around a MS compiler issue
      output_data[i] = Eigen::Map<const Eigen::Matrix<T, Eigen::Dynamic, 1>>(input_data + (i * blocks), blocks).sum();
    };
    concurrency::ThreadPool::TryBatchParallelFor(tp, block_size, lambda, 0);
  } else {
    EigenVectorMap<T> out_vec(output_data, block_size);
    out_vec = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks).rowwise().sum();
  }
}

template <typename T>
Tensor ReduceSum<T>::Impl(const Tensor& input, const std::vector<int64_t>& reduce_axes,
                          AllocatorPtr allocator, concurrency::ThreadPool* tp, bool keep_dims,
                          const TensorShape* input_shape_override) {
  FastAllocVector<T> transposed_input_data(allocator);
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;

  bool no_transpose = PrepareForReduce<T>(&input, transposed_input_data, block_size, blocks,
                                          reduce_axes, keep_dims, reduced_dims, true, input_shape_override);

  Tensor output(input.DataType(), reduced_dims, allocator);

  ReduceSumCore(input.template Data<T>(), output.template MutableData<T>(),
                no_transpose, blocks, block_size, transposed_input_data, tp);

  return output;
}

template <typename T>
Status ReduceSumSquare<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;
  std::vector<int64_t> reduced_dims;
  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);

  T* output_data = reduced->template MutableData<T>();

  if (no_transpose) {
    const T* input_data = input->template Data<T>();

    for (int64_t i = 0; i < block_size; ++i) {
      output_data[i] = ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).squaredNorm();
    }
  } else {
    EigenVectorMap<T> out_vec(output_data, block_size);
    out_vec = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks).rowwise().squaredNorm();
  }

  return Status::OK();
}

template <typename T>
Status ArgMax<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;

  std::vector<int64_t> reduced_dims;
  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);
  int64_t* output_data = reduced->template MutableData<int64_t>();
  Eigen::MatrixXf::Index maxIndex;

  if (no_transpose) {
    const T* input_data = ctx->Input<Tensor>(0)->template Data<T>();
    if (select_last_index_) {
      assert(blocks > 0);
      for (int64_t i = 0; i < block_size; ++i) {
        gsl::span<const T> row(input_data, blocks);
        auto first = row.cbegin();
        auto const end = row.cend();
        auto max_el = first;
        while (++first < end) {
          if (*first >= *max_el) {
            max_el = first;
          }
        }
        *(output_data++) = max_el - row.cbegin();
        input_data += blocks;
      }
    } else {
      for (int64_t i = 0; i < block_size; ++i) {
        ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).maxCoeff(&maxIndex);
        *(output_data++) = maxIndex;
      }
    }
  } else {
    auto matrixData = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks);
    if (select_last_index_) {
      for (int i = 0; i < block_size; ++i) {
        int idx = 0;
        T max_val = matrixData(i, 0);
        for (int c = 1; c < blocks; ++c) {
          auto val = matrixData(i, c);
          if (val >= max_val) {
            idx = c;
            max_val = val;
          }
        }
        *(output_data++) = idx;
      }
    } else {
      for (int i = 0; i < block_size; ++i) {
        matrixData.row(i).maxCoeff(&maxIndex);
        *(output_data++) = maxIndex;
      }
    }
  }

  return Status::OK();
}

template <typename T>
Status ArgMin<T>::Compute(OpKernelContext* ctx) const {
  FastAllocVector<T> transposed_input_data(GetAllocator<T>(*ctx));
  int64_t block_size;
  int64_t blocks;

  std::vector<int64_t> reduced_dims;
  const Tensor* input = ctx->Input<Tensor>(0);

  bool no_transpose = PrepareForReduce<T>(input, transposed_input_data, block_size, blocks, axes_, keepdims_, reduced_dims, true);

  Tensor* reduced = ctx->Output(0, reduced_dims);
  int64_t* output_data = reduced->template MutableData<int64_t>();
  Eigen::MatrixXf::Index minIndex;

  if (no_transpose) {
    const T* input_data = ctx->Input<Tensor>(0)->template Data<T>();
    if (select_last_index_) {
      assert(blocks > 0);
      for (int64_t i = 0; i < block_size; ++i) {
        gsl::span<const T> row(input_data, blocks);
        auto first = row.cbegin();
        auto const end = row.cend();
        auto min_el = first;
        while (++first < end) {
          if (*first <= *min_el) {
            min_el = first;
          }
        }
        *(output_data++) = min_el - row.cbegin();
        input_data += blocks;
      }
    } else {
      for (int64_t i = 0; i < block_size; ++i) {
        ConstEigenVectorMap<T>(input_data + (i * blocks), blocks).minCoeff(&minIndex);
        *(output_data++) = minIndex;
      }
    }
  } else {
    auto matrixData = ConstEigenMatrixMap<T>(&transposed_input_data[0], block_size, blocks);
    if (select_last_index_) {
      for (int i = 0; i < block_size; ++i) {
        int idx = 0;
        T min_val = matrixData(i, 0);
        for (int c = 1; c < blocks; ++c) {
          auto val = matrixData(i, c);
          if (val <= min_val) {
            idx = c;
            min_val = val;
          }
        }
        *(output_data++) = idx;
      }
    } else {
      for (int i = 0; i < block_size; ++i) {
        matrixData.row(i).minCoeff(&minIndex);
        *(output_data++) = minIndex;
      }
    }
  }

  return Status::OK();
}

// Explicit template instantiation -
// Even though there are kernels registered for ReduceSum op for these types,
// these are needed because we seem to get linker errors without these when the linker
// tries to resolve symbols in the einsum_auxiliary_ops obj file
template class ReduceSum<float>;
template class ReduceSum<int32_t>;
template class ReduceSum<double>;
template class ReduceSum<int64_t>;

#define REGISTER_REDUCESUMCORE_TYPED(T)                                                                         \
  template void ReduceSumCore<T>(const T* input_data, T* output_data, bool no_transpose,                        \
                                 int64_t blocks, int64_t block_size, FastAllocVector<T>& transposed_input_data, \
                                 concurrency::ThreadPool* tp);

REGISTER_REDUCESUMCORE_TYPED(float)
REGISTER_REDUCESUMCORE_TYPED(double)
REGISTER_REDUCESUMCORE_TYPED(int32_t)
REGISTER_REDUCESUMCORE_TYPED(int64_t)

}  // namespace onnxruntime
