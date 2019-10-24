// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifdef _WIN32
// disable some warnings from protobuf to pass Windows build
#pragma warning(disable : 4244)
#endif

#include <cassert>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stack>

#include "gsl/gsl"
#include "core/common/logging/logging.h"
#include "core/framework/tensor_shape.h"
#include "core/framework/tensorprotoutils.h"
#include "core/framework/utils.h"
#include "core/graph/function.h"
#include "core/graph/function_impl.h"
#include "core/graph/graph_utils.h"
#include "core/graph/graph_viewer.h"
#include "core/graph/indexed_sub_graph.h"
#include "core/graph/schema_registry.h"
#include "core/graph/op.h"

#include "onnx/checker.h"

using namespace ONNX_NAMESPACE;
using namespace ONNX_NAMESPACE::Utils;
using namespace ONNX_NAMESPACE::checker;
using namespace ::onnxruntime::common;

namespace onnxruntime {

#define NO_CHANGE_ON_SYNC_FLAG(...)                  \
  do {                                               \
    const bool sync_needed = GraphProtoSyncNeeded(); \
    { __VA_ARGS__; }                                 \
    GraphProtoSyncNeeded(sync_needed);               \
  } while (0)

static bool UsingLatestOnnxOpset(const DomainToVersionMap& opset_versions) {
  bool is_latest_opset = false;
  auto onnx_opset = opset_versions.find(kOnnxDomain);

  if (onnx_opset != opset_versions.cend()) {
    static int latest_onnx_version =
        ONNX_NAMESPACE::OpSchemaRegistry::DomainToVersionRange().Map().at(ONNX_NAMESPACE::ONNX_DOMAIN).second;

    if (onnx_opset->second == latest_onnx_version) {
      is_latest_opset = true;
    }
  }

  return is_latest_opset;
}

static Status MergeShapeInfo(const std::string& output_name,
                             const TypeProto_Tensor& source, TypeProto_Tensor& target,
                             bool strict) {
  try {
    ONNX_NAMESPACE::mergeInShapeInfo(source, target);
  } catch (const ONNX_NAMESPACE::InferenceError& ex) {
    // if this model was not created with the latest onnx version, allow the shape inferencing failure (strict == false).
    // we do this to have strict testing of the latest inferencing to detect bugs, but lenient shape inferencing for
    // older models in case later changes to the ONNX shape inferencing or ORT break them.
    if (!strict) {
      // mergeInShapeInfo does nothing unless source.shape() is not null, and there would be no conflict if
      // target.shape() was empty. 'assert' just in case that ever changes.
      assert(utils::HasShape(source) && utils::HasShape(target));
      LOGS_DEFAULT(WARNING) << "Error merging shape info for output. '" << output_name
                            << "' source:" << source.shape() << " target:" << target.shape()
                            << ". Falling back to lenient merge.";
      ONNX_NAMESPACE::UnionShapeInfo(source.shape(), target);
    } else {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Output:", output_name, " ", ex.what());
    }
  }

  return Status::OK();
}

static bool GraphLoadedFromModelFile(const GraphProto* graph_proto) {
  return graph_proto && (graph_proto->node_size() != 0 ||
                         graph_proto->output_size() != 0);
}

// there are some known invalid usages of dim_param and dim_value. remove them from the TypeProto so that
// they don't affect shape inferencing or the allocation planner
static void RemoveInvalidValues(ONNX_NAMESPACE::TypeProto& type) {
  if (utils::HasTensorType(type) && utils::HasShape(type.tensor_type())) {
    auto* shape = type.mutable_tensor_type()->mutable_shape();
    for (int i = 0, end = shape->dim_size(); i < end; ++i) {
      auto& dim = *shape->mutable_dim(i);
      if (utils::HasDimParam(dim)) {
        if (dim.dim_param().empty()) {
          dim.clear_dim_param();
        }
      } else if (utils::HasDimValue(dim)) {
        if (dim.dim_value() < 0) {
          dim.clear_dim_value();
        }
      }
    }
  }
}

static TypeProto TypeProtoFromTensorProto(const TensorProto& tensor) {
  TypeProto t;
  t.mutable_tensor_type()->set_elem_type(tensor.data_type());
  auto shape = t.mutable_tensor_type()->mutable_shape();
  for (auto dim : tensor.dims())
    shape->add_dim()->set_dim_value(dim);

  return t;
}

NodeArg::NodeArg(const std::string& name, const TypeProto* p_node_arg_type) {
  node_arg_info_.set_name(name);
  // If the name is empty, it means the arg does not exist.
  exists_ = !(name.empty());
  if (nullptr != p_node_arg_type) {
    (*node_arg_info_.mutable_type()) = *p_node_arg_type;
    RemoveInvalidValues(*node_arg_info_.mutable_type());
    type_ = DataTypeUtils::ToType(node_arg_info_.type());
  } else {
    type_ = nullptr;
  }
}

const std::string& NodeArg::Name() const noexcept {
  return node_arg_info_.name();
}

DataType NodeArg::Type() const noexcept {
  return type_;
}

const TypeProto* NodeArg::TypeAsProto() const noexcept {
  if (utils::HasType(node_arg_info_))
    return &node_arg_info_.type();

  return nullptr;
}

const TensorShapeProto* NodeArg::Shape() const {
  const TypeProto* type = TypeAsProto();
  if (type == nullptr) return nullptr;
  const auto typeCase = type->value_case();
  switch (typeCase) {
    case TypeProto::kTensorType: {
      if (utils::HasShape(type->tensor_type())) {
        return &(type->tensor_type().shape());
      }
      return nullptr;
    }
    case TypeProto::kSparseTensorType: {
      if (utils::HasShape(type->sparse_tensor_type())) {
        return &(type->sparse_tensor_type().shape());
      }
      return nullptr;
    }
    case TypeProto::kSequenceType:
    case TypeProto::kMapType:
    case TypeProto::kOpaqueType:
    case TypeProto::VALUE_NOT_SET:
    default:
      return nullptr;
  }
}

void NodeArg::SetShape(const TensorShapeProto& shape) {
  const auto type_case = node_arg_info_.type().value_case();
  switch (type_case) {
    case TypeProto::kTensorType:
      *(node_arg_info_.mutable_type()->mutable_tensor_type()->mutable_shape()) = shape;
      break;
    case TypeProto::kSparseTensorType:
      *(node_arg_info_.mutable_type()->mutable_sparse_tensor_type()->mutable_shape()) = shape;
      break;
    case TypeProto::kSequenceType:
    case TypeProto::kMapType:
    case TypeProto::kOpaqueType:
    case TypeProto::VALUE_NOT_SET:
    default:
      return;
  }
}

void NodeArg::ClearShape() {
  const auto type_case = node_arg_info_.type().value_case();
  switch (type_case) {
    case TypeProto::kTensorType:
      node_arg_info_.mutable_type()->mutable_tensor_type()->clear_shape();
      break;
    case TypeProto::kSparseTensorType:
      node_arg_info_.mutable_type()->mutable_sparse_tensor_type()->clear_shape();
      break;
    case TypeProto::kSequenceType:
    case TypeProto::kMapType:
    case TypeProto::kOpaqueType:
    case TypeProto::VALUE_NOT_SET:
    default:
      return;
  }
}

common::Status NodeArg::UpdateTypeAndShape(const ONNX_NAMESPACE::TypeProto& input_type, bool strict) {
  if (!utils::HasType(node_arg_info_)) {
    *node_arg_info_.mutable_type() = input_type;
    type_ = DataTypeUtils::ToType(node_arg_info_.type());
    return Status::OK();
  }

  auto& current_type = *node_arg_info_.mutable_type();
  const auto current_type_case = current_type.value_case();
  const auto input_type_case = input_type.value_case();

  if (current_type_case != input_type_case)
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Type mismatch. Current=",
                           current_type_case, " Input=", input_type_case);

  switch (input_type_case) {
    case TypeProto::kTensorType: {
      const auto& input_tensor_type = input_type.tensor_type();
      const auto& input_tensor_elem_type = input_tensor_type.elem_type();
      const auto& current_tensor_elem_type = current_type.tensor_type().elem_type();

      if (input_tensor_elem_type != current_tensor_elem_type)
        return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Tensor element type mismatch. ",
                               static_cast<TensorProto_DataType>(input_tensor_elem_type), " != ",
                               static_cast<TensorProto_DataType>(current_tensor_elem_type));

      if (utils::HasShape(input_tensor_type)) {
        auto& current_tensor_type = *current_type.mutable_tensor_type();
        if (utils::HasShape(current_tensor_type)) {
          ORT_RETURN_IF_ERROR(MergeShapeInfo(Name(), input_tensor_type, current_tensor_type, strict));
        } else {
          current_tensor_type = input_tensor_type;
        }
      }

      break;
    }
    case TypeProto::kSparseTensorType: {
      const auto& input_tensor_type = input_type.sparse_tensor_type();
      const auto input_tensor_elem_type = input_tensor_type.elem_type();
      const auto current_tensor_elem_type = current_type.sparse_tensor_type().elem_type();
      if (input_tensor_elem_type != current_tensor_elem_type) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "SparseTensor element type mismatch. ",
                               static_cast<TensorProto_DataType>(input_tensor_elem_type), " != ",
                               static_cast<TensorProto_DataType>(current_tensor_elem_type));
      }
      if (utils::HasShape(input_tensor_type)) {
        auto& current_tensor_type = *current_type.mutable_sparse_tensor_type();
        if (utils::HasShape(current_tensor_type)) {
          // TODO: Check if we need to merge shape here
          // if so we'd need to provide merging routine ONNX
          // mergeInShapeInfo(input_tensor_type, current_tensor_type);
        } else {
          current_tensor_type = input_tensor_type;
        }
      }
    } break;
    case TypeProto::kSequenceType:
    case TypeProto::kMapType:
    case TypeProto::kOpaqueType:
    case TypeProto::VALUE_NOT_SET:
      break;
  }

  return Status::OK();
}

common::Status NodeArg::UpdateTypeAndShape(const NodeArg& node_arg, bool strict) {
  auto status = Status::OK();

  if (utils::HasType(node_arg.node_arg_info_))
    status = UpdateTypeAndShape(node_arg.node_arg_info_.type(), strict);

  return status;
}

void NodeArg::SetType(DataType p_type) {
  if (nullptr == p_type) {
    return;
  }

  type_ = p_type;
  *(node_arg_info_.mutable_type()) = DataTypeUtils::ToTypeProto(p_type);
}

void NodeArg::SetType(const TypeProto& type_proto) {
  type_ = DataTypeUtils::ToType(type_proto);
  *(node_arg_info_.mutable_type()) = type_proto;
}

bool NodeArg::Exists() const noexcept {
  return exists_;
}

Node::EdgeEnd::EdgeEnd(const Node& node, int src_arg_index, int dst_arg_index) noexcept
    : node_(&node),
      src_arg_index_(src_arg_index),
      dst_arg_index_(dst_arg_index) {
}

Node::EdgeEnd::EdgeEnd(const Node& node) noexcept
    : EdgeEnd(node, INT_MAX, INT_MAX) {
}

const Node& Node::EdgeEnd::GetNode() const noexcept {
  return *node_;
}

int Node::EdgeEnd::GetSrcArgIndex() const {
  return src_arg_index_;
}

int Node::EdgeEnd::GetDstArgIndex() const {
  return dst_arg_index_;
}

Node::NodeConstIterator::NodeConstIterator(EdgeConstIterator p_iter) {
  m_iter = p_iter;
}

bool Node::NodeConstIterator::operator==(const NodeConstIterator& p_other) const {
  return m_iter == p_other.m_iter;
}

bool Node::NodeConstIterator::operator!=(const NodeConstIterator& p_other) const {
  return m_iter != p_other.m_iter;
}

void Node::NodeConstIterator::operator++() {
  ++m_iter;
}

void Node::NodeConstIterator::operator--() {
  --m_iter;
}

const Node& Node::NodeConstIterator::operator*() const {
  return (*m_iter).GetNode();
}

const Node* Node::NodeConstIterator::operator->() const {
  return &(operator*());
}

NodeIndex Node::Index() const noexcept {
  return index_;
}

const std::string& Node::Name() const noexcept {
  return name_;
}

const std::string& Node::OpType() const noexcept {
  return op_type_;
}

const std::string& Node::Description() const noexcept {
  return description_;
}

const std::string& Node::Domain() const noexcept {
  return domain_;
}

const OpSchema* Node::Op() const noexcept {
  return op_;
}

Node::Type Node::NodeType() const noexcept {
  return node_type_;
}

void Node::SetNodeType(Node::Type node_type) noexcept {
  node_type_ = node_type;
}

const Function* Node::GetFunctionBody() const noexcept {
  return func_body_;
}

void Node::SetFunctionBody(const Function& func) {
  func_body_ = &func;
  op_ = &func.OpSchema();
}

const std::string& Node::GetExecutionProviderType() const noexcept {
  return execution_provider_type_;
}

void Node::SetExecutionProviderType(ProviderType execution_provider_type) {
  execution_provider_type_ = execution_provider_type;
}

void Node::ToProto(NodeProto& proto) const {
  // Set name.
  proto.set_name(name_);
  // Set op type.
  proto.set_op_type(op_type_);
  // Set op domain;
  proto.set_domain(domain_);
  // Set doc string.
  proto.set_doc_string(description_);

  // Set attributes.
  proto.clear_attribute();
  for (const auto& attribute : attributes_) {
    const gsl::not_null<AttributeProto*> attr{proto.add_attribute()};
    *attr = attribute.second;
  }

  // Set inputs' definitions.
  proto.clear_input();
  for (auto& input_def : definitions_.input_defs) {
    proto.add_input(input_def->Name());
  }

  // Set outputs' definitions.
  proto.clear_output();
  for (auto& output_def : definitions_.output_defs) {
    proto.add_output(output_def->Name());
  }
}

void Node::Init(const std::string& name,
                const std::string& op_type,
                const std::string& description,
                const std::vector<NodeArg*>& input_args,
                const std::vector<NodeArg*>& output_args,
                const NodeAttributes* attributes,
                const std::string& domain) {
  name_ = name;
  op_type_ = op_type;
  description_ = description;
  definitions_.input_defs = input_args;
  definitions_.output_defs = output_args;
  domain_ = domain;
  if (kOnnxDomainAlias == domain_) {
    domain_ = kOnnxDomain;
  }

  // Set each arg count as 1 by default.
  // It could be adjusted when resolving the node with its operator
  // information.
  definitions_.input_arg_count.assign(input_args.size(), 1);

  if (attributes) {
    attributes_ = *attributes;

    for (auto& name_to_attr : attributes_) {
      if (utils::HasGraph(name_to_attr.second)) {
        CreateSubgraph(name_to_attr.first);
      }
    }
  }
}

Node::Definitions& Node::MutableDefinitions() noexcept {
  // someone fetching these is going to change something
  graph_->SetGraphResolveNeeded();
  graph_->SetGraphProtoSyncNeeded();
  return definitions_;
}

Node::Relationships& Node::MutableRelationships() noexcept {
  // someone fetching these is going to change something
  graph_->SetGraphResolveNeeded();
  graph_->SetGraphProtoSyncNeeded();
  return relationships_;
}

void Node::CreateSubgraph(const std::string& attr_name) {
  auto attr = attributes_.find(attr_name);

  if (attr != attributes_.cend() && utils::HasGraph(attr->second)) {
    GraphProto& mutable_graph = *attr->second.mutable_g();
    std::unique_ptr<Graph> subgraph{new Graph(*graph_, *this, mutable_graph)};
    attr_to_subgraph_map_.insert({std::string(attr_name), gsl::not_null<Graph*>{subgraph.get()}});
    subgraphs_.push_back(std::move(subgraph));
  }
}

void Node::AddAttribute(const std::string& attr_name, const AttributeProto& value) {
  graph_->SetGraphResolveNeeded();
  graph_->SetGraphProtoSyncNeeded();
  attributes_[attr_name] = value;
}

#define ADD_BASIC_ATTR_IMPL(type, enumType, field)                           \
  void Node::AddAttribute(const std::string& attr_name, const type& value) { \
    graph_->SetGraphResolveNeeded();                                         \
    graph_->SetGraphProtoSyncNeeded();                                       \
    AttributeProto a;                                                        \
    a.set_name(attr_name);                                                   \
    a.set_type(enumType);                                                    \
    a.set_##field(value);                                                    \
    attributes_[attr_name] = a;                                              \
  };

#define ADD_ATTR_IMPL(type, enumType, field)                                 \
  void Node::AddAttribute(const std::string& attr_name, const type& value) { \
    graph_->SetGraphResolveNeeded();                                         \
    graph_->SetGraphProtoSyncNeeded();                                       \
    AttributeProto a;                                                        \
    a.set_name(attr_name);                                                   \
    a.set_type(enumType);                                                    \
    *(a.mutable_##field()) = value;                                          \
    attributes_[attr_name] = a;                                              \
  };

#define ADD_LIST_ATTR_IMPL(type, enumType, field)            \
  void Node::AddAttribute(const std::string& attr_name,      \
                          const std::vector<type>& values) { \
    graph_->SetGraphResolveNeeded();                         \
    graph_->SetGraphProtoSyncNeeded();                       \
    AttributeProto a;                                        \
    a.set_name(attr_name);                                   \
    a.set_type(enumType);                                    \
    for (const auto& val : values) {                         \
      *(a.mutable_##field()->Add()) = val;                   \
    }                                                        \
    attributes_[attr_name] = a;                              \
  };

void Node::AddAttribute(const std::string& attr_name, const GraphProto& value) {
  graph_->SetGraphResolveNeeded();
  graph_->SetGraphProtoSyncNeeded();
  AttributeProto a;
  a.set_name(attr_name);
  a.set_type(AttributeProto_AttributeType::AttributeProto_AttributeType_GRAPH);
  *a.mutable_g() = value;
  attributes_[attr_name] = a;

  CreateSubgraph(attr_name);
};

ADD_BASIC_ATTR_IMPL(float, AttributeProto_AttributeType::AttributeProto_AttributeType_FLOAT, f)
ADD_BASIC_ATTR_IMPL(int64_t, AttributeProto_AttributeType::AttributeProto_AttributeType_INT, i)
ADD_BASIC_ATTR_IMPL(std::string, AttributeProto_AttributeType::AttributeProto_AttributeType_STRING, s)
ADD_ATTR_IMPL(TensorProto, AttributeProto_AttributeType::AttributeProto_AttributeType_TENSOR, t)
ADD_ATTR_IMPL(SparseTensorProto, AttributeProto_AttributeType::AttributeProto_AttributeType_SPARSE_TENSOR, sparse_tensor)
ADD_LIST_ATTR_IMPL(float, AttributeProto_AttributeType::AttributeProto_AttributeType_FLOATS, floats)
ADD_LIST_ATTR_IMPL(int64_t, AttributeProto_AttributeType::AttributeProto_AttributeType_INTS, ints)
ADD_LIST_ATTR_IMPL(std::string, AttributeProto_AttributeType::AttributeProto_AttributeType_STRINGS, strings)
ADD_LIST_ATTR_IMPL(TensorProto, AttributeProto_AttributeType::AttributeProto_AttributeType_TENSORS, tensors)
ADD_LIST_ATTR_IMPL(GraphProto, AttributeProto_AttributeType::AttributeProto_AttributeType_GRAPHS, graphs)
ADD_LIST_ATTR_IMPL(SparseTensorProto, AttributeProto_AttributeType::AttributeProto_AttributeType_SPARSE_TENSORS, sparse_tensors)

bool Node::ClearAttribute(const std::string& attr_name) {
  graph_->SetGraphResolveNeeded();
  graph_->SetGraphProtoSyncNeeded();
  return attributes_.erase(attr_name) > 0;
}

Status Node::UpdateInputArgCount() {
  // The node refers to a primitive operator.
  // Infer and verify node input arg type information.
  int total_arg_count = std::accumulate(definitions_.input_arg_count.cbegin(),
                                        definitions_.input_arg_count.cend(), 0);

  if (total_arg_count < 0 || static_cast<size_t>(total_arg_count) != definitions_.input_defs.size()) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL,
                           "This is an invalid model. "
                           "The sum of input arg count is not equal to size of input defs in node (",
                           name_, ")");
  }

  // op_ is always valid when this is called
  const ONNX_NAMESPACE::OpSchema& op = *Op();

  // Verify size of node arg count is same as input number in
  // operator definition.
  if (op.inputs().size() != definitions_.input_arg_count.size()) {
    // Adjust input arg count array with op definition
    // The adjustment will work as below,
    // In total, there're <total_arg_count> inputs, which
    // will be split as <1, 1, 1, 1, ... 1, x> or
    // <1, 1, 1, 1, ...1, 0, 0, ...0>. The final input
    // arg count array's element number will be the same
    // as op definition, and the sum of all elements will
    // be equal to <total_arg_count>.
    auto& input_arg_count = definitions_.input_arg_count;
    input_arg_count.clear();
    size_t m = 0;
    auto arg_count_left = total_arg_count;

    if (!op.inputs().empty()) {
      for (; m < op.inputs().size() - 1; ++m) {
        if (arg_count_left > 0) {
          input_arg_count.push_back(1);
          arg_count_left--;
        } else {
          input_arg_count.push_back(0);
        }
      }
    }

    // Set the arg count for the last input formal parameter.
    // NOTE: in the case that there's no .input(...) defined
    // in op schema, all input args will be fed as one input
    // of the operator.
    input_arg_count.push_back(arg_count_left);

    graph_->SetGraphResolveNeeded();
    graph_->SetGraphProtoSyncNeeded();
  }

  return Status::OK();
}

const NodeAttributes& Node::GetAttributes() const noexcept {
  return attributes_;
}

Graph* Node::GetMutableGraphAttribute(const std::string& attr_name) {
  Graph* subgraph = nullptr;

  const auto& entry = attr_to_subgraph_map_.find(attr_name);
  if (entry != attr_to_subgraph_map_.cend()) {
    subgraph = entry->second;
  }

  return subgraph;
}

const Graph* Node::GetGraphAttribute(const std::string& attr_name) const {
  return const_cast<Node*>(this)->GetMutableGraphAttribute(attr_name);
}

std::vector<gsl::not_null<const Graph*>> Node::GetSubgraphs() const {
  std::vector<gsl::not_null<const Graph*>> subgraphs;
  subgraphs.reserve(attr_to_subgraph_map_.size());
  using value_type = std::unordered_map<std::string, gsl::not_null<Graph*>>::value_type;
  std::transform(attr_to_subgraph_map_.cbegin(), attr_to_subgraph_map_.cend(), std::back_inserter(subgraphs),
                 [](const value_type& entry) { return entry.second; });

  return subgraphs;
}

void Node::ForEachDef(std::function<void(const onnxruntime::NodeArg&, bool is_input)> func,
                      bool include_missing_optional_defs) const {
  for (const auto* arg : InputDefs()) {
    if (include_missing_optional_defs || arg->Exists())
      func(*arg, true);
  }

  for (const auto* arg : ImplicitInputDefs()) {
    if (include_missing_optional_defs || arg->Exists())
      func(*arg, true);
  }

  for (const auto* arg : OutputDefs()) {
    if (include_missing_optional_defs || arg->Exists())
      func(*arg, false);
  }
};

void Node::ReplaceDefs(const std::map<const onnxruntime::NodeArg*, onnxruntime::NodeArg*>& replacements) {
  std::vector<std::vector<NodeArg*>*> all_defs = {&definitions_.input_defs, &definitions_.output_defs};

  for (auto pair : replacements)
    for (auto* defs : all_defs)
      for (auto& def : *defs)
        if (def == pair.first)
          def = pair.second;
}

// Constructor: Given a <GraphProto> loaded from model file, construct
// a <Graph> object and Resolve() it.
//Status Graph::LoadGraph(const GraphProto& graph_proto,
//                        const std::unordered_map<std::string, int>& domain_to_version,
//                        Version ir_version,
//                        std::unique_ptr<Graph>& new_graph) {
//  // create instance. need to call private ctor so can't use make_unique
//  GSL_SUPPRESS(r .11)
//  new_graph.reset(new Graph(nullptr, &graph_proto, domain_to_version, ir_version));
//
//  // as we just loaded from file we want to fully initialize/Resolve, but not let that change
//  // the proto sync flag
//  auto status = new_graph->Resolve(/* no_proto_sync_required */ true);
//  return status;
//}
using google::protobuf::RepeatedPtrField;

Graph::Graph(GraphProto* graph_proto,
             const std::unordered_map<std::string, int>& domain_to_version,
             Version ir_version,
             IOnnxRuntimeOpSchemaCollectionPtr schema_registry,
             const std::unordered_map<std::string, const ONNX_NAMESPACE::FunctionProto*>& model_functions)
    : Graph(graph_proto, domain_to_version, ir_version, schema_registry, nullptr, nullptr, model_functions) {}

Graph::Graph(GraphProto* graph_proto, const std::unordered_map<std::string, int>& domain_to_version, Version ir_version,
             IOnnxRuntimeOpSchemaCollectionPtr schema_registry, Graph* parent_graph, const Node* parent_node,
             const std::unordered_map<std::string, const ONNX_NAMESPACE::FunctionProto*>& model_functions)
    : graph_proto_(graph_proto),
      schema_registry_(schema_registry),
      graph_resolve_needed_(true),
      domain_to_version_(domain_to_version),
      model_functions_(model_functions),
      ir_version_(ir_version),
      using_latest_onnx_opset_(UsingLatestOnnxOpset(domain_to_version)),
      parent_graph_(parent_graph),
      parent_node_(parent_node) {
  ORT_ENFORCE(graph_proto != nullptr, "graph_proto cannot be null");
  ArgNameToTypeMap name_to_type_map;

  // Process 'Constant' nodes
  // Put the 'TensorProto' stored in the 'Constant' nodes attribute into the graphs initializer list
  for (auto& node : graph_proto_->node()) {
    if (node.op_type() != kConstant) {
      continue;
    }

    // Copy constant nodes _value to name_to_initial_tensor_
    const gsl::not_null<TensorProto*> tensor{graph_proto_->add_initializer()};
    const AttributeProto& constant_attribute = node.attribute(0);
    // TODO: Add support for parsing 'sparse_value' attribute from a 'Constant' node
    // Discussion surrounding handling the SparseTensorProto must be had.
    // An easy way is to implement a method that converts a SparseTensorproto into a TensorProto
    // to use the same downstream flow, but that is going to impact peak memory usage and probably a smarter way is required.
    ORT_ENFORCE(constant_attribute.has_t(), "Only 'value' attribute is supported within a 'Constant' node in ORT");
    *tensor = constant_attribute.t();
    *(tensor->mutable_name()) = node.output(0);
  }

  // Remove constant nodes as they're replaced with initializers above.
  const gsl::not_null<RepeatedPtrField<NodeProto>*> graph_mutable_nodes{graph_proto_->mutable_node()};
  graph_mutable_nodes->erase(
      std::remove_if(graph_mutable_nodes->begin(), graph_mutable_nodes->end(),
                     [](NodeProto& p) {
                       return (p.op_type() == kConstant);
                     }),
      graph_mutable_nodes->end());

  // Collect all node arg name, type, shape information in the graph.
  // type/shape information will be assigned to each node arg when going
  // thru all nodes later.

  // process graph inputs first as we want the type/shape from them to be preferred if a graph input
  // has a matching initializer
  for (auto& graph_input : graph_proto_->input()) {
    if (utils::HasName(graph_input) && utils::HasType(graph_input)) {
      name_to_type_map[graph_input.name()] = graph_input.type();
      GetOrCreateNodeArg(graph_input.name(), &graph_input.type());
    }
  }

  // Copy initial tensors to a map.
  for (auto& tensor : graph_proto_->initializer()) {
    name_to_initial_tensor_[tensor.name()] = &tensor;

    NodeArg* matching_graph_input = GetNodeArg(tensor.name());
    TypeProto t{TypeProtoFromTensorProto(tensor)};

    if (ir_version_ < 4) {
      // initializers can have matching graph inputs but are treated as constant,
      // so we prefer the shape from the initializer
      name_to_type_map[tensor.name()] = t;
      if (matching_graph_input != nullptr) {
        ORT_THROW_IF_ERROR(matching_graph_input->UpdateTypeAndShape(t));
      }
    } else {
      // v4 and later allows a constant initializer with no matching graph input. create a NodeArg for these.
      // otherwise we prefer the shape from the graph input so leave matching_graph_input as is.
      if (matching_graph_input == nullptr) {
        name_to_type_map[tensor.name()] = t;
        ORT_IGNORE_RETURN_VALUE(GetOrCreateNodeArg(tensor.name(), &t));
      }
    }
  }

  for (auto& graph_output : graph_proto_->output()) {
    if (utils::HasName(graph_output) && utils::HasType(graph_output)) {
      auto& name = graph_output.name();
      name_to_type_map[name] = graph_output.type();
      // always create NodeArg for graph output, in case it's from initializer
      GetOrCreateNodeArg(name, &graph_output.type());
    }
  }

  for (auto& node_arg : graph_proto_->value_info()) {
    if (utils::HasName(node_arg) && utils::HasType(node_arg)) {
      name_to_type_map[node_arg.name()] = node_arg.type();
    }
  }

  for (const auto& node_proto : graph_proto_->node()) {
    AddNode(node_proto, name_to_type_map);
  }
}

Graph::Graph(Graph& parent_graph, const Node& parent_node, ONNX_NAMESPACE::GraphProto& subgraph_proto)
    : Graph(&subgraph_proto,
            parent_graph.DomainToVersionMap(), parent_graph.IrVersion(), parent_graph.schema_registry_,
            &parent_graph,
            &parent_node) {
}

Status Graph::VerifyNoDuplicateName() {
  auto& inputs_and_initializers = resolve_context_.inputs_and_initializers;
  auto& output_args = resolve_context_.output_args;
  auto& node_name_to_index = resolve_context_.node_name_to_index;

  output_args.clear();
  node_name_to_index.clear();
  // inputs_and_initializers: this is passed in as a parameter, since functions don't have initializers
  // but graphs have them.

  for (auto& node : Nodes()) {
    // Verify node name should be unique.
    auto& node_name = node.Name();

    if (!node_name.empty() && node_name_to_index.end() != node_name_to_index.find(node_name)) {
      // The node has name and its name was used by another node.
      Status status(ONNXRUNTIME, FAIL,
                    "This is an invalid model. Error: two nodes with same node name (" + node_name + ").");
      return status;
    }

    node_name_to_index[node_name] = node.Index();

    // Verify node outputs' name should be unique.
    int output_index = -1;
    for (const auto* output_def : node.OutputDefs()) {
      ++output_index;
      if (output_def->Exists()) {
        auto& output_arg_name = output_def->Name();
        if (inputs_and_initializers.count(output_arg_name)) {
          Status status(ONNXRUNTIME, FAIL,
                        "This is an invalid model. Error: Duplicate definition of name (" + output_arg_name + ").");
          return status;
        }
        auto result = output_args.insert({output_arg_name, {&node, output_index}});
        if (!result.second) {
          // Two outputs with same name, so that insertion fails.
          Status status(ONNXRUNTIME, FAIL,
                        "This is an invalid model. Error: Duplicate definition of name (" + output_arg_name + ").");
          return status;
        }
      }
    }
  }
  return Status::OK();
}

// Recurse into any subgraphs to update the list of NodeArg values in outer scope.
// This information is needed to resolve any dependencies on outer scope values.
common::Status Graph::SetOuterScopeNodeArgs(const std::unordered_set<std::string>& outer_scope_node_args) {
  resolve_context_.outer_scope_node_args = outer_scope_node_args;

  if (!resolve_context_.nodes_with_subgraphs.empty()) {
    // Build the list of NodeArg's that are valid for a subgraph of this GraphBase instance:
    //   - outer scope for this graph
    //   - any inputs/initializers from this graph
    //   - any outputs from nodes in this graph
    //
    // NOTE: We must add the most outer most NodeArgs first, and then local NodeArgs, as the local should override
    // an outer scope value if they have the same name.
    //
    // We provide outputs from all nodes in this graph at this stage.
    // BuildConnections will link the node with the subgraph to any outer scope Node/NodeArgs it consumes.
    // PerformTopologicalSortAndCheckIsAcyclic will validate these links.
    std::unordered_set<std::string> node_args_in_scope_for_subgraph = outer_scope_node_args;

    node_args_in_scope_for_subgraph.insert(resolve_context_.inputs_and_initializers.cbegin(),
                                           resolve_context_.inputs_and_initializers.cend());

    std::transform(resolve_context_.output_args.cbegin(), resolve_context_.output_args.cend(),
                   std::inserter(node_args_in_scope_for_subgraph, node_args_in_scope_for_subgraph.end()),
                   [](const std::pair<std::string, std::pair<Node*, int>>& entry) { return entry.first; });

    for (auto* node : resolve_context_.nodes_with_subgraphs) {
      for (auto& subgraph : node->MutableSubgraphs()) {
        auto status = subgraph->SetOuterScopeNodeArgs(node_args_in_scope_for_subgraph);
        ORT_RETURN_IF_ERROR(status);
      }
    }
  }

  return Status::OK();
}

NodeArg* Graph::GetNodeArgIncludingParentGraphs(const std::string& node_arg_name) {
  NodeArg* node_arg = GetNodeArg(node_arg_name);

  if (!node_arg && parent_graph_) {
    node_arg = parent_graph_->GetNodeArgIncludingParentGraphs(node_arg_name);
  }

  return node_arg;
}

void Graph::AddEdge(NodeIndex src_node_index, NodeIndex dst_node_index, int src_arg_slot, int dst_arg_slot) {
  if (nodes_.size() <= src_node_index || src_arg_slot < 0 || nodes_.size() <= dst_node_index || dst_arg_slot < 0 ||
      nullptr == nodes_[src_node_index] || nullptr == nodes_[dst_node_index]) {
    // Invalid node indexes specified.
    ORT_THROW("Invalid node indexes specified when adding edge.");
  }

  NodeArg* src_arg = nullptr;
  NodeArg* dst_arg = nullptr;
  if (nodes_[src_node_index]->MutableDefinitions().output_defs.size() > static_cast<size_t>(src_arg_slot)) {
    src_arg = nodes_[src_node_index]->MutableDefinitions().output_defs[src_arg_slot];
  }

  if (nullptr == src_arg) {
    ORT_THROW("Invalid source node arg slot specified when adding edge.");
  }

  auto& dst_node_defs = nodes_[dst_node_index]->MutableDefinitions();
  NodeArg** dst_arg_pointer = nullptr;
  if (dst_node_defs.input_defs.size() > static_cast<size_t>(dst_arg_slot)) {
    dst_arg_pointer = &dst_node_defs.input_defs[dst_arg_slot];
    dst_arg = *dst_arg_pointer;
  } else {
    auto num_of_explicit_inputs = dst_node_defs.input_defs.size();
    if (num_of_explicit_inputs + dst_node_defs.implicit_input_defs.size() > static_cast<size_t>(dst_arg_slot)) {
      dst_arg_pointer = &dst_node_defs.implicit_input_defs[dst_arg_slot - num_of_explicit_inputs];
      dst_arg = *dst_arg_pointer;
    }
  }
  if (nullptr == dst_arg) {
    ORT_THROW("Invalid destination node arg slot specified when adding edge.");
  }

  if (src_arg != dst_arg) {
    if (src_arg->Type() != dst_arg->Type()) {
      // The output type of source node arg does not match the input type of destination node arg.
      ORT_THROW("Argument type mismatch when adding edge.");
    }
    *dst_arg_pointer = src_arg;
  }

  nodes_[src_node_index]->MutableRelationships().output_edges.insert(Node::EdgeEnd(*nodes_[dst_node_index], src_arg_slot, dst_arg_slot));
  nodes_[dst_node_index]->MutableRelationships().input_edges.insert(Node::EdgeEnd(*nodes_[src_node_index], src_arg_slot, dst_arg_slot));
}

void Graph::RemoveEdge(NodeIndex src_node_index, NodeIndex dst_node_index, int src_arg_slot, int dst_arg_slot) {
  if (nodes_.size() <= src_node_index || src_arg_slot < 0 || nodes_.size() <= dst_node_index || dst_arg_slot < 0 ||
      nullptr == nodes_[src_node_index] || nullptr == nodes_[dst_node_index]) {
    // Invalid node indexes specified.
    ORT_THROW("Invalid node indexes specified when removing edge.");
  }

  const NodeArg* src_arg = nullptr;
  const NodeArg* dst_arg = nullptr;
  if (nodes_[src_node_index]->GetDefinitions().output_defs.size() > static_cast<size_t>(src_arg_slot)) {
    src_arg = nodes_[src_node_index]->GetDefinitions().output_defs[src_arg_slot];
  }

  if (nullptr == src_arg) {
    ORT_THROW("Invalid source node arg slot specified when removing edge.");
  }

  auto& dst_node_defs = nodes_[dst_node_index]->GetDefinitions();
  if (dst_node_defs.input_defs.size() > static_cast<size_t>(dst_arg_slot)) {
    dst_arg = dst_node_defs.input_defs[dst_arg_slot];
  } else {
    auto num_of_explicit_inputs = dst_node_defs.input_defs.size();
    if (num_of_explicit_inputs + dst_node_defs.implicit_input_defs.size() > static_cast<size_t>(dst_arg_slot)) {
      dst_arg = dst_node_defs.implicit_input_defs[dst_arg_slot - num_of_explicit_inputs];
    }
  }
  if (nullptr == dst_arg) {
    ORT_THROW("Invalid destination node arg slot specified when removing edge.");
  }

  if (src_arg != dst_arg) {
    // The edge ends specified by source and destination arg slot are not referring to same node arg.
    // It means there was no edge between these two slots before.
    ORT_THROW("Argument mismatch when removing edge.");
  }

  nodes_[dst_node_index]->MutableRelationships().input_edges.erase(Node::EdgeEnd(*nodes_[src_node_index], src_arg_slot, dst_arg_slot));
  nodes_[src_node_index]->MutableRelationships().output_edges.erase(Node::EdgeEnd(*nodes_[dst_node_index], src_arg_slot, dst_arg_slot));
}

GSL_SUPPRESS(es .84)  // ignoring return value from unordered_map::insert causes noisy complaint
Status Graph::BuildConnections(std::unordered_set<std::string>& outer_scope_node_args_consumed) {
  const std::unordered_set<std::string>& outer_scope_node_args = resolve_context_.outer_scope_node_args;
  std::unordered_set<Node*> inner_nodes;

  // recurse into subgraphs first so we can update any nodes in this graph that are used by those subgraphs
  if (!resolve_context_.nodes_with_subgraphs.empty()) {
    const bool loaded_from_model_file = GraphLoadedFromModelFile(graph_proto_);

    for (auto* node : resolve_context_.nodes_with_subgraphs) {
      for (auto& subgraph : node->MutableSubgraphs()) {
        std::unordered_set<std::string> node_args_consumed;
        ORT_RETURN_IF_ERROR(subgraph->BuildConnections(node_args_consumed));

        for (auto& node_arg_name : node_args_consumed) {
          auto node_arg = GetNodeArg(node_arg_name);

          if (node_arg == nullptr) {
            // it's a node arg from outside this graph's scope, so add that to the list we return
            // so that we can add the dependency at the next level up. this happens if you have multiple
            // levels of subgraphs between the graph with the original NodeArg and the subgraph with implicit usage.
            ORT_IGNORE_RETURN_VALUE(outer_scope_node_args_consumed.insert(node_arg_name));

            if (!parent_graph_) {
              return ORT_MAKE_STATUS(
                  ONNXRUNTIME, INVALID_GRAPH,
                  "This is an invalid model. At top level graph without matching NodeArg that subgraph consumes. Name=",
                  node_arg_name,
                  " Graph may not conform to the ONNX spec and contain initializers that are not graph inputs.");
            }

            node_arg = parent_graph_->GetNodeArgIncludingParentGraphs(node_arg_name);

            // make sure the node arg is found in the parent graph/s
            if (!node_arg) {
              return ORT_MAKE_STATUS(
                  ONNXRUNTIME, INVALID_GRAPH,
                  "This is an invalid model. Failed to find NodeArg in all parent graphs. Name=", node_arg_name,
                  " Graph may not conform to the ONNX spec and contain initializers that are not graph inputs.");
            }
          }

          // add it to the Node's list of implicit inputs
          auto& implicit_inputs = node->MutableDefinitions().implicit_input_defs;
          int input_slot_index = static_cast<int>(node->GetDefinitions().input_defs.size());
          auto iter = std::find(implicit_inputs.cbegin(), implicit_inputs.cend(), node_arg);
          if (implicit_inputs.cend() == iter) {
            implicit_inputs.push_back(node_arg);
            input_slot_index += static_cast<int>(implicit_inputs.size() - 1);
          } else {
            input_slot_index += static_cast<int>(iter - implicit_inputs.cbegin());
          }

          auto entry = resolve_context_.output_args.find(node_arg_name);
          if (entry != resolve_context_.output_args.end()) {
            // Create relationship between this node (node), and the node providing the output (output_node).
            Node& output_node = *entry->second.first;
            AddEdge(output_node.Index(), node->Index(), entry->second.second, input_slot_index);

            inner_nodes.insert(&output_node);

            // If this Graph was built manually, remove the implicit input from the graph outputs if it is present there
            // and not explicitly listed in the ordered graph outputs (as that implies we should leave it as an output).
            // If the Graph was loaded from a GraphProto, honor the explicit graph outputs and leave as is.
            if (!loaded_from_model_file) {
              graph_outputs_.erase(std::remove(graph_outputs_.begin(), graph_outputs_.end(), node_arg),
                                   graph_outputs_.end());
            }
          }
        }
      }
    }
  }

  // now build connections within this Graph instance
  for (auto& node : Nodes()) {
    // Need mutable input defs to be able to set any outer scope NodeArg implicit inputs
    auto& input_args = node.MutableInputDefs();

    if (!input_args.empty()) {
      // This node needs inputs.

      int input_slot_index = -1;
      for (const auto* input_arg : input_args) {
        ++input_slot_index;
        if (!input_arg->Exists()) {
          // This input could be optional and it does not exist in this case.
          continue;
        }

        const auto& input_arg_name = input_arg->Name();
        auto output_arg_iter = resolve_context_.output_args.find(input_arg_name);
        if (resolve_context_.output_args.end() != output_arg_iter) {
          // The input to this node is an output from a previous node in this graph.
          // Create relationship between this node (node), and the node providing the output (output_node).
          Node& output_node = *output_arg_iter->second.first;
          AddEdge(output_node.Index(), node.Index(), output_arg_iter->second.second, input_slot_index);

          inner_nodes.insert(&output_node);
        } else {
          // the value is either an input, an initializer, or coming from outer scope. we only need to take action
          // if coming from outer scope, so first check if this is a subgraph (otherwise there is no outer scope).
          if (parent_graph_ != nullptr) {
            // make sure it's not an input or initializer first as those override any outer scope values
            if (resolve_context_.inputs_and_initializers.find(input_arg_name) ==
                resolve_context_.inputs_and_initializers.cend()) {
              // If it is present in the outer scope it will be 'fed' by the execution frame
              // providing access to the OrtValue from the outer scope. Pass the name back up so nodes can
              // be linked correctly at that level.
              if (outer_scope_node_args.find(input_arg_name) != outer_scope_node_args.cend()) {
                ORT_IGNORE_RETURN_VALUE(outer_scope_node_args_consumed.insert(input_arg_name));
              }
            }
          }
        }
      }
    } else if (node.OutputDefs().empty()) {
      // This is a useless node.
      // It has no input/output.
      RemoveNode(node.Index());
    }
  }

  return Status::OK();
}  // namespace onnxruntime

void Graph::ReverseDFSFrom(const std::vector<NodeIndex>& from,
                           const std::function<void(const Node*)>& enter,
                           const std::function<void(const Node*)>& leave,
                           const std::function<bool(const Node*, const Node*)>& comp) const {
  std::vector<const Node*> node_vec;
  node_vec.reserve(from.size());
  for (auto i : from) {
    node_vec.push_back(GetNode(i));
  }

  ReverseDFSFrom(node_vec, enter, leave, comp);
}

void Graph::ReverseDFSFrom(const std::vector<const Node*>& from,
                           const std::function<void(const Node*)>& enter,
                           const std::function<void(const Node*)>& leave,
                           const std::function<bool(const Node*, const Node*)>& comp) const {
  using WorkEntry = std::pair<const Node*, bool>;  // bool represents leave or not
  std::vector<WorkEntry> stack(from.size());
  for (size_t i = 0; i < from.size(); i++) {
    stack[i] = WorkEntry(from[i], false);
  }

  std::vector<bool> visited(MaxNodeIndex(), false);
  while (!stack.empty()) {
    const WorkEntry last_entry = stack.back();
    stack.pop_back();
    const Node& n = *last_entry.first;
    if (last_entry.second) {
      // leave node
      leave(&n);
      continue;
    }

    if (visited[n.Index()]) continue;

    visited[n.Index()] = true;

    if (enter) enter(&n);

    if (leave) stack.emplace_back(&n, true);

    if (comp) {
      std::vector<const Node*> sorted_nodes;
      for (auto iter = n.InputNodesBegin(); iter != n.InputNodesEnd(); ++iter) {
        sorted_nodes.push_back(&(*iter));
      }
      std::sort(sorted_nodes.begin(), sorted_nodes.end(), comp);
      for (const auto* in : sorted_nodes) {
        const NodeIndex idx = in->Index();
        if (!visited[idx]) {
          stack.emplace_back(in, false);
        }
      }
    } else {
      for (auto iter = n.InputNodesBegin(); iter != n.InputNodesEnd(); ++iter) {
        const NodeIndex idx = (*iter).Index();
        if (!visited[idx]) {
          stack.emplace_back(GetNode(idx), false);
        }
      }
    }
  }
}

GSL_SUPPRESS(es .84)  // noisy warning about ignoring return value from insert(...)
Status Graph::PerformTopologicalSortAndCheckIsAcyclic() {
  nodes_in_topological_order_.clear();
  // nodes that have been processed and added to nodes_in_topological_order.
  std::unordered_set<NodeIndex> processed_nodes;
  std::unordered_set<NodeIndex> output_nodes;
  std::unordered_set<NodeIndex> nodes_added_for_processing;
  std::stack<NodeIndex> stack;

  // push the top level nodes into nodes_in_topological_order in the order they were added
  // to ensure that is consistent.
  auto& nodes_in_original_order = Nodes();
  std::for_each(nodes_in_original_order.cbegin(), nodes_in_original_order.cend(),
                [&](const Node& node) {
                  auto index = node.Index();

                  // find the top level nodes in the graph.
                  // need to also consider nodes that only have Constants as inputs as top level nodes,
                  // as the constant will get replaced by an initializer.
                  auto input_edges = node.GetRelationships().input_edges;
                  auto has_inputs = std::any_of(input_edges.cbegin(), input_edges.cend(), [](const Node::EdgeEnd& edge) {
                    return edge.GetNode().OpType() != kConstant;
                  });

                  if (!has_inputs) {
                    // add to the topological list, and ensure we skip these nodes when walking the graph
                    nodes_in_topological_order_.push_back(index);
                    processed_nodes.insert(index);

                    // mark this as added as we've fully processed it and don't need to do it again later
                    nodes_added_for_processing.insert(index);
                  }
                });

  // start at the bottom and work our way up the graph
  for (auto iter = Nodes().begin(); iter != Nodes().end(); ++iter) {
    if (iter->relationships_.output_edges.empty()) {
      // This is a leaf node.
      stack.push(iter->Index());
    }
  }

  while (!stack.empty()) {
    const NodeIndex current = stack.top();
    stack.pop();

    if (processed_nodes.find(current) != processed_nodes.end()) {
      continue;
    }

    if (nodes_added_for_processing.find(current) != nodes_added_for_processing.end()) {
      // we popped the stack and are back to a node that was added previously,
      // so we know all the upstream nodes from it have been fully processed,
      nodes_in_topological_order_.push_back(current);
      processed_nodes.insert(current);
      output_nodes.erase(current);
      continue;
    }

    const Node* node = GetNode(current);
    if (!node) {
      continue;
    }

    stack.push(current);
    output_nodes.insert(current);

    for (auto iter = node->InputNodesBegin(); iter != node->InputNodesEnd(); ++iter) {
      const NodeIndex idx = (*iter).Index();
      if (output_nodes.find(idx) != output_nodes.end()) {
        Status status(ONNXRUNTIME, FAIL, "This is an invalid model. Error: the graph is not acyclic.");
        return status;
      }

      // avoid re-processing nodes
      if (nodes_added_for_processing.find(idx) == nodes_added_for_processing.end()) {
        stack.push(idx);
      }
    }

    nodes_added_for_processing.insert(current);
  }

  if (num_of_nodes_ >= 0 && static_cast<size_t>(num_of_nodes_) == nodes_in_topological_order_.size()) {
    return Status::OK();
  }
  return Status(ONNXRUNTIME, FAIL, "This is an invalid model. Error: the graph is not acyclic.");
}

bool FullyDefinedType(const TypeProto& type_proto) {
  switch (type_proto.value_case()) {
    case TypeProto::kTensorType: {
      auto& tensor_type = type_proto.tensor_type();
      return utils::HasElemType(tensor_type);
    }
    case TypeProto::kSparseTensorType: {
      auto& tensor_type = type_proto.sparse_tensor_type();
      return utils::HasElemType(tensor_type);
    }
    case TypeProto::kSequenceType: {
      auto& seq_type = type_proto.sequence_type();
      return utils::HasElemType(seq_type) && FullyDefinedType(seq_type.elem_type());
    }
    case TypeProto::kMapType: {
      auto& map_type = type_proto.map_type();
      return utils::HasKeyType(map_type) &&
             utils::HasValueType(map_type) &&
             FullyDefinedType(map_type.value_type());
    }
    case TypeProto::kOpaqueType:
      return true;
    case TypeProto::VALUE_NOT_SET:
    default:
      return false;
  }
}

// function to handle type/shape inferencing of a subgraph.
// parameters are the Graph instance for the subgraph, the input types from the control flow node that contains
// the subgraph, and the vector to write the output from the inferencing.
using SubgraphInferencingFunc =
    std::function<Status(const Node&, Graph&, const std::vector<const TypeProto*>&, std::vector<const TypeProto*>&)>;

class GraphInferencerImpl : public ONNX_NAMESPACE::GraphInferencer {
 public:
  GraphInferencerImpl(const Node& node, Graph& graph, SubgraphInferencingFunc& inferencing_func)
      : node_(node), graph_(graph), inferencing_func_(inferencing_func) {
  }

  // Perform inferencing on the graph contained in GraphInferencer.
  // Returns the graph output types post-inferencing.
  // We ignore input_data currently as the inferencing happens prior to receiving user input.
  std::vector<const TypeProto*> doInferencing(const std::vector<const TypeProto*>& input_types,
                                              const std::vector<const TensorProto*>& /*input_data*/) override {
    std::vector<const TypeProto*> output_types;

    auto status = inferencing_func_(node_, graph_, input_types, output_types);

    if (status != Status::OK()) {
      fail_type_inference("Graph attribute inferencing failed: ", status.ErrorMessage());
    }

    return output_types;
  }

 private:
  const Node& node_;
  Graph& graph_;
  SubgraphInferencingFunc& inferencing_func_;
};

// An implementation of the InferenceContext interface required by operator-specific
// shape inference for onnxruntime graphs.
class InferenceContextImpl : public ONNX_NAMESPACE::InferenceContext {
  using AttributeGraphMap = std::unordered_map<std::string, Graph*>;

 public:
  InferenceContextImpl(Node& node,
                       SubgraphInferencingFunc subgraph_inferencing_func,
                       const Graph& graph) noexcept
      : node_(node),
        subgraph_inferencing_func_(subgraph_inferencing_func),
        graph_(graph) {
    node_output_types_.resize(node.OutputDefs().size());
  }

  void RunInferencing() {
    auto schema = node_.Op();
    if (nullptr != schema) {
      schema->GetTypeAndShapeInferenceFunction()(*this);
    }
  }

  std::vector<TypeProto> InferredOutputTypes() const { return node_output_types_; }

  const AttributeProto* getAttribute(const std::string& name) const override {
    auto& attribute_value_map = node_.GetAttributes();
    auto iter = attribute_value_map.find(name);
    if (iter == attribute_value_map.end()) {
      return nullptr;
    }
    return &iter->second;
  }

  size_t getNumInputs() const noexcept override {
    return node_.InputDefs().size();
  }

  const TypeProto* getInputType(size_t index) const override {
    const TypeProto* type = nullptr;
    auto p_node_arg = node_.InputDefs().at(index);
    if ((nullptr != p_node_arg) && p_node_arg->Exists()) {
      type = p_node_arg->TypeAsProto();
    }

    return type;
  }

  size_t getNumOutputs() const noexcept override {
    return node_output_types_.size();
  }

  TypeProto* getOutputType(size_t index) override {
    return &node_output_types_[index];
  }

  const TensorProto* getInputData(size_t index) const override {
    auto def = node_.InputDefs()[index];
    if (!def)
      return nullptr;

    // only return data if it's for a constant initializer. checks for outer scope initializers
    // if this is a subgraph and the name isn't found locally.
    const TensorProto* initializer = graph_utils::GetConstantInitializer(graph_, def->Name(), true);
    return initializer;
  }

  GraphInferencer* getGraphAttributeInferencer(const std::string& attribute_name) override {
    GraphInferencer* graph_inferencer = nullptr;

    auto* subgraph = node_.GetMutableGraphAttribute(attribute_name);

    if (subgraph) {
      auto inferencer = onnxruntime::make_unique<GraphInferencerImpl>(node_, *subgraph, subgraph_inferencing_func_);
      graph_inferencer = inferencer.get();
      graph_inferencers_.push_back(std::move(inferencer));
    } else {
      fail_type_inference("No Graph instance was found for attribute ",
                          attribute_name, " in node ", node_.Name());
    }

    return graph_inferencer;
  }

 private:
  Node& node_;
  // node_output_types_ will be populated by the operator-specific shape inference.
  std::vector<TypeProto> node_output_types_;
  SubgraphInferencingFunc subgraph_inferencing_func_;
  std::vector<std::unique_ptr<GraphInferencerImpl>> graph_inferencers_;
  const Graph& graph_;
};

Status Graph::InferAndVerifySubgraphTypes(const Node& node, Graph& subgraph,
                                          const std::vector<const TypeProto*>& input_types,
                                          std::vector<const TypeProto*>& output_types) {
  auto status = Status::OK();

  output_types.clear();

  // the spec says all inputs should be provided for the subgraph so default to that first
  auto* subgraph_inputs = &subgraph.GetInputsIncludingInitializers();
  auto num_subgraph_inputs = subgraph_inputs->size();

  if (num_subgraph_inputs != input_types.size()) {
    // we also allow for just the required inputs to be provided to be user friendly due to ONNX requiring
    // initializers to have matching inputs (making them optional inputs that most likely the user doesn't want to
    // override).
    auto& required_subgraph_inputs = subgraph.GetInputs();
    auto num_required_subgraph_inputs = required_subgraph_inputs.size();

    if (num_required_subgraph_inputs != input_types.size()) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Size mismatch validating subgraph inputs. Got ", input_types.size(),
                             " inputs but subgraph has ", num_subgraph_inputs,
                             " inputs and requires ", num_required_subgraph_inputs,
                             " inputs. Either provide all subgraph inputs, or just the required inputs.");
    }

    subgraph_inputs = &required_subgraph_inputs;
    num_subgraph_inputs = num_required_subgraph_inputs;
  }

  // apply type/shape info to the subgraph's inputs
  for (size_t i = 0; i < num_subgraph_inputs; ++i) {
    const auto& input_type = *input_types[i];
    const auto& subgraph_input = *subgraph_inputs->at(i);

    NodeArg* mutable_nodearg = subgraph.GetNodeArg(subgraph_input.Name());
    status = mutable_nodearg->UpdateTypeAndShape(input_type);
    if (!status.IsOK()) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Node:", node.Name(), " ", status.ErrorMessage());
    }
  }

  // Apply any current input type/shape information to the Nodes in the subgraph that are implicitly
  // consuming NodeArg's from this scope or higher.
  // The NodeArg's that implicit_input_defs point to would have any type/shape inferencing applied to them
  // by now. As the subgraph is referring to the outer scope NodeArg, we simply replace any information in
  // the subgraph with the details from the outer scope NodeArg.
  auto implicit_input_defs = node.GetDefinitions().implicit_input_defs;
  for (const auto* implicit_node_arg : implicit_input_defs) {
    auto subgraph_nodearg = subgraph.GetNodeArg(implicit_node_arg->Name());

    // the implicit input defs may be for a nested subgraph, so it won't necessarily match here.
    // if that is the case, we will update the type/shape information when we descend into the
    // nested subgraph later.
    if (!subgraph_nodearg)
      continue;

    status = subgraph_nodearg->UpdateTypeAndShape(*implicit_node_arg);
    if (!status.IsOK()) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Node:", node.Name(), " ", status.ErrorMessage());
    }

    // all values above us should have a type by now due to ONNX requirements.
    if (subgraph_nodearg->Type() == nullptr)
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Subgraph input missing type.");
  }

  // now that we have handled the input types, do the type/shape inferencing for the subgraph
  // to flow the type/shape info through it
  status = subgraph.PerformTypeAndShapeInferencing();
  ORT_RETURN_IF_ERROR(status);

  auto& subgraph_outputs = subgraph.GetOutputs();
  for (const auto* output : subgraph_outputs) {
    output_types.push_back(output->TypeAsProto());
  }

  return Status::OK();
}

// Implementation of type-inference and type-checking for a single node
GSL_SUPPRESS(f .23)  // spurious warning about inferred_type never being checked for null
Status Graph::InferAndVerifyTypeMatch(Node& node, const OpSchema& op) {
  auto& node_name = node.Name();

  // if we're building a graph we permit outer scope node args to have no type
  // as the 'real' Resolve at runtime will have type inferencing
  auto is_outer_scope_nodearg = [this](const std::string& name) {
    return outer_scope_node_arg_names_.find(name) != outer_scope_node_arg_names_.cend();
  };

  // <k> index used to navigate node->InputDefs().
  int k = 0;
  std::unordered_map<std::string, DataType> type_parameter_to_type_map;

  for (size_t i = 0; i < node.InputArgCount().size(); ++i) {
    // Number of inputs corresponding to the i-th argument.
    const int arg_count = node.InputArgCount()[i];
    // The i-th formal parameter definition.
    auto op_formal_parameter = op.inputs()[i];

    // Check all <arg_count> actual parameters (corresponding to the k-th input)
    // match the formal parameter definition (i-th argument).
    for (int j = 0; j < arg_count; ++j, ++k) {
      auto& input_def = node.MutableDefinitions().input_defs[k];
      if (!input_def->Exists())
        continue;

      if (input_def->Type() == nullptr) {
        // if we are building a subgraph that uses outer scope values,
        // allow an empty type as it will be copied from the outer scope graph at runtime
        if (is_outer_scope_nodearg(input_def->Name()))
          continue;

        // Logic error: This should not happen if we properly checked that every use has
        // a corresponding def, for which type-inference already produced a valid type
        Status status(ONNXRUNTIME, FAIL,
                      "This is an invalid model. "
                      "Node (" +
                          node_name + ") input arg (" +
                          input_def->Name() + ") does not have type information set by parent node.");
        return status;
      }

      // Verify that the actual parameter's type is one of permitted types of the formal parameter
      DataType input_type = input_def->Type();
      auto& permitted_types = op_formal_parameter.GetTypes();
      if (0 == permitted_types.count(input_type)) {
        std::string null_pointer("(null)");
        if (input_type == nullptr) input_type = &null_pointer;
        // Type error in input model/graph.

        Status status(ONNXRUNTIME, INVALID_GRAPH,
                      "This is an invalid model. "
                      "Type Error: Type '" +
                          *input_type + "' of input parameter (" + input_def->Name() +
                          ") of operator (" + op.Name() + ") in node (" + node_name + ") is invalid.");
        return status;
      }

      // When multiple parameters have the same type-variable, they are all required
      // to have the same type. E.g., when adding tensors A and B, it is an error if
      // input A is of type "tensor(int32)" and B is of type "tensor(float)".
      // For variadic arguments, this verification rule is normally applicable:
      // e.g., Concat/Max/Mean/Min/Sum all require all input tensors to be of same type.
      // However, some ops, like the control-flow constructs (Scan, If, Loop) have variadic
      // inputs and outputs of different types. The check is not applicable to such ops.
      if (op_formal_parameter.GetIsHomogeneous()) {
        auto param_to_type_iter = type_parameter_to_type_map.find(op_formal_parameter.GetTypeStr());
        if (type_parameter_to_type_map.end() == param_to_type_iter) {
          // Bind the corresponding type-parameter's value to the actual type:
          type_parameter_to_type_map[op_formal_parameter.GetTypeStr()] = input_type;
        } else if (param_to_type_iter->second != input_type) {
          // Type error in input model/graph:
          // The type-parameter T is bound to different values for different inputs.
          Status status(ONNXRUNTIME, FAIL,
                        "Type Error: Type parameter (" + op_formal_parameter.GetTypeStr() +
                            ") bound to different types (" + *(param_to_type_iter->second) +
                            " and " + *(input_def->Type()) +
                            " in node (" + node_name + ").");
          return status;
        }
      }
    }
  }

  // Apply ONNX's type/shape inference to this node.
  // This will call InferAndVerifySubgraphTypes if the ONNX level type/shape inferencing for the Node attempts
  // to do subgraph type/shape inferencing (Scan/If/Loop nodes).
  // InferAndVerifySubgraphTypes will call PerformTypeAndShapeInferencing for the subgraph, which will recursively
  // handle type/shape inferencing for it.
  // Once that completes, the outputs from the node containing the subgraph will be updated, and the final values
  // returned here.
  SubgraphInferencingFunc func(Graph::InferAndVerifySubgraphTypes);
  InferenceContextImpl context(node, func, *this);

  try {
    context.RunInferencing();
  } catch (const std::exception& ex) {
    return Status(ONNXRUNTIME, FAIL, ex.what());
  }

  const auto& onnx_inferred_types(context.InferredOutputTypes());

  // Infer and verify node output arg type information.
  int i = -1;
  for (auto& output_def : node.MutableDefinitions().output_defs) {
    ++i;
    if (!output_def->Exists()) continue;

    // if the number of actual parameters exceeds the number of formal parameters,
    // then the op has variadic outputs and the trailing extra actual parameters
    // correspond to the last formal parameter. (The ONNX schema verification check
    // would have checked that the corresponding formal parameter is variadic.)

    const int num_formal_params = gsl::narrow_cast<int>(op.outputs().size());
    auto operand_index = std::min(i, num_formal_params - 1);
    auto op_formal_parameter = op.outputs().at(operand_index);

    const TypeProto& onnx_inferred_type = onnx_inferred_types[i];
    DataType existing_type = output_def->Type();
    DataType inferred_type = nullptr;

    // Infer output arg type if it is constrained to be of the same type as some input:
    // For example, the output of "Abs" is of the same type as its input.
    bool homogeneous = op_formal_parameter.GetIsHomogeneous();
    auto input_types_iter = type_parameter_to_type_map.find(op_formal_parameter.GetTypeStr());
    if (homogeneous && (type_parameter_to_type_map.end() != input_types_iter)) {
      inferred_type = input_types_iter->second;
    } else if (1 == op_formal_parameter.GetTypes().size()) {
      // Infer output arg type if operator definition specifies unique output type:
      inferred_type = *(op_formal_parameter.GetTypes().begin());
    } else if (FullyDefinedType(onnx_inferred_type)) {
      // Use output type inferred by ONNX inference
      inferred_type = DataTypeUtils::ToType(onnx_inferred_type);
    } else if (existing_type != nullptr) {
      inferred_type = existing_type;
    } else {
      // This should not happen: indicates incompleteness in ONNX inference.
      Status status(ONNXRUNTIME, FAIL,
                    "Node (" + node_name + ") output arg (" + output_def->Name() + ") type inference failed");
      return status;
    }

    if ((existing_type != inferred_type) && (existing_type != nullptr)) {
      // A type exists for this output but does not match the inferred type.
      return Status(ONNXRUNTIME, FAIL,
                    "Type Error: Type (" + *existing_type + ") of output arg (" +
                        output_def->Name() + ") of node (" + node_name +
                        ") does not match expected type (" + *inferred_type + ").");
    }

    if (existing_type == nullptr)
      output_def->SetType(inferred_type);

    // Update output-shape if it was inferred:
    if (utils::HasTensorType(onnx_inferred_type)) {
      auto& tensor_type = onnx_inferred_type.tensor_type();
      if (utils::HasShape(tensor_type)) {
        if (output_def->Shape() == nullptr) {
          output_def->SetShape(tensor_type.shape());
        } else {
          // we need to merge the shapes as a subgraph may have placeholder dimensions to represent the rank
          // that have no values.
          TypeProto_Tensor merge_target;
          (*merge_target.mutable_shape()) = *output_def->Shape();
          auto status = MergeShapeInfo(output_def->Name(), tensor_type, merge_target, using_latest_onnx_opset_);
          if (!status.IsOK()) {
            return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Node:", node_name, " ", status.ErrorMessage());
          }

          // we may have cleared the shape if there was a mismatch so handle that
          if (utils::HasShape(merge_target))
            output_def->SetShape(merge_target.shape());
          else
            output_def->ClearShape();
        }
      }
    }
  }

  return Status::OK();
}

// Apply type-inference and type-checking to all inputs and initializers:
common::Status Graph::TypeCheckInputsAndInitializers() {
  // Check that the type of every input is specified:
  for (auto* graph_input : GetInputs()) {
    if (nullptr == graph_input->Type()) {
      Status status(ONNXRUNTIME, FAIL,
                    "This is an invalid model. "
                    "Model input (" +
                        graph_input->Name() + ") does not have type information.");
      return status;
    }
  }

  // Infer/check type and shape for all initializers from their values
  for (auto& initializer_pair : name_to_initial_tensor_) {
    const std::string& name = initializer_pair.first;
    auto* node_arg = GetNodeArg(name);
    // If node_arg is null, we ignore this as a potentially unused initializer here
    if (nullptr != node_arg) {
      const TensorProto* tensor_proto = initializer_pair.second;
      TypeProto tensor_type;
      tensor_type.mutable_tensor_type()->set_elem_type(tensor_proto->data_type());
      auto inferred_type = DataTypeUtils::ToType(tensor_type);
      auto existing_type = node_arg->Type();
      if (nullptr == existing_type)
        node_arg->SetType(inferred_type);
      else if (inferred_type != existing_type) {
        return Status(ONNXRUNTIME, FAIL,
                      "Type Error: Value of initializer " + name + " does not match its type.");
      }

      // Set shape accordingly.
      TensorShapeProto inferred_shape;
      for (auto dim : tensor_proto->dims()) {
        inferred_shape.add_dim()->set_dim_value(dim);
      }

      const TensorShapeProto* p_existing_shape = node_arg->Shape();
      if (nullptr == p_existing_shape) {
        // use the inferred shape if this is a constant initializer (cannot be overridden).
        // if not it has a matching graph input, and we prefer the shape info (or lack of info) from the graph input
        if (graph_utils::IsConstantInitializer(*this, name, false)) {
          node_arg->SetShape(inferred_shape);
        }
      } else {
        if (p_existing_shape->dim_size() != tensor_proto->dims_size()) {
          return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL,
                                 "Type Error: Shape of initializer ", name, " does not match. ",
                                 *p_existing_shape, " != ", *tensor_proto);
        }

        for (int i = 0; i < p_existing_shape->dim_size(); ++i) {
          auto& d = p_existing_shape->dim(i);
          if (utils::HasDimValue(d) && (d.dim_value() != tensor_proto->dims(i))) {
            return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL,
                                   "Type Error: Shape of initializer ", name, " does not match. ",
                                   *p_existing_shape, " != ", *tensor_proto);
          }
        }
      }
    }
  }

  return Status::OK();
}

Status Graph::VerifyNodeAndOpMatch() {
  CheckerContext ctx;
  ctx.set_ir_version(gsl::narrow_cast<int>(IrVersion()));
  ctx.set_opset_imports(DomainToVersionMap());
  ctx.set_schema_registry(schema_registry_.get());

  LexicalScopeContext lsc;
  lsc.output_names.insert(resolve_context_.inputs_and_initializers.cbegin(),
                          resolve_context_.inputs_and_initializers.cend());

  // technically we could add values from Node.GetDefinitions().implicit_input_defs on a per-node basis inside
  // the below loop so that we only check against the specific outer dependencies of the node.
  // doing that requires lots of copies of LexicalScopeContext.output_names to clear out the per-Node values
  // after each loop. instead add all the outer scope values upfront so we can just accumulate new inner scope values
  // during each loop iteration.
  lsc.output_names.insert(resolve_context_.outer_scope_node_args.cbegin(),
                          resolve_context_.outer_scope_node_args.cend());

  // we may have some locally defined outer scope args if we're in the middle of constructing a subgraph
  // and need to call Resolve
  lsc.output_names.insert(outer_scope_node_arg_names_.cbegin(), outer_scope_node_arg_names_.cend());

  for (auto node_index : nodes_in_topological_order_) {
    // Node verification.
    auto& node = *GetNode(node_index);

    NodeProto node_proto;
    node.ToProto(node_proto);
    auto& node_name = node.Name();
    auto& domain = node.Domain();

    auto iter = model_functions_.find(node.OpType());
    if (iter != model_functions_.end()) {
      const ONNX_NAMESPACE::FunctionProto* model_function_proto = iter->second;
      auto model_func_ptr = onnxruntime::make_unique<onnxruntime::FunctionImpl>(*this, node.Index(), *model_function_proto);
      function_container_.emplace_back(std::move(model_func_ptr));
      node.SetFunctionBody(*function_container_.back());
    }

    if (!node.Op()) {
      try {
        checker::check_node(node_proto, ctx, lsc);
      } catch (const std::exception& ex) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_GRAPH, "This is an invalid model. Error in Node:", node_name, " : ", ex.what());
      }

      auto maxInclusiveVersion = DomainToVersionMap().find(domain)->second;
      node.op_ = schema_registry_->GetSchema(node.OpType(), maxInclusiveVersion, node.Domain());

      if (node.op_ && node.op_->Deprecated()) {
        node.op_ = nullptr;
      }

      if (node.op_ && node.op_->HasFunction()) {
        auto onnx_function_proto = node.op_->GetFunction();
        auto func_ptr = onnxruntime::make_unique<onnxruntime::FunctionImpl>(*this, node.Index(), *onnx_function_proto);
        function_container_.emplace_back(std::move(func_ptr));
        node.SetFunctionBody(*function_container_.back());
      }

      if (!node.op_) {
        return Status(ONNXRUNTIME, FAIL, "Fatal error: " + node.OpType() + " is not a registered function/op");
      }
    }

    ORT_RETURN_IF_ERROR(node.UpdateInputArgCount());

    // currently an Op is required by ValidateVersion, so we use gsl::not_null to validate that.
    // This may change in the future to allow a null Op
    const gsl::not_null<const OpSchema*> p_op{node.Op()};

    // Attribute verification and fill node attribute with
    // default value defined in operator definition if needed.
    // Fill node attribute with default value specified in operator definition if any.
    const auto& node_attributes = node.GetAttributes();
    for (const auto& attr_def : p_op->attributes()) {
      auto node_attr_iter = node_attributes.find(attr_def.first);
      if (node_attributes.end() == node_attr_iter) {
        // The attribute was not specified in the node.
        if (!attr_def.second.required) {
          if (utils::HasName(attr_def.second.default_value)) {
            // Set default value to the node attributes.
            node.AddAttribute(attr_def.first, attr_def.second.default_value);
          }
          // TODO: Handle optional attribute but no default value specified in op definition.
        } else {
          Status status(ONNXRUNTIME, FAIL,
                        "This is an invalid model. "
                        "Node (" +
                            node_name + ") attribute (" + attr_def.first +
                            ") is required but not specified.");
          return status;
        }
      }
    }

    NO_CHANGE_ON_SYNC_FLAG(ORT_RETURN_IF_ERROR(InferAndVerifyTypeMatch(node, *p_op)));

    // Accumulate output names of the iterated Node
    for (auto& output_name : node_proto.output()) {
      lsc.output_names.insert(output_name);
    }
  }

  return Status::OK();
}

void Graph::FindAllSubgraphs(std::vector<Graph*>& subgraphs) {
  for (auto& node : Nodes()) {
    for (auto& subgraph : node.MutableSubgraphs()) {
      subgraphs.push_back(subgraph.get());
      subgraph->FindAllSubgraphs(subgraphs);
    }
  }
}

Status Graph::VerifyInputAndInitializerNames() {
  std::unordered_set<std::string>& inputs_and_initializers = resolve_context_.inputs_and_initializers;

  for (auto* input : GetInputs()) {
    auto result = inputs_and_initializers.insert(input->Name());
    if (!result.second) {
      Status status(ONNXRUNTIME, FAIL,
                    "Error: Duplicate definition-site for (" + input->Name() + ").");
      return status;
    }
  }

  for (auto& initializer_pair : name_to_initial_tensor_) {
    GSL_SUPPRESS(es .84)
    inputs_and_initializers.insert(initializer_pair.first);
    // Initializers are expected to be included in inputs (according to ONNX spec).
    // onnxruntime relaxes this constraint. No duplicate-name check here.
  }

  return Status::OK();
}

Status Graph::InitInputsInitializersOutputs() {
  resolve_context_.Clear();

  // clear the previous relationships, as we re-create them when resolving.
  // same applies to the implicit input defs as they are built from any subgraphs within this graph.
  for (auto& node : Nodes()) {
    node.MutableRelationships().Clear();
    node.MutableDefinitions().implicit_input_defs.clear();
  }

  // add the subgraph pointers to the resolve context.
  for (auto& node : Nodes()) {
    auto& subgraphs = node.MutableSubgraphs();
    if (!subgraphs.empty()) {
      resolve_context_.nodes_with_subgraphs.insert(&node);
    }
  }

  ORT_RETURN_IF_ERROR(SetGraphInputsOutputs());
  ORT_RETURN_IF_ERROR(VerifyInputAndInitializerNames());
  ORT_RETURN_IF_ERROR(VerifyNoDuplicateName());

  return Status::OK();
}

Status Graph::PerformTypeAndShapeInferencing() {
  ORT_RETURN_IF_ERROR(TypeCheckInputsAndInitializers());

  // type/shape inferencing on the nodes is done recursively as we need subgraph outputs
  // to be applied to Node outputs for the node containing the subgraph.
  // Call path is
  // VerifyNodeAndOpMatch
  //   Iterates Nodes
  //     Runs ONNX type/shape inferencing for each Node
  //      - If it hits a node with a subgraph, InferenceContext::getGraphAttributeInferencer is called
  //        by the ONNX level type/shape inferencing, which updates the subgraph inputs using GraphInferencerImpl
  //      - GraphInferencerImpl::doInferencing calls PerformTypeShapeInferencing to execute type/shape inferencing
  //        for all nodes in the subgraph. This leads to recursively handling all subgraphs contained in the node.
  //      - once we finish processing the subgraph/s we apply resultant type/shape information to the outputs
  //        of the node that contains the subgraph.
  ORT_RETURN_IF_ERROR(VerifyNodeAndOpMatch());

  return Status::OK();
}

Status Graph::ForThisAndAllSubgraphs(const std::vector<Graph*>& subgraphs, std::function<Status(Graph&)> func) {
  auto status = func(*this);
  ORT_RETURN_IF_ERROR(status);

  for (auto& subgraph : subgraphs) {
    status = func(*subgraph);
    ORT_RETURN_IF_ERROR(status);
  }

  return status;
}

Status Graph::Resolve() {
  return Resolve(false);
}

Status Graph::Resolve(bool no_proto_sync_required) {
  if (parent_graph_) {
    // Resolve must start at the top level graph in-order to handle outer scope
    // connections correctly, so recurse up to that level to start
    return parent_graph_->Resolve(no_proto_sync_required);
  }

  // find all subgraphs including nested ones.
  std::vector<Graph*> all_subgraphs;
  FindAllSubgraphs(all_subgraphs);

  bool subgraphs_need_resolve = std::any_of(all_subgraphs.cbegin(), all_subgraphs.cend(),
                                            [](const Graph* graph) {
                                              return graph->GraphResolveNeeded();
                                            });

  if (!GraphResolveNeeded() && !subgraphs_need_resolve) {
    return Status::OK();
  }

  // init all graph/subgraphs. non-recursive.
  auto init_func = [](Graph& graph) { return graph.InitInputsInitializersOutputs(); };
  ORT_RETURN_IF_ERROR(ForThisAndAllSubgraphs(all_subgraphs, init_func));

  // recursively set the outer scope node args.
  ORT_RETURN_IF_ERROR(SetOuterScopeNodeArgs(resolve_context_.outer_scope_node_args));

  std::unordered_set<std::string> outer_scope_node_args_consumed;

  // recursively build connections between nodes in this graph and all subgraphs
  ORT_RETURN_IF_ERROR(BuildConnections(outer_scope_node_args_consumed));
  ORT_ENFORCE(outer_scope_node_args_consumed.empty(),
              "Shouldn't be possible to have NodeArgs that haven't been handled already.");

  // topological sort of this and any subgraphs is non-recursive
  auto topo_sort_func = [](Graph& graph) { return graph.PerformTopologicalSortAndCheckIsAcyclic(); };
  ORT_RETURN_IF_ERROR(ForThisAndAllSubgraphs(all_subgraphs, topo_sort_func));

  // type/shape validation and inferencing on this and any subgraphs
  // recurses into subgraphs via the ONNX checker, which descends into the GraphProto in node attributes
  // which define a subgraph.
  ORT_RETURN_IF_ERROR(PerformTypeAndShapeInferencing());

  // perform the final steps for this graph and all subgraphs
  auto finalize_func = [&no_proto_sync_required](Graph& graph) {
            graph.CleanUnusedInitializers();
            graph.GraphResolveNeeded(false);

            // if we are resolving immediately after loading from a GraphProto, we don't need to
            // do a proto sync
            if (no_proto_sync_required) {
                graph.GraphProtoSyncNeeded(false);
            }

            return Status::OK(); };

  ORT_RETURN_IF_ERROR(ForThisAndAllSubgraphs(all_subgraphs, finalize_func));

  ++num_resolves_;

  return Status::OK();
}

const std::string& Graph::Name() const noexcept {
  return graph_proto_->name();
}

void Graph::SetName(const std::string& name) {
  graph_proto_->set_name(name);
}

const std::string& Graph::Description() const noexcept {
  return graph_proto_->doc_string();
}

void Graph::SetDescription(const std::string& description) {
  graph_proto_->set_doc_string(description);
}

void Graph::AddInitializedTensor(const TensorProto& tensor) {
  if (name_to_initial_tensor_.end() != name_to_initial_tensor_.find(tensor.name())) {
    return;
  }

  const gsl::not_null<TensorProto*> tensor_added{graph_proto_->add_initializer()};
  *(tensor_added) = tensor;
  name_to_initial_tensor_[tensor.name()] = tensor_added;

  if (!GraphLoadedFromModelFile(graph_proto_) && GetNodeArg(tensor.name()) == nullptr) {
    // make sure there is a NodeArg for the initializer as SetGraphInputsOutputs may add it to the graph inputs.
    // the shape will be set to the correct value in TypeCheckInputsAndInitializers as we don't yet know whether there
    // will be a matching graph input for this initializer (we prefer shape info from the graph input).
    TypeProto t;
    t.mutable_tensor_type()->set_elem_type(tensor.data_type());

    ORT_IGNORE_RETURN_VALUE(GetOrCreateNodeArg(tensor.name(), &t));
  }

  SetGraphProtoSyncNeeded();
  SetGraphResolveNeeded();
}

void Graph::RemoveInitializedTensor(const std::string& tensor_name) {
  auto iter = name_to_initial_tensor_.find(tensor_name);
  if (name_to_initial_tensor_.end() != iter) {
    name_to_initial_tensor_.erase(tensor_name);
    SetGraphProtoSyncNeeded();
    SetGraphResolveNeeded();
  }
}

Status Graph::ReplaceInitializedTensor(const ONNX_NAMESPACE::TensorProto& new_initializer) {
  // name_to_initial_tensor_ maps from name to const TensorProto*, so we first
  // look up the const pointer by name, then find and modify the mutable
  // pointed-to TensorProto in graph_proto_.

  const auto& initializer_name = new_initializer.name();
  const auto name_to_initializer_it = name_to_initial_tensor_.find(initializer_name);
  ORT_RETURN_IF_NOT(name_to_initializer_it != name_to_initial_tensor_.end(),
                    "Failed to find existing initializer with name ", initializer_name, ".");

  const auto& old_initializer = *(name_to_initializer_it->second);

  auto dims_eq = [&old_initializer, &new_initializer]() {
    if (old_initializer.dims_size() != new_initializer.dims_size()) return false;
    for (int i = 0; i < old_initializer.dims_size(); ++i) {
      if (old_initializer.dims(i) != new_initializer.dims(i)) return false;
    }
    return true;
  };

  ORT_RETURN_IF_NOT(dims_eq(), "Replacement tensor's dimensions do not match.");
  ORT_RETURN_IF_NOT(old_initializer.data_type() == new_initializer.data_type(),
                    "Replacement tensor's data type does not match.");

  auto& mutable_initializers = *(graph_proto_->mutable_initializer());
  auto old_mutable_initializer_ptr_it = std::find(
      mutable_initializers.pointer_begin(), mutable_initializers.pointer_end(), &old_initializer);
  ORT_ENFORCE(old_mutable_initializer_ptr_it != mutable_initializers.pointer_end());
  auto& old_mutable_initializer = **old_mutable_initializer_ptr_it;

  old_mutable_initializer = new_initializer;

  return Status::OK();
}

bool Graph::GetInitializedTensor(const std::string& tensor_name, const TensorProto*& value) const {
  auto iter = name_to_initial_tensor_.find(tensor_name);
  if (name_to_initial_tensor_.end() == iter) {
    value = nullptr;
    return false;
  }
  value = iter->second;
  return true;
}

void Graph::CleanAllInitializedTensors() noexcept {
  name_to_initial_tensor_.clear();
  removed_initializer_indexes_.clear();

  // Clearing RepeatedPtrFields does not free objects' memory. The memory is retained
  // and can be reused. Need to explicitly release the cleared objects and free the
  // memory.
  graph_proto_->mutable_initializer()->Clear();
  const int num_cleared = graph_proto_->initializer().ClearedCount();
  for (int i = 0; i < num_cleared; i++) {
    delete graph_proto_->mutable_initializer()->ReleaseCleared();
  }
}

const InitializedTensorSet& Graph::GetAllInitializedTensors() const noexcept {
  return name_to_initial_tensor_;
}

const std::vector<const NodeArg*>& Graph::GetValueInfo() const noexcept {
  return value_info_;
}

std::vector<NodeArg*> Graph::CreateNodeArgs(const google::protobuf::RepeatedPtrField<std::string>& names,
                                            const ArgNameToTypeMap& name_to_type_map) {
  const auto name_to_type_map_end = name_to_type_map.end();
  std::vector<NodeArg*> results;
  results.reserve(names.size());

  for (auto& name : names) {
    const TypeProto* type = nullptr;

    auto name_to_type_iter = name_to_type_map.find(name);
    if (name_to_type_iter != name_to_type_map_end) {
      // This node input arg type/shape does exist in graph proto.
      // Assign type/shape information to node input arg.
      type = &(name_to_type_iter->second);
    }

    auto node_arg = &GetOrCreateNodeArg(name, type);
    results.push_back(node_arg);
  }

  return results;
}

Node& Graph::AddNode(const Node& other) {
  const auto& definitions = other.GetDefinitions();

  auto& new_node = AddNode(other.Name(), other.OpType(), other.Description(),
                           definitions.input_defs,
                           definitions.output_defs,
                           &other.GetAttributes(),
                           other.Domain());

  return new_node;
}

Node& Graph::AddNode(const NodeProto& node_proto,
                     const ArgNameToTypeMap& name_to_type_map) {
  auto input_defs = CreateNodeArgs(node_proto.input(), name_to_type_map);
  auto output_defs = CreateNodeArgs(node_proto.output(), name_to_type_map);

  const int num_attributes = node_proto.attribute_size();
  NodeAttributes attributes;
  attributes.reserve(num_attributes);

  for (int i = 0; i < num_attributes; ++i) {
    auto& attr = node_proto.attribute(i);
    attributes[attr.name()] = attr;
  }

  return AddNode(node_proto.name(),
                 node_proto.op_type(),
                 node_proto.doc_string(),
                 input_defs,
                 output_defs,
                 &attributes,
                 node_proto.domain());
}

std::string Graph::GenerateNodeArgName(const std::string& base_name) {
  std::string new_name;
  do {
    std::ostringstream str;
    str << base_name << "_" << name_generator_++;
    new_name = str.str();
  } while (node_args_.find(new_name) != node_args_.end());
  return new_name;
}

std::string Graph::GenerateNodeName(const std::string& base_name) {
  std::string new_name;
  bool keep_going = true;

  do {
    std::ostringstream str;
    str << base_name << "_" << name_generator_++;
    new_name = str.str();

    keep_going = std::find_if(nodes_.cbegin(), nodes_.cend(), [&new_name](const std::unique_ptr<Node>& n) {
                   return (n != nullptr) && (n->Name() == new_name);
                 }) != nodes_.end();
  } while (keep_going);

  return new_name;
}

Node& Graph::AddNode(const std::string& name,
                     const std::string& op_type,
                     const std::string& description,
                     const std::vector<NodeArg*>& input_args,
                     const std::vector<NodeArg*>& output_args,
                     const NodeAttributes* attributes,
                     const std::string& domain) {
  std::vector<NodeArg*> inputs;
  std::vector<NodeArg*> outputs;
  inputs.resize(input_args.size());
  outputs.resize(output_args.size());
  int i = 0;
  for (auto input_arg : input_args) {
    inputs[i++] = &GetOrCreateNodeArg(input_arg->Name(), input_arg->TypeAsProto());
  }
  i = 0;
  for (auto output_arg : output_args) {
    outputs[i++] = &GetOrCreateNodeArg(output_arg->Name(), output_arg->TypeAsProto());
  }

  const gsl::not_null<Node*> node = AllocateNode();
  node->Init(name, op_type, description, inputs, outputs, attributes, domain);
  if (0 != op_type.compare(kNoOp)) {
    graph_proto_sync_needed_ = true;
  }

  return *node;
}

bool Graph::RemoveNode(NodeIndex p_index) {
  auto node = GetNode(p_index);
  if (nullptr == node) {
    return false;
  }

  // Node must be disconnected from any downstream nodes before removal
  ORT_ENFORCE(node->GetOutputEdgesCount() == 0, "Can't remove node ", node->Name(), " as it still has output edges.");

  // Remove all input edges.
  // Need to copy the edge info first so we can remove the real edges while iterating the copy of edge info.
  auto input_edges = node->GetRelationships().input_edges;

  for (auto& input_edge : input_edges) {
    RemoveEdge(input_edge.GetNode().Index(), p_index, input_edge.GetSrcArgIndex(), input_edge.GetDstArgIndex());
  }

  return ReleaseNode(p_index);
}

bool Graph::AddControlEdge(NodeIndex src_node_index, NodeIndex dst_node_index) {
  if (nodes_.size() <= src_node_index ||
      nodes_.size() <= dst_node_index ||
      nullptr == nodes_[src_node_index] ||
      nullptr == nodes_[dst_node_index]) {
    // Invalid node indexes specified.
    return false;
  }

  GSL_SUPPRESS(es .84) {  // ignoring return from insert()
    nodes_[src_node_index]->MutableRelationships().output_edges.insert(Node::EdgeEnd(*nodes_[dst_node_index]));
    nodes_[dst_node_index]->MutableRelationships().input_edges.insert(Node::EdgeEnd(*nodes_[src_node_index]));
    nodes_[dst_node_index]->MutableRelationships().control_inputs.insert(nodes_[src_node_index]->Name());
  }

  return true;
}

const ONNX_NAMESPACE::GraphProto& Graph::ToGraphProto() {
  if (!GraphProtoSyncNeeded()) {
    return *graph_proto_;
  }

  // Nodes.
  ToGraphProtoInternal(*graph_proto_);

  if (!removed_initializer_indexes_.empty()) {
    // Move initializers.
    std::sort(removed_initializer_indexes_.begin(), removed_initializer_indexes_.end());
    int lastInUseInitializerIndex = graph_proto_->initializer_size() - 1;
    int start = 0;
    int end = gsl::narrow_cast<int>(removed_initializer_indexes_.size()) - 1;
    int lastRemovedInitializerIndex = removed_initializer_indexes_[end];

    for (; start <= end; start++) {
      // Find a lastInUseInitializer.
      while (start <= end && lastInUseInitializerIndex == lastRemovedInitializerIndex) {
        graph_proto_->mutable_initializer()->RemoveLast();
        lastInUseInitializerIndex--;
        end--;
        if (start <= end) {
          lastRemovedInitializerIndex = removed_initializer_indexes_[end];
        }
      }

      if (start <= end) {
        // Copy the <lastInUseInitializerIndex> initializer in use to the <start> slot which is removed.
        *graph_proto_->mutable_initializer(removed_initializer_indexes_[start]) = graph_proto_->initializer(lastInUseInitializerIndex);
        graph_proto_->mutable_initializer()->RemoveLast();
        lastInUseInitializerIndex--;
      }
    }
    removed_initializer_indexes_.clear();
  }

  GraphProtoSyncNeeded(false);

  return *graph_proto_;
}

ONNX_NAMESPACE::GraphProto Graph::ToGraphProto() const {
  if (!GraphProtoSyncNeeded()) {
    return *graph_proto_;
  }
  GraphProto result;
  ToGraphProtoInternal(result);

  for (auto initializer : GetAllInitializedTensors()) {
    *result.add_initializer() = *initializer.second;
  }

  return result;
}

void Graph::ToGraphProtoInternal(ONNX_NAMESPACE::GraphProto& graph_proto) const {
  graph_proto_->clear_node();
  graph_proto_->clear_input();
  graph_proto_->clear_output();
  graph_proto_->clear_value_info();
  graph_proto.set_name(Name());
  graph_proto.set_doc_string(Description());

  for (const auto* input_arg : GetInputsIncludingInitializers()) {
    *(graph_proto.mutable_input()->Add()) = input_arg->ToProto();
  }

  for (const auto* output_arg : GetOutputs()) {
    *(graph_proto.mutable_output()->Add()) = output_arg->ToProto();
  }

  for (const auto* value_info : value_info_) {
    *(graph_proto.mutable_value_info()->Add()) = value_info->ToProto();
  }

  // add the NodeArg info for outer scope NodeArgs so we capture the type information
  for (const auto& name : outer_scope_node_arg_names_) {
    auto* node_arg = GetNodeArg(name);
    ORT_ENFORCE(node_arg, "Outer scope node arg name '" + name + "'was added but does not exist. ");
    *(graph_proto.mutable_value_info()->Add()) = node_arg->ToProto();
  }

  GraphViewer graph_viewer(*this);
  // Nodes must be sorted in Topological Order in the GraphProto per ONNX spec.
  for (auto& node_idx : graph_viewer.GetNodesInTopologicalOrder()) {
    const gsl::not_null<NodeProto*> node_proto{graph_proto.add_node()};
    const gsl::not_null<const Node*> p_node{GetNode(node_idx)};
    p_node->ToProto(*node_proto);
  }
}

void Graph::CleanUnusedInitializers() {
  std::unordered_set<std::string> used_args;

  const auto& inputs = GetInputs();
  const auto& outputs = GetOutputs();

  std::for_each(inputs.cbegin(), inputs.cend(), [&used_args](const NodeArg* input) {
    ORT_IGNORE_RETURN_VALUE(used_args.insert(input->Name()));
  });

  std::for_each(outputs.cbegin(), outputs.cend(), [&used_args](const NodeArg* output) {
    ORT_IGNORE_RETURN_VALUE(used_args.insert(output->Name()));
  });

  for (const auto& node : Nodes()) {
    for (const auto* def : node.InputDefs()) {
      ORT_IGNORE_RETURN_VALUE(used_args.insert(def->Name()));
    }

    for (const auto* def : node.ImplicitInputDefs()) {
      ORT_IGNORE_RETURN_VALUE(used_args.insert(def->Name()));
    }
  }

  std::vector<std::string> erase_list;
  auto end = used_args.end();
  for (const auto& pv : name_to_initial_tensor_) {
    const std::string& name = pv.first;
    if (used_args.find(name) == end) {
      // on the first call to Graph::Resolve we are removing unnecessary initializers that should be removed
      // from the model.
      // on later calls we are removing initializers that optimizations have made redundant.
      if (num_resolves_ == 0) {
        LOGS_DEFAULT(WARNING) << "Removing initializer '"
                              << name << "'. It is not used by any node and should be removed from the model.";
      } else {
        LOGS_DEFAULT(INFO) << "Removing initializer '" << name << "'. It is no longer used by any node.";
      }

      erase_list.push_back(name);
    }
  }

  std::for_each(erase_list.cbegin(), erase_list.cend(),
                [this](const std::string& name) { name_to_initial_tensor_.erase(name); });
}

GSL_SUPPRESS(es .84)  // warning about ignoring return value from insert(...)
Status Graph::SetGraphInputsOutputs() {
  // Reset graph inputs excluding initializers/value_info.
  graph_inputs_excluding_initializers_.clear();
  value_info_.clear();

  // Flag indicates that this graph is loaded from model file.
  // If it's true, then graph inputs and outputs will keep the same
  // as what are specified in the model, otherwise, graph inputs
  // and outputs will be inferred.
  const bool loaded_from_model_file = GraphLoadedFromModelFile(graph_proto_);

  if (loaded_from_model_file) {
    // Reset graph inputs/outputs.
    graph_inputs_including_initializers_.clear();
    graph_outputs_.clear();

    // Name to NodeArg mapping of all graph initializers.
    std::unordered_map<std::string, const NodeArg*> graph_initializers;

    // Name to NodeArg mapping of all graph inputs.
    std::unordered_map<std::string, const NodeArg*> graph_inputs;

    // Name to NodeArg mapping of all graph node outputs.
    std::unordered_map<std::string, const NodeArg*> nodes_outputs;

    for (auto& initializer : graph_proto_->initializer()) {
      auto& initializer_name = initializer.name();
      auto initializer_arg = GetNodeArg(initializer_name);
      graph_initializers.insert({initializer_name, initializer_arg});
    }

    // Set graph inputs.
    // <graph_inputs_including_initializers_> contains inputs exactly specified in proto.
    // <graph_inputs_excluding_initializers_> contains inputs without default value (specified as initializer).
    for (auto& graph_input : graph_proto_->input()) {
      auto& name = graph_input.name();
      const auto* node_arg = GetNodeArg(name);
      ORT_ENFORCE(node_arg, "Graph ctor should have created NodeArg for initializer.");
      graph_inputs.insert({name, node_arg});
      graph_inputs_including_initializers_.push_back(node_arg);
      if (graph_initializers.end() == graph_initializers.find(name)) {
        graph_inputs_excluding_initializers_.push_back(node_arg);
      }
    }

    for (const auto& node : Nodes()) {
      for (const auto* output_def : node.OutputDefs()) {
        nodes_outputs.insert({output_def->Name(), output_def});
      }
    }

    // Set graph outputs.
    // Graph outputs specified in the model must be nodes' outputs, initializer or graph inputs.
    for (auto& graph_output : graph_proto_->output()) {
      auto& graph_output_name = graph_output.name();
      auto iter = nodes_outputs.find(graph_output_name);
      if (nodes_outputs.end() == iter) {
        // Graph output is not found as any node's output.
        auto iter2 = graph_initializers.find(graph_output_name);
        if (graph_initializers.end() == iter2) {
          // Graph output is not found as any initializer.
          auto iter3 = graph_inputs.find(graph_output_name);
          if (graph_inputs.end() == iter3) {
            // Graph output is not found as any graph input.
            return Status(ONNXRUNTIME, FAIL,
                          "This is an invalid model. "
                          "Graph output (" +
                              graph_output_name + ") does not exist in the graph.");
          }
          graph_outputs_.push_back(iter3->second);
          continue;
        }
        graph_outputs_.push_back(iter2->second);
        continue;
      }
      graph_outputs_.push_back(iter->second);
    }

    // Set graph value_info_.
    for (auto& graph_value_info : graph_proto_->value_info()) {
      auto& name = graph_value_info.name();
      const auto* node_arg = GetNodeArg(name);
      value_info_.push_back(node_arg);
    }

  } else {
    std::unordered_map<std::string, size_t> output_name_to_node_arg_index;
    std::vector<const NodeArg*> output_node_args_in_order;

    // if something is coming from outer scope, consider it already added
    std::unordered_set<std::string> added_input_names{outer_scope_node_arg_names_};
    if (!graph_inputs_manually_set_) {
      graph_inputs_including_initializers_.clear();
    }

    if (!graph_outputs_manually_set_) {
      graph_outputs_.clear();
    }

    // Collect all nodes' outputs
    for (const auto& node : Nodes()) {
      for (const auto* output_def : node.OutputDefs()) {
        if (output_def->Exists()) {
          output_node_args_in_order.push_back(output_def);
          output_name_to_node_arg_index.insert({output_def->Name(), output_node_args_in_order.size() - 1});
        }
      }
    }

    // Init graph output args with copy of all node output args.
    auto graph_output_args = output_name_to_node_arg_index;
    for (const auto& node : Nodes()) {
      // Go thru all node's inputs.
      for (const auto* input_arg : node.InputDefs()) {
        if (!input_arg->Exists()) {
          // It's an optional input and does not exist in this case.
          continue;
        }

        auto output_arg_iter = output_name_to_node_arg_index.find(input_arg->Name());
        if (output_name_to_node_arg_index.end() == output_arg_iter) {
          // This input arg is not the output of another node so must come from either a graph input or an initializer.
          const std::string& name = input_arg->Name();

          if (added_input_names.end() == added_input_names.find(name)) {
            // This graph input has not been added into <graph_inputs_>.
            bool is_initializer = name_to_initial_tensor_.find(name) != name_to_initial_tensor_.end();

            if (!graph_inputs_manually_set_) {
              // if IR version < 4 all initializers must have a matching graph input
              // (even though the graph input is not allowed to override the initializer).
              // if IR version >= 4 initializers are not required to have a matching graph input.
              // any graph inputs that are to override initializers must be specified by calling SetInputs.
              if (!is_initializer || ir_version_ < 4) {
                graph_inputs_including_initializers_.push_back(input_arg);
              }
            } else {
              // graph_inputs_including_initializers_ has been manually populated by SetInputs.
              // Validation: the <input_arg> must be in graph inputs or initializers when it's manually set.
              if (!is_initializer) {
                const auto& inputs = graph_inputs_including_initializers_;
                bool in_inputs = std::find(inputs.begin(), inputs.end(), input_arg) != inputs.end();
                if (!in_inputs) {
                  return Status(ONNXRUNTIME, FAIL,
                                name + " must be either specified in graph inputs or graph initializers.");
                }
              }
            }

            if (!is_initializer) {
              graph_inputs_excluding_initializers_.push_back(input_arg);
            }

            added_input_names.insert(name);
          }
        } else if (graph_output_args.erase(output_arg_iter->first) >= 1) {
          // Remove the output arg name from graph outputs since it's
          // the input of this node, which we call it intermediate result
          // and store it in <m_valueinfo>.
          value_info_.push_back(input_arg);
        }
      }
    }

    if (!graph_outputs_manually_set_) {
      // Set graph outputs in order.
      std::vector<size_t> graph_output_args_index;
      graph_output_args_index.reserve(graph_output_args.size());
      for (const auto& output_arg : graph_output_args) {
        graph_output_args_index.push_back(output_arg.second);
      }
      std::sort(graph_output_args_index.begin(), graph_output_args_index.end());
      for (auto& output_arg_index : graph_output_args_index) {
        graph_outputs_.push_back(output_node_args_in_order[output_arg_index]);
      }
    }
  }

  ComputeOverridableInitializers();

  return Status::OK();
}

void Graph::ComputeOverridableInitializers() {
  graph_overridable_initializers_.clear();
  if (CanOverrideInitializer()) {
    // graph_inputs_excluding_initializers_ and graph_inputs_including_initializers_
    // are inserted in the same order. So we walk and compute the difference.
    auto f_incl = graph_inputs_including_initializers_.cbegin();
    const auto l_incl = graph_inputs_including_initializers_.cend();
    auto f_excl = graph_inputs_excluding_initializers_.cbegin();
    const auto l_excl = graph_inputs_excluding_initializers_.cend();

    while (f_incl != l_incl) {
      // Equal means not an initializer
      if (f_excl != l_excl && *f_incl == *f_excl) {
        ++f_incl;
        ++f_excl;
        continue;
      }
      graph_overridable_initializers_.push_back(*f_incl);
      ++f_incl;
    }
  }
}

// calling private ctor
GSL_SUPPRESS(r .11)
gsl::not_null<Node*> Graph::AllocateNode() {
  ORT_ENFORCE(nodes_.size() < static_cast<unsigned int>(std::numeric_limits<int>::max()));
  std::unique_ptr<Node> new_node(new Node(nodes_.size(), *this));
  Node* node{new_node.get()};

  nodes_.push_back(std::move(new_node));
  ++num_of_nodes_;
  graph_resolve_needed_ = true;

  return gsl::not_null<Node*>{node};
}

// TODO: Does this need (and maybe AllocateNode) to be threadsafe so nodes_ and num_of_nodes_ managed more carefully?
bool Graph::ReleaseNode(NodeIndex index) {
  if (index >= nodes_.size()) {
    return false;
  }

  // index is valid, but the entry may already be empty
  if (nodes_[index] != nullptr) {
    nodes_[index] = nullptr;
    --num_of_nodes_;
    graph_proto_sync_needed_ = true;
    graph_resolve_needed_ = true;
  }

  return true;
}

IOnnxRuntimeOpSchemaCollectionPtr Graph::GetSchemaRegistry() const {
  return schema_registry_;
}

Node& Graph::FuseSubGraph(std::unique_ptr<::onnxruntime::IndexedSubGraph> sub_graph,
                          const std::string& fused_node_name) {
  ORT_ENFORCE(nullptr != sub_graph && nullptr != sub_graph->GetMetaDef());

  auto func_meta_def = sub_graph->GetMetaDef();
  ORT_ENFORCE(nullptr != func_meta_def);
  std::vector<NodeArg*> input_args;
  std::vector<NodeArg*> output_args;
  for (auto& arg_name : func_meta_def->inputs) {
    input_args.push_back(GetNodeArg(arg_name));
  }
  for (auto& arg_name : func_meta_def->outputs) {
    output_args.push_back(GetNodeArg(arg_name));
  }

  auto& fused_node = AddNode(fused_node_name,
                             func_meta_def->name,
                             func_meta_def->doc_string,
                             input_args,
                             output_args,
                             &func_meta_def->attributes,
                             func_meta_def->domain);

  fused_node.SetNodeType(Node::Type::Fused);
  function_container_.emplace_back(MakeFunction(*this, std::move(sub_graph)));
  fused_node.SetFunctionBody(*function_container_.back());

  // Remove nodes fused above.
  auto& sub_graph_ref = function_container_.back()->GetIndexedSubGraph();
  for (auto node_index : sub_graph_ref.nodes) {
    auto node = GetNode(node_index);
    if (nullptr == node) {
      continue;
    }
    auto output_edges = node->GetRelationships().output_edges;
    for (auto output_edge : output_edges) {
      RemoveEdge(node->Index(), output_edge.GetNode().Index(), output_edge.GetSrcArgIndex(), output_edge.GetDstArgIndex());
    }
    RemoveNode(node_index);
  }
  return fused_node;
}

Status Graph::InlineFunction(Node& node) {
  // Remove the function node, add the nodes in function's subgraph into the
  // main graph.
  const Graph& subgraph = node.GetFunctionBody()->Body();
  auto output_edges = node.GetRelationships().output_edges;
  for (auto output_edge : output_edges) {
    RemoveEdge(node.Index(), output_edge.GetNode().Index(), output_edge.GetSrcArgIndex(), output_edge.GetDstArgIndex());
  }
  RemoveNode(node.Index());
  for (const auto& subgraph_node : subgraph.Nodes()) {
    AddNode(subgraph_node);
  }
  ORT_RETURN_IF_ERROR(this->Resolve());
  return Status::OK();
}

void Graph::SetInputs(const std::vector<const NodeArg*>& inputs) {
  if (GraphLoadedFromModelFile(graph_proto_)) {
    // TODO: add this support.
    ORT_THROW("This API is not supported when model is loaded from proto file right now.");
  }

  graph_inputs_including_initializers_ = inputs;
  graph_inputs_manually_set_ = true;
}

void Graph::SetOutputs(const std::vector<const NodeArg*>& outputs) {
  if (GraphLoadedFromModelFile(graph_proto_)) {
    // TODO: add this support.
    ORT_THROW("This API is not supported when model is loaded from proto file right now.");
  }
  graph_outputs_ = outputs;
  graph_outputs_manually_set_ = true;
}

void Graph::AddFunction(const ONNX_NAMESPACE::FunctionProto* func_proto) {
  this->model_functions_[func_proto->name()] = func_proto;
}

Graph::~Graph() {
  // nothing to do, but we put it here so we don't need to fully define types in Graph that are held in unique_ptr
  // such as   std::unique_ptr<FunctionContainer> function_container_;
}
}  // namespace onnxruntime
