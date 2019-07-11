// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#pragma once

#include <string>
#include "core/graph/training/loss_func/loss_func_common.h"

namespace onnxruntime {
namespace training {

struct BertLoss : public ILossFunction {
  GraphAugmenter::GraphDefs operator()(const Graph& graph, const LossFunctionInfo& loss_func_info) override;

 private:
  static TypeProto* GetMaskedLMTypeProto(const NodeArg* prediction_arg,
                                         ONNX_NAMESPACE::TensorProto_DataType data_type,
                                         int64_t max_predictions_per_sequence,
                                         GraphAugmenter::GraphDefs& graph_defs);
  static TypeProto* GetNSLabelTypeProto(const NodeArg* prediction_arg, GraphAugmenter::GraphDefs& graph_defs);
  static TypeProto* GetLossTypeProto(GraphAugmenter::GraphDefs& graph_defs);
};

}  // namespace training
}  // namespace onnxruntime
