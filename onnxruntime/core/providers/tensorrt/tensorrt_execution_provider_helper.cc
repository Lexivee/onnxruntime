// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/shared_library/provider_api.h"
#include "tensorrt_execution_provider.h"
#include <iostream>

namespace onnxruntime {

// The newly-built graph has not yet being resolved by Graph::Resolve(), so we can't leverage ORT Graph IsInputInitializerOrOutput() API.
// We have to do it by ourselves.
bool TensorrtExecutionProvider::IsInputInitializerOrOutput(Graph * graph,
                                                           const std::string& name,
                                                           bool check_ancestors,
                                                           std::unordered_map<std::string, std::unique_ptr<SubGraphContext>>& subgraph_context_map) const {
  return IsLocalValue(graph, name, subgraph_context_map) ||
         (check_ancestors && graph->ParentGraph() != nullptr &&
          IsInputInitializerOrOutput(graph->MutableParentGraph(), name, check_ancestors, subgraph_context_map));
}

// The newly-built graph has not yet being resolved by Graph::Resolve(), so we can't leverage ORT Graph IsOuterScopeValues() API.
// We have to do it by ourselves.
bool TensorrtExecutionProvider::IsOuterScopeValue(Graph* graph,
                                                  const std::string& name,
                                                  std::unordered_map<std::string, std::unique_ptr<SubGraphContext>>& subgraph_context_map) const {
  return graph->ParentGraph() != nullptr && IsInputInitializerOrOutput(graph->MutableParentGraph(), name, true, subgraph_context_map);
}

// The newly-built graph has not yet being resolved by Graph::Resolve(), so we can't leverage ORT Graph IsLocalValue() API.
// We have to do it by ourselves.
bool TensorrtExecutionProvider::IsLocalValue(Graph* graph,
                                             const std::string& name,
                                             std::unordered_map<std::string, std::unique_ptr<SubGraphContext>>& subgraph_context_map) const {
  std::string subgraph_name = graph->Name();

  if (subgraph_context_map.find(subgraph_name) == subgraph_context_map.end()) {
    return false;
  }
  SubGraphContext* context = subgraph_context_map.at(subgraph_name).get();
  return context->output_args.find(name) != context->output_args.cend() ||
         context->inputs_and_initializers.find(name) != context->inputs_and_initializers.cend();

}

/**
* Set inputs, initializers and outputs for all subgraphs during TensorrtExecutionProvider::GetSupportedList() and save those information in subgraph context data structure.
* It's useful for building a valid graph and make Graph::Resolve() happy especially when dealing with nested control-flow op graph.
*/
void TensorrtExecutionProvider::BuildSubGraphContext(Graph* graph,
                                                     std::unordered_map<std::string, std::unique_ptr<SubGraphContext>>& subgraph_context_map) const {

  // Iterate all the nodes and recurse into inner most subgraph first
  for (int i = 0; i < graph->MaxNodeIndex(); ++i) {
    auto node = graph->GetNode(i);
    if (node == nullptr) {
      continue;
    }

    auto subgraph_map = node->GetAttributeNameToMutableSubgraphMap();
    for (auto& entry : subgraph_map) {
      Graph* subgraph = entry.second;
      BuildSubGraphContext(subgraph, subgraph_context_map);
    }
  }

  std::string subgraph_name = graph->Name();

  // Subgraph context has been built before, no need to do it again
  if (subgraph_context_map.find(subgraph_name) != subgraph_context_map.end()) {
    return;
  }

  subgraph_context_map.emplace(subgraph_name, std::make_unique<SubGraphContext>());
  SubGraphContext* context = subgraph_context_map.at(subgraph_name).get();

  // Collect all nodes' outputs and nodes' name
  for (int i = 0; i < graph->MaxNodeIndex(); ++i) {
    auto node = graph->GetNode(i);
    if (node == nullptr) {
      continue;
    }

    for (const auto& output : node->OutputDefs()) {
      context->output_args.insert(output->Name());
    }
  }

  // Go thru all node's inputs
  for (int i = 0; i < graph->MaxNodeIndex(); ++i) {
    auto node = graph->GetNode(i);
    if (node == nullptr) {
      continue;
    }

    for (const auto& input : node->InputDefs()) {
      if (context->output_args.find(input->Name()) != context->output_args.end()) {
        continue;
      }
      // This input arg is not the output of another node so must come from either a graph input or an initializer.
      context->inputs_and_initializers[input->Name()] = input;
    }
  }
}

// Set outer scope values for subgraphs and add thoes values as top-level graph's inputs if needed.
void TensorrtExecutionProvider::SetGraphOuterScopeValuesAndInputs(Graph* graph_build,
                                                                  const Graph* graph,
                                                                  std::unordered_map<std::string, std::unique_ptr<SubGraphContext>>& subgraph_context_map) const {

  // Iterate all the nodes and recurse into inner most subgraph first
  for (int i = 0; i < graph_build->MaxNodeIndex(); ++i) {
    auto graph_build_node = graph_build->GetNode(i);
    if (graph_build_node == nullptr) {
      continue;
    }

    auto graph_build_map = graph_build_node->GetAttributeNameToMutableSubgraphMap();
    std::unordered_map<std::string, gsl::not_null<const Graph*>> subgraph_map;
    const Node* graph_node = nullptr;

    // Find original graph node's subgraphs
    for (int j = 0; j < graph->MaxNodeIndex(); ++j) {
      if (graph->GetNode(j) && graph->GetNode(j)->Name() == graph_build_node->Name()) {
        graph_node = graph->GetNode(j);
        subgraph_map = graph_node->GetAttributeNameToSubgraphMap();
        break;
      }
    }

    for (auto& entry : graph_build_map) {
      auto attr_name = entry.first;
      Graph* subgraph_build = entry.second;
      if (subgraph_map.find(attr_name) != subgraph_map.end()) {
        // recurse into subgraph
        const Graph* subgraph = subgraph_map.at(attr_name);
        SetGraphOuterScopeValuesAndInputs(subgraph_build, subgraph, subgraph_context_map);
      }
    }
  }

  // Start from the inner most subgraph first and check whether its outer scope values are existed in the newly built graph.
  // If not, we need to add those outer scope values as explict inputs to the top-level of newly built graph.
  if (graph_build->ParentNode()) {

    auto top_level_graph = graph_build;
    while (top_level_graph->MutableParentGraph()) {
      top_level_graph = top_level_graph->MutableParentGraph();
    }
    if (subgraph_context_map.find(top_level_graph->Name()) == subgraph_context_map.end()) {
      LOGS_DEFAULT(ERROR) << "[TensorRT EP] Can't find top-level graph context. Please check BuildSubGraphContext() has built the graph context correctly.";
      return;
    }

    SubGraphContext* context = subgraph_context_map.at(top_level_graph->Name()).get();

    LOGS_DEFAULT(VERBOSE) << "[TensorRT EP] Subgraph name is " << graph_build->Name();
    LOGS_DEFAULT(VERBOSE) << "[TensorRT EP] Its parent node is " << graph->ParentNode()->Name();
    LOGS_DEFAULT(VERBOSE) << "[TensorRT EP] Its parent node's implicit inputs:";


    // Iterate all the implict inputs to set outer scope value for the newly built subgraph
    for (const auto& input : graph->ParentNode()->ImplicitInputDefs()) {
      LOGS_DEFAULT(VERBOSE) << "[TensorRT EP] \t"<< input->Name();

      // The node arg in parent node's implicit inputs could be used for parent node's other subgraph, for example "If" op has two subgraphs.
      // So we need to make sure that the node arg is used in current subgraph only. (GetNodeArg searches for specific node arg in all node args in the graph)
      if (graph_build->GetNodeArg(input->Name())) {
        graph_build->AddOuterScopeNodeArg(input->Name());
        LOGS_DEFAULT(VERBOSE) << "[TensorRT EP] \t" << input->Name() << " is used in this subgraph";

        if (context && (context->manually_added_graph_inputs.find(input->Name()) != context->manually_added_graph_inputs.end())) {
          LOGS_DEFAULT(VERBOSE) << "[TensorRT EP] \t" << input->Name() << " is already been added as an explicit input to graph";
          continue;
        }

        // Handle the case where this outer scope value is not existed in any outer scope levels of the newly built graph (the newly built graph is the subgraph of the original graph)
        // need to add the outer scope value as an explict input to the top-level of newly built graph
        if (!IsOuterScopeValue(graph_build, input->Name(), subgraph_context_map)) {
          const auto& name = input->Name();
          auto graph_inputs_including_initializers = top_level_graph->GetInputsIncludingInitializers();
          auto added_graph_input = std::find_if(graph_inputs_including_initializers.begin(), graph_inputs_including_initializers.end(),
                                                [&name](const NodeArg* entry) { return entry->Name() == name; });

          if (added_graph_input == graph_inputs_including_initializers.end()) {
            if (context) {
              auto type_proto = ONNX_NAMESPACE::TypeProto::Create();
              type_proto->copy_from(input->TypeAsProto());
              auto& n_input = top_level_graph->GetOrCreateNodeArg(name, type_proto.get());
              context->manually_added_graph_inputs[n_input.Name()] = &n_input;
              LOGS_DEFAULT(VERBOSE) << "[TensorRT EP] \t" << n_input.Name() << " is added as an explicit input into the newly built graph";
            }
          }
        }
      }
    }
  }
}

// If ORT TRT manually sets graph input in TensorrtExecutionProvider::SetGraphOuterScopeValuesAndInputs(), we have to manully set all the graph inputs in order to pass Graph::Resolve()
void TensorrtExecutionProvider::SetAllGraphInputs(Graph* graph,
                                                  std::unordered_map<std::string, std::unique_ptr<SubGraphContext>>& subgraph_context_map) const {

  std::string subgraph_name = graph->Name();

  // If ORT TRT doesn't manully set graph input in TensorrtExecutionProvider::SetGraphOuterScopeValuesAndInputs(),
  // Graph::Resolve() will help set graph inputs in Graph::SetGraphInputsOutputs(), so no need to set graph inputs here.
  if (subgraph_context_map.find(subgraph_name) == subgraph_context_map.end() || subgraph_context_map[subgraph_name].get()->manually_added_graph_inputs.size() == 0) {
    return;
  }

  SubGraphContext* context = subgraph_context_map[subgraph_name].get();
  std::vector<const NodeArg*> graph_inputs_including_initializers;
  std::unordered_set<std::string> graph_inputs_including_initializers_set; 

  for (const auto& entry : context->inputs_and_initializers) {
    graph_inputs_including_initializers.push_back(entry.second);
    graph_inputs_including_initializers_set.insert(entry.first);
  }

  for (const auto& entry : context->manually_added_graph_inputs) {
    if (graph_inputs_including_initializers_set.find(entry.first) == graph_inputs_including_initializers_set.end()) {
      graph_inputs_including_initializers.push_back(entry.second);
      graph_inputs_including_initializers_set.insert(entry.first);
    }
  }

  for (const auto& node_arg : graph->GetInputsIncludingInitializers()) {
    if (graph_inputs_including_initializers_set.find(node_arg->Name()) == graph_inputs_including_initializers_set.end()) {
      graph_inputs_including_initializers.push_back(node_arg);
      graph_inputs_including_initializers_set.insert(node_arg->Name());
    }
  }

  graph->SetInputs(graph_inputs_including_initializers);
}
}
