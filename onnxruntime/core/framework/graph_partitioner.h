// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/common/common.h"
#include "core/graph/graph_viewer.h"
#include "core/framework/op_kernel.h"
#include "core/framework/fuse_nodes_funcs.h"

namespace onnxruntime {

class ExecutionProviders;
class KernelRegistryManager;

class GraphPartitioner {
 public:
  //The order of providers represents the user preference.
  GraphPartitioner(KernelRegistryManager& kernel_registry_mgr, const ExecutionProviders& providers)
      : kernel_registry_mgr_(kernel_registry_mgr),
        providers_(providers) {}

  Status Partition(Graph& graph, bool export_dll, FuncManager& func_mgr) const;

 private:
  ORT_DISALLOW_COPY_ASSIGNMENT_AND_MOVE(GraphPartitioner);

#if !defined(ORT_MINIMAL_BUILD)
  /**
  Returns a list of compute capabilities that are prefered on CPU. 
  They are commonly shape-related computation subgraphs.
  @param graph Graph viewer 
  @param provider The targe execution provider
  @param capabilities Capabilities returned by target EP's GetCapacity() function
  */
  std::vector<std::unique_ptr<ComputeCapability>>
  GetCpuPreferedCapability(const onnxruntime::GraphViewer& graph,
                           const std::unique_ptr<IExecutionProvider>& provider,
                           const std::vector<std::unique_ptr<ComputeCapability>>& capabilities) const;
#endif

  KernelRegistryManager& kernel_registry_mgr_;
  const ExecutionProviders& providers_;
};
}  // namespace onnxruntime
