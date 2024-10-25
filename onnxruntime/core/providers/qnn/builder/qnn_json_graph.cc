// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/qnn/builder/qnn_json_graph.h"

#include "core/framework/data_types.h"

namespace onnxruntime {
namespace qnn {

std::ostream& operator<<(std::ostream& out, const Qnn_Scalar_t& scalar) {
  switch (scalar.dataType) {
    case QNN_DATATYPE_INT_8:
      out << static_cast<int32_t>(scalar.int8Value);
      break;
    case QNN_DATATYPE_INT_16:
      out << scalar.int16Value;
      break;
    case QNN_DATATYPE_INT_32:
      out << scalar.int32Value;
      break;
    case QNN_DATATYPE_INT_64:
      out << "int64_t is not supported";
      break;
    case QNN_DATATYPE_UINT_8:
      out << static_cast<int32_t>(scalar.uint8Value);
      break;
    case QNN_DATATYPE_UINT_16:
      out << scalar.uint16Value;
      break;
    case QNN_DATATYPE_UINT_32:
      out << scalar.uint32Value;
      break;
    case QNN_DATATYPE_UINT_64:
      out << "uint64_t is not supported";
      break;
    case QNN_DATATYPE_FLOAT_16:
      break;
    case QNN_DATATYPE_FLOAT_32:
      out << scalar.floatValue;
      break;
    case QNN_DATATYPE_SFIXED_POINT_8:
    case QNN_DATATYPE_SFIXED_POINT_16:
    case QNN_DATATYPE_SFIXED_POINT_32:
    case QNN_DATATYPE_UFIXED_POINT_8:
    case QNN_DATATYPE_UFIXED_POINT_16:
    case QNN_DATATYPE_UFIXED_POINT_32:
      out << "usigned fixedpoint data is not supported";
      break;
    case QNN_DATATYPE_BOOL_8:
      out << static_cast<int32_t>(scalar.bool8Value);
      break;
    default:
      ORT_THROW("Unknown Qnn Data type");
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_DataType_t& data_type) {
  switch (data_type) {
    case QNN_DATATYPE_INT_8:
      out << "QNN_DATATYPE_INT_8";
      break;
    case QNN_DATATYPE_INT_16:
      out << "QNN_DATATYPE_INT_16";
      break;
    case QNN_DATATYPE_INT_32:
      out << "QNN_DATATYPE_INT_32";
      break;
    case QNN_DATATYPE_INT_64:
      out << "QNN_DATATYPE_INT_64";
      break;
    case QNN_DATATYPE_UINT_8:
      out << "QNN_DATATYPE_UINT_8";
      break;
    case QNN_DATATYPE_UINT_16:
      out << "QNN_DATATYPE_UINT_16";
      break;
    case QNN_DATATYPE_UINT_32:
      out << "QNN_DATATYPE_UINT_32";
      break;
    case QNN_DATATYPE_UINT_64:
      out << "QNN_DATATYPE_UINT_64";
      break;
    case QNN_DATATYPE_FLOAT_16:
      out << "QNN_DATATYPE_FLOAT_16";
      break;
    case QNN_DATATYPE_FLOAT_32:
      out << "QNN_DATATYPE_FLOAT_32";
      break;
    case QNN_DATATYPE_SFIXED_POINT_8:
      out << "QNN_DATATYPE_SFIXED_POINT_8";
      break;
    case QNN_DATATYPE_SFIXED_POINT_16:
      out << "QNN_DATATYPE_SFIXED_POINT_16";
      break;
    case QNN_DATATYPE_SFIXED_POINT_32:
      out << "QNN_DATATYPE_SFIXED_POINT_32";
      break;
    case QNN_DATATYPE_UFIXED_POINT_8:
      out << "QNN_DATATYPE_UFIXED_POINT_8";
      break;
    case QNN_DATATYPE_UFIXED_POINT_16:
      out << "QNN_DATATYPE_UFIXED_POINT_16";
      break;
    case QNN_DATATYPE_UFIXED_POINT_32:
      out << "QNN_DATATYPE_UFIXED_POINT_32";
      break;
    case QNN_DATATYPE_BOOL_8:
      out << "QNN_DATATYPE_BOOL_8";
      break;
    case QNN_DATATYPE_SFIXED_POINT_4:
      out << "QNN_DATATYPE_SFIXED_POINT_4";
      break;
    case QNN_DATATYPE_UFIXED_POINT_4:
      out << "QNN_DATATYPE_UFIXED_POINT_4";
      break;
    default:
      ORT_THROW("Unknown Qnn Data type");
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_Definition_t& definition) {
  switch (definition) {
    case QNN_DEFINITION_IMPL_GENERATED:
      out << "QNN_DEFINITION_IMPL_GENERATED";
      break;
    case QNN_DEFINITION_DEFINED:
      out << "QNN_DEFINITION_DEFINED";
      break;
    case QNN_DEFINITION_UNDEFINED:
      out << "QNN_DEFINITION_UNDEFINED";
      break;
    default:
      out << "Undefined";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_QuantizationEncoding_t& encoding) {
  switch (encoding) {
    case QNN_QUANTIZATION_ENCODING_SCALE_OFFSET:
      out << "QNN_QUANTIZATION_ENCODING_SCALE_OFFSET";
      break;
    case QNN_QUANTIZATION_ENCODING_AXIS_SCALE_OFFSET:
      out << "QNN_QUANTIZATION_ENCODING_AXIS_SCALE_OFFSET";
      break;
    case QNN_QUANTIZATION_ENCODING_BW_SCALE_OFFSET:
      out << "QNN_QUANTIZATION_ENCODING_BW_SCALE_OFFSET";
      break;
    case QNN_QUANTIZATION_ENCODING_BW_AXIS_SCALE_OFFSET:
      out << "QNN_QUANTIZATION_ENCODING_BW_AXIS_SCALE_OFFSET";
      break;
    case QNN_QUANTIZATION_ENCODING_UNDEFINED:
      out << "QNN_QUANTIZATION_ENCODING_UNDEFINED";
      break;
    default:
      out << "Uknown quantization encoding";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_QuantizeParams_t& quantize_params) {
  out << " encodingDefinition=" << quantize_params.encodingDefinition;
  out << " quantizationEncoding=" << quantize_params.quantizationEncoding;
  if (quantize_params.encodingDefinition == QNN_DEFINITION_IMPL_GENERATED ||
      quantize_params.encodingDefinition == QNN_DEFINITION_DEFINED) {
    if (quantize_params.quantizationEncoding == QNN_QUANTIZATION_ENCODING_SCALE_OFFSET) {
      out << " scale=" << quantize_params.scaleOffsetEncoding.scale;
      out << " offset=" << quantize_params.scaleOffsetEncoding.offset;
    } else if (quantize_params.quantizationEncoding == QNN_QUANTIZATION_ENCODING_BW_SCALE_OFFSET) {
      out << " bitwidth=" << quantize_params.bwScaleOffsetEncoding.bitwidth;
      out << " scale=" << quantize_params.bwScaleOffsetEncoding.scale;
      out << " offset=" << quantize_params.bwScaleOffsetEncoding.offset;
    } else if (quantize_params.quantizationEncoding == QNN_QUANTIZATION_ENCODING_AXIS_SCALE_OFFSET) {
      out << " axis=" << quantize_params.axisScaleOffsetEncoding.axis;
      size_t num_elems = quantize_params.axisScaleOffsetEncoding.numScaleOffsets;
      bool truncate = num_elems > 20;
      num_elems = truncate ? 20 : num_elems;
      out << " scales=(";
      for (size_t i = 0; i < num_elems; i++) {
        out << quantize_params.axisScaleOffsetEncoding.scaleOffset[i].scale << (i == num_elems - 1 ? "" : " ");
      }
      out << ") offsets=(";
      for (size_t i = 0; i < num_elems; i++) {
        out << quantize_params.axisScaleOffsetEncoding.scaleOffset[i].offset << (i == num_elems - 1 ? "" : " ");
      }
      out << (truncate ? "...)" : ")");
    } else if (quantize_params.quantizationEncoding == QNN_QUANTIZATION_ENCODING_BW_AXIS_SCALE_OFFSET) {
      out << " axis=" << quantize_params.bwAxisScaleOffsetEncoding.axis;
      out << " bw=" << quantize_params.bwAxisScaleOffsetEncoding.bitwidth;
      size_t num_elems = quantize_params.bwAxisScaleOffsetEncoding.numElements;
      bool truncate = num_elems > 20;
      num_elems = truncate ? 20 : num_elems;
      out << " scales=(";
      for (size_t i = 0; i < num_elems; i++) {
        out << quantize_params.bwAxisScaleOffsetEncoding.scales[i] << (i == num_elems - 1 ? "" : " ");
      }
      out << ") offsets=(";
      for (size_t i = 0; i < num_elems; i++) {
        out << quantize_params.bwAxisScaleOffsetEncoding.offsets[i] << (i == num_elems - 1 ? "" : " ");
      }
      out << (truncate ? "...)" : ")");
    } else {
      out << " encoding not supported.";
    }
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_TensorType_t& tensor_type) {
  switch (tensor_type) {
    case QNN_TENSOR_TYPE_APP_WRITE:
      out << "QNN_TENSOR_TYPE_APP_WRITE";
      break;
    case QNN_TENSOR_TYPE_APP_READ:
      out << "QNN_TENSOR_TYPE_APP_READ";
      break;
    case QNN_TENSOR_TYPE_APP_READWRITE:
      out << "QNN_TENSOR_TYPE_APP_READWRITE";
      break;
    case QNN_TENSOR_TYPE_NATIVE:
      out << "QNN_TENSOR_TYPE_NATIVE";
      break;
    case QNN_TENSOR_TYPE_STATIC:
      out << "QNN_TENSOR_TYPE_STATIC";
      break;
    case QNN_TENSOR_TYPE_NULL:
      out << "QNN_TENSOR_TYPE_NULL";
      break;
    default:
      out << "Unsupported type";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_TensorMemType_t& mem_type) {
  switch (mem_type) {
    case QNN_TENSORMEMTYPE_RAW:
      out << "QNN_TENSORMEMTYPE_RAW";
      break;
    case QNN_TENSORMEMTYPE_MEMHANDLE:
      out << "QNN_TENSORMEMTYPE_MEMHANDLE";
      break;
    default:
      out << "Unsupported mem type";
  }
  return out;
}
template <typename T>
std::ostream& operator<<(std::ostream& out, const Qnn_ClientBuffer_t& client_bufer) {
  T* data = reinterpret_cast<T*>(client_bufer.data);
  out << " dataSize=" << client_bufer.dataSize;
  uint32_t count = client_bufer.dataSize / sizeof(T);
  const bool truncate = count > 100;

  count = truncate ? 100 : count;  // limit to 100 data
  out << " clientBuf=(";
  for (uint32_t i = 0; i < count; i++) {
    if constexpr (sizeof(T) == 1) {
      out << static_cast<int32_t>(data[i]) << " ";
    } else {
      out << data[i] << " ";
    }
  }
  out << (truncate ? "..." : "") << ")";
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_Tensor_t& tensor) {
  out << " name=" << GetQnnTensorName(tensor);
  out << " id=" << GetQnnTensorID(tensor);
  out << " version=" << tensor.version;
  out << " type=" << GetQnnTensorType(tensor);
  out << " dataFormat=" << GetQnnTensorDataFormat(tensor);
  out << " dataType=" << GetQnnTensorDataType(tensor);
  out << " rank=" << GetQnnTensorRank(tensor);
  out << " dimensions=(";
  for (uint32_t i = 0; i < GetQnnTensorRank(tensor); i++) {
    out << GetQnnTensorDims(tensor)[i] << " ";
  }
  out << ")";
  out << " memType=" << GetQnnTensorMemType(tensor);
// TODO: the code below has compilation errors with the latest ABSL
#if 0
  if (GetQnnTensorMemType(tensor) == QNN_TENSORMEMTYPE_RAW) {
    if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_FLOAT_32) {
      operator<< <float>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_UINT_32 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_UFIXED_POINT_32) {
      operator<< <uint32_t>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_INT_32 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_SFIXED_POINT_32) {
      operator<< <int32_t>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_UINT_16 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_UFIXED_POINT_16) {
      operator<< <uint16_t>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_INT_16 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_SFIXED_POINT_16) {
      operator<< <int16_t>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_UINT_8 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_UFIXED_POINT_8) {
      operator<< <uint8_t>(out, GetQnnTensorClientBuf(tensor));
    } else {
      operator<< <int8_t>(out, GetQnnTensorClientBuf(tensor));
    }
  }
#endif
  out << " quantizeParams:" << GetQnnTensorQParams(tensor);
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_ParamType_t& param_type) {
  switch (param_type) {
    case QNN_PARAMTYPE_SCALAR:
      out << "QNN_PARAMTYPE_SCALAR";
      break;
    case QNN_PARAMTYPE_TENSOR:
      out << "QNN_PARAMTYPE_TENSOR";
      break;
    default:
      out << "Unknown type";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_Param_t& qnn_param) {
  out << " type=" << qnn_param.paramType;
  out << " name=" << qnn_param.name;
  if (qnn_param.paramType == QNN_PARAMTYPE_TENSOR) {
    out << qnn_param.tensorParam;
  } else {
    out << " value=" << qnn_param.scalarParam;
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const QnnOpConfigWrapper& op_conf_wrapper) {
  out << "Qnn_OpConfig node name: " << op_conf_wrapper.GetOpName()
      << " package_name: " << op_conf_wrapper.GetPackageName()
      << " QNN_op_type: " << op_conf_wrapper.GetTypeName()
      << " num_of_inputs: " << op_conf_wrapper.GetInputsNum()
      << " num_of_outputs: " << op_conf_wrapper.GetOutputsNum()
      << " num_of_params: " << op_conf_wrapper.GetParamsNum();

  out << std::endl
      << " node_inputs:" << std::endl;
  for (uint32_t i = 0; i < op_conf_wrapper.GetInputsNum(); i++) {
    out << op_conf_wrapper.GetInputTensors()[i] << std::endl;
  }
  out << " node_outputs:" << std::endl;
  for (uint32_t i = 0; i < op_conf_wrapper.GetOutputsNum(); i++) {
    out << op_conf_wrapper.GetOutputTensors()[i] << std::endl;
  }
  out << " node_params:" << std::endl;
  for (uint32_t i = 0; i < op_conf_wrapper.GetParamsNum(); i++) {
    out << op_conf_wrapper.GetParams()[i] << std::endl;
  }
  return out;
}

// Returns a JSON array from a gsl::span.
template <typename T>
static inline nlohmann::json JSONFromSpan(gsl::span<const T> elems) {
  nlohmann::json json_array = nlohmann::json::array();

  for (auto elem : elems) {
    json_array.push_back(elem);
  }

  return json_array;
}

// Fills json array with elements from the raw source buffer.
// Returns the number of bytes copied from the raw source buffer.
template <typename T>
static inline uint32_t FillJSONArrayFromRawData(nlohmann::json* json_array, const void* ptr, uint32_t num_elems) {
  gsl::span<const T> elems{reinterpret_cast<const T*>(ptr), static_cast<size_t>(num_elems)};
  for (auto elem : elems) {
    json_array->push_back(elem);
  }

  return num_elems * sizeof(T);
}

template <>
inline uint32_t FillJSONArrayFromRawData<MLFloat16>(nlohmann::json* json_array, const void* ptr, uint32_t num_elems) {
  gsl::span<const MLFloat16> elems{reinterpret_cast<const MLFloat16*>(ptr), static_cast<size_t>(num_elems)};
  for (auto elem : elems) {
    json_array->push_back(elem.ToFloat());
  }

  return num_elems * sizeof(MLFloat16);
}

// Fills json array with typed elements from the raw source buffer.
// Returns the number of bytes copied from the raw source buffer.
static uint32_t AppendQnnElemsToJSONArray(nlohmann::json* json_array, const void* data, uint32_t num_elems,
                                          Qnn_DataType_t data_type) {
  switch (data_type) {
    case QNN_DATATYPE_BOOL_8:  // Handle bool the same as int8 (0 or 1)
    case QNN_DATATYPE_INT_8:
      return FillJSONArrayFromRawData<int8_t>(json_array, data, num_elems);
    case QNN_DATATYPE_INT_16:
      return FillJSONArrayFromRawData<int16_t>(json_array, data, num_elems);
    case QNN_DATATYPE_INT_32:
      return FillJSONArrayFromRawData<int32_t>(json_array, data, num_elems);
    case QNN_DATATYPE_INT_64:
      return FillJSONArrayFromRawData<int64_t>(json_array, data, num_elems);
    case QNN_DATATYPE_UINT_8:
      return FillJSONArrayFromRawData<uint8_t>(json_array, data, num_elems);
    case QNN_DATATYPE_UINT_16:
      return FillJSONArrayFromRawData<uint16_t>(json_array, data, num_elems);
    case QNN_DATATYPE_UINT_32:
      return FillJSONArrayFromRawData<uint32_t>(json_array, data, num_elems);
    case QNN_DATATYPE_UINT_64:
      return FillJSONArrayFromRawData<uint64_t>(json_array, data, num_elems);
    case QNN_DATATYPE_FLOAT_32:
      return FillJSONArrayFromRawData<float>(json_array, data, num_elems);
    case QNN_DATATYPE_FLOAT_16:
      return FillJSONArrayFromRawData<MLFloat16>(json_array, data, num_elems);
    default:
      return 0;  // Do not append anything for unsupported types.
  }
}

// Returns a JSON array that contains static tensor data. The resulting JSON array is constructed hierarchically
// according to the provided dimensions/shape.
//
// Example:
// If buf = [0, 1, 2, 3, 4, 5] and dims = [1, 2, 3]
//   => returns JSON array [[[0, 1, 2], [3, 4, 5]]]
static nlohmann::json GetQnnClientBufJSON(const Qnn_ClientBuffer_t& buf, Qnn_DataType_t data_type,
                                          gsl::span<const uint32_t> dims) {
  using json = nlohmann::json;
  const char* data_ptr = reinterpret_cast<const char*>(buf.data);

  // Calculate number of elements.
  uint32_t num_elems = 1;
  for (auto d : dims) {
    num_elems *= d;
  }

  if (num_elems == 0) {
    return json::array();
  }

  const uint32_t last_dim = dims.back();
  const uint32_t num_dims = gsl::narrow_cast<uint32_t>(dims.size());
  std::vector<json> curr;
  curr.reserve(num_elems / last_dim);

  // Group raw data into individual JSON arrays of size `last_dim` each.
  // Store these JSON arrays in the `curr` vector.
  for (uint32_t j = num_elems; j > 0; j -= last_dim) {
    curr.push_back(json::array());
    data_ptr += AppendQnnElemsToJSONArray(&curr.back(), data_ptr, last_dim, data_type);
  }

  // Iterate through dimension values backwards (starting at second-to-last).
  // In each iteration, we collect the JSON arrays in the `curr` vector into groups (i.e., new JSON arrays) of
  // size `dim_val`. This new/smaller collection of JSON arrays becomes the input for the next iteration.
  for (uint32_t i = num_dims - 1; i-- > 0;) {
    const uint32_t dim_val = dims[i];
    std::vector<json> next;

    for (uint32_t j = 0; j < curr.size(); ++j) {
      if (j % dim_val == 0) {
        next.push_back(json::array());
      }

      next.back().emplace_back(std::move(curr[j]));
    }

    curr = std::move(next);
  }

  assert(curr.size() == 1);
  return curr[0];
}

// Returns a JSON representation of a QNN tensor.
// Example:
//
// {
//     "id" : 1652639423,
//     "type" : 3
//     "dataFormat" : 0,
//     "data_type" : 562,
//     "dims" : [ 1, 224, 224, 3 ],
//     "quant_params" : { ... },
//     "axis_format" : "NOT_YET_DEFINED",
//     "src_axis_format" : "NOT_YET_DEFINED",
// }
static nlohmann::json GetQnnTensorJSON(const Qnn_Tensor_t& tensor, bool include_static_data = false) {
  using json = nlohmann::json;
  json tensor_json = json::object();
  const Qnn_TensorType_t tensor_type = GetQnnTensorType(tensor);

  tensor_json["id"] = GetQnnTensorID(tensor);
  tensor_json["type"] = tensor_type;
  tensor_json["dataFormat"] = GetQnnTensorDataFormat(tensor);
  tensor_json["data_type"] = GetQnnTensorDataType(tensor);
  tensor_json["src_axis_format"] = "NOT_YET_DEFINED";
  tensor_json["axis_format"] = "NOT_YET_DEFINED";

  const Qnn_QuantizeParams_t& quant_params = GetQnnTensorQParams(tensor);
  tensor_json["quant_params"] = {
      {"definition", quant_params.encodingDefinition},
      {"encoding", quant_params.quantizationEncoding},
      {"scale_offset",
       {{"scale", quant_params.scaleOffsetEncoding.scale}, {"offset", quant_params.scaleOffsetEncoding.offset}}}};

  gsl::span<const uint32_t> dims{GetQnnTensorDims(tensor), GetQnnTensorRank(tensor)};
  tensor_json["dims"] = JSONFromSpan(dims);

  if (tensor_type == Qnn_TensorType_t::QNN_TENSOR_TYPE_STATIC) {
    if (include_static_data) {
      tensor_json["data"] = GetQnnClientBufJSON(GetQnnTensorClientBuf(tensor), GetQnnTensorDataType(tensor), dims);
    } else {
      std::stringstream ss;
      ss << CalcQnnTensorNumElems(tensor);
      tensor_json["params_count"] = ss.str();
    }
  }

  return tensor_json;
}

// Returns a JSON object representation of a QNN scalar parameter. Example: { "306": 1 }
// Note that the key is the stringified data type.
static nlohmann::json GetQnnScalarParamJSON(const Qnn_Scalar_t& param) {
  nlohmann::json param_json = nlohmann::json::object();
  std::stringstream ss;
  ss << static_cast<uint64_t>(param.dataType);

  switch (param.dataType) {
    case QNN_DATATYPE_BOOL_8:  // Print bool the same as int8 (0 or 1)
    case QNN_DATATYPE_INT_8:
      param_json[ss.str()] = param.int8Value;
      break;
    case QNN_DATATYPE_INT_16:
      param_json[ss.str()] = param.int16Value;
      break;
    case QNN_DATATYPE_INT_32:
      param_json[ss.str()] = param.int32Value;
      break;
    case QNN_DATATYPE_UINT_8:
      param_json[ss.str()] = param.uint8Value;
      break;
    case QNN_DATATYPE_UINT_16:
      param_json[ss.str()] = param.uint16Value;
      break;
    case QNN_DATATYPE_UINT_32:
      param_json[ss.str()] = param.uint32Value;
      break;
    case QNN_DATATYPE_FLOAT_32:
      param_json[ss.str()] = param.floatValue;
      break;
    default:
      // Do nothing for unsupported types.
      break;
  }

  return param_json;
}

// Returns a JSON array initialized with the names of the provided QNN tensors.
static nlohmann::json GetQnnTensorNamesJSON(gsl::span<const Qnn_Tensor_t> tensors) {
  nlohmann::json names_json = nlohmann::json::array();

  for (const auto& tensor : tensors) {
    names_json.push_back(GetQnnTensorName(tensor));
  }

  return names_json;
}

// Returns a JSON representation of a QNN operator.
// Example:
// {
//     "package": "qti.aisw",
//     "type": "Conv2d",
//     "input_names": [ "Transpose_token_2012_out0", "weight_quantized", "beta_quantized" ],
//     "output_names": [ "resnetv17_relu0_fwd_QuantizeLinear" ],
//     "scalar_params": { "group": {...} },
//     "tensor_params": { "stride": {...} },
//     "macs_per_inference": ""
// }
static nlohmann::json GetQnnOpJSON(const QnnOpConfigWrapper& op_config) {
  using json = nlohmann::json;
  json op_json = json::object();
  op_json["package"] = op_config.GetPackageName();
  op_json["type"] = op_config.GetTypeName();

  json tensor_params_json = json::object();
  json scalar_params_json = json::object();

  gsl::span<const Qnn_Param_t> params{op_config.GetParams(), op_config.GetParamsNum()};
  for (const auto& param : params) {
    if (param.paramType == QNN_PARAMTYPE_SCALAR) {
      scalar_params_json[param.name] = GetQnnScalarParamJSON(param.scalarParam);
    } else if (param.paramType == QNN_PARAMTYPE_TENSOR) {
      tensor_params_json[param.name][GetQnnTensorName(param.tensorParam)] = GetQnnTensorJSON(param.tensorParam, true);
    }
  }

  op_json["tensor_params"] = std::move(tensor_params_json);
  op_json["scalar_params"] = std::move(scalar_params_json);
  op_json["input_names"] =
      GetQnnTensorNamesJSON(gsl::span<const Qnn_Tensor_t>{op_config.GetInputTensors(), op_config.GetInputsNum()});
  op_json["output_names"] =
      GetQnnTensorNamesJSON(gsl::span<const Qnn_Tensor_t>{op_config.GetOutputTensors(), op_config.GetOutputsNum()});
  op_json["macs_per_inference"] = "";  // Metadata set by QNN converter tools. Not needed.

  return op_json;
}

QnnJSONGraph::QnnJSONGraph() {
  using json = nlohmann::json;

  json_ = {// Use dummy model.cpp and model.bin files when loading JSON with QNN Netron.
           // They don't have to exist in order to visualize the graph.
           {"model.cpp", "N/A"},
           {"model.bin", "N/A"},
           {"converter_command", ""},
           {"copyright_str", "Copyright (c) Microsoft Corporation. All rights reserved."},
           {"op_types", json::array()},
           {"Total parameters", ""},
           {"Total MACs per inference", ""},
           {"graph", {{"tensors", json::object()}, {"nodes", json::object()}}}};
}

void QnnJSONGraph::AddOp(const QnnOpConfigWrapper& op_conf_wrapper) {
  // Serialize inputs and outputs.
  AddOpTensors({op_conf_wrapper.GetInputTensors(), op_conf_wrapper.GetInputsNum()});
  AddOpTensors({op_conf_wrapper.GetOutputTensors(), op_conf_wrapper.GetOutputsNum()});

  // Track unique op types (serialized in Finalize()).
  const std::string& op_type = op_conf_wrapper.GetTypeName();
  if (seen_op_types_.count(op_type) == 0) {
    seen_op_types_.insert(op_type);
  }

  // Serialize op
  json_["graph"]["nodes"][op_conf_wrapper.GetOpName()] = GetQnnOpJSON(op_conf_wrapper);
}

void QnnJSONGraph::AddOpTensors(gsl::span<const Qnn_Tensor_t> tensors) {
  for (const auto& tensor : tensors) {
    std::string name = GetQnnTensorName(tensor);  // Copies name into std::string, which is moved into seen_tensors_.
    if (seen_tensors_.count(name) == 0) {
      json_["graph"]["tensors"][name] = GetQnnTensorJSON(tensor);
      seen_tensors_.insert(std::move(name));
    }
  }
}

const nlohmann::json& QnnJSONGraph::Finalize() {
  json_["op_types"] = seen_op_types_;
  return json_;
}

}  // namespace qnn
}  // namespace onnxruntime
