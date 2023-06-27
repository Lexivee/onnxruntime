// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "gemm_float8.h"
#include "core/providers/cuda/cu_inc/common.cuh"
#include "core/providers/cuda/shared_inc/cuda_utils.h"
#include <algorithm>
#include <cuda_runtime.h>

namespace onnxruntime {
namespace contrib {
namespace cuda {

static const char* cublasGetErrorEnum(cublasStatus_t error) {
  switch (error) {
    case CUBLAS_STATUS_SUCCESS:
      return "CUBLAS_STATUS_SUCCESS";
    case CUBLAS_STATUS_NOT_INITIALIZED:
      return "CUBLAS_STATUS_NOT_INITIALIZED";
    case CUBLAS_STATUS_ALLOC_FAILED:
      return "CUBLAS_STATUS_ALLOC_FAILED";
    case CUBLAS_STATUS_INVALID_VALUE:
      return "CUBLAS_STATUS_INVALID_VALUE";
    case CUBLAS_STATUS_ARCH_MISMATCH:
      return "CUBLAS_STATUS_ARCH_MISMATCH";
    case CUBLAS_STATUS_MAPPING_ERROR:
      return "CUBLAS_STATUS_MAPPING_ERROR";
    case CUBLAS_STATUS_EXECUTION_FAILED:
      return "CUBLAS_STATUS_EXECUTION_FAILED";
    case CUBLAS_STATUS_INTERNAL_ERROR:
      return "CUBLAS_STATUS_INTERNAL_ERROR";
    case CUBLAS_STATUS_NOT_SUPPORTED:
      return "CUBLAS_STATUS_NOT_SUPPORTED";
    case CUBLAS_STATUS_LICENSE_ERROR:
      return "CUBLAS_STATUS_LICENSE_ERROR";
    default:
      return "<unknown>";
  }
}

static const char* CudaDataTypeToString(cudaDataType_t dt) {
  switch (dt) {
    case CUDA_R_16F:
      return "CUDA_R_16F";
    case CUDA_R_16BF:
      return "CUDA_R_16BF";
    case CUDA_R_32F:
      return "CUDA_R_32F";
#if (CUDA_VERSION >= 11080)
    case CUDA_R_8F_E4M3:
      return "CUDA_R_8F_E4M3";
    case CUDA_R_8F_E5M2:
      return "CUDA_R_8F_E5M2";
#endif
    default:
      return "<unknown>";
  }
}

static const char* CublasComputeTypeToString(cublasComputeType_t ct) {
  switch (ct) {
    case CUBLAS_COMPUTE_16F:
      return "CUBLAS_COMPUTE_16F";
    case CUBLAS_COMPUTE_32F:
      return "CUBLAS_COMPUTE_32F";
    case CUBLAS_COMPUTE_32F_FAST_16F:
      return "CUBLAS_COMPUTE_32F_FAST_16F";
    case CUBLAS_COMPUTE_32F_FAST_16BF:
      return "CUBLAS_COMPUTE_32F_FAST_16BF";
    case CUBLAS_COMPUTE_32F_FAST_TF32:
      return "CUBLAS_COMPUTE_32F_FAST_TF32";
    case CUBLAS_COMPUTE_64F:
      return "CUBLAS_COMPUTE_64F";
    default:
      return "<unknown>";
  }
}

// It must exist somewhere already.
cudaDataType_t ToCudaDataType(int32_t element_type) {
  switch (element_type) {
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT:
      return CUDA_R_32F;
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT16:
      return CUDA_R_16F;
    case ONNX_NAMESPACE::TensorProto_DataType_BFLOAT16:
      return CUDA_R_16BF;
#if (!defined(DISABLE_FLOAT8_TYPES) && (CUDA_VERSION >= 11080))
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT8E4M3FN:
      return CUDA_R_8F_E4M3;
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT8E5M2:
      return CUDA_R_8F_E5M2;
#endif
    default:
      ORT_THROW("Unexpected element_type=", element_type, ".");
  }
}

// It must exist somewhere already.
int32_t TypeSize(int32_t element_type) {
  switch (element_type) {
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT:
      return 4;
    case ONNX_NAMESPACE::TensorProto_DataType_BFLOAT16:
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT16:
      return 2;
#if (!defined(DISABLE_FLOAT8_TYPES) && (CUDA_VERSION >= 11080))
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT8E4M3FN:
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT8E5M2:
      return 1;
#endif
    default:
      ORT_THROW("Unexpected element_type=", element_type, ".");
  }
}

void GemmFloat8::set(const TensorShape& a_shape, const TensorShape& b_shape, int& M, int& N, int& K, int& lda, int& ldb, int& ldd, bool row_major) const {
  constexpr int ir = 0;
  constexpr int ic = 1 - ir;
  if (transA_ && !transB_) {  // TN
    M = a_shape[ic];
    N = b_shape[ic];
    K = a_shape[ir];
    lda = a_shape[row_major ? ic : ir];
    ldb = b_shape[row_major ? ic : ir];
    ldd = b_shape[row_major ? ic : ir];
  } else if (!transA_ && !transB_) {  // NN
    M = a_shape[ir];
    N = b_shape[ic];
    K = a_shape[ic];
    lda = a_shape[row_major ? ic : ir];
    ldb = b_shape[row_major ? ic : ir];
    ldd = b_shape[row_major ? ic : ir];
  } else if (!transA_ && transB_) {  // NT
    M = a_shape[ir];
    N = b_shape[ir];
    K = a_shape[ic];
    lda = a_shape[row_major ? ic : ir];
    ldb = b_shape[row_major ? ic : ir];
    ldd = b_shape[row_major ? ir : ic];
  } else {  // TT
    M = a_shape[ic];
    N = b_shape[ir];
    K = a_shape[ir];
    lda = a_shape[row_major ? ir : ic];
    ldb = b_shape[row_major ? ir : ic];
    ldd = b_shape[row_major ? ic : ir];
  }
}

template <typename TValue>
int32_t GetTypeAndShape(const TValue* input,
                        TensorShape& shape,
                        bool swap = false) {
  shape = input->Shape();
  ORT_ENFORCE(shape.NumDimensions() == 2);
  if (swap) {
    std::swap(shape[0], shape[1]);
  }
  return input->GetElementType();
}

Status GemmFloat8::ComputeInternal(OpKernelContext* ctx) const {
  const Tensor* input_A = nullptr;
  const Tensor* input_B = nullptr;
  const Tensor* input_C = nullptr;
  const Tensor* scale_A = nullptr;
  const Tensor* scale_B = nullptr;
  const Tensor* scale_Y = nullptr;
  bool has_scales = false;
  bool has_bias = false;
  int n_inputs = ctx->InputCount();
  ORT_ENFORCE(n_inputs >= 2, "A and/or B is missing.");
  input_A = ctx->Input<Tensor>(0);
  input_B = ctx->Input<Tensor>(1);
  if (n_inputs == 3) {
    input_C = ctx->Input<Tensor>(2);
    has_bias = true;
  } else if (n_inputs > 3) {
    ORT_ENFORCE(n_inputs == 6, "Unexpected number of inputs=", n_inputs, ".");
    has_scales = true;
    scale_A = ctx->Input<Tensor>(3);
    scale_B = ctx->Input<Tensor>(4);
    scale_Y = ctx->Input<Tensor>(5);
    ORT_ENFORCE(scale_A->GetElementType() == ONNX_NAMESPACE::TensorProto_DataType_FLOAT);
    ORT_ENFORCE(scale_B->GetElementType() == ONNX_NAMESPACE::TensorProto_DataType_FLOAT);
    ORT_ENFORCE(scale_Y->GetElementType() == ONNX_NAMESPACE::TensorProto_DataType_FLOAT);
    if (ctx->Input<Tensor>(2) != nullptr) {
      input_C = ctx->Input<Tensor>(2);
      has_bias = true;
      ORT_ENFORCE(input_C->GetElementType() == dtype_, "Bias type must be equal to dtype.");
    }
  }

  if (row_major_)
    return ComputeColMajor(ctx, n_inputs, has_bias, has_scales, input_A, input_B,
                           input_C, scale_A, scale_B, scale_Y);
  return ComputeRowMajor(ctx, n_inputs, has_bias, has_scales, input_A, input_B,
                         input_C, scale_A, scale_B, scale_Y);
}

Status GemmFloat8::ComputeRowMajor(
    OpKernelContext* ctx, int n_inputs, bool has_bias, bool has_scales,
    const Tensor* input_A, const Tensor* input_B,
    const Tensor* input_C, const Tensor* scale_A,
    const Tensor* scale_B, const Tensor* scale_Y) const {
  TensorShape shape_A, shape_B, shape_C, shape_Y;
  int32_t dtype_A, dtype_B, dtype_C, dtype_Y;
  dtype_A = GetTypeAndShape(input_A, shape_A);
  dtype_B = GetTypeAndShape(input_B, shape_B);

  int M, N, K, lda, ldb, ldd;
  set(shape_A, shape_B, M, N, K, lda, ldb, ldd, true);

  TensorShape dimensions{M, N};
  Tensor* Y = ctx->Output(0, dimensions);
  dtype_Y = GetTypeAndShape(Y, shape_Y);
  dtype_C = has_bias ? GetTypeAndShape(input_C, shape_C)
                     : ONNX_NAMESPACE::TensorProto_DataType_FLOAT;
  return ComputeGemm(ctx, n_inputs, has_bias, has_scales, dtype_A, dtype_B, dtype_C,
                     dtype_Y, shape_A, shape_B, shape_C, shape_Y, transA_, transB_,
                     input_A->DataRaw(), input_B->DataRaw(),
                     has_bias ? input_C->DataRaw() : nullptr,
                     has_scales ? scale_A->DataRaw() : nullptr,
                     has_scales ? scale_B->DataRaw() : nullptr,
                     has_scales ? scale_Y->DataRaw() : nullptr,
                     Y->MutableDataRaw(), M, N, K, lda, ldb, ldd);
}

Status GemmFloat8::ComputeColMajor(
    OpKernelContext* ctx, int n_inputs, bool has_bias, bool has_scales,
    const Tensor* input_A, const Tensor* input_B,
    const Tensor* input_C, const Tensor* scale_A,
    const Tensor* scale_B, const Tensor* scale_Y) const {
  TensorShape shape_A, shape_B, shape_C, shape_Y;
  int32_t dtype_A, dtype_B, dtype_C, dtype_Y;
  dtype_A = GetTypeAndShape(input_A, shape_A);
  dtype_B = GetTypeAndShape(input_B, shape_B);

  int M, N, K, lda, ldb, ldd;
  set(shape_A, shape_B, M, N, K, lda, ldb, ldd, true);

  std::swap(shape_A[0], shape_A[1]);
  std::swap(shape_B[0], shape_B[1]);

  TensorShape dimensions{M, N};
  Tensor* Y = ctx->Output(0, dimensions);
  dtype_Y = GetTypeAndShape(Y, shape_Y);
  dtype_C = has_bias ? GetTypeAndShape(input_C, shape_C, true)
                     : ONNX_NAMESPACE::TensorProto_DataType_FLOAT;

  return ComputeGemm(ctx, n_inputs, has_bias, has_scales, dtype_B, dtype_A, dtype_C,
                     dtype_Y, shape_B, shape_A, shape_C, shape_Y, transB_, transA_,
                     input_B->DataRaw(), input_A->DataRaw(),
                     has_bias ? input_C->DataRaw() : nullptr,
                     has_scales ? scale_B->DataRaw() : nullptr,
                     has_scales ? scale_A->DataRaw() : nullptr,
                     has_scales ? scale_Y->DataRaw() : nullptr,
                     Y->MutableDataRaw(), N, M, K, ldb, lda, ldd);
}

Status GemmFloat8::ComputeGemm(
    OpKernelContext* ctx, int n_inputs, bool has_bias, bool has_scales,
    int32_t dtype_A, int32_t dtype_B,
    int32_t dtype_C, int32_t dtype_Y,
    const TensorShape& shape_A, const TensorShape& shape_B,
    const TensorShape& shape_C, const TensorShape& shape_Y,
    bool trans_A, bool trans_B, const void* p_input_a, const void* p_input_b,
    const void* p_input_c, const void* p_scale_a, const void* p_scale_b,
    const void* p_scale_y, void* p_output_y, int M, int N, int K, int lda,
    int ldb, int ldd) const {
  cudaStream_t stream = Stream(ctx);
  CUDA_RETURN_IF_ERROR(cudaStreamSynchronize(stream));

  cublasLtHandle_t cublasLt;
  CUBLAS_RETURN_IF_ERROR(cublasLtCreate(&cublasLt));

  cublasLtMatmulDesc_t operationDesc = nullptr;
  cublasLtMatrixLayout_t Adesc = nullptr, Bdesc = nullptr, Cdesc = nullptr,
                         Ddesc = nullptr;

  // Create matrix descriptors. Not setting any extra attributes.
  cudaDataType_t a_cuda_type = ToCudaDataType(dtype_A);
  cudaDataType_t b_cuda_type = ToCudaDataType(dtype_B);
  cudaDataType_t d_cuda_type = ToCudaDataType(dtype_Y);
  cudaDataType_t scale_cuda_type =
      ToCudaDataType(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
  cudaDataType_t bias_cuda_type = ToCudaDataType(dtype_C);

  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutCreate(
      &Adesc, a_cuda_type, trans_A ? K : M, trans_A ? M : K, lda));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutCreate(
      &Bdesc, b_cuda_type, trans_B ? N : K, trans_B ? K : N, ldb));
  CUBLAS_RETURN_IF_ERROR(
      cublasLtMatrixLayoutCreate(&Ddesc, d_cuda_type, M, N, ldd));

  if (row_major_) {
    cublasLtOrder_t matrixOrder = CUBLASLT_ORDER_ROW;
    CUBLAS_RETURN_IF_ERROR(
        cublasLtMatrixLayoutSetAttribute(Adesc, CUBLASLT_MATRIX_LAYOUT_ORDER,
                                         &matrixOrder, sizeof(matrixOrder)));
    CUBLAS_RETURN_IF_ERROR(
        cublasLtMatrixLayoutSetAttribute(Bdesc, CUBLASLT_MATRIX_LAYOUT_ORDER,
                                         &matrixOrder, sizeof(matrixOrder)));
  }

  CUBLAS_RETURN_IF_ERROR(
      cublasLtMatmulDescCreate(&operationDesc, compute_type_, scale_cuda_type));
  cublasOperation_t ctransa = trans_A ? CUBLAS_OP_T : CUBLAS_OP_N;
  cublasOperation_t ctransb = trans_B ? CUBLAS_OP_T : CUBLAS_OP_N;
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(
      operationDesc, CUBLASLT_MATMUL_DESC_TRANSA, &ctransa, sizeof(ctransa)));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(
      operationDesc, CUBLASLT_MATMUL_DESC_TRANSB, &ctransb, sizeof(ctransb)));

  if (sm_count_ != 0) {
    int math_sm_count = static_cast<int>(sm_count_);
    CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(
        operationDesc, CUBLASLT_MATMUL_DESC_SM_COUNT_TARGET, &math_sm_count,
        sizeof(math_sm_count)));
  }

  if (has_scales) {
    // gemm float 8
    const int8_t ifast_accumulation_mode = fast_accumulation_mode_ ? 1 : 0;
    CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(
        operationDesc,
        cublasLtMatmulDescAttributes_t::CUBLASLT_MATMUL_DESC_FAST_ACCUM,
        &ifast_accumulation_mode, sizeof(ifast_accumulation_mode)));
    CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(
        operationDesc, CUBLASLT_MATMUL_DESC_A_SCALE_POINTER, &p_scale_a,
        sizeof(p_scale_a)));
    CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(
        operationDesc, CUBLASLT_MATMUL_DESC_B_SCALE_POINTER, &p_scale_b,
        sizeof(p_scale_b)));
    CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(
        operationDesc, CUBLASLT_MATMUL_DESC_D_SCALE_POINTER, &p_scale_y,
        sizeof(p_scale_b)));

    // float 8
#if CUDA_VERSION >= 11080
    if (dtype_Y == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E4M3FN ||
        dtype_Y == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E5M2) {
      // For FP8 output, cuBLAS requires C_type to be same as bias_type
      CUBLAS_RETURN_IF_ERROR(
          cublasLtMatrixLayoutCreate(&Cdesc, bias_cuda_type, M, N, ldd));
      CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(
          operationDesc, CUBLASLT_MATMUL_DESC_BIAS_DATA_TYPE, &bias_cuda_type,
          sizeof(bias_cuda_type)));
    } else {
      CUBLAS_RETURN_IF_ERROR(
          cublasLtMatrixLayoutCreate(&Cdesc, d_cuda_type, M, N, ldd));
    }
  } else {
    CUBLAS_RETURN_IF_ERROR(
        cublasLtMatrixLayoutCreate(&Cdesc, d_cuda_type, M, N, ldd));
  }
#else
    // An output is still needed but it is not initialized.
    CUBLAS_RETURN_IF_ERROR(
        cublasLtMatrixLayoutCreate(&Cdesc, d_cuda_type, M, N, ldd));
#endif

  if (row_major_) {
    cublasLtOrder_t matrixOrder = CUBLASLT_ORDER_ROW;
    CUBLAS_RETURN_IF_ERROR(
        cublasLtMatrixLayoutSetAttribute(Cdesc, CUBLASLT_MATRIX_LAYOUT_ORDER,
                                         &matrixOrder, sizeof(matrixOrder)));
    CUBLAS_RETURN_IF_ERROR(
        cublasLtMatrixLayoutSetAttribute(Ddesc, CUBLASLT_MATRIX_LAYOUT_ORDER,
                                         &matrixOrder, sizeof(matrixOrder)));
  }

  cublasLtEpilogue_t epilogue = CUBLASLT_EPILOGUE_DEFAULT;
  cublasLtMatmulDescSetAttribute(operationDesc, CUBLASLT_MATMUL_DESC_EPILOGUE,
                                 &epilogue, sizeof(epilogue));

  // See
  // https://docs.nvidia.com/cuda/cublas/index.html?highlight=cublasLtMatmulPreferenceAttributes_t#cublasltmatmulpreferenceattributes-t
  // The workspace should be allocated once from OpKernelContext assuming
  // only one cuda function is running at a time (which is not necessarily true
  // with H100).
  size_t workspaceSize = (size_t)(1 << 25);  // suggested fixed value 32Mb
  cublasLtMatmulPreference_t preference = nullptr;
  cublasLtMatmulPreferenceCreate(&preference);
  cublasLtMatmulPreferenceSetAttribute(preference,
                                       CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES,
                                       &workspaceSize, sizeof(workspaceSize));

  // https://docs.nvidia.com/cuda/cublas/index.html?highlight=cublasLtMatmulAlgoGetHeuristic#cublasltmatmulalgogetheuristic
  cublasLtMatmulHeuristicResult_t heuristicResult = {};
  int returnedResults = 0;
  cublasStatus_t cuda_status = cublasLtMatmulAlgoGetHeuristic(
      cublasLt, operationDesc, Adesc, Bdesc, Cdesc, Ddesc, preference, 1,
      &heuristicResult, &returnedResults);
  ORT_ENFORCE(
      returnedResults > 0 && cuda_status == CUBLAS_STATUS_SUCCESS,
      " Unable to find any suitable algorithm due to ",
      cublasGetErrorEnum(cuda_status), ", returnedResults=", returnedResults,
      ", alpha=", alpha_, ", beta=", beta_, ", n_inputs=", n_inputs,
      ", A_type=", CudaDataTypeToString(a_cuda_type),
      ", B_type=", CudaDataTypeToString(b_cuda_type),
      ", C_type=", CudaDataTypeToString(bias_cuda_type),
      ", result_type=", CudaDataTypeToString(d_cuda_type),
      ", bias_type=", CudaDataTypeToString(bias_cuda_type),
      ", scale_type=", CudaDataTypeToString(scale_cuda_type),
      ", computeType=", CublasComputeTypeToString(compute_type_),
      ", epilogue=", epilogue, ", smCount=", sm_count_, ", transA=", trans_A,
      ", transB=", trans_B,
      ", fastAccumulationMode=", (fast_accumulation_mode_ ? 1 : 0),
      ", shape_A=", shape_A[0], "x", shape_A[1], ", shape_B=", shape_B[0], "x",
      shape_B[1], ", shape_C=", (shape_C.NumDimensions() > 0 ? shape_C[0] : 0), "x",
      (shape_C.NumDimensions() > 1 ? shape_C[1] : 0), ", M=", M, ", N=", N, ", K=", K,
      ", lda=", lda, ", ldb=", ldb, ", ldd=", ldd,
      ", workspaceSize=", workspaceSize, ", rowMajor=", (row_major_ ? 1 : 0),
      ". Check NVIDIA documentation to see what combination is valid: ",
      "https://docs.nvidia.com/cuda/cublas/"
      "index.html?highlight=cublasLtMatmulAlgoGetHeuristic#"
      "cublasltmatmulalgogetheuristic.");

  void* workspace = nullptr;
  if (workspaceSize > 0) {
    CUDA_RETURN_IF_ERROR(cudaMalloc((void**)&workspace, workspaceSize));
  }
  // https://docs.nvidia.com/cuda/cublas/index.html?highlight=cublasLtMatmul#cublasltmatmul
  const void* bias = has_bias ? p_input_c : p_output_y;
  cuda_status = cublasLtMatmul(
      cublasLt, operationDesc, static_cast<const void*>(&alpha_), /* alpha */
      p_input_a,                                                  /* A */
      Adesc, p_input_b,                                           /* B */
      Bdesc, static_cast<const void*>(&beta_),                    /* beta */
      bias,                                                       /* C */
      Cdesc, p_output_y,                                          /* Y */
      Ddesc, &heuristicResult.algo,                               /* algo */
      workspace,                                                  /* workspace */
      workspaceSize, stream);                                     /* stream */
  ORT_ENFORCE(
      cuda_status == CUBLAS_STATUS_SUCCESS,
      " Unable to run cublasLtMatmul due to ", cublasGetErrorEnum(cuda_status),
      ", returnedResults=", returnedResults, ", alpha=", alpha_,
      ", n_inputs=", n_inputs, ", A_type=", CudaDataTypeToString(a_cuda_type),
      ", B_type=", CudaDataTypeToString(b_cuda_type),
      ", result_type=", CudaDataTypeToString(d_cuda_type),
      ", bias_type=", CudaDataTypeToString(bias_cuda_type),
      ", scale_type=", CudaDataTypeToString(scale_cuda_type),
      ", computeType=", CublasComputeTypeToString(compute_type_),
      ", epilogue=", epilogue, ", smCount=", sm_count_, ", transA=", trans_A,
      ", transB=", trans_B,
      ", fastAccumulationMode=", (fast_accumulation_mode_ ? 1 : 0),
      ", shape_A=", shape_A[0], "x", shape_A[1], ", shape_B=", shape_B[0], "x",
      shape_B[1], ", M=", M, ", N=", N, ", K=", K, ", lda=", lda, ", ldb=", ldb,
      ", ldd=", ldd, ", workspaceSize=", workspaceSize,
      ", rowMajor=", (row_major_ ? 1 : 0), ".");

  if (workspaceSize > 0) {
    CUDA_RETURN_IF_ERROR(cudaFree(workspace));
  }

  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulPreferenceDestroy(preference));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutDestroy(Ddesc));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutDestroy(Cdesc));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutDestroy(Bdesc));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutDestroy(Adesc));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescDestroy(operationDesc));
  CUBLAS_RETURN_IF_ERROR(cublasLtDestroy(cublasLt));
  return Status::OK();
}

}  // namespace cuda
}  // namespace contrib
}  // namespace onnxruntime
