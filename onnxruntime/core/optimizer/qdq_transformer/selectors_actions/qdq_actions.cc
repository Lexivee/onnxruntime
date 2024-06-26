// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/optimizer/qdq_transformer/selectors_actions/qdq_actions.h"
#include "core/optimizer/qdq_transformer/qdq_util.h"
#include "core/optimizer/initializer.h"
#include "core/graph/node_attr_utils.h"
#include "core/mlas/inc/mlas_q4.h"

namespace onnxruntime {
namespace QDQ {

namespace {
using NTO = NodesToOptimize;

// moves for replacing a node with a single DQ input with the qlinear version
std::vector<NodeAndMoveInfo> UnaryMoves() {
  NTO::NodeLocation dq{NTO::NodeType::kInput, 0};
  NTO::NodeLocation q{NTO::NodeType::kOutput, 0};

  std::vector<NodeAndMoveInfo> moves{
      MoveAll(dq, ArgType::kInput),                           // append all inputs from dq to new node
      MoveAndAppend(q, ArgType::kInput, 1, ArgType::kInput),  // append scale (input 1) from q
      MoveAndAppend(q, ArgType::kInput, 2, ArgType::kInput),  // append zp (input 2) from q
      MoveAll(q, ArgType::kOutput)};

  return moves;
}

// moves for replacing a node with two DQ inputs with the qlinear version
std::vector<NodeAndMoveInfo> BinaryMoves() {
  NTO::NodeLocation dq1{NTO::NodeType::kInput, 0};
  NTO::NodeLocation dq2{NTO::NodeType::kInput, 1};
  NTO::NodeLocation q{NTO::NodeType::kOutput, 0};

  std::vector<NodeAndMoveInfo> moves{
      MoveAll(dq1, ArgType::kInput),                          // append all inputs from dq1 to new node
      MoveAll(dq2, ArgType::kInput),                          // append all inputs from dq2
      MoveAndAppend(q, ArgType::kInput, 1, ArgType::kInput),  // append scale (input 1) from q
      MoveAndAppend(q, ArgType::kInput, 2, ArgType::kInput),  // append zp (input 2) from q
      MoveAll(q, ArgType::kOutput)};                          // and use the outputs from q

  return moves;
}

// moves for replacing a node with a single variadic DQ input with the qlinear version
std::vector<NodeAndMoveInfo> VariadicMoves() {
  NTO::NodeLocation variadic_dq{NTO::NodeType::kInput, 0};
  NTO::NodeLocation q{NTO::NodeType::kOutput, 0};

  std::vector<NodeAndMoveInfo> moves{
      MoveAndAppend(q, ArgType::kInput, 1, ArgType::kInput),  // append scale (input 1) from q
      MoveAndAppend(q, ArgType::kInput, 2, ArgType::kInput),  // append zp (input 2) from q
      MoveAll(variadic_dq, ArgType::kInput),                  // append all inputs from all dq nodes
      MoveAll(q, ArgType::kOutput)};                          // and use the outputs from q

  return moves;
}

// moves for replacing a node with a Conv node with DQ inputs with the qlinear version
std::vector<NodeAndMoveInfo> ConvMoves() {
  NTO::NodeLocation dq_x{NTO::NodeType::kInput, 0};
  NTO::NodeLocation dq_w{NTO::NodeType::kInput, 1};
  NTO::NodeLocation dq_bias{NTO::NodeType::kInput, 2};
  NTO::NodeLocation q{NTO::NodeType::kOutput, 0};

  std::vector<NodeAndMoveInfo> moves{
      MoveAll(dq_x, ArgType::kInput),                                     // append all inputs from x
      MoveAll(dq_w, ArgType::kInput),                                     // append all inputs from w
      MoveAndAppend(q, ArgType::kInput, 1, ArgType::kInput),              // append scale (input 1) from q
      MoveAndAppend(q, ArgType::kInput, 2, ArgType::kInput),              // append zp (input 2) from q
      MoveAndAppend(dq_bias, ArgType::kInput, 0, ArgType::kInput, true),  // (optional) append bias
      MoveAll(q, ArgType::kOutput)};                                      // and use the outputs from q

  return moves;
}
std::vector<NodeAndMoveInfo> WhereMoves() {
  NTO::NodeLocation dq_x{NTO::NodeType::kInput, 0};
  NTO::NodeLocation dq_y{NTO::NodeType::kInput, 1};
  NTO::NodeLocation target{NTO::NodeType::kTarget, 0};
  NTO::NodeLocation q{NTO::NodeType::kOutput, 0};

  std::vector<NodeAndMoveInfo> moves{
      MoveAndAppend(target, ArgType::kInput, 0, ArgType::kInput),  // move the condition to the new node
      MoveAll(dq_x, ArgType::kInput),                              // append all inputs from x
      MoveAll(dq_y, ArgType::kInput),                              // append all inputs from x
      MoveAndAppend(q, ArgType::kInput, 1, ArgType::kInput),       // append scale (input 1) from q
      MoveAndAppend(q, ArgType::kInput, 2, ArgType::kInput),       // append zp (input 2) from q
      MoveAll(q, ArgType::kOutput)};
  return moves;
}
QDQReplaceWithNew SplitReplacer(bool has_split_as_input) {
  NTO::NodeLocation dq{NTO::NodeType::kInput, 0};
  NTO::NodeLocation target{NTO::NodeType::kTarget, 0};
  NTO::NodeLocation q{NTO::NodeType::kOutput, 0};
  std::vector<NodeAndMoveInfo> moves{MoveAndAppend(dq, ArgType::kInput, 0, ArgType::kInput)};

  if (has_split_as_input) {
    // Move the optional split input to the new node.
    moves.push_back(MoveAndAppend(target, ArgType::kInput, 1, ArgType::kInput, true));
  }

  moves.push_back(MoveAll(q, ArgType::kOutput));

  return QDQReplaceWithNew(kOnnxDomain, "Split", std::move(moves));
}

QDQReplaceWithNew MatMulIntToFloatReplacer() {
  NTO::NodeLocation dq1{NTO::NodeType::kInput, 0};
  NTO::NodeLocation dq2{NTO::NodeType::kInput, 1};
  NTO::NodeLocation target{NTO::NodeType::kTarget, 0};

  std::vector<NodeAndMoveInfo> moves{
      MoveAndAppend(dq1, ArgType::kInput, 0, ArgType::kInput),
      MoveAndAppend(dq2, ArgType::kInput, 0, ArgType::kInput),
      MoveAndAppend(dq1, ArgType::kInput, 1, ArgType::kInput),
      MoveAndAppend(dq2, ArgType::kInput, 1, ArgType::kInput),
      MoveAndAppend(dq1, ArgType::kInput, 2, ArgType::kInput),
      MoveAndAppend(dq2, ArgType::kInput, 2, ArgType::kInput),
      MoveAll(target, ArgType::kOutput)};

  return QDQReplaceWithNew(kMSDomain, "MatMulIntegerToFloat", std::move(moves));
}

struct SetOptionalZeroPoint {
  static void UpdateNodes(Graph&, const NodesToOptimize& selected_nodes);

 private:
  // We assume this function won't fail
  static const ONNX_NAMESPACE::TensorProto init_optional_zero_point_int8() {
    // guid as arbitrary name to provide a unique value
    const char* const name = "init_optional_zero_point_int8_b33fd0fa-cd7b-4b10-ae5a-df64cabfe1f8";
    std::array<uint8_t, 1> a{0};
    ONNX_NAMESPACE::TensorProto tensor_proto;
    tensor_proto.set_name(name);
    tensor_proto.set_data_type(ONNX_NAMESPACE::TensorProto_DataType_INT8);
    tensor_proto.set_raw_data(a.data(), sizeof(int8_t));

    return tensor_proto;
  };

  // We assume this function won't fail
  static const ONNX_NAMESPACE::TensorProto init_optional_zero_point_uint8() {
    // guid as arbitrary name to provide a unique value
    const char* const name = "init_optional_zero_point_uint8_b33f88f7-c464-43e3-8692-97ac832bb14a";
    std::array<uint8_t, 1> a{0};
    ONNX_NAMESPACE::TensorProto tensor_proto;
    tensor_proto.set_name(name);
    tensor_proto.set_data_type(ONNX_NAMESPACE::TensorProto_DataType_UINT8);
    tensor_proto.set_raw_data(a.data(), sizeof(uint8_t));

    return tensor_proto;
  };
  static ONNX_NAMESPACE::TensorProto GetOptionalZeroPointInt8() {
    static ONNX_NAMESPACE::TensorProto proto = init_optional_zero_point_int8();
    return proto;
  }
  static ONNX_NAMESPACE::TensorProto GetOptionalZeroPointUint8() {
    static ONNX_NAMESPACE::TensorProto proto = init_optional_zero_point_uint8();
    return proto;
  }
};

void SetOptionalZeroPoint::UpdateNodes(Graph& graph, const NodesToOptimize& selected_nodes) {
  const auto nodes = selected_nodes.AllNodes();
  for (Node* node_ptr : nodes) {
    if (node_ptr == nullptr) {
      continue;
    }

    Node& node = *node_ptr;

    bool is_dq = node.OpType() == DQOpName;
    bool is_q = node.OpType() == QOpName;
    if (!is_dq && !is_q) {
      continue;
    }

    std::vector<NodeArg*>& input_defs = node.MutableInputDefs();
    bool has_zp_input = input_defs.size() == 3;
    if (has_zp_input && input_defs[InputIndex::ZERO_POINT_ID]->Exists()) {
      continue;  // zero point was set. No need to fill.
    }

    bool is_default_zp_signed = false;
    if (is_dq) {
      auto input_type = input_defs[0]->TypeAsProto()->tensor_type().elem_type();
      is_default_zp_signed = ONNX_NAMESPACE::TensorProto_DataType_INT8 == input_type;
    }

    const ONNX_NAMESPACE::TensorProto& zp_tensor_proto = is_default_zp_signed
                                                             ? GetOptionalZeroPointInt8()
                                                             : GetOptionalZeroPointUint8();

    const ONNX_NAMESPACE::TensorProto* dummy_zp_tensor_proto;
    if (!graph.GetInitializedTensor(zp_tensor_proto.name(), dummy_zp_tensor_proto)) {
      graph.AddInitializedTensor(zp_tensor_proto);
    }

    auto& node_arg = graph.GetOrCreateNodeArg(zp_tensor_proto.name(), nullptr);
    if (!has_zp_input) {
      input_defs.push_back(&node_arg);
    } else {
      input_defs[InputIndex::ZERO_POINT_ID] = &node_arg;
    }
  }
}

}  // namespace

Status QDQReplaceWithNew::Run(Graph& graph, const NodesToOptimize& selected_nodes) const {
  SetOptionalZeroPoint::UpdateNodes(graph, selected_nodes);
  return ReplaceWithNew::Run(graph, selected_nodes);
}

#if !defined(ORT_MINIMAL_BUILD)
Status QDQReplaceWithNew::RunForSave(Graph& graph, const NodesToOptimize& selected_nodes,
                                     const SatRuntimeOptimizationSaveContext& save_context,
                                     SavedState& saved_state, bool& graph_modified) const {
  SetOptionalZeroPoint::UpdateNodes(graph, selected_nodes);
  graph_modified = true;
  return ReplaceWithNew::RunForSave(graph, selected_nodes, save_context, saved_state, graph_modified);
}
#endif  // !defined(ORT_MINIMAL_BUILD)

UnaryReplaceWithQLinear::UnaryReplaceWithQLinear(std::string domain)
    : ReplaceWithQLinear(std::move(domain), UnaryMoves()) {
}

NodeAttributes UnaryReplaceWithQLinear::ExtraAttributes(const RuntimeState& state) const {
  const auto& target = state.selected_nodes.Target();
  NodeAttributes attr;
  if (target.OpType() == "Softmax") {
    attr["opset"] = utils::MakeAttribute(std::string("opset"), int64_t(target.SinceVersion()));
  }
  return attr;
}

BinaryReplaceWithQLinear::BinaryReplaceWithQLinear(std::string domain)
    : ReplaceWithQLinear(std::move(domain), BinaryMoves()) {
}

VariadicReplaceWithQLinear::VariadicReplaceWithQLinear(std::string domain)
    : ReplaceWithQLinear(std::move(domain), VariadicMoves()) {
}

ConvReplaceWithQLinear::ConvReplaceWithQLinear()
    : ReplaceWithQLinear(kOnnxDomain, ConvMoves()) {
}
WhereReplaceWithQLinear::WhereReplaceWithQLinear()
    : ReplaceWithQLinear(kMSDomain, WhereMoves()) {
}
MatMulReplaceWithQLinear::MatMulReplaceWithQLinear()
    : matmul_int_to_float_replacer_{MatMulIntToFloatReplacer()},
      qlinear_matmul_replacer_{kOnnxDomain} {
}

Status SplitReplaceWithQuant::Run(Graph& graph, const NodesToOptimize& selected_nodes) const {
  const auto& target_node = selected_nodes.Target();
  const auto& input_defs = target_node.InputDefs();

  // The 'split' attribute became an optional input at opset 13.
  bool has_split_as_input = target_node.SinceVersion() >= 13 && input_defs.size() == 2;
  return SplitReplacer(has_split_as_input).Run(graph, selected_nodes);
}

Status MatMulReplaceWithQLinear::Run(Graph& graph, const NodesToOptimize& selected_nodes) const {
  // if the output is empty there were no Q nodes selected, so replace with MatMulIntegerToFloat
  // otherwise replace with QLinearMatMul
  bool matmul_integer_to_float = selected_nodes.num_outputs == 0;
  if (matmul_integer_to_float) {
    return matmul_int_to_float_replacer_.Run(graph, selected_nodes);
  } else {
    return qlinear_matmul_replacer_.Run(graph, selected_nodes);
  }
}

DQMatMulReplaceWithMatMulNBits::DQMatMulReplaceWithMatMulNBits(int64_t accuracy_level)
    : accuracy_level_{accuracy_level},
      domain_{kMSDomain},
      op_type_{"MatMulNBits"},
      value_moves_{[]() {
        NTO::NodeLocation target{NTO::NodeType::kTarget, 0};
        return std::vector<NodeAndMoveInfo>{
            MoveAndAppend(target, ArgType::kInput, 0, ArgType::kInput),
            MoveAll(target, ArgType::kOutput)};
      }()} {
}

NodeAttributes DQMatMulReplaceWithMatMulNBits::ExtraAttributes(const Graph&, const NodesToOptimize& selected_nodes) const {
  NodeAttributes extra_attributes;

  const auto* dq_node = selected_nodes.Input(0);
  auto& attrs = dq_node->GetAttributes();
  const auto* weight_shape = dq_node->InputDefs()[0]->Shape();

  ORT_ENFORCE(weight_shape->dim(0).has_dim_value() && weight_shape->dim(1).has_dim_value(),
              "Input x of DQ node must have rank 2 shape dimensions");

  utils::SetNodeAttribute(utils::MakeAttribute("K", weight_shape->dim(0).dim_value()), extra_attributes);
  utils::SetNodeAttribute(utils::MakeAttribute("N", weight_shape->dim(1).dim_value()), extra_attributes);
  if (accuracy_level_ > -1) {
    utils::SetNodeAttribute(utils::MakeAttribute("accuracy_level", accuracy_level_), extra_attributes);
  }
  // currently only 4bits is supported. In the future, derive bits from DQ's weight type.
  utils::SetNodeAttribute(utils::MakeAttribute("bits", static_cast<int64_t>(4)), extra_attributes);
  utils::SetNodeAttribute(utils::MakeAttribute("block_size", attrs.at("block_size").i()), extra_attributes);

  return extra_attributes;
}

void DQMatMulReplaceWithMatMulNBits::AddTransposedInitializers(Graph& graph,
                                                               const NodesToOptimize& selected_nodes,
                                                               Node& replacement_node) const {
  const auto* dq_node = selected_nodes.Input(0);
  const auto* weight_arg = dq_node->InputDefs()[0];
  const auto* scale_arg = dq_node->InputDefs()[1];
  const auto* zp_arg = dq_node->InputDefs().size() > 2 ? dq_node->InputDefs()[2] : nullptr;
  const auto& attrs = dq_node->GetAttributes();

  const ONNX_NAMESPACE::TensorProto* weight_tensor_proto = nullptr;
  const ONNX_NAMESPACE::TensorProto* scale_tensor_proto = nullptr;
  const ONNX_NAMESPACE::TensorProto* zp_tensor_proto = nullptr;
  graph.GetInitializedTensor(weight_arg->Name(), weight_tensor_proto);
  graph.GetInitializedTensor(scale_arg->Name(), scale_tensor_proto);
  if (zp_arg) {
    graph.GetInitializedTensor(zp_arg->Name(), zp_tensor_proto);
  }

  auto K = weight_arg->Shape()->dim(0).dim_value();
  auto N = weight_arg->Shape()->dim(1).dim_value();
  auto block_size = attrs.at("block_size").i();
  auto quant_num = (K + block_size - 1) / block_size;
  auto blob_bytes = (block_size + 1) / 2;

  // Unfortunately iterating the source data is complicated, the data maybe in
  // external file, a raw buffer, or a repeated field depending on the data
  // type.  UnpackTensor() already contains some of these logic and is closest
  // to what we need. But it does not handle external data.
  Initializer weight_src(*weight_tensor_proto, graph.ModelPath());
  Initializer scale_src(*scale_tensor_proto, graph.ModelPath());
  std::unique_ptr<Initializer> zp_src_ptr = nullptr;
  Initializer weight_dst(ONNX_NAMESPACE::TensorProto_DataType_UINT8,
                         graph.GenerateNodeArgName(weight_arg->Name() + "_T"),
                         std::vector<int64_t>{N, quant_num, blob_bytes});
  Initializer scale_dst(static_cast<ONNX_NAMESPACE::TensorProto_DataType>(scale_src.data_type()),
                        graph.GenerateNodeArgName(scale_arg->Name() + "_T"),
                        std::vector<int64_t>{N * quant_num});
  std::unique_ptr<Initializer> zp_dst_ptr = nullptr;

  if (zp_tensor_proto) {
    zp_src_ptr = std::make_unique<Initializer>(*zp_tensor_proto, graph.ModelPath());
    zp_dst_ptr = std::make_unique<Initializer>(ONNX_NAMESPACE::TensorProto_DataType_UINT8,
                                               graph.GenerateNodeArgName(zp_arg->Name() + "_T"),
                                               std::vector<int64_t>{N * ((quant_num + 1) / 2)});
  }

  OrtThreadPoolParams to;
  auto tp = concurrency::CreateThreadPool(&onnxruntime::Env::Default(), to,
                                          concurrency::ThreadPoolType::INTRA_OP);

  if (scale_src.data_type() == ONNX_NAMESPACE::TensorProto_DataType_FLOAT) {
    MlasQDQTransposeBlockwiseQuantized<float, 4>(weight_src.DataAsByteSpan().data(),
                                                 scale_src.data<float>(),
                                                 zp_src_ptr ? zp_src_ptr->DataAsByteSpan().data() : nullptr,
                                                 weight_dst.data<uint8_t>(),
                                                 scale_dst.data<float>(),
                                                 zp_dst_ptr ? zp_dst_ptr->data<uint8_t>() : nullptr,
                                                 true,
                                                 static_cast<int>(K),
                                                 static_cast<int>(N),
                                                 static_cast<int>(block_size),
                                                 tp.get());
  } else {
    MlasQDQTransposeBlockwiseQuantized<MLFloat16, 4>(weight_src.DataAsByteSpan().data(),
                                                     scale_src.data<MLFloat16>(),
                                                     zp_src_ptr ? zp_src_ptr->DataAsByteSpan().data() : nullptr,
                                                     weight_dst.data<uint8_t>(),
                                                     scale_dst.data<MLFloat16>(),
                                                     zp_dst_ptr ? zp_dst_ptr->data<uint8_t>() : nullptr,
                                                     true,
                                                     static_cast<int>(K),
                                                     static_cast<int>(N),
                                                     static_cast<int>(block_size),
                                                     tp.get());
  }

  ONNX_NAMESPACE::TensorProto weight_T_tp;
  ONNX_NAMESPACE::TensorProto scale_T_tp;
  std::unique_ptr<ONNX_NAMESPACE::TensorProto> zp_T_tp_ptr = nullptr;

  weight_dst.ToProto(weight_T_tp);
  scale_dst.ToProto(scale_T_tp);
  if (zp_dst_ptr) {
    zp_T_tp_ptr = std::make_unique<ONNX_NAMESPACE::TensorProto>();
    zp_dst_ptr->ToProto(*zp_T_tp_ptr);
  }

  auto& input_defs = replacement_node.MutableInputDefs();
  input_defs.push_back(&graph_utils::AddInitializer(graph, weight_T_tp));
  replacement_node.MutableInputArgsCount().push_back(1);
  input_defs.push_back(&graph_utils::AddInitializer(graph, scale_T_tp));
  replacement_node.MutableInputArgsCount().push_back(1);

  if (zp_T_tp_ptr) {
    input_defs.push_back(&graph_utils::AddInitializer(graph, *zp_T_tp_ptr));
    replacement_node.MutableInputArgsCount().push_back(1);
  }
}

Status DQMatMulReplaceWithMatMulNBits::Run(Graph& graph, const NodesToOptimize& selected_nodes) const {
  const auto attributes = ExtraAttributes(graph, selected_nodes);
  const auto& target = selected_nodes.Target();

  // create node. we'll populate the input and output defs via moves
  auto& replacement = graph.AddNode(target.Name(),
                                    op_type_,
                                    target.Description(),
                                    {},  // input defs
                                    {},  // output defs
                                    &attributes,
                                    domain_);

  const auto& target_provider = target.GetExecutionProviderType();
  replacement.SetExecutionProviderType(target_provider.empty() ? kCpuExecutionProvider : target_provider);

  ORT_RETURN_IF_ERROR(MoveInputOutput(graph, selected_nodes, replacement, value_moves_, false));

  AddTransposedInitializers(graph, selected_nodes, replacement);

  return node_remover_.Run(graph, selected_nodes);
}

static std::vector<NodeAndMoveInfo> GetGemmMoveInfo(bool does_q_node_exist) {
  NTO::NodeLocation dq_A{NTO::NodeType::kInput, 0};
  NTO::NodeLocation dq_B{NTO::NodeType::kInput, 1};
  NTO::NodeLocation dq_bias{NTO::NodeType::kInput, 2};
  NTO::NodeLocation target{NTO::NodeType::kTarget, 0};
  NTO::NodeLocation q{NTO::NodeType::kOutput, 0};

  std::vector<NodeAndMoveInfo> moves{
      MoveAll(dq_A, ArgType::kInput),                                            // append all inputs from DQ of A
      MoveAll(dq_B, ArgType::kInput),                                            // append all inputs from DQ of B
      MoveAndAppend(dq_bias, ArgType::kInput, 0, ArgType::kInput, true, true)};  // (optional) append bias

  if (does_q_node_exist) {
    moves.push_back(MoveAndAppend(q, ArgType::kInput, 1, ArgType::kInput));  // append scale (input 1) from Q
    moves.push_back(MoveAndAppend(q, ArgType::kInput, 2, ArgType::kInput));  // append zp (input 2) from Q
    moves.push_back(MoveAll(q, ArgType::kOutput));                           // and use the outputs from Q
  } else {
    moves.push_back(MoveAll(target, ArgType::kOutput));
  }

  return moves;
}

GemmReplaceWithQuant::GemmReplaceWithQuant()
    : qgemm_with_float_as_output_replacer_(kMSDomain, "QGemm", GetGemmMoveInfo(false)),
      qgemm_with_8bits_as_output_replacer_(kMSDomain, "QGemm", GetGemmMoveInfo(true)) {
}

Status GemmReplaceWithQuant::Run(Graph& graph, const NodesToOptimize& selected_nodes) const {
  RemoveAttrBeta(selected_nodes);
  bool is_output_float = selected_nodes.num_outputs == 0;
  if (is_output_float) {
    return qgemm_with_float_as_output_replacer_.Run(graph, selected_nodes);
  }

  return qgemm_with_8bits_as_output_replacer_.Run(graph, selected_nodes);
}

#if !defined(ORT_MINIMAL_BUILD)
Status GemmReplaceWithQuant::RunForSave(Graph& graph,
                                        const NodesToOptimize& selected_nodes,
                                        const SatRuntimeOptimizationSaveContext& save_context,
                                        SavedState& saved_state,
                                        bool& graph_modified) const {
  RemoveAttrBeta(selected_nodes);
  bool is_output_float = selected_nodes.num_outputs == 0;
  if (is_output_float) {
    return qgemm_with_float_as_output_replacer_.RunForSave(graph,
                                                           selected_nodes,
                                                           save_context,
                                                           saved_state,
                                                           graph_modified);
  }

  return qgemm_with_8bits_as_output_replacer_.RunForSave(graph,
                                                         selected_nodes,
                                                         save_context,
                                                         saved_state,
                                                         graph_modified);
}
#endif  // !defined(ORT_MINIMAL_BUILD)

}  // namespace QDQ
}  // namespace onnxruntime
