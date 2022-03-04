// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/framework/tensorprotoutils.h"

#include "tvm_runner_impl.h"
#include "tvm_utils.h"
#include "tvm_api.h"


namespace onnxruntime {
namespace tvm {

/* ------------------------------------ RunnerImplFactory ----------------------------- */

std::shared_ptr<RunnerImpl> getTVMRunnerImpl(const std::string& name, const std::shared_ptr<TvmModule>& mod) {
    if (name == "graph") {
        return std::make_shared<GERunnerImpl>(mod);
    } else if (name == "vm") {
        return std::make_shared<VMRunnerImpl>(mod);
    }
    return nullptr;
}

/* ------------------------------------ RunnerImpl ------------------------------------ */

RunnerImpl::RunnerImpl(const std::shared_ptr<TvmModule>& mod) :
  mod_(mod) {
}

common::Status RunnerImpl::run(FunctionState state, const OrtCustomOpApi* api, OrtKernelContext* context) {
  Ort::CustomOpApi ort{*api};

  set_input(ort, context);

  connect_output_tensors2ort(ort, context);

  run_and_get_output();

  return Status::OK();
}

void RunnerImpl::convert_input_tensors2dl_tensors(Ort::CustomOpApi& ort,
                                                  OrtKernelContext* context,
                                                  std::vector<DLTensor>& dst,
                                                  std::vector<size_t>& dst_inds) {
  size_t num = inputs_info_.size();
  dst.reserve(num);
  dst_inds.reserve(num);
  size_t counter = 0u;
  for (auto& info : inputs_info_) {
    // TODO(vvchernov): decomposition declaration only available with -std=c++1z or -std=gnu++1z
    auto& i = info.first;
    auto& shape = info.second;
    const OrtValue* input_tensor = ort.KernelContext_GetInput(context, i);
    ORT_ENFORCE(input_tensor->IsTensor());
    const Tensor& tensor = input_tensor->Get<Tensor>();
    const OrtDevice& device = tensor.Location().device;
    auto tensor_info = ort.GetTensorTypeAndShape(input_tensor);
    auto tensor_type = ort.GetTensorElementType(tensor_info);
    ort.ReleaseTensorTypeAndShapeInfo(tensor_info);

    DLTensor t;
    t.device = GetDLDevice(device);
    t.dtype = GetDataType(tensor_type);
    t.strides = nullptr;
    t.byte_offset = 0;
    t.data = const_cast<void*>(ort.GetTensorData<void>(input_tensor));
    t.ndim = shape.size();
    t.shape = shape.data();
    dst[counter] = t;
    dst_inds[counter++] = i;
  }
}

void RunnerImpl::add_device_type_data2output_tensors(Ort::CustomOpApi& ort,
                                                     OrtKernelContext* context) {
  size_t num_outputs = tensors_outputs_.size();
  for (auto i = 0u; i < num_outputs; i++) {
    //setup output tensor property
    OrtValue* output_tensor = ort.KernelContext_GetOutput(context,
                                                          i,
                                                          output_shapes_[i].data(),
                                                          output_shapes_[i].size());
    ORT_ENFORCE(output_tensor->IsTensor());
    const Tensor& tensor = output_tensor->Get<Tensor>();
    const OrtDevice& device = tensor.Location().device;
    auto tensor_info = ort.GetTensorTypeAndShape(output_tensor);
    auto tensor_type = ort.GetTensorElementType(tensor_info);
    ort.ReleaseTensorTypeAndShapeInfo(tensor_info);

    tensors_outputs_[i].device = GetDLDevice(device);
    tensors_outputs_[i].dtype = GetDataType(tensor_type);
    tensors_outputs_[i].data = ort.GetTensorMutableData<void>(output_tensor);
  }
}

bool RunnerImpl::compare_shapes(const TVMTensorShape& shape1, const TVMTensorShape& shape2) const {
  size_t size = shape1.size();
  if (shape2.size() == size) {
    for (size_t i = 0; i < size; ++i) {
      if(shape1[i] != shape2[i]) {
        return false;
      }
    }
  } else {
    return false;
  }

  return true;
}

/* ------------------------------------ GERunnerImpl ------------------------------------ */

GERunnerImpl::GERunnerImpl(const std::shared_ptr<TvmModule>& mod) :
  RunnerImpl(mod) {
}

void GERunnerImpl::set_input(Ort::CustomOpApi& ort, OrtKernelContext* context) {
  std::vector<size_t> inds;
  std::vector<DLTensor> dl_tensors_inputs;
  convert_input_tensors2dl_tensors(ort, context, dl_tensors_inputs, inds);

  tvm::TVMSetInputs(*mod_, inds, dl_tensors_inputs);
}

void GERunnerImpl::connect_output_tensors2ort(Ort::CustomOpApi& ort, OrtKernelContext* context) {
  add_device_type_data2output_tensors(ort, context);
}

void GERunnerImpl::run_and_get_output() {
  tvm::TVMRun(*mod_);
  tvm::TVMGetOutputs(*mod_, tensors_outputs_);
}

/* ------------------------------------ VMRunnerImpl ------------------------------------ */

VMRunnerImpl::VMRunnerImpl(const std::shared_ptr<TvmModule>& mod) :
  RunnerImpl(mod) {
}

void VMRunnerImpl::set_input(Ort::CustomOpApi& ort, OrtKernelContext* context) {
  std::vector<size_t> inds;
  std::vector<DLTensor> dl_tensors_inputs;
  convert_input_tensors2dl_tensors(ort, context, dl_tensors_inputs, inds);

  tvm::TVM_VM_SetInputs(*mod_, inds, dl_tensors_inputs);
}

void VMRunnerImpl::connect_output_tensors2ort(Ort::CustomOpApi& ort, OrtKernelContext* context) {
  if(!probe_infer_) {
    infer_once_to_get_output_shapes();
  }

  add_device_type_data2output_tensors(ort, context);
}

void VMRunnerImpl::run_and_get_output() {
  tvm::TVM_VM_Run(*mod_);
  tvm::TVM_VM_GetOutputs(*mod_, tensors_outputs_);
}

void VMRunnerImpl::infer_once_to_get_output_shapes() {
  tvm::TVM_VM_Run(*mod_);
  size_t num_outputs = tensors_outputs_.size();
  tvm::TVMGetOutputShapes(*mod_, num_outputs, output_shapes_);
  for (size_t i = 0; i < num_outputs; ++i) {
    tensors_outputs_[i].ndim = output_shapes_[i].size();
    tensors_outputs_[i].shape = output_shapes_[i].data();
  }
  probe_infer_ = true;
}

}   // namespace tvm
}   // namespace onnxruntime
