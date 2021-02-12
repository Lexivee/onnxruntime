// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/common/common.h"
#include "core/common/type_list.h"
#include "core/framework/data_types.h"
#include "core/framework/data_types_internal.h"
#include "core/framework/op_kernel.h"
#include "core/framework/tensorprotoutils.h"
#include "core/providers/op_kernel_type_control_utils.h"

namespace onnxruntime {

using ConstantOfShapeDefaultOutputTypes =
    TypeList<
        MLFloat16,
        float, double,
        int8_t, int16_t, int32_t, int64_t,
        uint8_t, uint16_t, uint32_t, uint64_t,
        bool>;

template <typename EnabledOutputTypeList = ConstantOfShapeDefaultOutputTypes>
class ConstantOfShapeBase {
 protected:
  ConstantOfShapeBase(const OpKernelInfo& info) {
    ONNX_NAMESPACE::TensorProto t_proto;
    if (info.GetAttr<ONNX_NAMESPACE::TensorProto>("value", &t_proto).IsOK()) {
      ORT_ENFORCE(t_proto.dims_size() == 1, "Must have a single dimension");
      ORT_ENFORCE(t_proto.dims()[0] == 1, "Must have a single dimension of 1");
      SetValueFromTensorProto(t_proto);
    } else {
      float f_value = 0.f;
      SetValue(sizeof(float), reinterpret_cast<void*>(&f_value));
    }
  }

  void* GetValuePtr() const { return p_value_; }

  static Status PrepareCompute(OpKernelContext* ctx, Tensor** output_tensor) {
    const auto shape_tensor = ctx->Input<Tensor>(0);
    const auto& input_shape = shape_tensor->Shape();

    // If empty the output is a scalar with empty shape
    // TensorShape::Size() will still return 1 and we will output
    // one value
    ORT_RETURN_IF_NOT(input_shape.NumDimensions() > 0, "Must have a valid input shape.");

    const auto span = shape_tensor->DataAsSpan<int64_t>();

    TensorShape output_shape(span.begin(), span.size());
    (*output_tensor) = ctx->Output(0, output_shape);

    return Status::OK();
  }

 private:
  union SizeBasedValue {
    int8_t int8_;
    int16_t int16_;
    int32_t int32_;
    int64_t int64_;
  } s_value_;
  void* p_value_;

  void SetValue(size_t size, void* value) {
    switch (size) {
      case sizeof(int8_t):
        s_value_.int8_ = *(reinterpret_cast<int8_t*>(value));
        p_value_ = reinterpret_cast<void*>(&(s_value_.int8_));
        break;
      case sizeof(int16_t):
        s_value_.int16_ = *(reinterpret_cast<int16_t*>(value));
        p_value_ = reinterpret_cast<void*>(&(s_value_.int16_));
        break;
      case sizeof(int32_t):
        s_value_.int32_ = *(reinterpret_cast<int32_t*>(value));
        p_value_ = reinterpret_cast<void*>(&(s_value_.int32_));
        break;
      case sizeof(int64_t):
        s_value_.int64_ = *(reinterpret_cast<int64_t*>(value));
        p_value_ = reinterpret_cast<void*>(&(s_value_.int64_));
        break;
      default:
        ORT_THROW("Unsupported value attribute datatype with size: ", size);
    }
  }

  void SetValueFromTensorProto(const ONNX_NAMESPACE::TensorProto&);
};

#define CASE_FETCH_VALUE_DATA(c_type)                                                                       \
  case utils::ToTensorProtoElementType<c_type>(): {                                                         \
    if (utils::HasType<EnabledOutputTypeList, c_type>()) {                                                  \
      c_type val;                                                                                           \
      auto unpack_status = utils::UnpackTensor(t_proto, raw_data, raw_data_len, &val, 1);                   \
      ORT_ENFORCE(unpack_status.IsOK(), "Value attribute unpacking failed:", unpack_status.ErrorMessage()); \
      SetValue(sizeof(c_type), reinterpret_cast<void*>(&val));                                              \
    } else {                                                                                                \
      ORT_THROW("Value attribute datatype is not enabled in this build: ", tensor_type);                    \
    }                                                                                                       \
    break;                                                                                                  \
  }

template <typename EnabledOutputTypeList>
void ConstantOfShapeBase<EnabledOutputTypeList>::SetValueFromTensorProto(const ONNX_NAMESPACE::TensorProto& t_proto) {
  ORT_ENFORCE(utils::HasDataType(t_proto));
  ORT_ENFORCE(ONNX_NAMESPACE::TensorProto::DataType_IsValid(t_proto.data_type()));
  ORT_ENFORCE(!utils::HasExternalData(t_proto),
              "Tensor proto with external data for value attribute is not supported.");
  const auto tensor_type = static_cast<ONNX_NAMESPACE::TensorProto_DataType>(t_proto.data_type());
  const void* const raw_data = utils::HasRawData(t_proto) ? t_proto.raw_data().data() : nullptr;
  const size_t raw_data_len = utils::HasRawData(t_proto) ? t_proto.raw_data().size() : 0;
  switch (tensor_type) {
    CASE_FETCH_VALUE_DATA(bool)
    CASE_FETCH_VALUE_DATA(float)
    CASE_FETCH_VALUE_DATA(MLFloat16)
    CASE_FETCH_VALUE_DATA(double)
    CASE_FETCH_VALUE_DATA(int8_t)
    CASE_FETCH_VALUE_DATA(int16_t)
    CASE_FETCH_VALUE_DATA(int32_t)
    CASE_FETCH_VALUE_DATA(int64_t)
    CASE_FETCH_VALUE_DATA(uint8_t)
    CASE_FETCH_VALUE_DATA(uint16_t)
    CASE_FETCH_VALUE_DATA(uint32_t)
    CASE_FETCH_VALUE_DATA(uint64_t)
    default:
      ORT_THROW("Unsupported value attribute datatype: ", tensor_type);
  }
}

#undef CASE_FETCH_VALUE_DATA

}  // namespace onnxruntime
