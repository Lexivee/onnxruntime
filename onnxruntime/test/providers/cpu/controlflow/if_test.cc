// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "gtest/gtest.h"
#include "gmock/gmock.h"
// #include "core/framework/customregistry.h"
#include "core/framework/session_state.h"
#include "core/providers/cpu/controlflow/if.h"
#include "test/providers/provider_test_utils.h"
#include "core/session/inference_session.h"

#include "test/util/include/default_providers.h"

using namespace ONNX_NAMESPACE;

namespace onnxruntime {
namespace test {

namespace {
struct RunOptions {
  bool include_dim_values_in_main_graph = false;
  int symbolic_dim_value_in_main_graph = -1;
  bool include_dim_values_in_subgraph = true;
  bool mixed_execution_providers = false;
};
}

static const ONNX_NAMESPACE::GraphProto CreateSubgraph(bool then_branch, const RunOptions& options);

/*
 Main graph

 split_input          if_cond      if_graph_input_0,
      |                   |              |
   [Split]                |          [Identity]
      |                   |              |
      |                   |         if_input_0
      |  split_out_0      |              |
      ------------------[If]--------------   (see below for then/else subgraphs in If node)
         split_out_1      |
                          |
                       if_out_0
*/

class IfOpTester : public OpTester {
 public:
  IfOpTester(const RunOptions& options) : OpTester("If"), options_{options} {
  }

 protected:
  void AddNodes(onnxruntime::Graph& graph,
                std::vector<onnxruntime::NodeArg*>& graph_input_defs,
                std::vector<onnxruntime::NodeArg*>& graph_output_defs,
                std::vector<std::function<void(onnxruntime::Node& node)>>& /*add_attribute_funcs*/) override {
    // Graph inputs are 0:Split input, 1:Cond for If, 2:if input
    ASSERT_EQ(graph_input_defs.size(), 3);
    ASSERT_EQ(graph_output_defs.size(), 1);

    NodeArg* split_input = graph_input_defs[0];
    NodeArg* if_cond_input = graph_input_defs[1];
    NodeArg* if_input = graph_input_defs[2];

    std::vector<NodeArg*> inputs;
    std::vector<NodeArg*> outputs;

    // add Split node
    {
      TypeProto split_out_type;
      split_out_type.mutable_tensor_type()->set_elem_type(TensorProto_DataType_FLOAT);
      auto& split_out_0 = graph.GetOrCreateNodeArg("split_out_0", &split_out_type);
      auto& split_out_1 = graph.GetOrCreateNodeArg("split_out_1", &split_out_type);

      inputs = {split_input};
      outputs = {&split_out_0, &split_out_1};

      graph.AddNode("split", "Split", "Split into 2", inputs, outputs);
    }

    // add If node
    {
      inputs = {if_cond_input};
      outputs = {graph_output_defs[0]};

      auto& if_node = graph.AddNode("if", "If", "If node", inputs, outputs);

      auto then_proto = CreateSubgraph(true, options_);
      auto else_proto = CreateSubgraph(false, options_);
      if_node.AddAttribute("then_branch", {then_proto});
      if_node.AddAttribute("else_branch", {else_proto});
    }

    // add Identity node so if_graph_input_0 comes from graph inputs
    {
      inputs = {if_input};
      outputs = {&graph.GetOrCreateNodeArg("if_input_0", if_input->TypeAsProto())};
      graph.AddNode("identity", "Identity", "Pass if input through from graph inputs.", inputs, outputs);
    }
  }

 private:
  RunOptions options_;
};

   /* Subgraphs looks like this. All inputs come from outer scope so we just
   create a NodeArg with the input name. The numbers in [] are the values the tests are expected to produce
   as output from each node.

THEN branch
    split_out_0    if_input_0   [1]
             \          |
       [1]    \         |
               \------[Add]
                        |
                   add_out_0    [2]

ELSE branch
    split_out_1    if_input_0   [1]
            \          |
      [10]   \         |
              \------[Add]
                        |
                   add_out_1    [11]
*/

static const ONNX_NAMESPACE::GraphProto CreateSubgraph(bool then_branch, const RunOptions& options) {
  bool include_dim_values = options.include_dim_values_in_subgraph;
  bool sym_dim_zero = options.symbolic_dim_value_in_main_graph == 0;

  Model model(then_branch ? "If_then" : "If_else");
  auto& graph = model.MainGraph();

  std::vector<NodeArg*> inputs;
  std::vector<NodeArg*> outputs;

  const std::string suffix = then_branch ? "0" : "1";

  // graph input has to have type and rank even though it's an outer scope value.
  TypeProto input_tensor_type;
  input_tensor_type.mutable_tensor_type()->set_elem_type(TensorProto_DataType_FLOAT);
  auto* mutable_dim = input_tensor_type.mutable_tensor_type()->mutable_shape()->add_dim();
  if (include_dim_values) {
    mutable_dim->set_dim_value(1);
  } else if (sym_dim_zero) {
    mutable_dim->set_dim_param("symbolic");
  }

  // outer scope values
  auto& split_output = graph.GetOrCreateNodeArg("split_out_" + suffix, &input_tensor_type);
  auto& if_input = graph.GetOrCreateNodeArg("if_input_0", &input_tensor_type);

  // add so that we don't end up with it being considered a graph input
  graph.AddOuterScopeNodeArg("split_out_" + suffix);
  graph.AddOuterScopeNodeArg("if_input_0");

  {
    // Add

    // graph output has to have type and shape
    TypeProto add_output_tensor;
    add_output_tensor.mutable_tensor_type()->set_elem_type(TensorProto_DataType_FLOAT);
    mutable_dim = add_output_tensor.mutable_tensor_type()->mutable_shape()->add_dim();
    if (include_dim_values) {
      mutable_dim->set_dim_value(1);
    } else if (sym_dim_zero) {
      mutable_dim->set_dim_param("symbolic");
    }

    auto& add_out = graph.GetOrCreateNodeArg("add_out_" + suffix, &add_output_tensor);

    inputs = {&split_output, &if_input};
    outputs = {&add_out};

    graph.AddNode("add", "Add", "Add two inputs.", inputs, outputs);
  }

  auto status = graph.Resolve();
  EXPECT_EQ(status, Status::OK());

  auto& proto = graph.ToGraphProto();

  return proto;
}


// The following subgraph creator is to test the opset-11 "If" node
// which is allowed to produce different shape outputs on the "then" and "else" branches 

/* Subgraphs looks like this.
                                                              if_cond    
THEN branch                                                    /
         [Constant]                            [Identity]-----/
             |                                     |
         constant_out_0                       identity_out_0   
 (output_shape: [1]. output_value = [1])     (output_shape: [1]. output_value = [true]) 


                                                               if_cond     
ELSE branch                                                    /
         [Constant]                            [Identity]-----/
             |                                     |
         constant_out_0                       identity_out_0   
 (output_shape: [2]. output_value = [1, 1])     (output_shape: [1]. output_value = [false]) 
*/

static const ONNX_NAMESPACE::GraphProto CreateSubgraphWithConstantNode(bool then_branch) {
  Model model(then_branch ? "If_then" : "If_else");
  auto& graph = model.MainGraph();

  std::vector<NodeArg*> inputs;
  std::vector<NodeArg*> outputs;

  // graph output types
  // constant_out
  TypeProto float_proto;
  float_proto.mutable_tensor_type()->set_elem_type(TensorProto_DataType_FLOAT);

  // "then" branch produces 1D output of shape [1]
  if (then_branch) {
    float_proto.mutable_tensor_type()->mutable_shape()->add_dim()->set_dim_value(1);  
  } 
  else {
    // "else" branch produces 1D output of shape [2]
    float_proto.mutable_tensor_type()->mutable_shape()->add_dim()->set_dim_value(2);  
  }
  

  // graph outputs
  auto& constant_out = graph.GetOrCreateNodeArg("constant_out", &float_proto);

  // produce constant_out
  {
    outputs = {&constant_out};

    TensorProto constant_tensor_proto;

    auto& constant_node = graph.AddNode("constant_out", "Constant", "Produce constant_out", {}, outputs);

    AttributeProto attr_proto;
    attr_proto.set_name("value");
    attr_proto.set_type(AttributeProto_AttributeType_TENSOR);

    auto* constant_attribute_tensor_proto = attr_proto.mutable_t();
    // "then" branch produces 1D output of shape [1]
    if (then_branch) {
      *constant_attribute_tensor_proto->mutable_dims()->Add() = 1;  // 1D output of shape [1]
    } else {
      // "else" branch produces 1D output of shape [2]
      *constant_attribute_tensor_proto->mutable_dims()->Add() = 2;  // 1D output of shape [2]
    }

    constant_attribute_tensor_proto->set_data_type(TensorProto_DataType_FLOAT);  // float output
    *constant_attribute_tensor_proto->mutable_float_data()->Add() = 1.0f;        // float output with value 1.0f in the data container

    if (!then_branch) {
      // "else" branch produces 1D output of shape [2], hence add the extra value in the data container
      *constant_attribute_tensor_proto->mutable_float_data()->Add() = 1.0f;  // 1D float output with value 1.0f
    }

    constant_node.AddAttribute("value", attr_proto);
  }

  graph.SetOutputs({&constant_out});

  auto status = graph.Resolve();
  EXPECT_EQ(status, Status::OK());

  return graph.ToGraphProto();
};

void RunTest(bool condition_value,
             RunOptions options,
             bool is_tensorrt_supported = true,
             OpTester::ExpectResult expect_result = OpTester::ExpectResult::kExpectSuccess,
             const std::string& failure_message = "") {
  IfOpTester test{options};

  test.AddShapeToTensorData(options.include_dim_values_in_main_graph,
                            options.symbolic_dim_value_in_main_graph);

  // add the main graph inputs and outputs.
  // we will handle the 'If' inputs in the AddNodes override, and as 'If' is the last node
  // it's outputs are 1:1 with the graph outputs.

  // simple tensor that we split into 2, and use one output for the 'then' and one for the 'else' branch in the If
  test.AddInput<float>("split_input", {2}, {1.f, 10.f});

  // graph input to specify which branch to take
  test.AddInput<bool>("if_cond", {1}, {condition_value});

  test.AddInput<float>("if_graph_input_0", {1}, {1.f});

  std::vector<int64_t> output_shape{1};
  if (condition_value) {
    test.AddOutput<float>("if_out_0", output_shape, {2.f});
  } else {
    test.AddOutput<float>("if_out_0", output_shape, {11.f});
  }

  std::unordered_set<std::string> excluded_providers;
  // Disable TensorRT on SymbolicShape or NoShape tests
  if (!is_tensorrt_supported) {
    excluded_providers.insert(kTensorrtExecutionProvider);
  }
  if (options.mixed_execution_providers) {
    // we want the CUDA provider to be first, and the CPU provider second. all except the If should run on
    // CUDA given that, which creates the scenario where we need to copy to/from CPU to execute the If node correctly.
    std::vector<std::unique_ptr<IExecutionProvider>> execution_providers;
    execution_providers.push_back(DefaultCudaExecutionProvider());
    execution_providers.push_back(DefaultCpuExecutionProvider());

    test.Run(expect_result, failure_message, excluded_providers, nullptr, &execution_providers);
  } else {
    test.Run(expect_result, failure_message, excluded_providers);
  }
}

TEST(If, ShapeInMainGraph_NoShapeInSubgraph_True) {
  RunOptions options{};
  options.include_dim_values_in_main_graph = true;
  options.include_dim_values_in_subgraph = false;

  RunTest(true, options, false);
}

TEST(If, ShapeInMainGraph_NoShapeInSubgraph_False) {
  RunOptions options{};
  options.include_dim_values_in_main_graph = true;
  options.include_dim_values_in_subgraph = false;

  RunTest(false, options, false);
}

TEST(If, NoShapeInMainGraph_ShapeInSubgraph_True) {
  RunOptions options{};
  options.include_dim_values_in_main_graph = false;
  options.include_dim_values_in_subgraph = true;

  RunTest(true, options, false);
}

TEST(If, NoShapeInMainGraph_ShapeInSubgraph_False) {
  RunOptions options{};
  options.include_dim_values_in_main_graph = false;
  options.include_dim_values_in_subgraph = true;

  RunTest(false, options, false);
}

#ifdef USE_CUDA
TEST(If, MixedExecutionProviders) {
  RunOptions options{};
  options.mixed_execution_providers = true;
  RunTest(true, options);
}
#endif  // USE_CUDA

TEST(If, SymbolicShapeInMainGraph_NoShapeInSubgraph_True) {
  RunOptions options;
  options.include_dim_values_in_main_graph = true;
  options.symbolic_dim_value_in_main_graph = 0;
  options.include_dim_values_in_subgraph = false;

  RunTest(true, options, false);
}

TEST(If, SymbolicShapeInMainGraph_NoShapeInSubgraph_False) {
  RunOptions options;
  options.include_dim_values_in_main_graph = true;
  options.symbolic_dim_value_in_main_graph = 0;
  options.include_dim_values_in_subgraph = false;

  RunTest(false, options, false);
}

TEST(If, Opset11ThenAndElseBranchesProduceDifferentOutputShapes) {
  OpTester test("If", 11);

  // add the attributes
  test.AddAttribute<GraphProto>("then_branch", CreateSubgraphWithConstantNode(true));
  test.AddAttribute<GraphProto>("else_branch", CreateSubgraphWithConstantNode(false));

  // "else" subgraph should be executed
  test.AddInput<bool>("if_cond", {1}, {false});

  // output is a tensor of shape [2] with values - [1.0f, 1.0f]
  test.AddOutput<float>("if_out_0", {2}, {1.0f, 1.0f});

  test.Run();
}

}  // namespace test
}  // namespace onnxruntime
