// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <charconv>

#include "core/common/inlined_containers.h"
#include "core/common/string_utils.h"
#include "core/optimizer/graph_transformer.h"

namespace onnxruntime {

/**
@Class MemoryAlleviation

Find and recompute/offload activations for found subgraphs.
*/

class MemoryAlleviation : public GraphTransformer {
 private:
  using NodeOutputPort = std::pair<const Node*, int>;
  using ActivationUsedMap = InlinedHashMap<std::string, std::pair<bool, bool>>;

  /**
   * @brief Level to control allowed operations during subgraph detecting.
   * Level 0: only allow cheap-to-compute operations.
   * Level 1: allow more expensive operations.
   */
  enum class ProbeLevel {
    Basic = 0,
    Advanced = 1,
  };

  /**
   * @brief Type of memory reduction techniques.
   */
  enum class AlleviationType {
    None = 0,  // Disabled.
    Recompute = 1,
  };

  /**
   * @brief Type of user config.
   * type: type of memory reduction techniques.
   * requested_count: the number of occurrences of a subgraph pattern for alleviation. -1 means apply all.
   *   One example: if a subgraph pattern is found 3 times, and requested_count is set 2, then the 1st and 2nd subgraph in
   *   typological order will be applied for alleviation. This is useful to avoid alleviating more memory than needed.
   */
  struct UserAlleviationConfig {
    AlleviationType type;
    int requested_count;
    int stride{1};
  };

  struct EntryOperatorConfig {
    InlinedVector<int> input_arg_indices;  // input index to iterate further (bottom up)
  };

  struct AlleviationSubGraphDesc {
    AlleviationSubGraphDesc() = default;

    InlinedHashMap<std::string, int> shape_str_frequency;  // shape string to frequency
    UserAlleviationConfig user_alleviation_config;
    std::string subgraph_representative_str;  // a string to represent the subgraph.
    int total_frequency{0};
    int applied_count{0};
    int skip_count{0};
    float saving_ratio{1.0f};
  };

  struct AlleviationSubGraphStores {
    // nodes in typo order, and a string to represent the subgraph.
    using GraphInstanceInfo = std::pair<InlinedVector<const Node*>, std::string>;

    size_t SubGraphCount() const {
      return subgraph_descs.size();
    }

    bool Contains(std::string_view subgraph_str) const {
      return subgraph_descs.find(subgraph_str) != subgraph_descs.end();
    }

    AlleviationSubGraphDesc& GetSubGraphDesc(std::string_view subgraph_string) {
      ORT_ENFORCE(Contains(subgraph_string), "Subgraph string not found.", subgraph_string);
      return subgraph_descs.at(subgraph_string);
    }

    AlleviationSubGraphDesc& CreateSubGraphDesc(const std::string& subgraph_string,
                                                UserAlleviationConfig& config) {
      ORT_ENFORCE(!Contains(subgraph_string), "Subgraph string already exists.", subgraph_string);
      std::cout << "CreateSubGraphDesc for " << subgraph_string << std::endl;
      subgraph_descs[subgraph_string].user_alleviation_config = config;
      subgraph_descs[subgraph_string].subgraph_representative_str = subgraph_string;
      return subgraph_descs[subgraph_string];
    }

    void AddRecomputeSubGraphInstance(const Node* node,
                                      const InlinedVector<const Node*>& nodes_in_topological_order,
                                      const AlleviationSubGraphDesc& subgraph_desc) {
      ORT_ENFORCE(recompute_graphs.find(node) == recompute_graphs.end());
      std::cout << "AddRecomputeSubGraphInstance for " << subgraph_desc.subgraph_representative_str << std::endl;
      recompute_graphs[node] = std::make_pair(nodes_in_topological_order, subgraph_desc.subgraph_representative_str);
    }

    // GetSubGraphInstanceFromStashedActivationProducer

    bool ContainsRecomputeSubGraphInstance(const Node* node) const {
      return recompute_graphs.find(node) != recompute_graphs.end();
    }

    GraphInstanceInfo& GetRecomputeSubGraphInstance(const Node* node) {
      ORT_ENFORCE(recompute_graphs.find(node) != recompute_graphs.end());
      return recompute_graphs[node];
    }

    InlinedHashMap<std::string /*subgraph_representative_str*/, AlleviationSubGraphDesc> subgraph_descs;
    InlinedHashMap<const Node*, GraphInstanceInfo> recompute_graphs;
  };

 public:
  MemoryAlleviation(const std::string& enable_memory_alleviation, const std::string& level);

  Status ApplyImpl(Graph& graph, bool& modified, int graph_level, const logging::Logger& logger) const override;

  bool ShouldOnlyApplyOnce() const override { return true; }

 private:
  Status ParseConfigFromString(const std::string& enable_memory_alleviation, const std::string& level);

  /**
   * @brief Prepare info including activation usage, node usage in fw and bw.
   *
   * @param graph Graph to iterate.
   * @param fw_op_output_arg_used_map Collected activation usage mapping.
   *   - key: node arg name
   *   - value: a pair of bool, representing whether the activation is used by forward nodes or by backward nodes.
   * @return int64_t value The boundary op (for example YieldOp) order in topological order. If no boundary op found, return -1;
   */
  int64_t PrepareForTransformation(const Graph& graph,
                                   ActivationUsedMap& fw_op_output_arg_used_map,
                                   InlinedHashMap<NodeIndex, size_t>&
                                       node_index_to_its_order_in_topological_sort_map) const;
  /**
   * @brief Find all stashed activations, e.g. activations used by forward operators and backward operators.
   *
   * @param graph Graph to iterate.
   * @param fw_op_output_arg_used_map Activation usage mapping.
   * @param candidate_output_args_map Candidate activations, which are consumed by both fw and bw ops.
   * @return Status
   */
  Status GetStashedActivationCandidates(
      const Graph& graph,
      const InlinedHashMap<std::string, std::pair<bool, bool>>& fw_op_output_arg_used_map,
      InlinedHashMap<const Node*, InlinedVector<size_t>>& candidate_output_args_map,
      const logging::Logger& logger) const;

  /**
   * @brief Find recomputable subgraphs (has at least one nodes, at most MAXIMUM_RECOMPUTE_NODE_COUNT nodes).
   *
   * @param node The entry node to start the subgraph matching (bottom-up), usually the last node of found subgraphs.
   * @param node_output_index_candidates Candidate output indices of "node", which are consumed by both fw and bw ops.
   * @param fw_op_output_arg_used_map The activation usage (in fw and bw) mapping.
   * @param node_index_to_its_order_in_topological_sort_map The mapping of node index to its order in topological sort.
   *   Used to re-order the collected subgraph nodes.
   * @param nodes_in_topological_order Collected vector of nodes of found subgraph, in the order of the topological sorted.
   * @return Status
   */
  Status SelectRecomputeSubgraph(const Node& node,
                                 const InlinedVector<size_t>& node_output_index_candidates,
                                 const ActivationUsedMap& fw_op_output_arg_used_map,
                                 const InlinedHashMap<NodeIndex, size_t>& node_index_to_its_order_in_topological_sort_map,
                                 InlinedVector<const Node*>& nodes_in_topological_order,
                                 const logging::Logger& logger,
                                 bool compromise_stashed_activation,
                                 bool& can_compromise_stashed_activation) const;

  bool IsNodeRecomputable(const Node& node,
                          const ActivationUsedMap& fw_op_output_arg_used_map,
                          const InlinedHashMap<NodeIndex, size_t>&
                              node_index_to_its_order_in_topological_sort_map,
                          const InlinedHashMap<const Node*, InlinedVector<size_t>>&
                              candidate_output_args_map,
                          AlleviationSubGraphStores& subgraph_stores,
                          const logging::Logger& logger,
                          bool compromise_stashed_activation,
                          bool& can_compromise_stashed_activation) const;

  /**
   * @brief Duplicate nodes to create a recompute subgraph.
   *
   * @param graph Graph to iterate.
   * @param nodes_in_topological_order Subgraph nodes to recompute.
   * @param recompute_subgraph_output_node The final node of the subgraph.
   * @return Status
   */
  Status CreateRecomputeGraph(Graph& graph,
                              const InlinedVector<const Node*>& nodes_in_topological_order,
                              Node*& recompute_subgraph_output_node) const;

  bool ModifyGraphForRecompute(Graph& graph,
                               const InlinedHashMap<NodeIndex, size_t>& node_index_to_its_order_in_topological_sort_map,
                               const InlinedHashMap<const Node*, InlinedVector<size_t>>& candidate_output_args_map,
                               const logging::Logger& logger,
                               int64_t boundary_op_order_in_topological_sort,
                               AlleviationSubGraphStores& subgraph_stores,
                               Node* node) const;

  /**
   * @brief Convert the recompute subgraph to its string representation.
   *
   * @param nodes_in_topological_order The subgraph nodes in topological order.
   * @param subgraph_string_representation Returns subgraph string representation.
   * @param log_info Returns log info for users.
   */
  void NodesInTypoOrderToString(const InlinedVector<const Node*>& nodes_in_topological_order,
                                std::string& subgraph_string_representation,
                                std::string& log_info) const;

  /**
   * @brief Convert alleviation type to string.
   *
   * @param type Alleviation type.
   * @return std::string
   */
  std::string UserAlleviationConfigToString(const UserAlleviationConfig& type) const;

  /**
   * @brief Summarize transformation details.
   *
   * @param stashed_activation_statistics statistics around stashed activation memory saving.
   * @return void
   */
  void PrintSummary(const AlleviationSubGraphStores& recompute_stores,
                    const AlleviationSubGraphStores& recompute_with_compromise_stores,
                    const logging::Logger& logger) const;

  // The op types that are supported predefined.
  InlinedHashMap<std::string, EntryOperatorConfig> recomputable_op_type_to_input_arg_index_map_;

  // User enabled map of the subgraph string representation to the alleviation type.
  InlinedHashMap<std::string, UserAlleviationConfig> pattern_subgraph_to_user_alleviation_config_map_;

  std::string memory_alleviation_config_;
  ProbeLevel level_;
};

}  // namespace onnxruntime
