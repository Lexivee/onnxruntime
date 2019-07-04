// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/common/status.h"
#include "core/framework/alloc_kind.h"
#include "core/framework/allocator.h"
#include "core/framework/sequential_execution_plan.h"
#include "core/graph/graph_viewer.h"
namespace ONNX_NAMESPACE {
class TensorShapeProto;
}
namespace onnxruntime {

class ExecutionProviders;
class KernelRegistryManager;
class OrtValueNameIdxMap;

// ISequentialPlannerContext abstracts how the planner accesses information (such as inferred shape)
// to do the planning.
class ISequentialPlannerContext {
 public:
  virtual const ONNX_NAMESPACE::TensorShapeProto* GetShape(const onnxruntime::NodeArg& arg) const = 0;
  // If it returns true, planner won't reuse output tensors
  // see PlannerImpl::ComputeReusePlan
  virtual bool IsParallelExecutionEnabled() const { return false; }

  // Set to 'true' to run only the nodes from feeds to required fetches.
  // So it is possible that only some of the nodes are executed.
  virtual bool OnlyExecutePathToFetches() const { return false; }
};

class SequentialPlannerContext : public ISequentialPlannerContext {
 public:
  SequentialPlannerContext(bool p_enable_parallel_execution,
                           bool p_only_execute_path_to_fetches)
      : m_enable_parallel_execution(p_enable_parallel_execution),
        m_only_execute_path_to_fetches(p_only_execute_path_to_fetches) {
  }

  const ONNX_NAMESPACE::TensorShapeProto* GetShape(const onnxruntime::NodeArg& arg) const override {
    return arg.Shape();
  }

  bool IsParallelExecutionEnabled() const override { return m_enable_parallel_execution; }

  bool OnlyExecutePathToFetches() const override { return m_only_execute_path_to_fetches; }

 private:
  bool m_enable_parallel_execution{false};
  bool m_only_execute_path_to_fetches{false};
};

class SequentialPlanner {
 public:
  // This API allows user to provide a custom planner context.
  static Status CreatePlan(const Node* parent_node, const onnxruntime::GraphViewer& graph,
                           const std::vector<const NodeArg*>& outer_scope_node_args,
                           const ExecutionProviders& providers, const KernelRegistryManager& kernel_registry,
                           const OrtValueNameIdxMap& ort_value_name_idx_map, const ISequentialPlannerContext& context,
                           std::unique_ptr<SequentialExecutionPlan>& plan);
};

}  // namespace onnxruntime
