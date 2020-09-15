// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/framework/graph_partitioner.h"
#include "core/framework/kernel_registry_manager.h"
#include "core/graph/function.h"
#include "core/graph/graph_viewer.h"
#include "core/framework/compute_capability.h"
#include "core/framework/kernel_registry_manager.h"
#include "core/framework/execution_providers.h"
#include "core/framework/kernel_registry.h"
#include "core/framework/func_kernel.h"
#include <queue>

// uncomment this line to count non-CUDA ops in ONNX domain
//#define COUNT_NON_CUDA_OPS

#ifdef COUNT_NON_CUDA_OPS
class NonCudaOps {
 public:
  ~NonCudaOps() {
    printf("Non-CUDA ops:\n");
    for (auto i : map_) {
      printf("%s: %d\n", i.first.c_str(), i.second);
    }
  }

  void AddOp(const std::string& name) {
    if (map_.count(name))
      map_.at(name)++;
    else
      map_.insert({name, 1});
  }

 private:
  std::map<std::string, int> map_;
};

NonCudaOps non_cuda;
#endif

using namespace ::onnxruntime::common;
namespace onnxruntime {

KernelDefBuilder& BuildFusedKernelDef(KernelDefBuilder& builder, const onnxruntime::Node& node) {
  auto schema = node.Op();
  builder.SetName(schema->Name())
      .SetDomain(schema->domain())
      .SinceVersion(schema->SinceVersion())
      .Provider(node.GetExecutionProviderType());
  return builder;
}

/**
 * Check if a node can be placed on a specific provider.
 * Do nothing if the node is already assigned
 * \param graph
 * \param capability
 * \param kernel_registry_mgr
 * \param provider_type name of the provider to test
 * \param count A counter for generating fused node names. Should be unique within this subgraph
 * \return Fused node. Return nullptr if there is no fuse
 */
static Node* PlaceNode(Graph& graph, std::unique_ptr<IndexedSubGraph> capability,
                       const KernelRegistryManager& kernel_registry_mgr, const std::string& provider_type, int& count) {
  if (nullptr == capability) {
    return nullptr;
  }

  if (nullptr == capability->GetMetaDef()) {
    // The <provider> can run a single node in the <graph> if not using meta-defs.
    // A fused kernel is not supported in this case.
    ORT_ENFORCE(1 == capability->nodes.size());

    auto* node = graph.GetNode(capability->nodes[0]);
    if (nullptr != node && node->GetExecutionProviderType().empty()) {
      // The node was not fused or assigned. Assign it to this <provider>.
      node->SetExecutionProviderType(provider_type);
    }
  } else {
    // The <provider> can run a fused <sub_graph> in the <graph>.
    ORT_ENFORCE(nullptr != capability->GetMetaDef());
    // Check whether any node in the <sub_graph> was already assigned.
    bool sub_graph_available_for_assignment = true;
    for (auto node_index : capability->nodes) {
      auto node = graph.GetNode(node_index);
      if (nullptr == node || !node->GetExecutionProviderType().empty()) {
        // The node was fused or assigned, so that the whole sub-graph will not be assigned to this <provider>
        // The assumption is that this <provider> can only run the sub-graph as a whole unit.
        sub_graph_available_for_assignment = false;
        break;
      }
    }
    if (sub_graph_available_for_assignment) {
      std::ostringstream oss;
      oss << provider_type << "_" << capability->GetMetaDef()->name << "_" << count++;
      std::string node_name = oss.str();
      auto& fused_node = graph.FuseSubGraph(std::move(capability), node_name);
      fused_node.SetExecutionProviderType(provider_type);
      // searching in kernel registries, if no kernel registered for the fused_node, use compile approach
      if (!KernelRegistryManager::HasImplementationOf(kernel_registry_mgr, fused_node, provider_type)) {
        return &fused_node;
      }
    }
  }
  return nullptr;
}

Status GraphPartitioner::Partition(Graph& graph, bool export_dll, FuncManager& func_mgr) const {
  // It is a greedy partitioning algorithm per provider preferences user provided when calling ONNX RUNTIME right now.
  // 1. Execution providers' capabilities are checked one by one.
  // 2. All sub-graphs that an execution provider returns will be assigned to it if it's not assigned yet.
  //    NOTE: A 'sub-graph' is a subset of nodes within the current Graph instance.
  //          The control flow nodes have nested Graph instance/s which are also called subgraphs,
  //          but are completely separate Graph instances and not a subset of nodes within a single Graph instance.
  // 3. CPU execution provider is expected to be able to run any node and is the last one in execution provider
  //    preference.
  if (providers_.Empty()) {
    return Status(ONNXRUNTIME, INVALID_ARGUMENT, "No provider specified.");
  }

  // handle testing edge case where optimizers or constant lifting results in graph with no nodes.
  // doing it here saves all providers checking for this in GetCapability
  if (graph.NumberOfNodes() == 0)
    return Status::OK();

  // recurse into nested graphs first so we partition bottom up.
  for (auto& node : graph.Nodes()) {
    for (auto& entry : node.GetAttributeNameToMutableSubgraphMap()) {
      Graph* subgraph = entry.second;
      // we pass through the export_dll value and FuncManager from the top level graph
      ORT_RETURN_IF_ERROR(Partition(*subgraph, export_dll, func_mgr));
    }
  }

  // fused_kernel_registry is preparing the kernels created on the fly for fused sub graph.
  // It is only visible for current session.
  std::shared_ptr<KernelRegistry> fused_kernel_registry = std::make_shared<KernelRegistry>();
  // Partitioning <graph> based on provider preference and their capabilities.
  GraphViewer graph_viewer(graph);

  // If an execution provider return the capability that he could run a sub-graph,
  // onnxruntime will fuse the sub-graph into a function node. if the execution provider
  // says he need to compile the graph at runtime (by need_compile flag),
  // onnxruntime will invoke the "Compile" method to get compiled binary.
  // There are two mode of compile, one is return the entry point to the compiled binary
  // directly, another is export the compiled binary to shared library for future reuse.

  // TODO: when the graph contain a function node, and user pass in the dll which could
  // run the function by SessionOption, we should create a function kernel for it and
  // delegate the compute to the functions inside the dlls.
  for (auto& provider : providers_) {
    int count = 0;
    std::vector<Node*> nodes_need_compile;
    std::vector<std::unique_ptr<ComputeCapability>> capabilities =
        provider->GetCapability(graph_viewer, kernel_registry_mgr_.GetKernelRegistriesByProviderType(provider->Type()));

#if !defined(ORT_MINIMAL_BUILD)
    // For CUDA EP, exclude the subgraph that is prefered to be placed in CPU
    // These are usually shape related computation subgraphs
    // Following logic can be extended for other EPs
    if (provider->Type() == kCudaExecutionProvider) {
      std::vector<std::unique_ptr<ComputeCapability>> cpu_capabilities =
          GetCpuPreferedCapability(graph_viewer, provider, capabilities);

      for (auto& cpu_capability : cpu_capabilities) {
        Node* n = PlaceNode(graph, std::move(cpu_capability->sub_graph), kernel_registry_mgr_, kCpuExecutionProvider, count);
        // CPU nodes doesn't need compile
        ORT_ENFORCE(n == nullptr);
      }
    }
#endif

    for (auto& capability : capabilities) {
      Node* n = PlaceNode(graph, std::move(capability->sub_graph), kernel_registry_mgr_, provider->Type(), count);
      if (n != nullptr) {
        nodes_need_compile.push_back(n);
      }
    }

    if (!nodes_need_compile.empty()) {
      if (export_dll) {
        std::string dll_path;
        ORT_RETURN_IF_ERROR(provider->Compile(nodes_need_compile, dll_path));
        for (auto* node : nodes_need_compile)
          ORT_RETURN_IF_ERROR(func_mgr.AddFuncInfo(node->Name(), dll_path));
      } else {
        std::vector<NodeComputeInfo> node_compute_funcs;
        ORT_RETURN_IF_ERROR(provider->Compile(nodes_need_compile, node_compute_funcs));
        ORT_ENFORCE(node_compute_funcs.size() == nodes_need_compile.size(),
                    "Provider doesn't return correct number of compiled functions");
        for (size_t j = 0; j < nodes_need_compile.size(); j++)
          ORT_RETURN_IF_ERROR(func_mgr.AddFuncInfo(nodes_need_compile[j]->Name(), node_compute_funcs[j].compute_func,
                                                   node_compute_funcs[j].create_state_func,
                                                   node_compute_funcs[j].release_state_func));
      }
      for (auto* node : nodes_need_compile) {
        //prepare the func kernel
        KernelDefBuilder builder;
        BuildFusedKernelDef(builder, *node);
        ORT_RETURN_IF_ERROR(fused_kernel_registry->Register(
            builder, static_cast<KernelCreatePtrFn>([](const OpKernelInfo& info) -> OpKernel* { return new FunctionKernel(info); })));
      }
    }
  }

  ORT_RETURN_IF_ERROR(graph.Resolve());

  // To see if the node with no provider can be inlined. If one such nodes can be
  // successfully inlined, we re-run the partitioner on the modified graph.
  std::vector<Node*> nodes_need_inline;
  for (auto& node : graph.Nodes()) {
    if (node.GetExecutionProviderType().empty()) {
      auto node_func = node.GetFunctionBody();
      if (nullptr == node_func) {
        continue;
      }
      nodes_need_inline.push_back(&node);
    }
  }

  for (auto* node : nodes_need_inline) {
    // If the node has a functionbody with no kernel and cannot be inlined
    // it is an invalid function
    ORT_RETURN_IF_ERROR(graph.InlineFunction(*node));
  }

  // Resolve and rerun graph partition
  if (!nodes_need_inline.empty()) {
    ORT_RETURN_IF_ERROR(graph.Resolve());
    ORT_RETURN_IF_ERROR(Partition(graph, export_dll, func_mgr));
  }

  //For some cases, like fp16 on cpu, right now we don't have any kernel support that.
  //But we will insert cast op to run the model, so skip the error checking here.
  //If after graph transform phase, the node still not assigned, we will report error
  //during kernel creation phase.
#ifdef COUNT_NON_CUDA_OPS
  for (auto& node : graph.Nodes()) {
    if (node.GetExecutionProviderType() != kCudaExecutionProvider &&
        node.Domain() != kMLDomain &&
        node.Domain() != kMSDomain)
      non_cuda.AddOp(node.OpType());
  }
#endif

  if (!fused_kernel_registry->IsEmpty()) kernel_registry_mgr_.RegisterKernelRegistry(fused_kernel_registry);

  return Status::OK();
}

#if !defined(ORT_MINIMAL_BUILD)
const int64_t Small_Initializer_Threshold = 100;

bool IsSmallInitializer(const onnxruntime::GraphViewer& graph, const NodeArg* arg) {
  const ONNX_NAMESPACE::TensorProto* initializer_tensor;
  if (!graph.GetInitializedTensor(arg->Name(), initializer_tensor))
    return false;
  int64_t size = 1;
  for (auto& dim : initializer_tensor->dims()) {
    size *= dim;
  }
  return size <= Small_Initializer_Threshold;
}

std::vector<std::unique_ptr<ComputeCapability>>
GraphPartitioner::GetCpuPreferedCapability(const onnxruntime::GraphViewer& graph,
                                           const std::unique_ptr<IExecutionProvider>& provider,
                                           const std::vector<std::unique_ptr<ComputeCapability>>& capabilities) const {
  const std::vector<NodeIndex>& ordered_nodes = graph.GetNodesInTopologicalOrder();
  std::vector<size_t> node_id_to_order_map(graph.MaxNodeIndex());
  for (size_t id = 0; id < ordered_nodes.size(); ++id) {
    const NodeIndex& node_id = ordered_nodes[id];
    node_id_to_order_map[node_id] = id;
  }

  // If return false, n1 will be output first; If return true, n2 will be output first
  auto greater_order_comp = [&](const NodeIndex n1, const NodeIndex n2) {
    return node_id_to_order_map[n1] > node_id_to_order_map[n2];
  };

  std::priority_queue<NodeIndex, std::vector<NodeIndex>, decltype(greater_order_comp)> candidates(greater_order_comp);
  std::unordered_set<NodeIndex> visited;

  std::unordered_set<const NodeArg*> cpu_output_args;
  std::unordered_set<NodeIndex> provider_nodes;

  for (auto& capability : capabilities) {
    const IndexedSubGraph* indexed_sub_graph = capability->sub_graph.get();
    if (indexed_sub_graph->GetMetaDef() != nullptr) {
      continue;
    }
    // The <provider> can run a single node in the <graph> if not using meta-defs.
    ORT_ENFORCE(1 == indexed_sub_graph->nodes.size());

    const NodeIndex node_id = indexed_sub_graph->nodes[0];
    provider_nodes.insert(node_id);
    const Node* node = graph.GetNode(node_id);

    const KernelCreateInfo* kernel_info;
    std::vector<const KernelRegistry*> kernel_registries =
        kernel_registry_mgr_.GetKernelRegistriesByProviderType(provider->Type());
    for (auto registry : kernel_registries) {
      auto st = registry->TryFindKernel(*node, provider->Type(), &kernel_info);
      if (st.IsOK())
        break;
    }
    // at least one registry has a CUDA kernel for this node
    ORT_ENFORCE(kernel_info != nullptr);

    // first, find all the direct consumer of cpu tensors.
    ORT_THROW_IF_ERROR(node->ForEachWithIndex(
        node->OutputDefs(),
        [&](const NodeArg& node_arg, size_t out_index) {
          if (kernel_info->kernel_def->IsOutputOnCpu(out_index)) {
            cpu_output_args.insert(&node_arg);
            auto consumer_nodes = graph.GetConsumerNodes(node_arg.Name());
            for (auto& consumer_node : consumer_nodes) {
              candidates.push(consumer_node->Index());
              LOGS_DEFAULT(INFO) << "Canditiate for fallback CPU execution: " << consumer_node->Name();
            }
          }
          return Status::OK();
        }));
  }

  const std::vector<const NodeArg*>& graph_inputs = graph.GetInputs();
  std::unordered_set<NodeIndex> cpu_nodes;
  // The algo below is trying to identity a subgraph that only depends on cpu tensors.
  // Usually it is a subgraph that doing shape calculation based on a GPU tensor, then reshape it back.
  // The detail:
  // for each candidate, if one of its input is a cpu tensor and the cuda kernel doesn't mark it as cpu input,
  // force the node to CPU to avoid memory cpu and add its output to the small cpu tensors.
  while (!candidates.empty()) {
    NodeIndex cur = candidates.top();
    candidates.pop();
    if (visited.count(cur) != 0)
      continue;
    visited.insert(cur);

    if (provider_nodes.find(cur) == provider_nodes.end())
      continue;

    auto* node = graph.GetNode(cur);
    // skip placing current node on CPU if no kernel is found
    if (!KernelRegistryManager::HasImplementationOf(kernel_registry_mgr_, *node, kCpuExecutionProvider))
      continue;

    const KernelCreateInfo* kernel_info;
    Status st = provider->GetKernelRegistry()->TryFindKernel(*node, provider->Type(), &kernel_info);

    bool place_in_cpu = true;
    for (size_t i = 0; i < node->InputDefs().size(); ++i) {
      auto* input = node->InputDefs()[i];

      // allow placing on CPU if it's a small initializer or graph input
      if (IsSmallInitializer(graph, input) ||
          std::find(graph_inputs.begin(), graph_inputs.end(), input) != graph_inputs.end()) {
        continue;
      }

      // the input is not a CPU tensor
      if (cpu_output_args.find(input) == cpu_output_args.end()) {
        place_in_cpu = false;
        break;
      }

      // input is a CPU tensor, but it's intended to be consumed as CPU input by the target EP
      if (kernel_info->kernel_def->IsInputOnCpu(i)) {
        place_in_cpu = false;
        break;
      }
    }

    if (place_in_cpu) {
      cpu_nodes.insert(cur);
      LOGS_DEFAULT(WARNING) << "Force fallback to CPU execution for node: " << node->Name();
      for (auto* output : node->OutputDefs()) {
        cpu_output_args.insert(output);
      }
      for (auto it = node->OutputNodesBegin(); it != node->OutputNodesEnd(); ++it) {
        candidates.push((*it).Index());
      }
    }
  }

  std::vector<std::unique_ptr<ComputeCapability>> result;
  for (auto index : cpu_nodes) {
    std::unique_ptr<IndexedSubGraph> sub_graph = onnxruntime::make_unique<IndexedSubGraph>();
    sub_graph->nodes.push_back(index);
    result.push_back(onnxruntime::make_unique<ComputeCapability>(std::move(sub_graph)));
  }
  return result;
}
#endif
}  // namespace onnxruntime
