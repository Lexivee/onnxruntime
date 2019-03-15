// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

// there's no way to use a raw pointer as the copy destination with std::copy_n
// (which gsl::copy uses with span::data() which returns a raw pointer) with the 14.11 toolset
// without generating a 4996 warning. going through an iterator is way too much overhead so turn off the warning.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include "core/providers/cpu/controlflow/scan.h"
#include "core/providers/cpu/controlflow/scan_utils.h"
#include "core/providers/cpu/controlflow/utils.h"

#include "core/framework/framework_common.h"
#include "core/framework/op_kernel_context_internal.h"
#include "core/framework/session_state.h"
#include "core/framework/tensorprotoutils.h"

#include "core/providers/cpu/tensor/utils.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

using namespace ONNX_NAMESPACE;
using namespace onnxruntime::common;
using namespace onnxruntime::scan::detail;

namespace onnxruntime {

/*
ONNX_OPERATOR_SET_SCHEMA(
    Scan,
    8,
    OpSchema()
    .SetDoc(scan_ver1_doc)
    .Input(
        0,
        "sequence_lens",
        "Optional tensor specifying lengths of the sequences in a batch. "
        "If this input is not specified, all sequences are assumed to be of "
        "the maximum sequence length (the dimension of the sequence axis of "
        "the scan_input tensors).",
        "I",
        OpSchema::Optional)
    .Input(
        1,
        "initial_state_and_scan_inputs",
        "Initial values of the loop's N state variables followed by M scan_inputs",
        "V",
        OpSchema::Variadic)
    .Output(
        0,
        "final_state_and_scan_outputs",
        "Final values of the loop's N state variables followed by K scan_outputs",
        "V",
        OpSchema::Variadic)
    .Attr(
        "body",
        "The graph run each iteration. It has N+M inputs: "
        "(loop state variables..., scan_input_elts...). It has N+K outputs: "
        "(loop state variables..., scan_output_elts...). Each "
        "scan_output is created by concatenating the value of the specified "
        "scan_output_elt value at the end of each iteration of the loop. It is an error"
        " if the dimensions of these values change across loop iterations.",
        AttributeProto::GRAPH,
        true)
    .Attr(
        "num_scan_inputs",
        "An attribute specifying the number of scan_inputs M. ",
        AttributeProto::INT,
        true)
    .Attr(
        "directions",
        "An optional list of M flags. The i-th element of the list specifies the direction "
        "to be scanned for the i-th scan_input tensor: 0 indicates forward direction and 1 "
        "indicates reverse direction. "
        "If omitted, all scan_input tensors will be scanned in the forward direction.",
        AttributeProto::INTS,
        false)
    .TypeConstraint("I", { "tensor(int64)" }, "Int64 tensor")
    .TypeConstraint("V", OpSchema::all_tensor_types(), "All Tensor types"));
*/

class Scan8Impl {
 public:
  Scan8Impl(OpKernelContextInternal& context,
            const SessionState& session_state,
            int64_t num_scan_inputs,
            const std::vector<int64_t>& directions);

  // Initialize by validating all the inputs, and allocating the output tensors
  Status Initialize();

  Status CreateFeedsFetchesManager(std::unique_ptr<FeedsFetchesManager>& ffm);

  // Execute the batch, by iterating the sequence in each batch entry
  // and calling the subgraph with each item in the sequence.
  Status Execute(FeedsFetchesManager* ffm, const FeedsFetchesManager* cached_ffm);

 private:
  // validate inputs and setup batch size and max sequence length.
  Status ValidateInput();
  Status ValidateSubgraphInput(int start_input, int end_input, bool is_loop_state_var,
                               const std::vector<const NodeArg*>& graph_inputs);

  Status AllocateOutputTensors();
  Status CreateLoopStateVariables(std::vector<std::vector<LoopStateVariable>>& batch_loop_state_variables,
                                  const FeedsFetchesManager& ffm);

  using ConstTensorSlicerIterators = std::vector<MLValueTensorSlicer<const MLValue>::Iterator>;
  using MutableTensorSlicerIterators = std::vector<MLValueTensorSlicer<MLValue>::Iterator>;

  OpKernelContextInternal& context_;
  const SessionState& session_state_;
  const GraphViewer& subgraph_;

  int num_loop_state_variables_;
  int num_variadic_inputs_;
  int num_variadic_outputs_;

  int64_t batch_size_ = -1;
  int64_t max_sequence_len_ = -1;

  const std::vector<int64_t>& directions_;
  const Tensor* sequence_lens_tensor_;
  std::vector<int64_t> sequence_lens_;

  std::vector<std::string> subgraph_output_names_;
  std::vector<std::unique_ptr<OutputIterator>> output_iterators_;

  std::unordered_map<std::string, const MLValue*> implicit_inputs_;
};

template <>
Scan<8>::Scan(const OpKernelInfo& info) : OpKernel(info) {
  // make sure the attribute was present even though we don't need it here.
  // The GraphProto is loaded as a Graph instance by main Graph::Resolve,
  // and a SessionState instance for executing the subgraph is created by InferenceSession.
  // This is available via Info().GetSubgraphSessionState("attribute_name") when Compute is called.
  ONNX_NAMESPACE::GraphProto proto;
  ORT_ENFORCE(info.GetAttr<ONNX_NAMESPACE::GraphProto>("body", &proto).IsOK());
  (void)proto;

  ORT_ENFORCE(info.GetAttr<int64_t>("num_scan_inputs", &num_scan_inputs_).IsOK());

  ReadDirections(info, "directions", input_directions_, num_scan_inputs_);
}

template <>
Status Scan<8>::Compute(OpKernelContext* ctx) const {
  auto ctx_internal = static_cast<OpKernelContextInternal*>(ctx);
  auto* session_state = ctx_internal->SubgraphSessionState("body");
  ORT_ENFORCE(session_state, "Subgraph SessionState was not found for 'body' attribute.");

  // TODO:
  //       Consider how usage of ExecutionFrame and SequentialExecutor can be optimized
  //         - initial implementation is focused on making it work, rather than optimizing.

  Scan8Impl scan_impl{*ctx_internal, *session_state, num_scan_inputs_, input_directions_};

  auto status = scan_impl.Initialize();
  ORT_RETURN_IF_ERROR(status);

  // create FeedsFetchesManager if needed and call ScanImpl::Execute
  status = controlflow::detail::SubgraphExecuteHelper(cached_feeds_fetches_manager_, scan_impl);

  return status;
}

Scan8Impl::Scan8Impl(OpKernelContextInternal& context,
                     const SessionState& session_state,
                     int64_t num_scan_inputs,
                     const std::vector<int64_t>& directions)
    : context_{context},
      session_state_{session_state},
      subgraph_{*session_state.GetGraphViewer()},
      directions_{directions},
      implicit_inputs_{context_.GetImplicitInputs()} {
  // optional first input so may be nullptr
  sequence_lens_tensor_ = context.Input<Tensor>(0);

  num_variadic_inputs_ = context_.NumVariadicInputs(1);
  num_variadic_outputs_ = context_.OutputCount();

  num_loop_state_variables_ = num_variadic_inputs_ - gsl::narrow_cast<int>(num_scan_inputs);
}

Status Scan8Impl::Initialize() {
  auto status = ValidateInput();
  ORT_RETURN_IF_ERROR(status);

  auto& subgraph_outputs = subgraph_.GetOutputs();
  subgraph_output_names_.reserve(subgraph_outputs.size());

  // save list of subgraph output names in their provided order to use when fetching the results
  // from each subgraph execution. the Scan outputs will match this order.
  for (auto& output : subgraph_outputs) {
    subgraph_output_names_.push_back(output->Name());
  }

  status = AllocateOutputTensors();
  ORT_RETURN_IF_ERROR(status);

  return Status::OK();
}

// get the Scan input that is used in a call to the subgraph as a Tensor,
// skipping over the optional arg to the Scan operator
static const Tensor& GetSubgraphInputTensor(const OpKernelContext& context, int index) {
  // skip the optional sequence_lens input
  return *context.Input<Tensor>(index + 1);
}

// get the Scan input that is used in a call to the subgraph as an MLValue,
// skipping over the optional arg to the Scan operator
static const MLValue& GetSubgraphInputMLValue(const OpKernelContextInternal& context, int index) {
  // skip the optional sequence_lens input
  return *context.GetInputMLValue(index + 1);
}

// Validate that the subgraph input has valid shapes
Status Scan8Impl::ValidateSubgraphInput(int start_input, int end_input, bool is_loop_state_var,
                                        const std::vector<const NodeArg*>& graph_inputs) {
  // first dim is batch size. optional sequence dim. dim/s for the data.
  // if there is no dim for the data treat it as a scalar.
  bool has_seq_len_dim = !is_loop_state_var;
  auto min_dims_required = has_seq_len_dim ? 2 : 1;

  for (int i = start_input; i < end_input; ++i) {
    auto& input_tensor = GetSubgraphInputTensor(context_, i);
    const auto& input_shape = input_tensor.Shape();

    if (input_shape.NumDimensions() < static_cast<size_t>(min_dims_required))
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Invalid scan input:", graph_inputs[i]->Name(),
                             " Expected ", min_dims_required,
                             " dimensions or more but input had shape of ", input_shape);

    auto this_batch_size = input_shape[0];

    if (batch_size_ < 0)
      batch_size_ = this_batch_size;
    else {
      if (batch_size_ != this_batch_size) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Scan inputs have inconsistent batch size. Previous value was ",
                               batch_size_, " but ", graph_inputs[i]->Name(), " has batch size of ",
                               this_batch_size);
      }
    }

    if (has_seq_len_dim) {
      auto this_seq_len = input_shape[1];

      if (max_sequence_len_ < 0) {
        max_sequence_len_ = this_seq_len;
      } else {
        if (max_sequence_len_ != this_seq_len) {
          return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Scan inputs have inconsistent sequence lengths. Previous value was ",
                                 max_sequence_len_, " but ", graph_inputs[i]->Name(),
                                 " has length of ", this_seq_len);
        }
      }
    }
  }

  return Status::OK();
}

Status Scan8Impl::ValidateInput() {
  auto& graph_inputs = subgraph_.GetInputs();
  auto num_graph_inputs = graph_inputs.size();

  if (num_graph_inputs != static_cast<size_t>(num_variadic_inputs_)) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "The subgraph in 'body' expects ", num_graph_inputs,
                           " inputs but Scan was only given ", num_variadic_inputs_);
  }

  // process any loop state variables, which will set the batch size
  auto status = ValidateSubgraphInput(0, num_loop_state_variables_, true, graph_inputs);
  ORT_RETURN_IF_ERROR(status);

  // process the scan inputs. sets/validates batch size and sequence length
  status = ValidateSubgraphInput(num_loop_state_variables_, num_variadic_inputs_, false, graph_inputs);
  ORT_RETURN_IF_ERROR(status);

  if (sequence_lens_tensor_ != nullptr) {
    auto num_entries = sequence_lens_tensor_->Shape().Size();

    if (num_entries != batch_size_) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "sequence_lens length of ", num_entries,
                             " did not match batch size of ", batch_size_);
    }

    auto d = sequence_lens_tensor_->DataAsSpan<int64_t>();
    sequence_lens_.assign(d.cbegin(), d.cend());

    if (std::all_of(sequence_lens_.cbegin(), sequence_lens_.cend(),
                    [this](int64_t value) { return value > 0 && value <= max_sequence_len_; }) == false) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT,
                             "Invalid entries in sequence_lens. Max sequence length was ", max_sequence_len_);
    }

  } else {
    sequence_lens_ = std::vector<int64_t>(batch_size_, max_sequence_len_);
  }

  return Status::OK();
}

Status Scan8Impl::AllocateOutputTensors() {
  Status status = Status::OK();
  auto& graph_outputs = subgraph_.GetOutputs();

  if (graph_outputs.size() != static_cast<size_t>(num_variadic_outputs_)) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Subgraph in 'body' produces ", graph_outputs.size(),
                           " outputs but Scan expects ", num_variadic_outputs_);
  }

  std::unique_ptr<OutputIterator> output_iter;

  for (int i = 0; i < num_loop_state_variables_; ++i) {
    status = AllocateOutput(context_, subgraph_, i, true, batch_size_, max_sequence_len_, output_iter);
    ORT_RETURN_IF_ERROR(status);
    output_iterators_.push_back(std::move(output_iter));
  }

  for (int i = num_loop_state_variables_, end = num_variadic_outputs_; i < end; ++i) {
    status = AllocateOutput(context_, subgraph_, i, false, batch_size_, max_sequence_len_, output_iter);
    ORT_RETURN_IF_ERROR(status);
    output_iterators_.push_back(std::move(output_iter));
  }

  return Status::OK();
}

// setup the loop state variables for each batch item
Status Scan8Impl::CreateLoopStateVariables(std::vector<std::vector<LoopStateVariable>>& batch_loop_state_variables,
                                           const FeedsFetchesManager& ffm) {
  // Setup loop state variables
  // 1. Slice the input/output loop state variable tensors provided to Scan into the per-batch-item chunks
  //    (slice on the first dimension which is the batch size).
  // 2. For each batch item, create the LoopStateVariable instances that can be used to pass state between
  //    each iteration of the subgraph. This minimizes copying of data during each iteration.

  std::vector<MLValueTensorSlicer<const MLValue>::Iterator> loop_state_input_iterators;
  loop_state_input_iterators.reserve(num_loop_state_variables_);

  // create the input and output slice iterator for each loop state variable.
  for (int i = 0; i < num_loop_state_variables_; ++i) {
    const MLValue& mlvalue = GetSubgraphInputMLValue(context_, i);
    MLValue* p_mlvalue = context_.GetOutputMLValue(i);

    ORT_ENFORCE(p_mlvalue, "Output MLValue has not been created for loop state variable output ", i);

    loop_state_input_iterators.push_back(MLValueTensorSlicer<const MLValue>::Create(mlvalue).begin());
  }

  batch_loop_state_variables.clear();
  batch_loop_state_variables.resize(batch_size_);

  AllocatorPtr alloc;
  auto status = context_.GetTempSpaceAllocator(&alloc);
  ORT_RETURN_IF_ERROR(status);

  const auto& ffi = ffm.GetFeedsFetchesInfo();
  const auto& allocation_plan = session_state_.GetExecutionPlan()->allocation_plan;

  // setup the loop state variables for each batch row
  for (int64_t b = 0; b < batch_size_; ++b) {
    std::vector<LoopStateVariable>& variables = batch_loop_state_variables[b];
    variables.reserve(num_loop_state_variables_);

    for (int i = 0; i < num_loop_state_variables_; ++i) {
      auto& input_iter = loop_state_input_iterators[i];
      auto& output_iter = *output_iterators_[i];

      // if the output is a copy of a pre-existing value we can avoid a data copy until the final iteration
      // by copying at the MLValue level (shared_ptr copy).
      auto fetch_mlvalue_idx = ffi.fetches_mlvalue_idxs[i];
      bool isCopyOfPreExistingValue = allocation_plan[fetch_mlvalue_idx].alloc_kind == AllocKind::kShare;

      variables.push_back(LoopStateVariable(*input_iter, *output_iter, sequence_lens_[b], alloc,
                                            isCopyOfPreExistingValue));

      ++input_iter;
      ++output_iter;
    }
  }

  return status;
}

Status Scan8Impl::CreateFeedsFetchesManager(std::unique_ptr<FeedsFetchesManager>& ffm) {
  return scan::detail::CreateFeedsFetchesManager(subgraph_, num_variadic_inputs_, implicit_inputs_,
                                                 subgraph_output_names_, session_state_.GetMLValueNameIdxMap(),
                                                 ffm);
}

Status Scan8Impl::Execute(FeedsFetchesManager* ffm, const FeedsFetchesManager* cached_ffm) {
  Status status = Status::OK();

  // for each batch item, std::vector of LoopStateVariables
  std::vector<std::vector<LoopStateVariable>> batch_loop_state_variables;
  status = CreateLoopStateVariables(batch_loop_state_variables, ffm ? *ffm : *cached_ffm);
  ORT_RETURN_IF_ERROR(status);

  for (int64_t b = 0; b < batch_size_; ++b) {
    auto sequence_len = sequence_lens_[b];

    // Setup input MLValue streams
    std::vector<MLValueTensorSlicer<const MLValue>::Iterator> scan_input_stream_iterators;
    scan_input_stream_iterators.reserve(num_variadic_inputs_ - num_loop_state_variables_);

    for (int i = num_loop_state_variables_, end = num_variadic_inputs_; i < end; ++i) {
      const auto& mlvalue = GetSubgraphInputMLValue(context_, i);

      // forward
      if (directions_[i - num_loop_state_variables_] == static_cast<int64_t>(ScanDirection::kForward)) {
        // the iterator is self contained, so we don't need to keep the MLValueTensorSlicer instance around
        scan_input_stream_iterators.push_back(MLValueTensorSlicer<const MLValue>::Create(mlvalue, 1, b).begin());
      } else {  // reverse
        scan_input_stream_iterators.push_back(MLValueTensorSlicer<const MLValue>::Create(mlvalue, 1, b).rbegin());
        // need to skip past the empty entries at the end of the input if sequence length is short
        auto offset = max_sequence_len_ - sequence_len;
        if (offset > 0) {
          // reverse iterator so += moves backwards through the input
          scan_input_stream_iterators.back() += offset;
        }
      }
    }

    // Call the subgraph for each item in the sequence
    status = IterateSequence(context_, session_state_, batch_loop_state_variables[b], scan_input_stream_iterators,
                             sequence_len, num_loop_state_variables_, num_variadic_inputs_, num_variadic_outputs_,
                             implicit_inputs_, output_iterators_, ffm, cached_ffm);

    // use the cached info from now on
    if (ffm) {
      cached_ffm = ffm;
      ffm = nullptr;
    }

    // zero out any remaining values in the sequence
    for (int64_t i = sequence_len; i < max_sequence_len_; ++i) {
      for (int output = num_loop_state_variables_; output < num_variadic_outputs_; ++output) {
        auto& iterator = *output_iterators_[output];
        iterator.ZeroOutCurrent();
        ++iterator;
      }
    }

    ORT_RETURN_IF_ERROR(status);
  }

  return status;
}

ONNX_CPU_OPERATOR_VERSIONED_KERNEL(Scan,
                                   8, 8,
                                   KernelDefBuilder()
                                       .TypeConstraint("I", DataTypeImpl::GetTensorType<int64_t>())
                                       .TypeConstraint("V", DataTypeImpl::AllTensorTypes()),
                                   Scan<8>);

}  // namespace onnxruntime
