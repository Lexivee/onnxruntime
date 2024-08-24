// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#if !defined(ORT_MINIMAL_BUILD)

#include "core/providers/cpu/ml/tree_ensemble_helper.h"
#include "core/common/common.h"
#include "onnx/defs/tensor_proto_util.h"
#include "core/common/safeint.h"

#include "core/framework/tensorprotoutils.h"


using namespace ::onnxruntime::common;
using namespace std;
namespace onnxruntime {
namespace ml {

template <typename TH>
Status GetVectorAttrsOrDefault(const OpKernelInfo& info, const std::string& name,
                               ONNX_NAMESPACE::TensorProto_DataType proto_type, std::vector<TH>& data) {
  ONNX_NAMESPACE::TensorProto proto;
  auto result = info.GetAttr(name, &proto);

  SafeInt<int64_t> n_elements(1);
  for (auto dim : proto.dims()) {
    n_elements *= dim;
  }

  if (proto.dims().empty()) {
    return Status::OK();
  }

  const SafeInt<size_t> tensor_size(n_elements);
  data.clear();
  data.resize(tensor_size);

  result = utils::UnpackTensor<TH>(proto, std::filesystem::path(), data.data(), tensor_size);
  ORT_ENFORCE(result.IsOK(), "TreeEnsemble could not unpack tensor attribute ", name);

  return Status::OK();
}

Status GetVectorAttrsOrDefault(const OpKernelInfo& info, const std::string& name, std::vector<double>& data) {
  return GetVectorAttrsOrDefault(info, name, ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_DOUBLE, data);
}

Status GetVectorAttrsOrDefault(const OpKernelInfo& info, const std::string& name, std::vector<float>& data) {
  return GetVectorAttrsOrDefault(info, name, ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_FLOAT, data);
}

Status GetVectorAttrsOrDefault(const OpKernelInfo& info, const std::string& name, std::vector<uint8_t>& data) {
  return GetVectorAttrsOrDefault(info, name, ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_UINT8, data);
}

}  // namespace ml
}  // namespace onnxruntime

#endif  // !defined(ORT_MINIMAL_BUILD)
