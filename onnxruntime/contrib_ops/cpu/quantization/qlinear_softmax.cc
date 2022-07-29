// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "contrib_ops/cpu/quantization/qlinear_softmax.h"

#include <cstdint>
#include <type_traits>
#include <utility>

#include "core/common/common.h"
#include "core/framework/tensorprotoutils.h"
#include "core/providers/common.h"
#include "core/providers/cpu/tensor/transpose.h"

#include "core/mlas/inc/mlas.h"
#include "core/platform/threadpool.h"
#include "gsl/gsl-lite.hpp"

namespace onnxruntime {
namespace contrib {

constexpr int OPSET13 = 13;

namespace {

void QlinearBuildLookupTableUint32(uint32_t* table,
                                   const float x_scale,
                                   size_t reduce_len, bool is_signed) {
  const double qscale =
      fmin(static_cast<double>(UINT32_MAX) / static_cast<double>(reduce_len), static_cast<double>(0x7fffff));
  for (int32_t i = 0; i < 256; i++) {
    double scaled_exp_xi = qscale * exp(static_cast<double>(i - 255) * static_cast<double>(x_scale));
    // we can't get the real max number of input tensor here, so we just assume 255.
    // in the process of computation, all numbers will have a shift to align 255
    //
    // if is_signed index = [1 2 3 ......126 127 -128 -127 ..... -3 -2 -1]
    // else [0 1 2 3 4 ..... 256]
    uint8_t index = is_signed ? static_cast<uint8_t>(i - 128) : gsl::narrow_cast<uint8_t>(i);
    table[index] = static_cast<uint32_t>(lrint(scaled_exp_xi));
  }
}

void BuildLookupTableIfFixed(const OpKernelInfo& info, std::vector<uint32_t>& fixed_lookup_table,
                             size_t reduce_len, bool is_signed) {
  const Tensor* tensor_x_scale = nullptr;

  bool get_x_scale = info.TryGetConstantInput(1, &tensor_x_scale);
  ORT_ENFORCE(tensor_x_scale == nullptr || IsScalarOr1ElementVector(tensor_x_scale),
              "QlinearBuildLookupTable : input X_scale must be a scalar or 1D tensor of size 1");
  bool is_fixed_parameters = get_x_scale;

  if (is_fixed_parameters) {
    fixed_lookup_table.resize(256);
    const float X_scale = *(tensor_x_scale->Data<float>());
    QlinearBuildLookupTableUint32(fixed_lookup_table.data(), X_scale, reduce_len, is_signed);
  }
}
}  // namespace


QLinearSoftmax::QLinearSoftmax(const OpKernelInfo& info)
    : OpKernel(info) {
  const auto& node = info.node();
  auto input_defs = node.InputDefs();
  auto input_type = input_defs[0]->TypeAsProto()->tensor_type().elem_type();
  is_signed_ = (input_type == ONNX_NAMESPACE::TensorProto_DataType_INT8);
  const auto* x_shape = input_defs[0]->Shape();
  if (x_shape == nullptr || x_shape->dim_size() == 0) {
    return;
  }
  int rank = x_shape->dim_size();

  int64_t opset = -1;
  Status status = info.GetAttr<int64_t>("opset", &opset);
  ORT_ENFORCE(status.IsOK(), "opset must be existed in attributes of QlinearSoftmax");
  opset_ = gsl::narrow_cast<int>(opset);

  int64_t axis = -1;
  status = info.GetAttr<int64_t>("axis", &axis);
  if (status.IsOK()) {
    axis_ = gsl::narrow_cast<int>(axis);
  } else {
    if (opset_ < OPSET13) {
      axis_ = 1;  // opset-12 and below, the default axis value is 1
    } else {
      axis_ = -1;  // opset-13, the default axis value is -1
    }
  }

  if (axis_ < 0) {
    axis_ = static_cast<int>(HandleNegativeAxis(axis_, int64_t(rank)));
  }
  auto input_shape = utils::GetTensorShapeFromTensorShapeProto(*x_shape);
  int64_t reduce_size = input_shape[axis_];
  if (opset_ < OPSET13) {
    reduce_size = input_shape.SizeFromDimension(axis_);
  }
  ORT_ENFORCE(reduce_size > 0, "invalid reduce_size for softmax");
  BuildLookupTableIfFixed(info, fixed_lookup_table_, reduce_size, is_signed_);
}

// compute method of Softmax
Status QLinearSoftmax::Compute(OpKernelContext* ctx) const {
  const auto* X = ctx->Input<Tensor>(0);
  const auto& X_shape = X->Shape();
  auto* Y = ctx->Output(0, X_shape);

  // edge case. one or more dims with value of 0. nothing to do
  if (X_shape.Size() == 0) {
    return Status::OK();
  }
  concurrency::ThreadPool* thread_pool = ctx->GetOperatorThreadPool();
  size_t D = X_shape[axis_];
  if (opset_ < OPSET13) {
    D = X_shape.SizeFromDimension(axis_);
  }
  uint32_t tmp_lookup_table[256];
  gsl::span<uint32_t> lookup_table_span = gsl::make_span(tmp_lookup_table, 256);
  gsl::span<const uint32_t> lookup_table = GetLookupTable(ctx, lookup_table_span, D);

  if (opset_ < OPSET13) {
    return ComputeImpl(ctx, *X, *Y, thread_pool, lookup_table);
  } else {
    return ComputeImplOpset13(ctx, *X, *Y, thread_pool, lookup_table);
  }
}

template <typename T>
common::Status QlinearSoftmaxCPU(size_t N,
                                 size_t D,
                                 const T* x_data,
                                 T* y_data,
                                 const uint32_t* lookup_table,
                                 uint32_t y_scale,
                                 T yzp,
                                 onnxruntime::concurrency::ThreadPool* thread_pool);

template <>
common::Status QlinearSoftmaxCPU<uint8_t>(size_t N,
                                          size_t D,
                                          const uint8_t* x_data,
                                          uint8_t* y_data,
                                          const uint32_t* lookup_table,
                                          uint32_t y_scale,
                                          uint8_t yzp,
                                          onnxruntime::concurrency::ThreadPool* thread_pool) {
  using onnxruntime::TensorOpCost;
  using onnxruntime::concurrency::ThreadPool;
  ThreadPool::TryParallelFor(
      thread_pool, N,
      // Read 3*N (max,sum,div) write N (div), computation=Read
      TensorOpCost{static_cast<double>(D * 3),
                   static_cast<double>(D),
                   static_cast<double>(D * 3)},
      [x_data, y_data, D, y_scale, yzp, &lookup_table](std::ptrdiff_t first, std::ptrdiff_t last) {
        const auto c_y_scale = y_scale;
        const auto c_y_zp = yzp;
        const uint8_t* x_t = x_data + first * D;
        uint8_t* y_t = y_data + first * D;
        for (; first < last; first++) {
          // reduceMaxUint8
          uint8_t xmax = *std::max_element(x_t, x_t + D);
          // we want the xmas to align with 255 for higher precision.
          // as we build a lookup table with X-255. So we could use the adjustment here
          // to let all numbers have a shift in the lookup table.
          // 1 2 3 4 5 ...........................254 255
          // 1   3   5 ... 10
          // after the shift --->
          //                        235  237  239  .. 255
          const uint32_t* shifted_lookuptable = lookup_table + 255 - xmax;
          size_t elements_n = D;
          // reduceSumUin8ToUint32: need speedup
          uint32_t vsum = 0;
          const uint8_t* x_t_cur = x_t;
          do {
            const size_t vx = *x_t_cur++;
            vsum += shifted_lookuptable[vx];
          } while (--elements_n != 0);
          if (vsum == 0) {
            return;
          }
          elements_n = D;
          x_t_cur = x_t;
          // elementwise div
          const uint32_t vrounding = (vsum >> 1);
          do {
            const size_t vx = *x_t_cur++;
            const uint32_t vt = shifted_lookuptable[vx];
            // simulate round function, and re-quant to uint8
            const uint32_t vq = ((vt * c_y_scale) + vrounding) / vsum + c_y_zp;
            const uint8_t vy = vq > 255 ? static_cast<uint8_t>(255) : static_cast<uint8_t>(vq);
            *y_t++ = vy;
          } while (--elements_n != 0);
          x_t = x_t_cur;
        }
      });

  return Status::OK();
}

template <>
common::Status QlinearSoftmaxCPU<int8_t>(size_t N,
                                         size_t D,
                                         const int8_t* x_data,
                                         int8_t* y_data,
                                         const uint32_t* lookup_table,
                                         uint32_t y_scale,
                                         int8_t yzp,
                                         onnxruntime::concurrency::ThreadPool* thread_pool) {
  using onnxruntime::TensorOpCost;
  using onnxruntime::concurrency::ThreadPool;
  ThreadPool::TryParallelFor(
      thread_pool, N,
      // Read 3*N (max,sum,div) write N (div), computation=Read
      TensorOpCost{static_cast<double>(D * 3),
                   static_cast<double>(D),
                   static_cast<double>(D * 3)},
      [x_data, y_data, D, y_scale, yzp, &lookup_table](std::ptrdiff_t first, std::ptrdiff_t last) {
        const auto c_y_scale = y_scale;
        const auto c_y_zp = yzp;

        const int8_t* x_t = x_data + first * D;
        int8_t* y_t = y_data + first * D;
        for (; first < last; first++) {
          // reduceMaxUint8
          int8_t xmax = *std::max_element(x_t, x_t + D);
          const size_t adjustment = 127 - xmax;
          const uint32_t* shifted_lookuptable = lookup_table;
          size_t elements_n = D;
          // reduceSumUin8ToUint32: need speedup
          uint32_t vsum = 0;
          const int8_t* x_t_cur = x_t;
          do {
            const size_t vx = uint8_t(adjustment + *x_t_cur++);
            vsum += shifted_lookuptable[vx];
          } while (--elements_n != 0);
          if (vsum == 0) {
            return;
          }
          elements_n = D;
          x_t_cur = x_t;
          // elementwise div
          const uint32_t vrounding = (vsum >> 1);
          do {
            const size_t vx = uint8_t(adjustment + *x_t_cur++);
            const uint32_t vt = shifted_lookuptable[vx];
            // simulate round function, and re-quant to int8
            const uint32_t vq = ((vt * c_y_scale) + vrounding) / vsum + c_y_zp;
            const int8_t vy = static_cast<int32_t>(vq) > 255 ? static_cast<int8_t>(255) : static_cast<int8_t>(vq);
            *y_t++ = vy;
          } while (--elements_n != 0);
          x_t = x_t_cur;
        }
      });

  return Status::OK();
}

gsl::span<const uint32_t> QLinearSoftmax::GetLookupTable(OpKernelContext* context,
                                                         gsl::span<uint32_t> lookup_table_span,
                                                         size_t reduce_len) const {
  gsl::span<const uint32_t> lookup_table = fixed_lookup_table_;
  if (fixed_lookup_table_.size() == 0) {
    lookup_table = lookup_table_span;
    const float X_scale = *(context->Input<Tensor>(1)->Data<float>());
    QlinearBuildLookupTableUint32(lookup_table_span.data(), X_scale, reduce_len, is_signed_);
  }
  return lookup_table;
}

// opset-12 and below
Status QLinearSoftmax::ComputeImpl(OpKernelContext* context, const Tensor& input, Tensor& output,
                                      concurrency::ThreadPool* thread_pool,
                                      gsl::span<const uint32_t> lookup_table) const {
  const auto* Y_scale_tensor = context->Input<Tensor>(3);
  const auto* Y_zp_tensor = context->Input<Tensor>(4);
  const auto Y_scale = gsl::narrow_cast<uint32_t>(1.0F / (*(Y_scale_tensor->Data<float>())));
  const auto& X_shape = input.Shape();
  const size_t N = X_shape.SizeToDimension(axis_);
  const size_t D = X_shape.SizeFromDimension(axis_);
  common::Status status;
  if (is_signed_) {
    using T=int8_t;
    const T Y_zp = Y_zp_tensor ? *(Y_zp_tensor->Data<T>()) : 0;
    status = QlinearSoftmaxCPU<T>(N, D, input.Data<T>(), output.MutableData<T>(),
                                lookup_table.data(), Y_scale, Y_zp, thread_pool);
  } else {
    using T = uint8_t;
    const T Y_zp = Y_zp_tensor ? *(Y_zp_tensor->Data<T>()) : 0;
    status = QlinearSoftmaxCPU<T>(N, D, input.Data<T>(), output.MutableData<T>(),
                                  lookup_table.data(), Y_scale, Y_zp, thread_pool);
  }
  return status;
}

// opset-13 and above
Status QLinearSoftmax::ComputeImplOpset13(OpKernelContext* context,
                                             const Tensor& input, Tensor& output,
                                             concurrency::ThreadPool* thread_pool,
                                             gsl::span<const uint32_t> lookup_table) const {
  const auto* Y_scale_tensor = context->Input<Tensor>(3);
  const auto* Y_zp_tensor = context->Input<Tensor>(4);
  const auto Y_scale = gsl::narrow_cast<uint32_t>(1.0F / (*(Y_scale_tensor->Data<float>())));

  const auto& X_shape = input.Shape();
  size_t rank = X_shape.NumDimensions();

  bool is_transpose_required = false;
  Tensor transposed_input;
  std::vector<int64_t> transposed_input_dims;
  Tensor intermediate_output;  // output that the softmax implementation will write into while using transposed input
  std::vector<size_t> permutation(rank);

  // The "semantic" meaning of axis has changed in opset-13.
  // Please compare: https://github.com/onnx/onnx/blob/master/docs/Operators.md#Softmax
  // with https://github.com/onnx/onnx/blob/master/docs/Changelog.md#Softmax-11 for detailed explanations
  // To account for the opset-13 behavior, our plan will be to transpose the "axis" dim to the innermost dim
  // and perform softmax and then reverse the transpose. We can skip the transposing aspect if the axis is already
  // the innermost dim
  if (size_t(axis_) != (rank - 1)) {
    is_transpose_required = true;
  }

  if (is_transpose_required) {
    AllocatorPtr alloc;
    ORT_RETURN_IF_ERROR(context->GetTempSpaceAllocator(&alloc));

    std::iota(std::begin(permutation), std::end(permutation), 0);

    // swap the innermost dim with the dim corresponding to axis
    permutation[axis_] = rank - 1;
    permutation[rank - 1] = axis_;

    transposed_input_dims.reserve(rank);
    for (auto e : permutation) {
      transposed_input_dims.push_back(X_shape[e]);
    }

    // Allocate a temporary tensor to hold transposed input
    Tensor temp_input(input.DataType(), TensorShape(transposed_input_dims), alloc);

    // Perform the transpose
    ORT_RETURN_IF_ERROR(TransposeBase::DoTranspose(permutation, input, temp_input));
    transposed_input = std::move(temp_input);

    // Allocate memory for the intermediate output
    intermediate_output = Tensor(output.DataType(), TensorShape(transposed_input_dims), alloc);
  }

  const size_t D = X_shape[axis_];
  const size_t N = X_shape.Size() / D;
  common::Status status;

  if (is_signed_) {
    using T = int8_t;
    const T Y_zp = Y_zp_tensor ? *(Y_zp_tensor->Data<T>()) : 0;
    const T* x_data = is_transpose_required ? transposed_input.Data<T>() : input.Data<T>();
    T* y_data = is_transpose_required ? intermediate_output.MutableData<T>() : output.MutableData<T>();

    status = (QlinearSoftmaxCPU<T>(N, D, x_data, y_data, lookup_table.data(), Y_scale, Y_zp, thread_pool));
  } else {
    using T = uint8_t;
    const T Y_zp = Y_zp_tensor ? *(Y_zp_tensor->Data<T>()) : 0;
    const T* x_data = is_transpose_required ? transposed_input.Data<T>() : input.Data<T>();
    T* y_data = is_transpose_required ? intermediate_output.MutableData<T>() : output.MutableData<T>();

    status = (QlinearSoftmaxCPU<T>(N, D, x_data, y_data, lookup_table.data(), Y_scale, Y_zp, thread_pool));
  }

  if (is_transpose_required) {
    // Perform the transpose to get the axes back to the original ordering
    status = (TransposeBase::DoTranspose(permutation, intermediate_output, output));
  }
  return status;
}

#define REGISTER_QLINEAR_LOOKUPTABLE_TYPED_KERNEL(op_name, version, data_type, KERNEL_CLASS) \
  ONNX_CPU_OPERATOR_TYPED_MS_KERNEL(                                                         \
      op_name, version, data_type,                                                           \
      KernelDefBuilder()                                                                     \
          .TypeConstraint("T", DataTypeImpl::GetTensorType<data_type>()),                    \
      KERNEL_CLASS);

REGISTER_QLINEAR_LOOKUPTABLE_TYPED_KERNEL(QLinearSoftmax, 1, uint8_t, QLinearSoftmax);
REGISTER_QLINEAR_LOOKUPTABLE_TYPED_KERNEL(QLinearSoftmax, 1, int8_t, QLinearSoftmax);

}  // namespace contrib
}  // namespace onnxruntime
