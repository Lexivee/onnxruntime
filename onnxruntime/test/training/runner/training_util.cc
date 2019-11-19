// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "constant.h"
#include "core/framework/data_types.h"
#include "core/framework/tensorprotoutils.h"
#include "test/training/runner/training_util.h"

using namespace std;

namespace onnxruntime {
namespace training {

DataSet::DataSet(const vector<string>& tensor_names) : tensor_names_(tensor_names) {
}

DataSet::~DataSet() {
  for (auto deleter : ortvalue_deleters_) {
    if (deleter.f != nullptr) {
      deleter.f(deleter.param);
    }
  }
  ortvalue_buffers_.clear();
  ortvalue_deleters_.clear();
}

const vector<string> DataSet::TensorNames() const {
  return tensor_names_;
}

common::Status DataSet::AddData(DataSet::SampleType&& single_sample) {
  if (single_sample->size() != NumInputs()) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "DataSet::AddData failed");
  }

  data_.emplace_back(move(single_sample));
  return Status::OK();
}

common::Status DataSet::AddData(const vector<ONNX_NAMESPACE::TensorProto>& features) {
  if (features.size() != NumInputs()) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "DataSet::AddData failed");
  }

  DataSet::SampleType sample = make_unique<vector<OrtValue>>();
  for (const auto& tensor_proto : features) {
    size_t cpu_tensor_length;
    ORT_RETURN_IF_ERROR(utils::GetSizeInBytesFromTensorProto<0>(tensor_proto, &cpu_tensor_length));
    OrtValue ort_value;
    OrtAllocatorInfo info("Cpu", OrtDeviceAllocator, OrtDevice{}, 0, OrtMemTypeDefault);
    std::unique_ptr<char[]> buffer(new char[cpu_tensor_length]);
    OrtCallback deleter;
    ORT_RETURN_IF_ERROR(utils::TensorProtoToMLValue(
        Env::Default(), nullptr, tensor_proto, MemBuffer(buffer.get(), cpu_tensor_length, info), ort_value, deleter));

    sample->push_back(ort_value);
    ortvalue_buffers_.emplace_back(std::move(buffer));
    if (deleter.f != nullptr) {
      ortvalue_deleters_.emplace_back(deleter);
    }
  }

  data_.emplace_back(move(sample));
  return Status::OK();
}

size_t DataSet::TotalBatch(size_t batch_size) const {
  batch_size = min(batch_size, NumSamples());
  return NumSamples() / batch_size + ((NumSamples() % batch_size > 0) ? 1 : 0);
}

std::vector<OrtValue> DataSet::GetKthBatch(size_t batch_size, size_t k_th, AllocatorPtr allocator) const {
  batch_size = min(batch_size, data_.size());

  std::vector<OrtValue> result;
  for (size_t input_index = 0; input_index < NumInputs(); ++input_index) {
    const Tensor& first_tensor = data_[0]->at(input_index).Get<Tensor>();

    MLDataType element_type = first_tensor.DataType();
    TensorShape shape = first_tensor.Shape();
    if (shape.Size() > 1) {
      shape.insert(shape.begin(), batch_size);
    } else {
      shape.clear();
      shape.emplace_back(batch_size);
    }

    AllocatorPtr alloc = allocator ? allocator : TrainingUtil::GetCpuAllocator();
    auto p_tensor = std::make_unique<Tensor>(element_type, shape, alloc);
    void* buffer = p_tensor->MutableDataRaw();
    size_t memory_size_per_sample = first_tensor.SizeInBytes();

    size_t offset = k_th * batch_size;
    for (size_t i = offset; i < offset + batch_size; ++i) {
      size_t index = (offset + i) % NumSamples();
      const void* raw_value = data_[index]->at(input_index).Get<Tensor>().DataRaw();
      memcpy(buffer, raw_value, memory_size_per_sample);
      buffer = static_cast<char*>(buffer) + memory_size_per_sample;
    }

    result.emplace_back(p_tensor.release(),
                        DataTypeImpl::GetType<Tensor>(),
                        DataTypeImpl::GetType<Tensor>()->GetDeleteFunc());
  }

  return result;
}

void DataSet::RandomShuffle() {
  random_shuffle(data_.begin(), data_.end());
}

std::vector<OrtValue> RandomDataSet::GetKthBatch(size_t /*batch_size*/, size_t /*k_th*/, AllocatorPtr allocator) const {
  std::vector<OrtValue> result;

  for (size_t input_index = 0; input_index < NumInputs(); ++input_index) {
    MLDataType element_type;
    TensorShape shape = tensor_shapes_[input_index];

    if (tensor_types_[input_index] == onnx::TensorProto_DataType_INT64) {
      element_type = NonOnnxType<int64_t>::Type();
    } else if (tensor_types_[input_index] == onnx::TensorProto_DataType_INT32) {
      element_type = NonOnnxType<int32_t>::Type();
    } else if (tensor_types_[input_index] == onnx::TensorProto_DataType_FLOAT) {
      element_type = NonOnnxType<float>::Type();
    }
    AllocatorPtr alloc = allocator ? allocator : TrainingUtil::GetCpuAllocator();
    auto p_tensor = std::make_unique<Tensor>(element_type, shape, alloc);
    memset(p_tensor->MutableDataRaw(), 0, p_tensor->SizeInBytes());

    result.emplace_back(p_tensor.release(),
                        DataTypeImpl::GetType<Tensor>(),
                        DataTypeImpl::GetType<Tensor>()->GetDeleteFunc());
  }

  return result;
}

void TrainingUtil::PrintNameMLValMap(const NameMLValMap& mlvalue_map) {
  for (auto pair : mlvalue_map) {
    auto name = pair.first;
    MLValue value = pair.second;
    const Tensor& tensor = value.Get<Tensor>();

    printf("Name: %s \n", name.c_str());
    const float* data = tensor.template Data<float>();
    int64_t size = tensor.Shape().Size();
    for (int64_t i = 0; i < size; ++i) {
      printf("%0.04f\t ", *data);
      data++;
    }
    printf("\n\n");
  }
}

void TrainingUtil::PrintTensor(const string& name, const Tensor& tensor, ostream& os) {
  auto data_type = tensor.DataType();

  os << name << "\n";
  if (DataTypeImpl::GetType<float>() == data_type) {
    const float* data = tensor.template Data<float>();
    for (int i = 0; i < tensor.Shape().Size(); ++i) {
      os << data[i] << "\t";
    }
  } else if (DataTypeImpl::GetType<int64_t>() == data_type) {
    const int64_t* data = tensor.template Data<int64_t>();
    for (int i = 0; i < tensor.Shape().Size(); ++i) {
      os << data[i] << "\t";
    }
  } else if (DataTypeImpl::GetType<bool>() == data_type) {
    const bool* data = tensor.template Data<bool>();
    for (int i = 0; i < tensor.Shape().Size(); ++i) {
      os << data[i] << "\t";
    }
  } else {
    os << "Unsupported data type.";
  }
  os << "\n\n";
}

std::unique_ptr<LearningRateScheduler> LearningRateScheduler::Create(LearningRateParameters& lr_params, size_t training_step_count) {
  if (lr_params.warmup_mode == LRSchedule_NoWarmup) {
    return std::make_unique<NoWarmpScheduler>(lr_params, training_step_count);
  } else if (lr_params.warmup_mode == LRSchedule_Cosine) {
    return std::make_unique<CosineScheduler>(lr_params, training_step_count);
  } else if (lr_params.warmup_mode == LRSchedule_Constant) {
    return std::make_unique<ConstantScheduler>(lr_params, training_step_count);
  } else if (lr_params.warmup_mode == LRSchedule_Linear) {
    return std::make_unique<LinearScheduler>(lr_params, training_step_count);
  } else if (lr_params.warmup_mode == LRSchedule_Poly) {
    return std::make_unique<PolyScheduler>(lr_params, training_step_count);
  } else {
    ORT_THROW("Unsupported learning rate warmup schedule");
  }
}

}  // namespace training
}  // namespace onnxruntime
