import onnx
from onnx import helper, numpy_helper, shape_inference
from onnx import AttributeProto, TensorProto, GraphProto
import os

_this_dir = os.path.abspath(os.path.dirname(__file__))

def _onnx_export(graph_def, relative_path, verbose=False):
  model = helper.make_model(graph_def, producer_name='makalini', opset_imports=[helper.make_operatorsetid("", 11)])
  onnx.checker.check_model(model)
  inferred_model = shape_inference.infer_shapes(model)
  onnx.checker.check_model(inferred_model)
  model_path = os.path.join(_this_dir, relative_path)
  os.makedirs(os.path.dirname(model_path), exist_ok=True)
  onnx.save_model(model, model_path)
  if verbose:
    print()
    print(inferred_model)
    import onnxruntime as rt
    rt.InferenceSession(model_path)

def cse1():
  graph_def = helper.make_graph(
    nodes = [
      helper.make_node(op_type = "MatMul", inputs = ['w', 'x'], outputs = ['MatMul1'], name = 'matmul_1'),
      helper.make_node(op_type = "MatMul", inputs = ['w', 'x'], outputs = ['MatMul2'], name = 'matmul_2'),
      helper.make_node(op_type = "Add", inputs = ['MatMul1', 'b'], outputs = ['Add1'], name = 'add_1'),
      helper.make_node(op_type = "Add", inputs = ['MatMul2', 'b'], outputs = ['Add2'], name = 'add_2'),
      helper.make_node(op_type = "Relu", inputs = ['Add1'], outputs = ['Relu1'], name = 'relu_1'),
      helper.make_node(op_type = "Relu", inputs = ['Add2'], outputs = ['Relu2'], name = 'relu_2'),
      helper.make_node(op_type = "Add", inputs = ['Relu1', 'Relu2'], outputs = ['Result'], name = 'result')
    ],
    name = 'cse1',
    inputs = [
      helper.make_tensor_value_info("x", TensorProto.FLOAT, [5])
    ],
    outputs = [
      helper.make_tensor_value_info('Result', TensorProto.FLOAT, [2])
    ],
    initializer = [
      helper.make_tensor('w', TensorProto.FLOAT, [2, 5], list(range(2*5))),
      helper.make_tensor('b', TensorProto.FLOAT, [2], list(range(2))),
    ]
  )
  _onnx_export(graph_def, 'cse1.onnx')

def cse_initializer_simple():
  nodes = []
  initializers = []
  outputs = []
  
  for i in range(0, 5):
    padding_output_name = 'x_padding_{}'.format(i)
    
    # generate pad nodes with either 1 or 2 as the padding
    # so that we end up with 2 equivalence classes
    pad_val_ = 1 + i%2;
    
    padding = helper.make_node(
      op_type = "Constant",
      inputs = [],
      outputs = ['padding_{}'.format(i)],
      value=numpy_helper.from_array(numpy_helper.np.array([0, 0, pad_val_, pad_val_, 0, 0, pad_val_, pad_val_])))
      
    pad_node = helper.make_node(
      op_type = "Pad",
      inputs = ['x', 'padding_{}'.format(i)],
      outputs = [padding_output_name],
      name = 'padding_{}'.format(i)
    )
        
    conv_output_name = 'x_padding_conv_{}'.format(i)
    
    conv_node = helper.make_node(
      op_type = "Conv",
      inputs = [padding_output_name, 'weights_{}'.format(i)],
      outputs = [conv_output_name],
      name = 'conv_{}'.format(i),
      dilations=[1,1],
      group=1,
      kernel_shape=[3,3],
      pads=[0,0,0,0],
      strides=[1,1]
    )
    
    conv_weights = numpy_helper.np.random.randn(1,3,3,3)
    conv_weights = numpy_helper.from_array(conv_weights)
    conv_weights.name = 'weights_{}'.format(i)
        
    conv_output = helper.make_tensor_value_info(
      conv_output_name,
      TensorProto.DOUBLE,
      [])
      
    nodes.append(padding)
    nodes.append(pad_node)
    nodes.append(conv_node)
    initializers.append(conv_weights)
    
    outputs.append(conv_output)
    
  graph_def = helper.make_graph(
    nodes=nodes,
    name = 'cse_initializer_graph_output',
    inputs = [ helper.make_tensor_value_info("x", TensorProto.DOUBLE, [1, 3, 5, 5])],
    outputs = outputs,
    initializer = initializers)
  
  _onnx_export(graph_def, 'cse_initializer_simple.onnx')

def cse_initializer_commutative_ops():
  mul_input_2 = numpy_helper.np.random.randn(1,3,5,5)
  mul_input_2 = numpy_helper.from_array(mul_input_2)
  mul_input_2.name = 'mul_constants'
  
  nodes = []
  initializers = [mul_input_2]
  outputs = []
  
  for i in range(0, 4):
    mul_output_name = 'mul_{}'.format(i)
    
    mul_inputs = \
      ['x', 'mul_constants'] if i%2 == 0 else ['mul_constants', 'x']
      
    mult_node = helper.make_node(
      op_type = "Mul",
      inputs = mul_inputs,
      outputs = [mul_output_name],
      name = mul_output_name,
    )
    
    conv_output_name = 'x_mul_conv_{}'.format(i)
    
    conv_weights = numpy_helper.np.random.randn(1,3,3,3)
    conv_weights = numpy_helper.from_array(conv_weights)
    conv_weights.name = 'weights_{}'.format(i)
    
    conv_node = helper.make_node(
      op_type = "Conv",
      inputs = [mul_output_name, 'weights_{}'.format(i)],
      outputs = [conv_output_name],
      name = 'conv_{}'.format(i),
      dilations=[1,1],
      group=1,
      kernel_shape=[3,3],
      pads=[0,0,0,0],
      strides=[1,1]
    )
        
    conv_output = helper.make_tensor_value_info(
      conv_output_name,
      TensorProto.DOUBLE,
      [])
      
    nodes.append(mult_node)
    nodes.append(conv_node)
    initializers.append(conv_weights)
    
    outputs.append(conv_output)
    
  graph_def = helper.make_graph(
    nodes=nodes,
    name = 'cse_initializer_graph_output',
    inputs = [ helper.make_tensor_value_info("x", TensorProto.DOUBLE, [1, 3, 5, 5])],
    outputs = outputs,
    initializer = initializers)
  
  _onnx_export(graph_def, 'cse_initializer_commutative.onnx')

def cse_graph_output():
  graph_def = helper.make_graph(
    nodes = [
      helper.make_node(op_type = "Add", inputs = ['x', 'b'], outputs = ['res1'], name = 'add_1'),
      helper.make_node(op_type = "Add", inputs = ['x', 'b'], outputs = ['res2'], name = 'add_2'),
    ],
    name = 'cse_graph_output',
    inputs = [
      helper.make_tensor_value_info("x", TensorProto.FLOAT, [5])
    ],
    outputs = [
      helper.make_tensor_value_info('res1', TensorProto.FLOAT, [5]),
      helper.make_tensor_value_info('res2', TensorProto.FLOAT, [5])
    ],
    initializer = [
      helper.make_tensor('b', TensorProto.FLOAT, [5], list(range(5))),
    ]
  )

  _onnx_export(graph_def, 'cse_graph_output.onnx')

def cse_optional_args():
  n = 5
  graph_def = helper.make_graph(
    nodes = [
      helper.make_node(op_type = "Clip", inputs = ['x'], outputs = ['Clipped0'], name = 'clip_0'),
      helper.make_node(op_type = "Clip", inputs = ['x', ''], outputs = ['Clipped1'], name = 'clip_1'),
      helper.make_node(op_type = "Clip", inputs = ['x', '', ''], outputs = ['Clipped2'], name = 'clip_2'),
      helper.make_node(op_type = "Clip", inputs = ['x', '', 'c'], outputs = ['Clipped3'], name = 'clip_3'),
      helper.make_node(op_type = "Clip", inputs = ['x', 'c', ''], outputs = ['Clipped4'], name = 'clip_4'),
      helper.make_node(op_type = "Sum", inputs = ['Clipped0', 'Clipped1', 'Clipped2', 'Clipped3', 'Clipped4'], outputs = ['Result'], name = 'sum_1')
    ],
    name = 'cse_optional_args',
    inputs = [
      helper.make_tensor_value_info("x", TensorProto.FLOAT, [n])
    ],
    outputs = [
      helper.make_tensor_value_info('Result', TensorProto.FLOAT, [n])
    ],
    initializer = [
      helper.make_tensor('c', TensorProto.FLOAT, [], [0.5])
    ]
  )
  _onnx_export(graph_def, 'cse_optional_args.onnx')

def cse_subgraph():
  if_true_graph = helper.make_graph(
    nodes = [
      helper.make_node(op_type = "Sum", inputs = ['x', 'x'], outputs = ['Result1'], name = 'iftrue_res_1'),
      helper.make_node(op_type = "Sum", inputs = ['x', 'x'], outputs = ['Result2'], name = 'iftrue_res_2'),
      helper.make_node(op_type = "Mul", inputs = ['x', 'x'], outputs = ['Intermediate1'], name = 'iftrue_intermediate_1'),
      helper.make_node(op_type = "Mul", inputs = ['x', 'x'], outputs = ['Intermediate2'], name = 'iftrue_intermediate_2'),
      helper.make_node(op_type = "Sum", inputs = ['Intermediate1', 'Intermediate2'], outputs = ['Result3'], name = 'iftrue_res_3'),
    ],
    name = 'if_true_graph',
    inputs = [
    ],
    outputs = [
      helper.make_tensor_value_info('Result1', TensorProto.FLOAT, [2]),
      helper.make_tensor_value_info('Result2', TensorProto.FLOAT, [2]),
      helper.make_tensor_value_info('Result3', TensorProto.FLOAT, [2]),
    ],
    initializer = [
    ]
  )

  if_false_graph = helper.make_graph(
    nodes = [
      helper.make_node(op_type = "Mul", inputs = ['x', 'x'], outputs = ['Result1'], name = 'iffalse_res_1'),
      helper.make_node(op_type = "Mul", inputs = ['x', 'x'], outputs = ['Result2'], name = 'iffalse_res_2'),
      helper.make_node(op_type = "Sum", inputs = ['x', 'x'], outputs = ['Intermediate1'], name = 'iffalse_intermediate_1'),
      helper.make_node(op_type = "Sum", inputs = ['x', 'x'], outputs = ['Intermediate2'], name = 'iffalse_intermediate_2'),
      helper.make_node(op_type = "Mul", inputs = ['Intermediate1', 'Intermediate2'], outputs = ['Result3'], name = 'iffalse_res_3'),
    ],
    name = 'if_false_graph',
    inputs = [
    ],
    outputs = [
      helper.make_tensor_value_info('Result1', TensorProto.FLOAT, [2]),
      helper.make_tensor_value_info('Result2', TensorProto.FLOAT, [2]),
      helper.make_tensor_value_info('Result3', TensorProto.FLOAT, [2]),
    ],
    initializer = [
    ]
  )

  graph_def = helper.make_graph(
    nodes = [
      helper.make_node(op_type = "If", inputs = ['b'], outputs = ['Result1', 'Result2', 'Result3'], name = 'if_0', then_branch=if_true_graph, else_branch=if_false_graph),
    ],
    name = 'cse_subgraph',
    inputs = [
      helper.make_tensor_value_info("b", TensorProto.BOOL, [1]),
      helper.make_tensor_value_info("x", TensorProto.FLOAT, [2])
    ],
    outputs = [
      helper.make_tensor_value_info('Result1', TensorProto.FLOAT, [2]),
      helper.make_tensor_value_info('Result2', TensorProto.FLOAT, [2]),
      helper.make_tensor_value_info('Result3', TensorProto.FLOAT, [2]),
    ],
    initializer = [
    ]
  )
  _onnx_export(graph_def, 'cse_subgraph.onnx')

def cse_random():
  n = 5
  graph_def = helper.make_graph(
    nodes = [
      helper.make_node(op_type = "RandomUniform", inputs = [], outputs = ['Random1'], name = 'random_uniform_1', shape=[n]),
      helper.make_node(op_type = "RandomUniform", inputs = [], outputs = ['Random2'], name = 'random_uniform_2', shape=[n]),
      helper.make_node(op_type = "RandomUniform", inputs = [], outputs = ['Random3'], name = 'random_uniform_3', shape=[n], seed=1.0),
      helper.make_node(op_type = "RandomUniform", inputs = [], outputs = ['Random4'], name = 'random_uniform_4', shape=[n], seed=1.0),
      helper.make_node(op_type = "Sum", inputs = ['x', 'Random1', 'Random2', 'Random3', 'Random4'], outputs = ['Result'], name = 'sum_1')
    ],
    name = 'cse_random',
    inputs = [
      helper.make_tensor_value_info("x", TensorProto.FLOAT, [n])
    ],
    outputs = [
      helper.make_tensor_value_info('Result', TensorProto.FLOAT, [n])
    ],
    initializer = [
    ]
  )
  _onnx_export(graph_def, 'cse_random.onnx')

def cse_merge_constants():
  n = 3
  graph_def = helper.make_graph(
    nodes = [
      helper.make_node(op_type = "Add", inputs = ['c', 'c'], outputs = ['Add1'], name = 'add_1'),
      helper.make_node(op_type = "Add", inputs = ['c', 'c'], outputs = ['Add2'], name = 'add_2'),
      helper.make_node(op_type = "Add", inputs = ['Add1', 'x'], outputs = ['Add3'], name = 'add_3'),
      helper.make_node(op_type = "Add", inputs = ['Add2', 'x'], outputs = ['Add4'], name = 'add_4'),
      helper.make_node(op_type = "Add", inputs = ['Add3', 'Add4'], outputs = ['Result'], name = 'add_5'),
    ],
    name = 'cse_merge_constants',
    inputs = [
      helper.make_tensor_value_info("x", TensorProto.FLOAT, [n])
    ],
    outputs = [
      helper.make_tensor_value_info('Result', TensorProto.FLOAT, [n])
    ],
    initializer = [
      helper.make_tensor('c', TensorProto.FLOAT, [n], list(range(n)))
    ]
  )
  _onnx_export(graph_def, 'cse_merge_constants.onnx')

def cse_only_one_graph_output():
  graph_def = helper.make_graph(
    nodes = [
      helper.make_node(op_type = "Split", inputs = ['x'], outputs = ['Split1Output1', 'Split1Output2'], name = 'split_1'),
      helper.make_node(op_type = "Split", inputs = ['x'], outputs = ['Split2Output1', 'Split2Output2'], name = 'split_2'),
      helper.make_node(op_type = "ReduceSum", inputs = ['Split1Output1'], outputs = ['ReduceSum1'], name = 'reducesum_1'),
      helper.make_node(op_type = "ReduceSum", inputs = ['Split2Output1'], outputs = ['ReduceSum2'], name = 'reducesum_2'),
      helper.make_node(op_type = "Add", inputs = ['ReduceSum1', 'ReduceSum2'], outputs = ['Add'], name = 'add_1'),
    ],
    name = 'cse_only_one_graph_output',
    inputs = [
      helper.make_tensor_value_info("x", TensorProto.FLOAT, [4, 4])
    ],
    outputs = [
      helper.make_tensor_value_info('Split1Output2', TensorProto.FLOAT, [2, 4]),
      helper.make_tensor_value_info('Split2Output2', TensorProto.FLOAT, [2, 4]),
      helper.make_tensor_value_info('Add', TensorProto.FLOAT, [1, 1]),
    ],
    initializer = [
    ]
  )
  _onnx_export(graph_def, 'cse_only_one_graph_output.onnx')


def generate_all():
  cse1()
  cse_initializer_simple()
  cse_initializer_commutative_ops()
  cse_graph_output()
  cse_optional_args()
  cse_subgraph()
  cse_random()
  cse_merge_constants()
  cse_only_one_graph_output()

if __name__ == '__main__':
  generate_all()


