// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cuda/tensor/transpose_impl.h"
#include "conv_transpose.h"

// To suppress FP static analyzer warnings:
// https://msdata.visualstudio.com/Vienna/_workitems/edit/1944928 and
// https://msdata.visualstudio.com/Vienna/_workitems/edit/1944950
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 26110)
#pragma warning(disable : 26117)
#endif

namespace onnxruntime {
namespace cuda {

// Op Set 11 for ConvTranspose only update document to clarify default dilations and strides value.
// which are already covered by op set 11 cpu version, so simply add declaration.
#define REGISTER_KERNEL_TYPED(T, DOMAIN, NHWC)                                                           \
  ONNX_OPERATOR_VERSIONED_TYPED_KERNEL_EX(                                                 \
      ConvTranspose,                                                                       \
      DOMAIN,                                                                         \
      1, 10,                                                                               \
      T,                                                                                   \
      kCudaExecutionProvider,                                                              \
      (*KernelDefBuilder::Create()).TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      ConvTranspose<T, NHWC>);                                                                   \
  ONNX_OPERATOR_TYPED_KERNEL_EX(                                                           \
      ConvTranspose,                                                                       \
      DOMAIN,                                                                         \
      11,                                                                                  \
      T,                                                                                   \
      kCudaExecutionProvider,                                                              \
      (*KernelDefBuilder::Create()).TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      ConvTranspose<T, NHWC>);

REGISTER_KERNEL_TYPED(float, kOnnxDomain, false)
REGISTER_KERNEL_TYPED(double, kOnnxDomain, false)
REGISTER_KERNEL_TYPED(MLFloat16, kOnnxDomain, false)

REGISTER_KERNEL_TYPED(float, kMSInternalNHWCDomain, true)
REGISTER_KERNEL_TYPED(MLFloat16, kMSInternalNHWCDomain, true)

template <typename T, bool NHWC>
Status ConvTranspose<T, NHWC>::ComputeInternal(OpKernelContext* context) const {
  return DoConvTranspose(context, false);
}

template <typename T, bool NHWC>
Status ConvTranspose<T, NHWC>::DoConvTranspose(OpKernelContext* context, bool dynamic_padding) const {
  typedef typename ToCudaType<T>::MappedType CudaT;

  const Tensor* X = context->Input<Tensor>(0);
  const TensorShape& x_shape = X->Shape();
  auto x_dims = x_shape.AsShapeVector();
  auto x_data = reinterpret_cast<const CudaT*>(X->Data<T>());

  auto x_dimensions = X->Shape().NumDimensions();
  if (x_dimensions < 3 || x_dimensions > 5) {
    // TODO: the error message should tell which operator raises it.
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "Input X must be 3-, 4- or 5-dimensional.",
                           " X: ", X->Shape().ToString().c_str());
  }
  const Tensor* W = context->Input<Tensor>(1);
  const TensorShape& w_shape = W->Shape();
  TensorShapeVector w_dims = w_shape.AsShapeVector();
  auto w_data = reinterpret_cast<const CudaT*>(W->Data<T>());

  size_t num_inputs = OpKernel::Node().InputDefs().size();
  bool has_bias = dynamic_padding ? num_inputs == 4 : num_inputs == 3;

  CudaT* y_data = nullptr;
  if (x_dimensions == 3) {
    x_dims.insert(x_dims.begin() + 2, 1);
    w_dims.insert(w_dims.begin() + 2, 1);
  }

  {
    std::lock_guard<OrtMutex> lock(s_.mutex);
    // CUDNN_CONFIG_RETURN_IF_ERROR(cudnnSetStream(CudnnHandle(), Stream(context)));
    // TODO: add a global cache if need to handle cases for multiple frames running simultaneously with different batch_size
    bool input_dims_changed = (s_.last_x_dims.AsShapeVector() != x_dims);
    bool w_dims_changed = (s_.last_w_dims.AsShapeVector() != w_dims);
    if (input_dims_changed || w_dims_changed) {
      if (input_dims_changed)
        s_.last_x_dims = gsl::make_span(x_dims);

      if (w_dims_changed) {
        s_.last_w_dims = gsl::make_span(w_dims);
        s_.cached_benchmark_results.clear();
      }

      // If we remove the contrib op NhwcConv we can rewrite this to be much simpler
      // basically only strides of W will change and a transpose kernel is launched if NHWC is true
      // currently we are only allowed to transpose the weight if node is part of kMSInternalNHWCDomain
      if (transpose_weights_) {
        // if the OP is registered in domain kMSInternalNHWCDomain the weights are not transposed beforehand.
        auto nchw_strides = generateStrides(w_dims, false);
        w_dims = {w_dims[0], w_dims[2], w_dims[3], w_dims[1]};
        auto nhwc_strides = generateStrides(w_dims, true);
        cudaStream_t compute_stream = Stream(context);
        const T * w_data_nchw = W->Data<T>();
        size_t weight_bytes = W->SizeInBytes();
        IAllocatorUniquePtr<void> w_data_nhwc_temp = GetTransientScratchBuffer<void>(weight_bytes);
        const TArray<int64_t> nchw_strides_arr = {};
        const TArray<fast_divmod> nhwc_strides_arr = {};
        auto status = TransposeImpl(
          compute_stream, sizeof(T), w_dims.size(),
          nchw_strides_arr, w_data_nchw, nhwc_strides_arr, w_data_nhwc_temp.get(), w_dims.size()
        );

        T * w_data_nchw_out = const_cast<T*>(W->Data<T>());
        CUDA_RETURN_IF_ERROR(cudaMemcpyAsync(w_data_nchw_out ,w_data_nhwc_temp.get() , weight_bytes, cudaMemcpyDeviceToDevice, compute_stream));
        CUDA_RETURN_IF_ERROR(cudaStreamSynchronize(compute_stream));
      }

      ConvTransposeAttributes::Prepare p;
      ORT_RETURN_IF_ERROR(conv_transpose_attrs_.PrepareForCompute(context, has_bias, p, dynamic_padding, nullptr, NHWC));

      auto y_dims = p.Y->Shape().AsShapeVector();
      if (x_dimensions == 3) {
        y_dims.insert(y_dims.begin() + 2, 1);
        p.kernel_shape.insert(p.kernel_shape.begin(), 1);
        p.pads.insert(p.pads.begin(), 0);
        p.pads.insert(p.pads.begin() + 2, 0);
        p.strides.insert(p.strides.begin(), 1);
        p.dilations.insert(p.dilations.begin(), 1);
      }
      s_.y_dims = gsl::make_span(y_dims);

    if (w_dims_changed) {
      if (NHWC) {
        ORT_RETURN_IF_ERROR(s_.w_desc.Set(CUDNN_TENSOR_NHWC,
                                          CudnnTensor::GetDataType<CudaT>(),
                                          static_cast<int>(w_dims[0]),
                                          static_cast<int>(w_dims[3]),
                                          static_cast<int>(w_dims[1]),
                                          static_cast<int>(w_dims[2])));
      } else {
        ORT_RETURN_IF_ERROR(s_.w_desc.Set(w_dims, CudnnTensor::GetDataType<CudaT>()));
      }
    }

      // Special case when there is a dim value of 0 in the shape.
      // Return only after we have cached the following for subsequent runs :
      // 1) `w_dims` in the `w_desc`
      // 2) `y_dims` in s_.y_dims
      if (p.Y->Shape().Size() == 0) {
        return Status::OK();
      }
      if (NHWC) {
        ORT_RETURN_IF_ERROR(s_.x_tensor.Set(CUDNN_TENSOR_NHWC, CudnnTensor::GetDataType<CudaT>(),
                                          static_cast<int>(x_dims[0]),
                                          static_cast<int>(x_dims[3]),
                                          static_cast<int>(x_dims[1]),
                                          static_cast<int>(x_dims[2])));
        ORT_RETURN_IF_ERROR(s_.y_tensor.Set(CUDNN_TENSOR_NHWC, CudnnTensor::GetDataType<CudaT>(),
                                            static_cast<int>(y_dims[0]),
                                            static_cast<int>(y_dims[3]),
                                            static_cast<int>(y_dims[1]),
                                            static_cast<int>(y_dims[2])));
      } else {
        ORT_RETURN_IF_ERROR(s_.x_tensor.Set(x_dims, CudnnTensor::GetDataType<CudaT>()));
        ORT_RETURN_IF_ERROR(s_.y_tensor.Set(y_dims, CudnnTensor::GetDataType<CudaT>()));
      }

      cudnnConvolutionMode_t mode = CUDNN_CROSS_CORRELATION;
      ORT_RETURN_IF_ERROR(s_.conv_desc.Set(p.kernel_shape.size(), p.pads, p.strides, p.dilations,
                                           gsl::narrow_cast<int>(conv_transpose_attrs_.group),
                                           mode, CudnnTensor::GetDataType<CudaT>()));

      if (has_bias) {
        const auto& b_shape = p.B->Shape();
        ORT_RETURN_IF_NOT(b_shape.NumDimensions() == 1, "bias should be 1D");
        TensorShapeVector b_dims(2 + p.kernel_shape.size());
        b_dims[0] = 1;                     // N
        b_dims[NHWC ? 3: 1] = b_shape[0];  // C
        for (size_t i = 0; i < p.kernel_shape.size(); i++)
          b_dims[(NHWC ? 1: 2) + i] = 1;

        ORT_RETURN_IF_ERROR(s_.b_tensor.Set(b_dims, CudnnTensor::GetDataType<CudaT>(), NHWC));
      }

      y_data = reinterpret_cast<CudaT*>(p.Y->MutableData<T>());

      if (!s_.cached_benchmark_results.contains(x_dims)) {
        IAllocatorUniquePtr<void> algo_search_workspace = GetScratchBuffer<void>(AlgoSearchWorkspaceSize, context->GetComputeStream());

        // set math type to tensor core before algorithm search
        if constexpr (std::is_same<T, MLFloat16>::value)
          CUDNN_RETURN_IF_ERROR(cudnnSetConvolutionMathType(s_.conv_desc, CUDNN_TENSOR_OP_MATH));

        cudnnConvolutionBwdDataAlgoPerf_t perf;
        int algo_count = 1;
        CUDNN_RETURN_IF_ERROR(cudnnFindConvolutionBackwardDataAlgorithmEx(
            GetCudnnHandle(context),
            s_.w_desc,
            w_data,
            s_.x_tensor,
            x_data,
            s_.conv_desc,
            s_.y_tensor,
            y_data,
            1,
            &algo_count,
            &perf,
            algo_search_workspace.get(),
            AlgoSearchWorkspaceSize));
        s_.cached_benchmark_results.insert(x_dims, {perf.algo, perf.memory, perf.mathType});
      }

      const auto& perf = s_.cached_benchmark_results.at(x_dims);
      CUDNN_RETURN_IF_ERROR(cudnnSetConvolutionMathType(s_.conv_desc, perf.mathType));
      s_.algo = perf.algo;
      s_.workspace_bytes = perf.memory;
    }

    // The following block will be executed in case there has been no change in the shapes of the
    // input and the filter compared to the previous run
    if (!y_data) {
      auto y_dims = s_.y_dims.AsShapeVector();
      if (x_dimensions == 3) {
        y_dims.erase(y_dims.begin() + 2);
      }
      Tensor* Y = context->Output(0, TensorShape(y_dims));
      y_data = reinterpret_cast<CudaT*>(Y->MutableData<T>());

      // Bail out early if one of the output dimensions is zero.
      if (Y->Shape().Size() == 0) {
        return Status::OK();
      }
    }

    const auto alpha = Consts<CudaT>::One;
    const auto beta = Consts<CudaT>::Zero;

    IAllocatorUniquePtr<void> workspace = GetScratchBuffer<void>(s_.workspace_bytes, context->GetComputeStream());

    CUDNN_RETURN_IF_ERROR(
        cudnnConvolutionBackwardData(
            GetCudnnHandle(context),
            &alpha,
            s_.w_desc,
            w_data,
            s_.x_tensor,
            x_data,
            s_.conv_desc,
            s_.algo,
            workspace.get(),
            s_.workspace_bytes,
            &beta,
            s_.y_tensor,
            y_data));

    if (has_bias) {
      const Tensor* B = dynamic_padding ? context->Input<Tensor>(3) : context->Input<Tensor>(2);
      auto b_data = reinterpret_cast<const CudaT*>(B->Data<T>());
      CUDNN_RETURN_IF_ERROR(cudnnAddTensor(GetCudnnHandle(context), &alpha, s_.b_tensor, b_data, &alpha, s_.y_tensor, y_data));
    }
  }

  return Status::OK();
}

}  // namespace cuda
}  // namespace onnxruntime

#ifdef _WIN32
#pragma warning(pop)
#endif
