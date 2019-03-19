// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/common/common.h"
#include "core/graph/graph_viewer.h"

namespace onnxruntime {

enum class TransformerLevel : uint32_t {
  Default = 0,
  Level1,
  Level2,
  // Convenience enum to always get the max available value. 
  // This way when we add more levels code which iterates over this enum does not need to change.
  MaxTransformerLevel
};

/**
@class GraphTransformer

The interface for in-place transformation of a Graph.
*/
class GraphTransformer {
 public:
  GraphTransformer(const std::string& name, const std::string& desc)
      : name_(name), desc_(desc) {
  }

  virtual ~GraphTransformer() = default;

  /** Gets the name of this graph transformer. */
  const std::string& Name() const noexcept {
    return name_;
  }

  /** Gets the description of this graph transformer. */
  const std::string& Description() const noexcept {
    return desc_;
  }

  /** Apply the in-place transformation defined by this transformer to the provided Graph instance.
  @param[in] providers Optional - providers this transformer can be applied to
  @param[out] modified Set to true if the Graph was modified.
  @returns Status with success or error information.
  */
  common::Status Apply(Graph& graph, bool& modified, const std::vector<std::string>& providers = {}) const;

 protected:
  /** Helper method to call ApplyImpl on any subgraphs in the Node. */
  common::Status Recurse(Node& node, bool& modified, int graph_level) const {
    int subgraph_level = ++graph_level;
    for (auto& entry : node.GetAttributeNameToMutableSubgraphMap()) {
      auto& subgraph = *entry.second;
      ORT_RETURN_IF_ERROR(ApplyImpl(subgraph, modified, subgraph_level));
    }

    return Status::OK();
  }

 private:
  ORT_DISALLOW_COPY_ASSIGNMENT_AND_MOVE(GraphTransformer);

  // Apply the transform to the graph.
  // graph_level is 0 for the main graph, and is incremented when descending into the subgraph of a node.
  // You MUST call Recurse for all valid Nodes in the graph to ensure any subgraphs in control flow nodes
  // (Scan/If/Loop) are processed as well.
  // You should avoid calling Graph::Resolve in ApplyImpl unless you are 100% sure it's required. In most cases
  // the call to Graph::Resolve in Apply prior to ApplyImpl being called, and after ApplyImpl fore the main graph
  // completes (if 'modified' is true) should suffice.
  virtual common::Status ApplyImpl(Graph& graph, bool& modified, int graph_level = 0) const = 0;

  const std::string name_;
  const std::string desc_;
};
}  // namespace onnxruntime
