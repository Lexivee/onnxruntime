// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/framework/execution_frame.h"

#include <sstream>

#include "core/framework/mem_pattern_planner.h"
#include "core/framework/ml_value_patterns_planner.h"
#include "core/framework/node_index_info.h"
#include "core/framework/op_kernel.h"
#include "core/framework/session_state.h"
#include "core/framework/utils.h"

using namespace onnxruntime::common;

namespace onnxruntime {

ExecutionFrame::ExecutionFrame(const std::vector<int>& feed_mlvalue_idxs,
                               const std::vector<MLValue>& feeds,
                               const std::vector<int>& fetch_mlvalue_idxs,
                               std::vector<MLValue>& fetches,
                               const std::unordered_map<size_t, IExecutor::CustomAllocator>& fetch_allocators,
                               const SessionState& session_state)
    : node_index_info_(session_state.GetNodeIndexInfo()),
      session_state_(session_state),
      mem_patterns_(nullptr),
      planner_(nullptr),
      fetch_mlvalue_idxs_{fetch_mlvalue_idxs} {
  Init(feed_mlvalue_idxs, feeds, fetch_mlvalue_idxs, fetches, fetch_allocators);

  // If the session enable memory pattern optimization
  // and we have execution plan generated, try to setup
  // memory pattern optimization.
  if (session_state.GetEnableMemoryPattern() &&
      session_state.GetExecutionPlan()) {
    std::vector<TensorShape> input_shapes;
    bool all_tensors = true;
    for (const auto& feed : feeds) {
      if (!(feed.IsTensor())) {
        all_tensors = false;
        break;
      }
      auto& tensor = feed.Get<Tensor>();
      input_shapes.push_back(tensor.Shape());
    }
    // if there is some traditional ml value type in inputs
    // disable the memory pattern optimization.
    if (all_tensors) {
      mem_patterns_ = session_state.GetMemoryPatternGroup(input_shapes);
      // if no existing patterns, generate one in this executionframe
      if (!mem_patterns_) {
        planner_ = std::make_unique<MLValuePatternPlanner>(*session_state.GetExecutionPlan());
      } else {
        // pre-allocate the big chunk requested in memory pattern.
        // all the internal kernel's input/output tensors will be allocated on these buffer.
        for (size_t i = 0; i < mem_patterns_->locations.size(); i++) {
          ORT_ENFORCE(buffers_.find(mem_patterns_->locations[i]) == buffers_.end());
          AllocatorPtr alloc = GetAllocator(mem_patterns_->locations[i]);
          void* buffer = mem_patterns_->patterns[i].PeakSize() > 0 ? alloc->Alloc(mem_patterns_->patterns[i].PeakSize()) : nullptr;
          buffers_[mem_patterns_->locations[i]] = BufferUniquePtr(buffer, alloc);
        }
      }
    }
  }
}

ExecutionFrame::~ExecutionFrame() = default;

Status ExecutionFrame::AllocateMLValueTensorSelfOwnBuffer(int mlvalue_index,
                                                          const DataTypeImpl* element_type,
                                                          const OrtAllocatorInfo& location,
                                                          const TensorShape& shape,
                                                          bool create_fence) {
  ORT_ENFORCE(mlvalue_index >= 0 && static_cast<size_t>(mlvalue_index) < all_values_.size());
  return AllocateMLValueTensorSelfOwnBufferHelper(mlvalue_index, element_type, location, shape, create_fence);
}

Status ExecutionFrame::AllocateMLValueTensorSelfOwnBufferHelper(int mlvalue_index,
                                                                const DataTypeImpl* element_type,
                                                                const OrtAllocatorInfo& location,
                                                                const TensorShape& shape,
                                                                bool create_fence) {
  if (mlvalue_index == NodeIndexInfo::kInvalidEntry)
    return Status(ONNXRUNTIME, FAIL, "Trying to allocate memory for unused optional inputs/outputs");

  auto p_mlvalue = &all_values_[mlvalue_index];
  if (p_mlvalue->IsAllocated()) {
    return Status::OK();
  }
  auto alloc = GetAllocator(location);
  size_t size;
  {
    int64_t len = shape.Size();
    if (len < 0) {
      return Status(ONNXRUNTIME, INVALID_ARGUMENT, "Tensor shape cannot contain any negative value");
    }
    if (!IAllocator::CalcMemSizeForArrayWithAlignment<64>(len, element_type->Size(), &size)) {
      return Status(ONNXRUNTIME, FAIL, "size overflow");
    }
  }
  // create fence if needed
  if (create_fence) {
    ORT_ENFORCE(p_mlvalue->Fence() == nullptr);
    FencePtr f = alloc->CreateFence(&GetSessionState());
    // it is OK to have fence been nullptr if the execution provider has no async execution,
    // and allocator::CreateFence returns nullptr
    p_mlvalue->SetFence(f);
  }

  // if we have pre-calculated memory pattern, and the mlvalue is not output mlvalue
  // try to allocated on pre-allocated big chunk.
  const auto& per_alloc_plan = GetAllocationPlan(mlvalue_index);
  if (mem_patterns_ && per_alloc_plan.alloc_kind != AllocKind::kAllocateOutput) {
    auto pattern = mem_patterns_->GetPatterns(location);
    if (pattern) {
      auto block = pattern->GetBlock(mlvalue_index);
      // if block not found, fall back to default behavior
      if (block) {
        auto it = buffers_.find(location);
        // if the block is not correct, log message then fall back to default behavior
        if (it != buffers_.end() && block->size_ == size) {
          void* buffer = it->second.get();
          auto status = AllocateTensorWithPreAllocateBufferHelper(
              p_mlvalue, static_cast<void*>(static_cast<char*>(buffer) + block->offset_),
              element_type, location, shape);
          return status;
        }
        if (block->size_ != size) {
          LOGS_DEFAULT(WARNING) << "For mlvalue with index: " << mlvalue_index << ", block in memory pattern size is: "
                                << block->size_ << " but the actually size is: " << size << ", fall back to default allocation behavior";
        } else if (it == buffers_.end()) {
          LOGS_DEFAULT(WARNING) << "For mlvalue with index: " << mlvalue_index << ", block not found in target loation. "
                                                                                  " fall back to default allocation behavior";
        }
      }
    }
  }
  //no memory pattern, or the pattern is not correct.
  void* buffer = size == 0 ? nullptr : alloc->Alloc(size);
  std::unique_ptr<Tensor> p_tensor = std::make_unique<Tensor>(element_type,
                                                              shape,
                                                              buffer,
                                                              location,
                                                              alloc);

  p_mlvalue->Init(p_tensor.release(),
                  DataTypeImpl::GetType<Tensor>(),
                  DataTypeImpl::GetType<Tensor>()->GetDeleteFunc());

  // trace the memory allocation.
  // don't trace the memory allocation on string tensors, as it need
  // placement new, we don't support it in memory pattern optimization.
  if (element_type != DataTypeImpl::GetType<std::string>())
    TraceAllocate(mlvalue_index, size);

  return Status::OK();
}

void ExecutionFrame::TraceAllocate(int mlvalue_idx, size_t size) {
  // don't trace the output tensors.
  auto& allocation_plan = GetAllocationPlan(mlvalue_idx);
  if (planner_ && allocation_plan.alloc_kind != AllocKind::kAllocateOutput) {
    auto status = planner_->TraceAllocation(mlvalue_idx, size);
    if (!status.IsOK())
      LOGS(session_state_.Logger(), WARNING) << "TraceAllocation for mlvalue_idx=" << mlvalue_idx << " size=" << size
                                             << " failed: " << status.ErrorMessage();
  }
}

Status ExecutionFrame::AllocateMLValueTensorPreAllocateBuffer(int mlvalue_index_to_allocate,
                                                              int mlvalue_index_reuse,
                                                              const DataTypeImpl* element_type,
                                                              const OrtAllocatorInfo& location,
                                                              const TensorShape& shape,
                                                              bool create_fence) {
  ORT_ENFORCE(mlvalue_index_to_allocate >= 0 && mlvalue_index_to_allocate < all_values_.size());
  MLValue* p_mlvalue = &all_values_[mlvalue_index_to_allocate];

  ORT_ENFORCE(mlvalue_index_reuse >= 0 && mlvalue_index_reuse < all_values_.size());
  MLValue* p_mlvalue_reuse = &all_values_[mlvalue_index_reuse];

  auto* reuse_tensor = p_mlvalue_reuse->GetMutable<Tensor>();
  void* reuse_buffer = reuse_tensor->MutableDataRaw();

  // create fence on reused mlvalue if needed
  // TODO: differentiate reuse and alias, by add AllocKind::kAlias?
  if (create_fence && p_mlvalue_reuse->Fence() == nullptr) {
    FencePtr f = GetAllocator(location)->CreateFence(&GetSessionState());
    p_mlvalue_reuse->SetFence(f);
  }

  // reused MLValue share the same fence
  p_mlvalue->ShareFenceWith(*p_mlvalue_reuse);
  return AllocateTensorWithPreAllocateBufferHelper(p_mlvalue, reuse_buffer, element_type, location, shape);
}

Status ExecutionFrame::AllocateTensorWithPreAllocateBufferHelper(MLValue* p_mlvalue,
                                                                 void* pBuffer,
                                                                 const DataTypeImpl* element_type,
                                                                 const OrtAllocatorInfo& location,
                                                                 const TensorShape& shape) {
  if (p_mlvalue->IsAllocated()) {
    return Status::OK();
  }
  std::unique_ptr<Tensor> p_tensor = std::make_unique<Tensor>(element_type,
                                                              shape,
                                                              pBuffer,
                                                              location);
  p_mlvalue->Init(p_tensor.release(),
                  DataTypeImpl::GetType<Tensor>(),
                  DataTypeImpl::GetType<Tensor>()->GetDeleteFunc());

  return Status::OK();
}

Status AllocateTraditionalMLValue(MLValue* p_mlvalue,
                                  const NonTensorTypeBase* type,
                                  const MLValueAllocationParameters& parameters) {
  // right now we don't need any parameter for ml value creation,
  // keep it in api for extensibility
  ORT_UNUSED_PARAMETER(parameters);
  auto creator = type->GetCreateFunc();
  p_mlvalue->Init(creator(),
                  type,
                  type->GetDeleteFunc());
  return Status::OK();
}

// This method is not thread safe!
Status ExecutionFrame::AllocateAsPerAllocationPlan(int mlvalue_index,
                                                   const MLValueAllocationParameters& parameters) {
  if (mlvalue_index == NodeIndexInfo::kInvalidEntry || mlvalue_index >= all_values_.size())
    return Status(ONNXRUNTIME, INVALID_ARGUMENT,
                  "Tried to allocated with invalid mlvalue index: " + std::to_string(mlvalue_index));

  // if there is a custom allocator for this mlvalue_index, call it to do the allocation
  auto custom_alloc_entry = custom_allocators_.find(mlvalue_index);
  if (custom_alloc_entry != custom_allocators_.cend()) {
    return (custom_alloc_entry->second)(parameters.GetTensorShape(), all_values_[mlvalue_index]);
  }

  const SequentialExecutionPlan* p_seq_exec_plan = session_state_.GetExecutionPlan();
  const auto& alloc_plan = p_seq_exec_plan->allocation_plan;
  ORT_ENFORCE(mlvalue_index >= 0 && mlvalue_index < alloc_plan.size());
  const auto& per_alloc_plan = alloc_plan[mlvalue_index];

  auto alloc_info = per_alloc_plan.location;
  auto ml_type = per_alloc_plan.value_type;
  if (ml_type == nullptr)
    return Status(ONNXRUNTIME, INVALID_ARGUMENT,
                  "Tried to allocate without valid type information, mlvalue index=" + std::to_string(mlvalue_index));
  if (!ml_type->IsTensorType()) {
    return AllocateTraditionalMLValue(&all_values_[mlvalue_index],
                                      static_cast<const NonTensorTypeBase*>(ml_type),
                                      parameters);
  }

  // tensors
  auto ml_data_type = static_cast<const TensorTypeBase*>(ml_type)->GetElementType();

  AllocKind alloc_kind = per_alloc_plan.alloc_kind;
  switch (alloc_kind) {
    // Right now for kAllocate and kAllocateOutput we are using same approach.
    // In the future we may want to have different way to handle it.
    case AllocKind::kAllocateOutput:
    case AllocKind::kAllocate: {
      ORT_RETURN_IF_ERROR(AllocateMLValueTensorSelfOwnBuffer(mlvalue_index,
                                                             ml_data_type,
                                                             alloc_info,
                                                             parameters.GetTensorShape(),
                                                             per_alloc_plan.create_fence_if_async));
      break;
    }
    case AllocKind::kReuse: {
      int reuse_mlvalue_index = per_alloc_plan.reused_buffer;
      ORT_RETURN_IF_ERROR(AllocateMLValueTensorPreAllocateBuffer(mlvalue_index,
                                                                 reuse_mlvalue_index,
                                                                 ml_data_type,
                                                                 alloc_info,
                                                                 parameters.GetTensorShape(),
                                                                 per_alloc_plan.create_fence_if_async));
      break;
    }
    default: {
      std::ostringstream ostr;
      ostr << "Invalid allocation kind: " << static_cast<std::underlying_type<AllocKind>::type>(alloc_kind);
      return Status(ONNXRUNTIME, FAIL, ostr.str());
    }
  }

  return Status::OK();
}

void ExecutionFrame::Init(const std::vector<int>& feed_mlvalue_idxs,
                          const std::vector<MLValue>& feeds,
                          const std::vector<int>& fetch_mlvalue_idxs,
                          std::vector<MLValue>& fetches,
                          const std::unordered_map<size_t, IExecutor::CustomAllocator>& fetch_allocators) {
  auto& mlvalue_idx_map = session_state_.GetMLValueNameIdxMap();

  // 1. resize the all_value_ vector
  all_values_.resize(mlvalue_idx_map.MaxIdx() + 1);

  // 2. Handle non-empty output vector
  if (!fetches.empty()) {
    // setup output_indices_, we don't want to generate mem plan on output tensors.
    auto num_fetches = fetch_mlvalue_idxs.size();

    for (size_t idx = 0; idx < num_fetches; ++idx) {
      int mlvalue_idx = fetch_mlvalue_idxs[idx];
      all_values_[mlvalue_idx] = fetches[idx];

      auto custom_alloc_entry = fetch_allocators.find(idx);
      if (custom_alloc_entry != fetch_allocators.cend()) {
        custom_allocators_[mlvalue_idx] = custom_alloc_entry->second;
      }
    }
  }

  // 3. handle the weights.
  // We do this after the fetches to handle an edge case (possibly dubious) where a Constant is an output.
  // The Constant gets lifted to an initializer so there's no Node producing the value as an output during Graph
  // execution (i.e. Graph execution won't write the value to all_values_).
  // A non-empty fetches vector will overwrite the actual weight in all_values_[mlvalue_idx] if we did this earlier.
  // This makes the ONNX Constant test (onnx\backend\test\data\node\test_constant) happy as that
  // involves a graph with a single Constant node.
  for (const auto& entry : session_state_.GetInitializedTensors()) {
    auto mlvalue_index = entry.first;
    all_values_[mlvalue_index] = entry.second;
  }

  // 4. handle feed in values. these can override initializer values so must be last
  for (size_t idx = 0, end = feed_mlvalue_idxs.size(); idx < end; ++idx) {
    int mlvalue_idx = feed_mlvalue_idxs[idx];
    // we are sharing the underline tensor/object for MLValue
    all_values_[mlvalue_idx] = feeds[idx];
  }
}

void ExecutionFrame::TraceFree(int mlvalue_idx) {
  // don't trace free on output tensors.
  if (planner_ &&
      std::find(fetch_mlvalue_idxs_.begin(), fetch_mlvalue_idxs_.end(), mlvalue_idx) == fetch_mlvalue_idxs_.end()) {
    const SequentialExecutionPlan* p_seq_exec_plan = session_state_.GetExecutionPlan();
    const auto& alloc_plan = p_seq_exec_plan->allocation_plan;
    const auto& per_alloc_plan = alloc_plan.at(mlvalue_idx);

    // only trace tensors
    auto ml_type = per_alloc_plan.value_type;
    if (ml_type->IsTensorType()) {
      // tensors
      auto ml_data_type = static_cast<const TensorTypeBase*>(ml_type)->GetElementType();
      // don't trace string tensors
      if (ml_data_type != DataTypeImpl::GetType<std::string>()) {
        auto status = planner_->TraceFree(mlvalue_idx);
        if (!status.IsOK()) {
          LOGS(session_state_.Logger(), WARNING) << "TraceFree for mlvalue_idx=" << mlvalue_idx
                                                 << " failed: " << status.ErrorMessage();
        }
      }
    }
  }
}

// generate memory pattern based on the tracing of memory allocation/free in current execution
// return error if the planner is not setup.
Status ExecutionFrame::GeneratePatterns(MemoryPatternGroup* out) const {
  if (!planner_) {
    return Status(ONNXRUNTIME, FAIL, "Memory pattern planner is not enabled on this execution framework.");
  }

  return planner_->GeneratePatterns(out);
}

int ExecutionFrame::GetNodeOffset(onnxruntime::NodeIndex node_index) const {
  return node_index_info_.GetNodeOffset(node_index);
}

// Return nullptr if index map to an value that is an unused optional input/output
const MLValue* ExecutionFrame::GetNodeInputOrOutputMLValue(int index) const {
  int mlvalue_idx = node_index_info_.GetMLValueIndex(index);
  return mlvalue_idx != NodeIndexInfo::kInvalidEntry ? &all_values_[mlvalue_idx] : nullptr;
}

// Return nullptr if index map to an value that is an unused optional input/output
MLValue* ExecutionFrame::GetMutableNodeInputOrOutputMLValue(int index) {
  return const_cast<MLValue*>(GetNodeInputOrOutputMLValue(index));
}

AllocatorPtr ExecutionFrame::GetAllocator(const OrtAllocatorInfo& info) {
  return utils::GetAllocator(session_state_, info);
}

static inline void VerifyShape(const MLValue* p_mlvalue,
                               const MLValueAllocationParameters& parameters) {
  if (p_mlvalue->IsTensor()) {
    const Tensor* tensor = &p_mlvalue->Get<Tensor>();

    ORT_ENFORCE(tensor->Shape() == parameters.GetTensorShape(),
                "MLValue shape verification failed. Current shape:", tensor->Shape(),
                " Requested shape:", parameters.GetTensorShape());
  }
}

// This method is not thread safe!
// Return S_OK and nullptr if index map to an value that is an unused optional input/output
Status ExecutionFrame::GetOrCreateNodeOutputMLValue(int index,
                                                    const MLValueAllocationParameters& parameters,
                                                    MLValue*& p_mlvalue) {
  int mlvalue_idx = node_index_info_.GetMLValueIndex(index);

  // return nullptr if it is optional
  if (mlvalue_idx == NodeIndexInfo::kInvalidEntry) {
    p_mlvalue = nullptr;
    return Status::OK();
  }

  p_mlvalue = &all_values_.at(mlvalue_idx);

  if (p_mlvalue->IsAllocated()) {
    // The ml has already been allocated.
    // Now only tensor need to be check.
    VerifyShape(p_mlvalue, parameters);  // TODO find a better way to do this
    return Status::OK();
  }

  // It's not allocated, then allocate it with given shape and return.
  // Perform allocation based on the allocation plan
  ORT_RETURN_IF_ERROR(AllocateAsPerAllocationPlan(mlvalue_idx, parameters));
  return Status::OK();
}

Status ExecutionFrame::GetOutputs(std::vector<MLValue>& fetches) {
  auto num_fetches = fetch_mlvalue_idxs_.size();

  if (fetches.empty()) {
    fetches.resize(num_fetches);
  } else {
    // if there's a mismatch things are out so sync so fail
    if (fetches.size() != num_fetches) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Fetches vector passed to GetOutputs contains ", fetches.size(),
                             " entries which doesn't match the number of fetches the frame was initialized with of ",
                             num_fetches);
    }
  }

  for (size_t idx = 0; idx < num_fetches; ++idx) {
    fetches[idx] = GetMLValue(fetch_mlvalue_idxs_[idx]);
  }

  return Status::OK();
}

Status ExecutionFrame::ReleaseMLValue(int mlvalue_idx) {
  if (mlvalue_idx == NodeIndexInfo::kInvalidEntry || static_cast<size_t>(mlvalue_idx) >= all_values_.size()) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "invalid index ", mlvalue_idx);
  }
  all_values_[mlvalue_idx] = MLValue();
  TraceFree(mlvalue_idx);
  return Status::OK();
}

const SequentialExecutionPlan::AllocPlanPerValue& ExecutionFrame::GetAllocationPlan(int mlvalue_idx) {
  const SequentialExecutionPlan* p_seq_exec_plan = session_state_.GetExecutionPlan();
  const auto& alloc_plan = p_seq_exec_plan->allocation_plan;
  ORT_ENFORCE(mlvalue_idx != NodeIndexInfo::kInvalidEntry && mlvalue_idx < alloc_plan.size());
  return alloc_plan[mlvalue_idx];
}
}  // namespace onnxruntime
