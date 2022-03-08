
#include "qorder_common.h"

// #if defined(CUDA_VERSION) && CUDA_VERSION >= 11000

#include <numeric>
#include <functional>
#include <sstream>
#include "gsl/gsl"

#include "core/providers/cuda/tensor/quantize_linear.cuh"

namespace onnxruntime {
namespace contrib {
namespace cuda {

using namespace onnxruntime::cuda;

ONNX_OPERATOR_KERNEL_EX(
    QuantizeWithOrder,
    kMSDomain,
    1,
    kCudaExecutionProvider,
    (*KernelDefBuilder::Create())
        .TypeConstraint("Q", DataTypeImpl::GetTensorType<int8_t>())
        .TypeConstraint("F", BuildKernelDefConstraints<float, MLFloat16>()),
    QuantizeWithOrder);

ONNX_OPERATOR_KERNEL_EX(
    DequantizeWithOrder,
    kMSDomain,
    1,
    kCudaExecutionProvider,
    (*KernelDefBuilder::Create())
        .TypeConstraint("F", BuildKernelDefConstraints<float, MLFloat16>())
        .TypeConstraint("Q", DataTypeImpl::GetTensorType<int8_t>()),
    DequantizeWithOrder);

ONNX_OPERATOR_KERNEL_EX(
    QOrderedMatMul,
    kMSDomain,
    1,
    kCudaExecutionProvider,
    (*KernelDefBuilder::Create())
        .TypeConstraint("F", BuildKernelDefConstraints<float>())
        .TypeConstraint("Q", DataTypeImpl::GetTensorType<int8_t>())
        .InputMemoryType(OrtMemTypeCPUInput, 1)
        .InputMemoryType(OrtMemTypeCPUInput, 3)
        .InputMemoryType(OrtMemTypeCPUInput, 4),
    QOrderedMatMul);

cublasLtOrder_t GetCublasLtOrderAttr(const OpKernelInfo& info, const char* order_attr) {
  int64_t order_value;
  Status status = info.GetAttr(order_attr, &order_value);
  ORT_ENFORCE(status.IsOK(), "Attribute ", order_attr, " is not set.");
  return gsl::narrow_cast<cublasLtOrder_t>(order_value);
}

int64_t CalcLeadingDimensionLt(int64_t rows, int64_t cols, cublasLtOrder_t order) {
  switch (order) {
    case CUBLASLT_ORDER_ROW:
      return cols;
    case CUBLASLT_ORDER_COL:
      return rows;
    case CUBLASLT_ORDER_COL32:
      return 32 * rows;
    case CUBLASLT_ORDER_COL4_4R2_8C:
      return 32 * ((rows + 8 - 1) / 8) * 8;
    case CUBLASLT_ORDER_COL32_2R_4R4:
      return 32 * ((rows + 32 - 1) / 32) * 32;
    default:
      return 0;
  }
}

void UpdateTileRequire(cublasLtOrder_t order, int64_t& row_tile, int64_t& col_tile) {
  switch (order) {
    case CUBLASLT_ORDER_ROW:
    case CUBLASLT_ORDER_COL:
      break;
    case CUBLASLT_ORDER_COL32:
      col_tile = std::max(col_tile, int64_t{32});
      break;
    case CUBLASLT_ORDER_COL4_4R2_8C:
      col_tile = std::max(col_tile, int64_t{32});
      row_tile = std::max(row_tile, int64_t{8});
      break;
    case CUBLASLT_ORDER_COL32_2R_4R4:
      col_tile = std::max(col_tile, int64_t{32});
      row_tile = std::max(row_tile, int64_t{32});
      break;
  }
}

static void cublasLtMatMulInt8SetupAlgo(cublasLtHandle_t cublasLt_handle, cublasLtMatmulAlgo_t& algo, int algoId, int swizzle,
                                        int customOption, int tile, int splitK_val, int reductionScheme, int stages) {
  cublasLtMatmulAlgoInit(cublasLt_handle, CUBLAS_COMPUTE_32I, CUDA_R_32F, CUDA_R_8I, CUDA_R_8I, CUDA_R_8I, CUDA_R_8I, algoId, &algo);
  cublasLtMatmulAlgoConfigSetAttribute(&algo, CUBLASLT_ALGO_CONFIG_CUSTOM_OPTION, &(customOption), sizeof(customOption));
  cublasLtMatmulAlgoConfigSetAttribute(&algo, CUBLASLT_ALGO_CONFIG_TILE_ID, &(tile), sizeof(tile));
  cublasLtMatmulAlgoConfigSetAttribute(&algo, CUBLASLT_ALGO_CONFIG_SPLITK_NUM, &(splitK_val), sizeof(splitK_val));
  cublasLtMatmulAlgoConfigSetAttribute(&algo, CUBLASLT_ALGO_CONFIG_CTA_SWIZZLING, &(swizzle), sizeof(swizzle));
  cublasLtMatmulAlgoConfigSetAttribute(&algo, CUBLASLT_ALGO_CONFIG_REDUCTION_SCHEME, &(reductionScheme), sizeof(int));
  cublasLtMatmulAlgoConfigSetAttribute(&algo, CUBLASLT_ALGO_CONFIG_STAGES_ID, &(stages), sizeof(stages));
}

static inline std::string AlgoKey(const cudaDeviceProp& /*device_prop*/,
                                  int batch_count, int m, int n, int k,
                                  cublasLtOrder_t order_weight, cublasLtOrder_t input_output_order) {
  std::stringstream ss;
  ss << batch_count << "-" << m << "_" << n << "_" << k << "-" << (int)order_weight << "-" << (int)input_output_order;
  return ss.str();
}

CublasLtMMAlgoMap& CublasLtMMAlgoMap::instance() {
  static CublasLtMMAlgoMap instance;
  return instance;
}

CublasLtMMAlgoMap::CublasLtMMAlgoMap() {
  // TODO: Load config file
  return;
}

void CublasLtMMAlgoMap::GetAlgo(cublasLtHandle_t cublasLt_handle, cublasLtMatmulAlgo_t& algo, const cudaDeviceProp& device_prop,
                                int batch_count, int m, int n, int k,
                                cublasLtOrder_t order_weight, cublasLtOrder_t input_output_order) const {
  std::string mark = AlgoKey(device_prop, batch_count, m, n, k, order_weight, input_output_order);
  auto algo_it = best_algos_.find(mark);
  if (algo_it != best_algos_.end() && algo_it->second.workspaceSize == 0) {
    const auto& algo_info = algo_it->second;
    cublasLtMatMulInt8SetupAlgo(cublasLt_handle, algo, algo_info.algoId, algo_info.swizzle, algo_info.customOption,
                                algo_info.tile, algo_info.splitK_val, algo_info.reductionScheme, algo_info.stages);
  } else {
    int algoId = (order_weight == CUBLASLT_ORDER_COL4_4R2_8C) ? 6 : 7 /* CUBLASLT_ORDER_COL32_2R_4R4 */;
    int stages = (order_weight == CUBLASLT_ORDER_COL4_4R2_8C) ? 13 : 15 /* CUBLASLT_ORDER_COL32_2R_4R4 */;
    cublasLtMatMulInt8SetupAlgo(cublasLt_handle, algo, algoId, 0, 0, 20, 0, 0, stages);
  }
}

static Status CreateLtMatrixLayout(cublasLtMatrixLayout_t& layoutDesc,
                                   int const batchCount, int64_t const rowsAfterOp, int64_t const colsAfterOp,
                                   cudaDataType_t const matType, cublasLtOrder_t const matOrder, cublasOperation_t const matTrans) {
  if (matTrans == CUBLAS_OP_T) {
    CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutCreate(&layoutDesc, matType, colsAfterOp, rowsAfterOp, CalcLeadingDimensionLt(colsAfterOp, rowsAfterOp, matOrder)));
  } else {
    CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutCreate(&layoutDesc, matType, rowsAfterOp, colsAfterOp, CalcLeadingDimensionLt(rowsAfterOp, colsAfterOp, matOrder)));
  }
  int64_t strideBatch = rowsAfterOp * colsAfterOp;
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutSetAttribute(layoutDesc, CUBLASLT_MATRIX_LAYOUT_ORDER, &matOrder, sizeof(matOrder)));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutSetAttribute(layoutDesc, CUBLASLT_MATRIX_LAYOUT_BATCH_COUNT, &batchCount, sizeof(batchCount)));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutSetAttribute(layoutDesc, CUBLASLT_MATRIX_LAYOUT_STRIDED_BATCH_OFFSET, &strideBatch, sizeof(strideBatch)));
  return Status::OK();
}

Status QOrdered_MatMul(cublasLtHandle_t cublasLt_handle, cudaStream_t stream, [[maybe_unused]] const cudaDeviceProp& device_prop,
                       int32_t batchCount, int64_t m, int64_t n, int64_t k,
                       const float* scale, const int8_t* A, const int8_t* B, int8_t* C,
                       cublasLtOrder_t order_weight) {
  cublasLtMatmulDesc_t matmul_desc = nullptr;
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescCreate(&matmul_desc, CUBLAS_COMPUTE_32I, CUDA_R_32F));
  auto clean_matmul_desc = gsl::finally([&matmul_desc]() {if (matmul_desc) cublasLtMatmulDescDestroy(matmul_desc); });
  const cublasOperation_t transpose_A = CUBLAS_OP_N;
  const cublasOperation_t transpose_B = CUBLAS_OP_T;
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(matmul_desc, CUBLASLT_MATMUL_DESC_TRANSA, &transpose_A, sizeof(transpose_A)));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(matmul_desc, CUBLASLT_MATMUL_DESC_TRANSB, &transpose_B, sizeof(transpose_B)));
  cublasLtPointerMode_t const pointMode = CUBLASLT_POINTER_MODE_HOST;
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(matmul_desc, CUBLASLT_MATMUL_DESC_POINTER_MODE, &pointMode, sizeof(pointMode)));

  cublasLtMatrixLayout_t desc_A = nullptr;
  ORT_RETURN_IF_ERROR(CreateLtMatrixLayout(desc_A, batchCount, m, k, CUDA_R_8I, CUBLASLT_ORDER_COL32, CUBLAS_OP_N));
  auto clean_desc_A = gsl::finally([&desc_A]() {if (desc_A) cublasLtMatrixLayoutDestroy(desc_A); });

  cublasLtMatrixLayout_t desc_B = nullptr;
  ORT_RETURN_IF_ERROR(CreateLtMatrixLayout(desc_B, batchCount, k, n, CUDA_R_8I, order_weight, CUBLAS_OP_T));
  auto clean_desc_B = gsl::finally([&desc_B]() {if (desc_B) cublasLtMatrixLayoutDestroy(desc_B); });

  cublasLtMatrixLayout_t desc_C = nullptr;
  ORT_RETURN_IF_ERROR(CreateLtMatrixLayout(desc_C, batchCount, m, n, CUDA_R_8I, CUBLASLT_ORDER_COL32, CUBLAS_OP_N));
  auto clean_desc_D = gsl::finally([&desc_C]() {if (desc_C) cublasLtMatrixLayoutDestroy(desc_C); });

  // get algo
  cublasLtMatmulAlgo_t algo;
  CublasLtMMAlgoMap::instance().GetAlgo(cublasLt_handle, algo, device_prop, batchCount, m, n, k, order_weight, CUBLASLT_ORDER_COL32);
  const float beta = 0.0f;
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmul(cublasLt_handle, matmul_desc,
                                        scale, A, desc_A, B, desc_B,
                                        &beta, C, desc_C, C, desc_C,
                                        &algo, nullptr, 0,  // algo, workspace, workspace_size
                                        stream));

  return Status::OK();
}

// Matmul descriptor must specify CUBLAS_OP_T on matrix B and CUBLAS_OP_N (default) on matrix A and C
Status QOrdered_Gemm(cublasLtHandle_t cublasLt_handle, cudaStream_t stream,
                     int32_t batchCount, int64_t m, int64_t n, int64_t k,
                     const float* alpha, const int8_t* A, const int8_t* B,
                     const float* beta, int8_t* C,
                     cublasLtOrder_t order_A, cublasLtOrder_t order_B, cublasLtOrder_t order_C,
                     const cudaDeviceProp& device_prop) {
  cublasLtMatmulDesc_t matmul_desc = nullptr;
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescCreate(&matmul_desc, CUBLAS_COMPUTE_32I, CUDA_R_32F));
  auto clean_matmul_desc = gsl::finally([&matmul_desc]() {if (matmul_desc) cublasLtMatmulDescDestroy(matmul_desc); });

  const cublasOperation_t transpose_A = CUBLAS_OP_N;
  const cublasOperation_t transpose_B = CUBLAS_OP_T;

  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(matmul_desc, CUBLASLT_MATMUL_DESC_TRANSA, &transpose_A, sizeof(transpose_A)));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(matmul_desc, CUBLASLT_MATMUL_DESC_TRANSB, &transpose_B, sizeof(transpose_B)));
  cublasLtPointerMode_t const pointMode = CUBLASLT_POINTER_MODE_HOST;
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmulDescSetAttribute(matmul_desc, CUBLASLT_MATMUL_DESC_POINTER_MODE, &pointMode, sizeof(pointMode)));

  cublasLtMatrixLayout_t desc_A = nullptr;
  ORT_RETURN_IF_ERROR(CreateLtMatrixLayout(desc_A, batchCount, m, k, CUDA_R_8I, order_A, transpose_A));
  auto clean_desc_A = gsl::finally([&desc_A]() {if (desc_A) cublasLtMatrixLayoutDestroy(desc_A); });

  cublasLtMatrixLayout_t desc_B = nullptr;
  ORT_RETURN_IF_ERROR(CreateLtMatrixLayout(desc_B, batchCount, k, n, CUDA_R_8I, order_B, transpose_B));
  auto clean_desc_B = gsl::finally([&desc_B]() {if (desc_B) cublasLtMatrixLayoutDestroy(desc_B); });

  cublasLtMatrixLayout_t desc_C = nullptr;
  ORT_RETURN_IF_ERROR(CreateLtMatrixLayout(desc_C, batchCount, m, n, CUDA_R_8I, order_C, CUBLAS_OP_N));
  auto clean_desc_D = gsl::finally([&desc_C]() {if (desc_C) cublasLtMatrixLayoutDestroy(desc_C); });

  // get algo
  cublasLtMatmulAlgo_t algo;
  assert((int)order_A == (int)order_C);
  CublasLtMMAlgoMap::instance().GetAlgo(cublasLt_handle, algo, device_prop, batchCount, m, n, k, order_B, order_A);
  CUBLAS_RETURN_IF_ERROR(cublasLtMatmul(cublasLt_handle, matmul_desc,
                                        alpha, A, desc_A, B, desc_B,
                                        beta, C, desc_C, C, desc_C,
                                        &algo, nullptr, 0,  // algo, workspace, workspace_size
                                        stream));

  return Status::OK();
}

Status Reorder(cublasLtHandle_t cublasLt, cudaStream_t stream,
               int32_t batchCount, int64_t rows, int64_t cols, cudaDataType_t data_type,
               const void* input, cublasLtOrder_t order_input, void* output, cublasLtOrder_t order_output) {
  cublasLtMatrixTransformDesc_t transform_desc = nullptr;
  auto clean_transform_desc = gsl::finally([&transform_desc]() {if (transform_desc) cublasLtMatrixTransformDescDestroy(transform_desc); });
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixTransformDescCreate(&transform_desc, CUDA_R_32I));

  cublasLtMatrixLayout_t InputLayout = nullptr;
  auto clean_InputLayout = gsl::finally([&InputLayout]() {if (InputLayout) cublasLtMatrixLayoutDestroy(InputLayout); });
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutCreate(&InputLayout, data_type, rows, cols, CalcLeadingDimensionLt(rows, cols, order_input)));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutSetAttribute(InputLayout, CUBLASLT_MATRIX_LAYOUT_ORDER, &order_input, sizeof(order_input)));

  cublasLtMatrixLayout_t OutputLayout = nullptr;
  auto clean_OutputLayout = gsl::finally([&OutputLayout]() {if (OutputLayout) cublasLtMatrixLayoutDestroy(OutputLayout); });
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutCreate(&OutputLayout, data_type, rows, cols, CalcLeadingDimensionLt(rows, cols, order_output)));
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutSetAttribute(OutputLayout, CUBLASLT_MATRIX_LAYOUT_ORDER, &order_output, sizeof(order_output)));

  if (batchCount > 1) {
    int64_t batch_stride_input = rows * cols;
    CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutSetAttribute(InputLayout, CUBLASLT_MATRIX_LAYOUT_BATCH_COUNT, &batchCount, sizeof(batchCount)));
    CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutSetAttribute(InputLayout, CUBLASLT_MATRIX_LAYOUT_STRIDED_BATCH_OFFSET, &batch_stride_input, sizeof(batch_stride_input)));
    CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutSetAttribute(OutputLayout, CUBLASLT_MATRIX_LAYOUT_BATCH_COUNT, &batchCount, sizeof(batchCount)));
    CUBLAS_RETURN_IF_ERROR(cublasLtMatrixLayoutSetAttribute(OutputLayout, CUBLASLT_MATRIX_LAYOUT_STRIDED_BATCH_OFFSET, &batch_stride_input, sizeof(batch_stride_input)));
  }

  int32_t alpha = 1;
  int32_t beta = 0;
  CUBLAS_RETURN_IF_ERROR(cublasLtMatrixTransform(cublasLt, transform_desc, &alpha, input, InputLayout,
                                                 &beta, nullptr, nullptr, output, OutputLayout, stream));

  return Status::OK();
};

static Status CheckTensorOrder(const Tensor& input_tensor, cublasLtOrder_t input_order, cublasLtOrder_t output_order,
                               int64_t& rows, int64_t& cols, int64_t& batchCount, int64_t& elementCount) {
  const auto dims = input_tensor.Shape().GetDims();
  cols = dims.back();
  rows = (dims.size() <= 1 ? 1LL : dims[dims.size() - 2]);
  batchCount = (dims.size() <= 2 ? 1LL : std::accumulate(dims.begin(), dims.begin() + (dims.size() - 2), 1LL, std::multiplies<int64_t>()));
  elementCount = cols * rows * batchCount;
  int64_t row_tile = 1, col_tile = 1;
  UpdateTileRequire(input_order, row_tile, col_tile);
  UpdateTileRequire(output_order, row_tile, col_tile);
  if (rows % row_tile != 0 || cols % col_tile != 0) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "Shape not meet clean tile requirement!", dims);
  }
  return Status::OK();
}

QuantizeWithOrder::QuantizeWithOrder(const OpKernelInfo& info) : CudaKernel(info) {
  order_input_ = GetCublasLtOrderAttr(info, "order_input");
  order_output_ = GetCublasLtOrderAttr(info, "order_output");
  ORT_ENFORCE(order_input_ == CUBLASLT_ORDER_ROW, "Only CUBLASLT_ORDER_ROW is supported for order_input");
  ORT_ENFORCE(order_output_ == CUBLASLT_ORDER_COL32 || order_output_ == CUBLASLT_ORDER_COL4_4R2_8C || order_output_ == CUBLASLT_ORDER_COL32_2R_4R4,
              "Only CUBLASLT_ORDER_COL32, CUBLASLT_ORDER_COL4_4R2_8C, CUBLASLT_ORDER_COL32_2R_4R4 are supported for order_output");
}

DequantizeWithOrder::DequantizeWithOrder(const OpKernelInfo& info) : CudaKernel(info) {
  order_input_ = GetCublasLtOrderAttr(info, "order_input");
  order_output_ = GetCublasLtOrderAttr(info, "order_output");
  ORT_ENFORCE(order_input_ == CUBLASLT_ORDER_COL32, "Only CUBLASLT_ORDER_COL32 is supported for order_input");
  ORT_ENFORCE(order_output_ == CUBLASLT_ORDER_ROW, "Only CUBLASLT_ORDER_ROW are supported for order_output");
}

QOrderedMatMul::QOrderedMatMul(const OpKernelInfo& info) : CudaKernel(info) {
  order_A_ = GetCublasLtOrderAttr(info, "order_A");
  order_B_ = GetCublasLtOrderAttr(info, "order_B");
  order_Y_ = GetCublasLtOrderAttr(info, "order_Y");
  ORT_ENFORCE(order_Y_ == CUBLASLT_ORDER_COL32 && order_A_ == CUBLASLT_ORDER_COL32, "Only CUBLASLT_ORDER_COL32 is supported for order_A and order_Y");
  ORT_ENFORCE(order_B_ == CUBLASLT_ORDER_COL4_4R2_8C || order_B_ == CUBLASLT_ORDER_COL32_2R_4R4,
              "Only CUBLASLT_ORDER_COL4_4R2_8C, CUBLASLT_ORDER_COL32_2R_4R4 are supported for order_B_");
}

Status QuantizeWithOrder::ComputeInternal(OpKernelContext* context) const {
  int64_t rows = 0, cols = 0, batch = 0, n = 0;

  const Tensor& input_tensor = *context->Input<Tensor>(0);
  ORT_RETURN_IF_ERROR(CheckTensorOrder(input_tensor, (cublasLtOrder_t)order_input_, (cublasLtOrder_t)order_output_, rows, cols, batch, n));
  const void* scale = context->Input<Tensor>(1)->DataRaw();
  Tensor* output_tensor = context->Output(0, input_tensor.Shape());
  cublasLtHandle_t cublasLt = CublasLtHandle();
  cudaStream_t stream = Stream();

  // TODO: Currently use existing quantize kernel first, may merge into one kernel if performance needed
  auto q8_buffer = GetScratchBuffer<int8_t>(order_input_ == order_output_ ? 0LL : n);
  int8_t* dst = order_input_ == order_output_ ? output_tensor->MutableData<int8_t>() : q8_buffer.get();
  if (input_tensor.IsDataType<float>()) {
    ORT_RETURN_IF_ERROR(CudaQuantizeLinear(stream, input_tensor.Data<float>(), dst, (const float*)scale, (const int8_t*)nullptr, n));
  } else {
    ORT_RETURN_IF_ERROR(CudaQuantizeLinear(stream, (const half*)input_tensor.Data<MLFloat16>(), dst, (const half*)scale, (const int8_t*)nullptr, n));
  }

  if (order_input_ != order_output_) {
    ORT_RETURN_IF_ERROR(Reorder(cublasLt, stream, gsl::narrow_cast<int>(batch), rows, cols, CUDA_R_8I,
                                q8_buffer.get(), (cublasLtOrder_t)order_input_, output_tensor->MutableDataRaw(), (cublasLtOrder_t)order_output_));
  }

  return Status::OK();
}

Status DequantizeWithOrder::ComputeInternal(OpKernelContext* context) const {
  int64_t rows = 0, cols = 0, batch = 0, n = 0;

  const Tensor& input_tensor = *context->Input<Tensor>(0);
  ORT_RETURN_IF_ERROR(CheckTensorOrder(input_tensor, (cublasLtOrder_t)order_output_, (cublasLtOrder_t)order_output_, rows, cols, batch, n));
  const Tensor& scale_tensor = *context->Input<Tensor>(1);
  const void* scale = scale_tensor.DataRaw();
  Tensor* output_tensor = context->Output(0, input_tensor.Shape());
  cublasLtHandle_t cublasLt = CublasLtHandle();
  cudaStream_t stream = Stream();

  // TODO: Currently use existing quantize kernel first, may merge into one kernel if performance needed
  const int8_t* src = input_tensor.Data<int8_t>();
  auto q8_buffer = GetScratchBuffer<int8_t>(order_input_ == order_output_ ? 0LL : n);
  if (order_input_ != order_output_) {
    src = (const int8_t*)q8_buffer.get();
    ORT_RETURN_IF_ERROR(Reorder(cublasLt, stream, gsl::narrow_cast<int>(batch), rows, cols, CUDA_R_8I,
                                input_tensor.DataRaw(), (cublasLtOrder_t)order_input_, q8_buffer.get(), (cublasLtOrder_t)order_output_));
  }
  if (scale_tensor.IsDataType<float>()) {
    ORT_RETURN_IF_ERROR(CudaDequantizeLinear(stream, src, output_tensor->MutableData<float>(), (const float*)scale, (const int8_t*)nullptr, n));
  } else {
    ORT_RETURN_IF_ERROR(CudaDequantizeLinear(stream, src, (half*)output_tensor->MutableData<MLFloat16>(), (const half*)scale, (const int8_t*)nullptr, n));
  }

  return Status::OK();
}

Status QOrderedMatMul::ComputeInternal(OpKernelContext* context) const {
  int64_t rowsA = 0, colsA = 0, batchA = 0, elementsA = 0;
  int64_t rowsB = 0, colsB = 0, batchB = 0, elementsB = 0;

  const Tensor& tensor_A = *context->Input<Tensor>(0);
  ORT_RETURN_IF_ERROR(CheckTensorOrder(tensor_A, (cublasLtOrder_t)order_A_, (cublasLtOrder_t)order_A_, rowsA, colsA, batchA, elementsA));
  const Tensor& tensor_B = *context->Input<Tensor>(2);
  ORT_RETURN_IF_ERROR(CheckTensorOrder(tensor_B, (cublasLtOrder_t)order_B_, (cublasLtOrder_t)order_B_, rowsB, colsB, batchB, elementsB));
  const float* scaleA = context->Input<Tensor>(1)->Data<float>();
  const float* scaleB = context->Input<Tensor>(3)->Data<float>();
  const float* scaleY = context->Input<Tensor>(4)->Data<float>();

  // Just handle simple case here.
  // TODO: check broadcast and correct the spae
  ORT_ENFORCE(batchA == batchB || batchB == 1, "batch count for matrix A and matrix B does not match");
  ORT_ENFORCE(colsA == rowsB, "Sahpe mis-match");
  TensorShape shapeY(tensor_A.Shape());
  shapeY[shapeY.NumDimensions() <= 1 ? size_t{0} : (shapeY.NumDimensions() - 1)] = colsB;
  Tensor* tensor_Y = context->Output(0, shapeY);

  cublasLtHandle_t cublasLt = CublasLtHandle();
  cudaStream_t stream = Stream();
  auto& device_prop = GetDeviceProp();

  const float scale = *scaleA * *scaleB / *scaleY;
  ORT_RETURN_IF_ERROR(QOrdered_MatMul(cublasLt, stream, device_prop,
                                      (int)batchA, rowsA, colsB, colsA,
                                      &scale, tensor_A.Data<int8_t>(), tensor_B.Data<int8_t>(),
                                      tensor_Y->MutableData<int8_t>(), (cublasLtOrder_t)order_B_));

  return Status::OK();
}

}  // namespace cuda
}  // namespace contrib
}  // namespace onnxruntime

// #endif
