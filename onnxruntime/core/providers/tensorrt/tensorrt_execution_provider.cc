// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "tensorrt_execution_provider.h"
#include "tensorrt_allocator.h"
#include "core/framework/execution_provider.h"
#include "core/framework/op_kernel.h"
#include "core/framework/kernel_registry.h"
#include "core/framework/compute_capability.h"
#include "core/providers/cpu/cpu_execution_provider.h"
#include "core/platform/env.h"
#include "onnx/shape_inference/implementation.h"
#include "cuda_runtime_api.h"
#include "gsl/pointers"
#include "core/graph/model.h"
#include "cuda_runtime_api.h"

using namespace std;
using namespace ONNX_NAMESPACE;
using namespace ::onnxruntime::logging;

namespace onnxruntime {

#define CHECK_CUDA(call)         \
  do {                           \
    cudaError_t status = call;   \
    if (status != cudaSuccess) { \
      return -1;                 \
    }                            \
  } while (0)

TensorrtExecutionProvider::TensorrtExecutionProvider()
    : IExecutionProvider{onnxruntime::kTensorrtExecutionProvider} {
  DeviceAllocatorRegistrationInfo trt_device_info({OrtMemTypeCPU, [](int) {
                                                     return std::make_unique<TensorrtPinnedAllocator>();
                                                   },
                                                   std::numeric_limits<size_t>::max()});
  InsertAllocator(CreateAllocator(trt_device_info));
  DeviceAllocatorRegistrationInfo default_device_info({OrtMemTypeDefault, [](int) {
                                                         return std::make_unique<TensorrtAllocator>();
                                                       },
                                                       std::numeric_limits<size_t>::max()});
  InsertAllocator(CreateAllocator(default_device_info));
}

TensorrtExecutionProvider::~TensorrtExecutionProvider() {}

std::unique_ptr<IndexedSubGraph> TensorrtExecutionProvider::GetSubGraph(SubGraph_t graph_nodes_index, int& kernels_index, const onnxruntime::GraphViewer& graph) const {
  const std::vector<NodeIndex>& node_index = graph.GetNodesInTopologicalOrder();
  std::unordered_set<size_t> node_set;
  node_set.reserve(graph_nodes_index.first.size());
  for (const auto& index : graph_nodes_index.first) {
    node_set.insert(node_index[index]);
  }
  std::unique_ptr<IndexedSubGraph> sub_graph = std::make_unique<IndexedSubGraph>();

  // Find inputs and outputs of the subgraph
  std::unordered_map<const NodeArg *, int> fused_inputs, fused_outputs, fused_outputs_to_add;
  std::unordered_set<const NodeArg*> erased;
  int input_order = 0;
  int output_order = 0;

  for (const auto& index : graph_nodes_index.first) {
    sub_graph->nodes.push_back(node_index[index]);
    const auto& node = graph.GetNode(node_index[index]);
    for (const auto& input : node->InputDefs()) {
      const auto& it = fused_outputs.find(input);
      if (it != fused_outputs.end()) {
        fused_outputs.erase(it);
        erased.insert(input);
      }
      //only when input is neither in output list nor erased list, add the input to input list
      else if (erased.find(input) == erased.end()) {
        fused_inputs[input] = input_order++;
      }
    }

    // For output searching, there is a special case:
    // If node's OutputEdges are more than its outputs, meaning certain output is used more than once,
    // if the output is connected to nodes that don't belong to the subgraph, the output need to be added
    // to the output list
    if (node->GetOutputEdgesCount() > node->OutputDefs().size()) {
      for (auto it = node->OutputEdgesBegin(), end = node->OutputEdgesEnd(); it != end; ++it) {
        const auto& node_idx = it->GetNode().Index();
        const auto& output = (it->GetNode()).InputDefs()[it->GetDstArgIndex()];
        if (node_set.find(node_idx) != node_set.end()) {
          const auto& iter = fused_inputs.find(output);
          if (iter != fused_inputs.end()) {
            fused_inputs.erase(iter);
            erased.insert(output);
          } else if (erased.find(output) == erased.end()) {
            fused_outputs[output] = output_order++;
          }
        } else {
          fused_outputs_to_add[output] = output_order++;
        }
      }
    } else {
      for (const auto& output : node->OutputDefs()) {
        const auto& it = fused_inputs.find(output);
        if (it != fused_inputs.end()) {
          fused_inputs.erase(it);
          erased.insert(output);
        }
        // only when output is neither in input list nor erased list, add the output to output list
        else if (erased.find(output) == erased.end()) {
          fused_outputs[output] = output_order++;
        }
      }
    }
  }

  fused_outputs.insert(fused_outputs_to_add.begin(), fused_outputs_to_add.end());

  // Sort inputs and outputs by the order they were added
  std::multimap<int, const NodeArg *> inputs, outputs;
  for (auto it = fused_inputs.begin(), end = fused_inputs.end(); it != end; ++it) {
    inputs.insert(std::pair<int, const NodeArg*>(it->second, it->first));
  }

  for (auto it = fused_outputs.begin(), end = fused_outputs.end(); it != end; ++it) {
    outputs.insert(std::pair<int, const NodeArg*>(it->second, it->first));
  }

  // Assign inputs and outputs to subgraph's meta_def
  auto meta_def = std::make_unique<::onnxruntime::IndexedSubGraph::MetaDef>();
  meta_def->name = "TRTKernel_" + std::to_string(kernels_index++);
  meta_def->domain = kMSDomain;

  for (const auto& input : inputs) {
    meta_def->inputs.push_back(input.second->Name());
  }

  for (const auto& output : outputs) {
    meta_def->outputs.push_back(output.second->Name());
  }

  meta_def->since_version = 1;
  sub_graph->SetMetaDef(meta_def);

  return sub_graph;
}

SubGraphCollection_t TensorrtExecutionProvider::GetSupportedList(SubGraphCollection_t nodes_vector_input, int iterations, const int& max_iterations,
                                                                 bool& early_termination, const onnxruntime::GraphViewer& graph) const {
  SubGraphCollection_t nodes_list_output;
  if (iterations > max_iterations) {
    early_termination = true;
    return nodes_list_output;
  }

  iterations++;
  const std::vector<NodeIndex>& node_index = graph.GetNodesInTopologicalOrder();
  int counter = 0;
  for (const auto& group : nodes_vector_input) {
    //construct subgraph
    if (!group.first.empty()) {
      std::unique_ptr<IndexedSubGraph> sub_graph = GetSubGraph(group, counter, graph);

      if (group.second) {
        nodes_list_output.push_back(group);
      } else {
        onnxruntime::Model model_build(graph.Name(), true, ModelMetaData(), IOnnxRuntimeOpSchemaRegistryList(), graph.DomainToVersionMap());
        onnxruntime::Graph& graph_build = model_build.MainGraph();

        //Add node and node args
        for (const auto& index : group.first) {
          const auto& node = graph.GetNode(node_index[index]);
          std::vector<onnxruntime::NodeArg *> inputs, outputs;
          for (auto input : node->InputDefs()) {
            auto& n_input = graph_build.GetOrCreateNodeArg(input->Name(), input->TypeAsProto());
            inputs.push_back(&n_input);
          }
          for (auto output : node->OutputDefs()) {
            auto& n_output = graph_build.GetOrCreateNodeArg(output->Name(), output->TypeAsProto());
            outputs.push_back(&n_output);
          }
          graph_build.AddNode(node->Name(), node->OpType(), node->Description(), inputs, outputs, &node->GetAttributes(), node->Domain());
        }

        for (const auto& input : sub_graph->GetMetaDef()->inputs) {
          const ONNX_NAMESPACE::TensorProto* initializer = nullptr;
          if (graph.GetInitializedTensor(input, initializer)) {
            graph_build.AddInitializedTensor(*initializer);
          }
        }
        ORT_ENFORCE(graph_build.Resolve().IsOK());

        // Serialize modelproto to string
        ONNX_NAMESPACE::ModelProto model_proto = model_build.ToProto();
        string string_buf;
        model_proto.SerializeToString(&string_buf);

        // Get supported node list
        SubGraphCollection_t parser_nodes_list;
        TensorrtLogger trt_logger(nvinfer1::ILogger::Severity::kWARNING);
        auto trt_builder = unique_pointer<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(trt_logger));
        auto trt_network = unique_pointer<nvinfer1::INetworkDefinition>(trt_builder->createNetwork());
        auto trt_parser = unique_pointer<nvonnxparser::IParser>(nvonnxparser::createParser(*trt_network, trt_logger));
        trt_parser->supportsModel(string_buf.data(), string_buf.size(), parser_nodes_list);

        SubGraphCollection_t next_nodes_list;
        const onnxruntime::GraphViewer graph_viewer(graph_build);
        next_nodes_list = GetSupportedList(parser_nodes_list, iterations, max_iterations, early_termination, graph_viewer);
        for (int i = 0, end = next_nodes_list.size(); i < end; ++i) {
          for (int j = 0, end = next_nodes_list[i].first.size(); j < end; ++j) {
            next_nodes_list[i].first[j] = group.first[next_nodes_list[i].first[j]];
          }
          nodes_list_output.push_back(next_nodes_list[i]);
        }
      }
    }
  }
  return nodes_list_output;
}

std::vector<std::unique_ptr<ComputeCapability>>
TensorrtExecutionProvider::GetCapability(const onnxruntime::GraphViewer& graph,
                                         const std::vector<const KernelRegistry*>& /*kernel_registries*/) const {
  // Construct modelproto from graph
  onnxruntime::Model model(graph.Name(), true, ModelMetaData(), IOnnxRuntimeOpSchemaRegistryList(), graph.DomainToVersionMap());
  onnxruntime::Graph& graph_build = model.MainGraph();
  for (const auto& node : graph.Nodes()) {
    std::vector<onnxruntime::NodeArg *> inputs, outputs;
    for (auto input : node.InputDefs()) {
      auto& n_input = graph_build.GetOrCreateNodeArg(input->Name(), input->TypeAsProto());
      inputs.push_back(&n_input);
    }
    for (auto output : node.OutputDefs()) {
      auto& n_output = graph_build.GetOrCreateNodeArg(output->Name(), output->TypeAsProto());
      outputs.push_back(&n_output);
    }
    graph_build.AddNode(node.Name(), node.OpType(), node.Description(), inputs, outputs, &node.GetAttributes(), node.Domain());
  }

  //Add initializer to graph
  const auto& init_tensors = graph.GetAllInitializedTensors();
  for (const auto& tensor : init_tensors) {
    graph_build.AddInitializedTensor(*(tensor.second));
  }

  ORT_ENFORCE(graph_build.Resolve().IsOK());
  ONNX_NAMESPACE::ModelProto model_proto = model.ToProto();
  model_proto.set_ir_version(ONNX_NAMESPACE::Version::IR_VERSION);

  // Serialize modelproto to string
  string string_buf;
  model_proto.SerializeToString(&string_buf);

  // Get supported node list
  SubGraphCollection_t parser_nodes_vector;
  TensorrtLogger trt_logger(nvinfer1::ILogger::Severity::kWARNING);
  auto trt_builder = unique_pointer<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(trt_logger));
  auto trt_network = unique_pointer<nvinfer1::INetworkDefinition>(trt_builder->createNetwork());
  auto trt_parser = unique_pointer<nvonnxparser::IParser>(nvonnxparser::createParser(*trt_network, trt_logger));
  trt_parser->supportsModel(string_buf.data(), string_buf.size(), parser_nodes_vector);

  SubGraphCollection_t supported_nodes_vector;
  const char* batch_env = getenv("ORT_TENSORRT_MAX_PARSER_ITERATIONS");
  const int max_iterations = batch_env ? atoi(batch_env) : max_parser_iterations_;
  bool early_termination = false;
  supported_nodes_vector = GetSupportedList(parser_nodes_vector, 0, max_iterations, early_termination, graph);
  if (early_termination) {
    supported_nodes_vector.clear();
  }

  // Construct subgraph capability from node list
  std::vector<std::unique_ptr<ComputeCapability>> result;
  int counter = 0;
  for (const auto& group : supported_nodes_vector) {
    if (!group.first.empty()) {
      std::unique_ptr<IndexedSubGraph> sub_graph = GetSubGraph(group, counter, graph);
      result.push_back(std::make_unique<ComputeCapability>(std::move(sub_graph)));
    }
  }

  return result;
}

std::shared_ptr<KernelRegistry> TensorrtExecutionProvider::GetKernelRegistry() const {
  static std::shared_ptr<KernelRegistry> kernel_registry = std::make_shared<KernelRegistry>();
  return kernel_registry;
}

common::Status TensorrtExecutionProvider::CopyTensor(const Tensor& src, Tensor& dst) const {
  ORT_UNUSED_PARAMETER(src);
  ORT_UNUSED_PARAMETER(dst);
  return Status::OK();
}

common::Status TensorrtExecutionProvider::Compile(const std::vector<onnxruntime::Node*>& fused_nodes,
                                                  std::vector<NodeComputeInfo>& node_compute_funcs) {
  for (const auto* fused_node : fused_nodes) {
    std::vector<int> input_indexes;
    std::vector<int> input_dim_sizes;
    std::vector<int> output_indexes;
    std::vector<int> output_dim_sizes;
    std::vector<std::vector<int64_t>> output_shapes;
    std::vector<int> output_types;

    // Build map from input name to its index in input definitions
    std::unordered_map<std::string, int> input_map;
    const auto& input_defs = fused_node->InputDefs();
    input_map.reserve(input_defs.size());
    for (int i = 0, end = input_defs.size(); i < end; ++i) {
      input_map[input_defs[i]->Name()] = i;
    }

    // Build map from output name to its index in output definitions
    std::unordered_map<std::string, int> output_map;
    const auto& output_defs = fused_node->OutputDefs();
    output_map.reserve(output_defs.size());
    for (int i = 0, end = output_defs.size(); i < end; ++i) {
      output_map[output_defs[i]->Name()] = i;
    }

    // Reconstruct graph from fused node's function body
    const auto* func_body = fused_node->GetFunctionBody();
    if (!func_body) {
      return common::Status(common::ONNXRUNTIME, common::INVALID_ARGUMENT, "Function body is empty");
    }
    const Graph& graph_body = func_body->Body();
    onnxruntime::Model model(graph_body.Name(), true, ModelMetaData(), IOnnxRuntimeOpSchemaRegistryList(), graph_body.DomainToVersionMap());
    onnxruntime::Graph& graph = model.MainGraph();

    for (const auto& graph_body_node : graph_body.Nodes()) {
      graph.AddNode(graph_body_node);
    }

    ORT_ENFORCE(graph.Resolve().IsOK());

    // Add initializer to graph
    const auto& init_tensors = graph_body.GetAllInitializedTensors();
    for (const auto& tensor : init_tensors) {
      graph.AddInitializedTensor(*(tensor.second));
    }

    // Add fused node's outputs to graph's outputs if the outputs are not included yet
    // for the case that node's output is connected to more than one EdgeEnd nodes and some of them don't belong to the graph
    ONNX_NAMESPACE::ModelProto model_proto = model.ToProto();
    const auto& graph_output = model_proto.graph().output();
    std::unordered_set<string> graph_outputs_set;
    graph_outputs_set.reserve(graph_output.size());
    for (int i = 0, end = graph_output.size(); i < end; ++i) {
      graph_outputs_set.insert(graph_output[i].name());
    }

    const auto& graph_value_info = model_proto.graph().value_info();
    std::vector<int> output_to_add;
    std::vector<int> location;
    int num_defs = output_defs.size();
    for (int i = num_defs - 1; i >= 0; --i) {
      const std::string& output_name = output_defs[i]->Name();
      if (graph_outputs_set.find(output_name) == graph_outputs_set.end()) {
        for (int j = 0, end = graph_value_info.size(); j < end; ++j) {
          if (output_name == graph_value_info[j].name()) {
            output_to_add.push_back(j);
            location.push_back(num_defs - 1 - i);
          }
        }
      }
    }

    // Add outputs and move them to the right places
    auto* mutable_output = model_proto.mutable_graph()->mutable_output();
    for (int i = 0, end = output_to_add.size(); i < end; ++i) {
      *(mutable_output->Add()) = graph_value_info[output_to_add[i]];
      int start_index = (*mutable_output).size() - 1;
      int end_index = start_index - location[i];
      for (int j = start_index; j > end_index; --j) {
        mutable_output->SwapElements(j, j - 1);
      }
    }

    // Serialize model proto
    model_proto.set_ir_version(ONNX_NAMESPACE::Version::IR_VERSION);
    string string_buf;
    model_proto.SerializeToString(&string_buf);

    // Create TensorRT engine
    TensorrtLogger trt_logger(nvinfer1::ILogger::Severity::kWARNING);
    auto trt_builder = unique_pointer<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(trt_logger));
    auto trt_network = unique_pointer<nvinfer1::INetworkDefinition>(trt_builder->createNetwork());
    auto trt_parser = unique_pointer<nvonnxparser::IParser>(nvonnxparser::createParser(*trt_network, trt_logger));
    trt_parser->parse(string_buf.data(), string_buf.size());

    const char* batch_env = getenv("ORT_TENSORRT_MAX_BATCH_SIZE");
    if (batch_env) {
      const int max_batch_size = atoi(batch_env);
      SetMaxBatchSize(max_batch_size);
    }

    const char* workspace_env = getenv("ORT_TENSORRT_MAX_WORKSPACE_SIZE");
    if (workspace_env) {
      const size_t max_workspace_size = atoi(workspace_env);
      SetMaxWorkspaceSize(max_workspace_size);
    }

    trt_builder->setMaxBatchSize(max_batch_size_);
    trt_builder->setMaxWorkspaceSize(max_workspace_size_);
    auto trt_engine = unique_pointer<nvinfer1::ICudaEngine>(trt_builder->buildCudaEngine(*trt_network.get()));
    ORT_ENFORCE(trt_engine != nullptr);

    // Build TensorRT context
    auto trt_context = unique_pointer<nvinfer1::IExecutionContext>(trt_engine->createExecutionContext());
    ORT_ENFORCE(trt_context != nullptr);

    // Get input shape and binding index
    int num_inputs = trt_network->getNbInputs();
    input_indexes.resize(num_inputs);
    input_dim_sizes.resize(num_inputs);
    for (int i = 0; i < num_inputs; ++i) {
      const std::string& name = trt_network->getInput(i)->getName();
      size_t bindingIndex = trt_engine->getBindingIndex(name.c_str());
      nvinfer1::Dims dimensions = trt_engine->getBindingDimensions(static_cast<int>(bindingIndex));
      auto iter = input_map.find(name);
      if (iter != input_map.end()) {
        input_indexes[bindingIndex] = iter->second;
      }
      size_t dim_size = 1;
      for (int j = 0, end = dimensions.nbDims; j < end; ++j) {
        dim_size *= dimensions.d[j];
      }
      input_dim_sizes[bindingIndex] = dim_size;
    }

    // Get output shape and binding index
    int num_outputs = trt_network->getNbOutputs();
    output_indexes.resize(num_outputs);
    output_dim_sizes.resize(num_outputs);
    output_shapes.resize(num_outputs);
    output_types.resize(num_outputs);
    for (int i = 0; i < num_outputs; ++i) {
      const std::string& name = trt_network->getOutput(i)->getName();
      size_t bindingIndex = trt_engine->getBindingIndex(name.c_str());
      nvinfer1::Dims dimensions = trt_engine->getBindingDimensions(static_cast<int>(bindingIndex));
      bindingIndex -= num_inputs;
      auto iter = output_map.find(name);
      if (iter != output_map.end()) {
        output_indexes[bindingIndex] = iter->second;
      }
      size_t dim_size = 1;
      for (int j = 0, end = dimensions.nbDims; j < end; ++j) {
        output_shapes[bindingIndex].push_back(dimensions.d[j]);
        dim_size *= dimensions.d[j];
      }
      output_dim_sizes[bindingIndex] = dim_size;

      const auto& tensor_type = graph_output[i].type().tensor_type();
      output_types[bindingIndex] = tensor_type.elem_type();

      const auto& tensor_shape = tensor_type.shape();
      if (tensor_shape.dim_size() == 1 && output_shapes[bindingIndex].back() == 1) {
        output_shapes[bindingIndex].pop_back();
      }
    }

    ORT_ENFORCE(trt_engine->getNbBindings() == (num_inputs + num_outputs));

    // Save engine, context and input/output info to map
    parsers_.emplace(fused_node->Name(), std::move(trt_parser));
    engines_.emplace(fused_node->Name(), std::move(trt_engine));
    contexts_.emplace(fused_node->Name(), std::move(trt_context));
    input_info_[fused_node->Name()].push_back(input_indexes);
    input_info_[fused_node->Name()].push_back(input_dim_sizes);
    output_info_[fused_node->Name()].push_back(output_indexes);
    output_info_[fused_node->Name()].push_back(output_dim_sizes);
    output_info_[fused_node->Name()].push_back(output_types);
    output_shapes_[fused_node->Name()] = output_shapes;

    // Create function state
    // TODO: remove default capture
    NodeComputeInfo compute_info;
    compute_info.create_state_func = [=](ComputeContext* context, FunctionState* state) {
      std::unique_ptr<TensorrtFuncState> p = std::make_unique<TensorrtFuncState>();
      *p = {context->allocate_func, context->release_func, context->allocator_handle, parsers_[context->node_name].get(), engines_[context->node_name].get(), contexts_[context->node_name].get(),
            input_info_[context->node_name], output_info_[context->node_name], output_shapes_[context->node_name], &tensorrt_mu_};
      *state = p.release();
      return 0;
    };

    // Release function state
    compute_info.release_state_func = [](FunctionState state) {
      if (state)
        delete static_cast<TensorrtFuncState*>(state);
    };

    // Create compute function
    compute_info.compute_func = [](FunctionState state, ONNXRunTimeTensor* input_tensors, size_t num_inputs, ONNXRunTimeTensor* output_tensors, size_t num_outputs) {
      ORT_UNUSED_PARAMETER(num_inputs);
      ORT_UNUSED_PARAMETER(num_outputs);
      TensorrtFuncState* trt_state = reinterpret_cast<TensorrtFuncState*>(state);
      const std::vector<int>& input_indexes = (trt_state->input_info)[0];
      const std::vector<int>& input_dim_sizes = (trt_state->input_info)[1];
      const std::vector<int>& output_indexes = (trt_state->output_info)[0];
      const std::vector<int>& output_dim_sizes = (trt_state->output_info)[1];
      const std::vector<int>& output_types = (trt_state->output_info)[2];
      std::vector<std::vector<int64_t>> output_shapes = trt_state->output_shapes;
      int num_binding_inputs = input_indexes.size();
      int num_binding_outputs = output_indexes.size();
      int total_bindings = num_binding_inputs + num_binding_outputs;
      cudaStream_t stream;
      CHECK_CUDA(cudaStreamCreate(&stream));
      std::vector<void*> buffers(total_bindings);
      int batch_size = 1;

      // Get batch size and allocate cuda memory for inputs
      for (int i = 0, end = num_binding_inputs; i < end; ++i) {
        const auto& tensor_input = input_tensors[input_indexes[i]];
        const auto& tensor_shape = tensor_input.shape;
        const int input_batch_size = tensor_shape[0];
        if (i > 0 && batch_size != input_batch_size) {
          ORT_THROW("Input batch size is inconsistent");
        }
        batch_size = input_batch_size;

        const float* input = static_cast<float*>(tensor_input.data);
        CHECK_CUDA(cudaMalloc(&buffers[i], input_batch_size * input_dim_sizes[i] * sizeof(float)));
        CHECK_CUDA(cudaMemcpy(buffers[i], input, input_batch_size * input_dim_sizes[i] * sizeof(float), cudaMemcpyHostToDevice));
      }

      // Allocate cuda memory for outputs
      for (int i = 0, end = num_binding_outputs; i < end; ++i) {
        CHECK_CUDA(cudaMalloc(&buffers[i + num_binding_inputs], batch_size * output_dim_sizes[i] * sizeof(float)));
      }

      // Run TRT inference
      std::lock_guard<OrtMutex> lock(*(trt_state->tensorrt_mu_ptr));
      trt_state->context->enqueue(batch_size, &buffers[0], stream, nullptr);

      // Copy TRT outputs to output tensors
      for (int i = 0, end = num_binding_outputs; i < end; ++i) {
        int output_index = output_indexes[i];
        output_shapes[i].insert(output_shapes[i].begin(), batch_size);
        const auto& shape_size = output_shapes[i].size();
        output_tensors[output_index].ndim = shape_size;
        output_tensors[output_index].shape = new int64_t[shape_size];
        memcpy(output_tensors[output_index].shape, &output_shapes[i][0], sizeof(int64_t) * shape_size);

        int output_size = batch_size * output_dim_sizes[i];
        if (output_types[i] == TensorProto::FLOAT) {
          output_tensors[output_index].dtype = DType::TFloat32;
          output_tensors[output_index].data = (*(trt_state->test_allocate_func))(trt_state->allocator, 32, output_size * sizeof(float));
          CHECK_CUDA(cudaMemcpy(output_tensors[output_index].data, buffers[i + num_binding_inputs], output_size * sizeof(float), cudaMemcpyDeviceToHost));
        } else if (output_types[i] == TensorProto::INT64) {
          output_tensors[output_index].dtype = DType::TInt64;
          output_tensors[output_index].data = (*(trt_state->test_allocate_func))(trt_state->allocator, 64, output_size * sizeof(int64_t));
          CHECK_CUDA(cudaMemcpy(output_tensors[output_index].data, buffers[i + num_binding_inputs], output_size * sizeof(int), cudaMemcpyDeviceToHost));
        } else {
          ORT_THROW("Output type (%i) is not supported by TensorRT", output_types[i]);
        }
      }

      // Sync stream
      cudaStreamSynchronize(stream);

      // Free CUDA memory
      cudaStreamDestroy(stream);

      for (int i = 0, end = total_bindings; i < end; ++i) {
        CHECK_CUDA(cudaFree(buffers[i]));
      }

      return 0;
    };

    node_compute_funcs.push_back(compute_info);
  }

  return Status::OK();
}
}  // namespace onnxruntime
