// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/safeint.h"
#include "core/common/narrow.h"
#include "core/providers/cpu/math/gemm_base.h"
#include "core/providers/cpu/math/gemm_helper.h"
#include "core/providers/cpu/quantization/matmul_integer_base.h"
#include "core/quantization/quantization.h"
#include "core/util/math_cpuonly.h"

namespace onnxruntime {
namespace contrib {

template <class S, class T>
void GemmBroadcastBiasScaleBackWithCast(Eigen::Index M, Eigen::Index N, const S* c_data, const TensorShape& bias_shape,
                                        T* output, float a_scale, float b_scale) {
  auto output_mat = EigenMatrixMapRowMajor<T>(output, M, N);
  if (bias_shape.Size() == 1) {
    // C is (), (1,) or (1, 1), set the scalar
    const auto constant = static_cast<T>(static_cast<float>(c_data[0]) * a_scale * b_scale);
    output_mat.setConstant(constant);
  } else if (bias_shape.NumDimensions() == 1 || bias_shape[0] == 1) {
    // C is (N,) or (1, N)
    output_mat.rowwise() = (ConstEigenVectorMap<S>(c_data, N).transpose().template cast<float>() *
                            a_scale * b_scale)
                               .template cast<T>();
  } else if (bias_shape[1] == 1) {
    // C is (M, 1)
    output_mat.colwise() = (ConstEigenVectorMap<S>(c_data, M).template cast<float>() *
                            a_scale * b_scale)
                               .template cast<T>();
  } else {
    // C is (M, N), no broadcast needed.
    output_mat = (ConstEigenMatrixMapRowMajor<S>(c_data, M, N).template cast<float>() *
                  a_scale * b_scale)
                     .template cast<T>();
  }
}

/// <summary>
/// This function will attempt to handle the case where K is zero while M and N are not
/// We need to fill the output either with zeros or with c_data if present.
/// </summary>
/// <param name="a_scale"></param>
/// <param name="b_scale"></param>
/// <param name="y"></param>
/// <param name="allocator"></param>
/// <param name="y_scale"></param>
/// <param name="y_zp"></param>
/// <param name="c_data"></param>
static void HandleZeroKCase(const Tensor& a_scale, const Tensor& b_scale, Tensor& y, const AllocatorPtr& allocator,
                            const Tensor* y_scale, const Tensor* y_zp, const Tensor* bias) {
  const auto output_dims = y.Shape().GetDims();
  const auto M = narrow<Eigen::Index>(output_dims[0]);
  const auto N = narrow<Eigen::Index>(output_dims[1]);
  const float a_scale_value = a_scale.Data<float>()[0];
  const float b_scale_value = b_scale.Data<float>()[0];

  if (y_zp == nullptr) {
    // Either fill with c_data if present or 0
    int8_t* output = reinterpret_cast<int8_t*>(y.MutableDataRaw());
    if (bias != nullptr) {
      GemmBroadcastBiasScaleBackWithCast<int32_t, int8_t>(M, N, bias->Data<int32_t>(), bias->Shape(), output,
                                                          1.f, 1.f);
    } else {
      EigenMatrixMapRowMajor<int8_t> output_mat(output, M, N);
      output_mat.setZero();
    }
  } else {
    if (bias != nullptr) {
      // scale c_data back to float with result = c_data * a_scale * b_scale.
      Tensor scaled_back(DataTypeImpl::GetType<float>(), y.Shape(), allocator);
      GemmBroadcastBiasScaleBackWithCast<int32_t, float>(M, N, bias->Data<int32_t>(), bias->Shape(),
                                                         scaled_back.MutableData<float>(),
                                                         a_scale_value, b_scale_value);

      // re-quantize
      if (y_zp->IsDataType<int8_t>()) {
        auto q_params = quantization::GetTensorQuantizationParams<int8_t>(y_scale, y_zp);
        quantization::Quantize<int8_t>(scaled_back.Data<float>(),
                                       reinterpret_cast<int8_t*>(y.MutableDataRaw()), q_params,
                                       narrow<size_t>(scaled_back.Shape().Size()));
      } else {
        auto q_params = quantization::GetTensorQuantizationParams<uint8_t>(y_scale, y_zp);
        quantization::Quantize<uint8_t>(scaled_back.Data<float>(),
                                        reinterpret_cast<uint8_t*>(y.MutableDataRaw()), q_params,
                                        narrow<size_t>(scaled_back.Shape().Size()));
      }
    } else {
      // Fill with y_zp
      if (y_zp->IsDataType<int8_t>()) {
        int8_t* output = reinterpret_cast<int8_t*>(y.MutableDataRaw());
        EigenMatrixMapRowMajor<int8_t> output_mat(output, M, N);
        output_mat.setConstant(*(y_zp->Data<int8_t>()));
      } else {
        uint8_t* output = reinterpret_cast<uint8_t*>(y.MutableDataRaw());
        EigenMatrixMapRowMajor<uint8_t> output_mat(output, M, N);
        output_mat.setConstant(*(y_zp->Data<uint8_t>()));
      }
    }
  }
}

class QGemm : protected GemmBase, public MatMulIntegerBase {
 public:
  QGemm(const OpKernelInfo& info) : GemmBase(info), MatMulIntegerBase(info) {
  }

  Status Compute(OpKernelContext* context) const override {
    const auto* a = context->Input<Tensor>(IN_A);
    const auto* b = packed_b_ ? nullptr : context->Input<Tensor>(IN_B);
    const auto& b_shape = b ? b->Shape() : b_shape_;

    const auto* c = context->Input<Tensor>(IN_C);
    GemmHelper helper(a->Shape(), trans_A_ != CblasNoTrans,
                      b_shape, trans_B_ != CblasNoTrans,
                      c != nullptr ? c->Shape() : TensorShape({}));
    if (!helper.State().IsOK())
      return helper.State();

    ptrdiff_t M = helper.M();
    ptrdiff_t N = helper.N();
    ptrdiff_t K = helper.K();

    // validate scales and zero points
    const auto* a_zp = context->Input<Tensor>(IN_A_ZERO_POINT);
    const auto* b_zp = context->Input<Tensor>(IN_B_ZERO_POINT);
    const auto* y_zp = context->Input<Tensor>(IN_Y_ZERO_POINT);
    const auto* a_scale = context->Input<Tensor>(IN_A_SCALE);
    const auto* b_scale = context->Input<Tensor>(IN_B_SCALE);
    const auto* y_scale = context->Input<Tensor>(IN_Y_SCALE);
    ORT_RETURN_IF_ERROR(CheckInputs(a_zp, b_zp, y_zp, a_scale, b_scale, y_scale, helper));

    AllocatorPtr allocator;
    ORT_RETURN_IF_ERROR(context->GetTempSpaceAllocator(&allocator));

    auto y = context->Output(OUT_Y, {M, N});
    if (M == 0 || N == 0) return Status::OK();

    if (K == 0) {
      HandleZeroKCase(*a_scale, *b_scale, *y, allocator, y_scale, y_zp, c);
      return Status::OK();
    }

    bool a_is_signed = a->IsDataType<int8_t>();
    const uint8_t* a_data = static_cast<const uint8_t*>(a->DataRaw());

    std::optional<Tensor> a_trans_buffer;
    if (trans_A_ == CblasTrans) {
      a_data = quantization::TransPoseInputData(a_data, a_trans_buffer, allocator, K, M);
    }

    bool b_is_signed;
    const uint8_t* b_data = nullptr;
    std::optional<Tensor> b_trans_buffer;
    if (nullptr == b) {
      b_data = static_cast<const uint8_t*>(packed_b_.get());
      b_is_signed = b_is_signed_;
    } else {
      b_data = static_cast<const uint8_t*>(b->DataRaw());
      b_is_signed = b->IsDataType<int8_t>();
      if (trans_B_ == CblasTrans) {
        b_data = quantization::TransPoseInputData(b_data, b_trans_buffer, allocator, N, K);
      }
    }

    // prepare output buffer of GEMM
    int32_t* gemm_output_data = nullptr;
    std::optional<Tensor> gemm_output_buffer;
    bool need_requant = y_scale != nullptr;
    if (need_requant) {
      TensorShape outputshape{static_cast<int64_t>(M), static_cast<int64_t>(N)};
      gemm_output_buffer.emplace(DataTypeImpl::GetType<int32_t>(), outputshape, allocator);
      gemm_output_data = gemm_output_buffer->MutableData<int32_t>();
    } else {
      // y_scale is NULL. Then y_zp must also be NULL. In this case Y's type must be int32_t. This is checked by the
      // OP schema type. So the following cast is safe.
      gemm_output_data = static_cast<int32_t*>(y->MutableDataRaw());
    }

    if (c != nullptr) {
      GemmBroadcastBias<int32_t>(M, N, 1, c->Data<int32_t>(), &(c->Shape()), gemm_output_data);
    }

    MLAS_GEMM_QUANT_SHAPE_PARAMS gemm_shape{narrow<size_t>(M), narrow<size_t>(N), narrow<size_t>(K), a_is_signed, b_is_signed, c != nullptr};
    MLAS_GEMM_QUANT_DATA_PARAMS gemm_param;

    gemm_param.A = a_data;
    gemm_param.lda = gemm_shape.K;
    gemm_param.ZeroPointA = *(static_cast<const uint8_t*>(a_zp->DataRaw()));

    gemm_param.B = b_data;
    gemm_param.ldb = gemm_shape.N;
    gemm_param.BIsPacked = bool(packed_b_);
    gemm_param.ZeroPointB = static_cast<const uint8_t*>(b_zp->DataRaw());

    gemm_param.C = gemm_output_data;
    gemm_param.ldc = gemm_shape.N;

    gemm_param.PerColumnZeroPoints = !IsScalarOr1ElementVector(b_zp);

    std::vector<float> output_scales = ComputeOutputScale(a_scale, b_scale, y_scale);
    std::optional<MLAS_QGEMM_SCALE_BIAS_OUTPUT_PROCESSOR> scale_bias_proc_ptr;
    std::optional<MLAS_QGEMM_REQUANT_OUTPUT_PROCESSOR> requant_proc_ptr;
    SetPostProcessor(y_zp, N, output_scales, y, gemm_param, scale_bias_proc_ptr, requant_proc_ptr);

    MlasGemmBatch(gemm_shape, &gemm_param, 1, context->GetOperatorThreadPool());
    return Status::OK();
  }

 protected:
  int GetBIdx() const override {
    return IN_B;
  }

  virtual bool IsBTransposed() const override {
    return trans_B_ == CblasTrans;
  }

 private:
  enum InputTensors : int {
    IN_A = 0,
    IN_A_SCALE = 1,
    IN_A_ZERO_POINT = 2,
    IN_B = 3,
    IN_B_SCALE = 4,
    IN_B_ZERO_POINT = 5,
    IN_C = 6,
    IN_Y_SCALE = 7,
    IN_Y_ZERO_POINT = 8
  };

  enum OutputTensors : int {
    OUT_Y = 0
  };

  static Status CheckInputs(const Tensor* a_zp, const Tensor* b_zp, const Tensor* y_zp,
                            const Tensor* a_scale, const Tensor* b_scale, const Tensor* y_scale, const GemmHelper& helper) {
    ORT_RETURN_IF_NOT(IsScalarOr1ElementVector(a_scale),
                      "QGemm : scale of input a must be a scalar or 1D tensor of size 1");
    ORT_RETURN_IF_NOT(IsScalarOr1ElementVector(a_zp),
                      "QGemm : zero point of input a must be a scalar or 1D tensor of size 1");

    const auto& b_zp_shape = b_zp->Shape();
    const auto& b_scale_shape = b_scale->Shape();
    ORT_RETURN_IF_NOT(b_zp_shape.NumDimensions() == 0 ||
                          (b_zp_shape.NumDimensions() == 1 && (b_zp_shape[0] == 1 || b_zp_shape[0] == helper.N())),
                      "QGemm : zero point of input b must be a scalar or 1D tensor of size 1 or N");
    ORT_RETURN_IF_NOT(b_scale_shape.NumDimensions() == 0 ||
                          (b_scale_shape.NumDimensions() == 1 && (b_scale_shape[0] == 1 || b_scale_shape[0] == helper.N())),
                      "QGemm : scale of input b must be a scalar or 1D tensor of size 1 or N");
    ORT_RETURN_IF_NOT(b_scale_shape.NumDimensions() == b_zp_shape.NumDimensions() &&
                          (b_scale_shape.NumDimensions() == 0 || (b_scale_shape[0] == b_zp_shape[0])),
                      "QGemm : zero point and scale of input b should have same shape size");

    ORT_RETURN_IF_NOT(y_zp == nullptr || IsScalarOr1ElementVector(y_zp),
                      "QGemm : zero point of y must be null or a scalar or 1D tensor of size 1");
    ORT_RETURN_IF_NOT(y_scale == nullptr || IsScalarOr1ElementVector(y_scale),
                      "QGemm : scale of y must be null or a scalar or 1D tensor of size 1");
    return Status::OK();
  }

  std::vector<float> ComputeOutputScale(const Tensor* a_scale, const Tensor* b_scale, const Tensor* y_scale) const {
    const int64_t output_scale_size = b_scale->Shape().Size();
    std::vector<float> output_scales(onnxruntime::narrow<size_t>(output_scale_size));
    auto a_scale_value = *(a_scale->Data<float>());
    const auto* b_scale_data = b_scale->Data<float>();
    for (int64_t i = 0; i < output_scale_size; i++) {
      output_scales[onnxruntime::narrow<size_t>(i)] = (alpha_ * a_scale_value * b_scale_data[i]);
      if (nullptr != y_scale) {
        output_scales[onnxruntime::narrow<size_t>(i)] /= *(y_scale->Data<float>());
      }
    }
    return output_scales;
  }

  static void SetPostProcessor(const Tensor* y_zp,
                               size_t out_lda,
                               const std::vector<float>& output_scales,
                               Tensor* y,
                               MLAS_GEMM_QUANT_DATA_PARAMS& gemm_param,
                               std::optional<MLAS_QGEMM_SCALE_BIAS_OUTPUT_PROCESSOR>& scale_bias_proc_ptr,
                               std::optional<MLAS_QGEMM_REQUANT_OUTPUT_PROCESSOR>& requant_proc_ptr) {
    if (nullptr != y_zp) {
      bool is_y_signed = y->IsDataType<int8_t>();
      int32_t y_zero_point = is_y_signed ? *y_zp->Data<int8_t>() : *y_zp->Data<uint8_t>();
      requant_proc_ptr.emplace(
          y->MutableDataRaw(),
          out_lda,
          nullptr,
          output_scales.data(),
          output_scales.size() > 1,
          y_zero_point,
          is_y_signed);
      gemm_param.OutputProcessor = &*requant_proc_ptr;
    } else {
      scale_bias_proc_ptr.emplace(
          static_cast<float*>(y->MutableDataRaw()),
          out_lda,
          output_scales.data(),
          nullptr,
          MLAS_QGEMM_OUTPUT_MODE::ZeroMode,
          output_scales.size() > 1 ? MLAS_QUANTIZATION_GRANULARITY::PerColumn : MLAS_QUANTIZATION_GRANULARITY::PerMatrix);
      gemm_param.OutputProcessor = &*scale_bias_proc_ptr;
    }
  }
};

ONNX_OPERATOR_TYPED_KERNEL_EX(
    QGemm,
    kMSDomain,
    1,
    uint8_t,
    kCpuExecutionProvider,
    KernelDefBuilder()
        .TypeConstraint("T", DataTypeImpl::GetTensorType<float>())
        .TypeConstraint("TA", DataTypeImpl::GetTensorType<uint8_t>())
        .TypeConstraint("TB", {DataTypeImpl::GetTensorType<uint8_t>(), DataTypeImpl::GetTensorType<int8_t>()})
        .TypeConstraint("TC", DataTypeImpl::GetTensorType<int32_t>())
        .TypeConstraint("TYZ", DataTypeImpl::GetTensorType<uint8_t>())
        .TypeConstraint("TY", {DataTypeImpl::GetTensorType<float>(), DataTypeImpl::GetTensorType<uint8_t>()}),
    QGemm);

ONNX_OPERATOR_TYPED_KERNEL_EX(
    QGemm,
    kMSDomain,
    1,
    int8_t,
    kCpuExecutionProvider,
    KernelDefBuilder()
        .TypeConstraint("T", DataTypeImpl::GetTensorType<float>())
        .TypeConstraint("TA", DataTypeImpl::GetTensorType<int8_t>())
        .TypeConstraint("TB", DataTypeImpl::GetTensorType<int8_t>())
        .TypeConstraint("TC", DataTypeImpl::GetTensorType<int32_t>())
        .TypeConstraint("TYZ", DataTypeImpl::GetTensorType<int8_t>())
        .TypeConstraint("TY", {DataTypeImpl::GetTensorType<float>(), DataTypeImpl::GetTensorType<int8_t>()}),
    QGemm);

}  // namespace contrib
}  // namespace onnxruntime
