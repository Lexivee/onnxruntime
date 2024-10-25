// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#pragma once

#include <functional>
#include <numeric>
#include <string>
#include <type_traits>
#include <vector>
#include <unordered_set>

#include "gsl/span"
#include "QnnTypes.h"
#include "core/framework/node_unit.h"
#include "core/util/qmath.h"
#include "core/providers/qnn/builder/qnn_model_wrapper.h"

namespace onnxruntime {
namespace qnn {
class QnnOpConfigWrapper;

namespace utils {
size_t GetElementSizeByType(const Qnn_DataType_t& data_type);

size_t GetElementSizeByType(ONNXTensorElementDataType elem_type);

Status GetQnnDataType(const bool is_quantized_tensor, const ONNX_NAMESPACE::TypeProto* type_proto,
                      Qnn_DataType_t& tensor_data_type);

const std::string& GetNodeName(const NodeUnit& node_unit);

bool OnnxDataTypeToQnnDataType(const int32_t data_type, Qnn_DataType_t& qnn_data_type, bool is_quantized = false);

inline Status GetOnnxTensorElemDataType(const NodeArg& node_arg, /*out*/ int32_t& onnx_data_type) {
  auto type_proto = node_arg.TypeAsProto();
  ORT_RETURN_IF_NOT(type_proto != nullptr && type_proto->has_tensor_type() && type_proto->tensor_type().has_elem_type(),
                    "NodeArg must have a tensor TypeProto");
  onnx_data_type = type_proto->tensor_type().elem_type();
  return Status::OK();
}

template <typename IntType>
static Status InvertPerm(gsl::span<const IntType> perm, /*out*/ gsl::span<IntType> perm_inv) {
  static_assert(std::is_integral<IntType>::value, "permutation arrays must contain integer elements");

  size_t rank = perm.size();
  ORT_RETURN_IF_NOT(perm_inv.size() == rank, "perm.size() != perm_inv.size()");

  for (size_t i = 0; i < rank; ++i) {
    size_t j = static_cast<size_t>(perm[i]);
    ORT_RETURN_IF_NOT(j < rank, "perm element out of range [0, rank - 1]");
    perm_inv[j] = static_cast<IntType>(i);
  }

  return Status::OK();
}

// Utility function that checks if an array of strings contains a specific string.
// Used to validate ONNX operator attributes.
template <size_t N>
static bool ArrayHasString(const std::array<std::string_view, N>& strings, std::string_view str) {
  for (auto s : strings) {
    if (s == str) {
      return true;
    }
  }

  return false;
}

std::pair<float, float> CheckMinMax(float rmin, float rmax);

template <typename T>
Status GetQminQmax(const Qnn_DataType_t qnn_data_type, T& qmin, T& qmax);

template <typename T>
inline T Saturate(const T qmax,
                  const T qmin,
                  const T quant_value) {
  if (quant_value > qmax) {
    return qmax;
  } else if (quant_value < qmin) {
    return qmin;
  } else {
    return quant_value;
  }
}

Status GetQuantParams(float rmin,
                      float rmax,
                      const Qnn_DataType_t qnn_data_type,
                      float& scale,
                      int32_t& zero_point,
                      bool symmetric = false);

double Dequantize(int32_t offset, float scale, const double quant_value);

Status Quantize(const double double_value,
                const float scale,
                const int32_t zero_point,
                const Qnn_DataType_t qnn_data_type,
                int& quant_value);

// Shape and transpose related functions.
// NCHW shape to channel last
Status NchwShapeToNhwc(const std::vector<uint32_t>& nchw_shape, std::vector<uint32_t>& nhwc_shape);

// NCHW shape to HWCN shape, required for Conv weight
Status NchwShapeToHwcn(const std::vector<uint32_t>& nchw_shape, std::vector<uint32_t>& hwcn_shape);

// CNHW shape to HWCN shape, required for Conv weight
Status CnhwShapeToHwcn(const std::vector<uint32_t>& cnhw_shape, std::vector<uint32_t>& hwcn_shape);

Status TransposeFromNchwToHwcn(const QnnModelWrapper& qnn_model_wrapper, const onnx::TensorProto& initializer,
                               std::vector<uint8_t>& transposed_data, bool is_3d = false);

Status TransposeFromCnhwToHwcn(const QnnModelWrapper& qnn_model_wrapper, const onnx::TensorProto& initializer,
                               std::vector<uint8_t>& transposed_data, bool is_3d = false);

Status TwoDimensionTranspose(const QnnModelWrapper& qnn_model_wrapper, std::vector<uint32_t>& data_shape,
                             const onnx::TensorProto& initializer, std::vector<uint8_t>& transposed_data);

}  // namespace utils
}  // namespace qnn
}  // namespace onnxruntime
