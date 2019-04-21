import os
import sys
import shutil

import onnx
import onnxruntime
import json

from google.protobuf.json_format import MessageToJson

# Current models only have one input and one output
def get_io_name(model_file_name):
  sess = onnxruntime.InferenceSession(model_file_name)
  return sess.get_inputs()[0].name, sess.get_outputs()[0].name


def tensor2dict(full_path):
  t = onnx.TensorProto()
  with open(full_path, 'rb') as f:
    t.ParseFromString(f.read())

  jsonStr = MessageToJson(t, use_integers_for_enums=True)
  data = json.loads(jsonStr)

  return data


def gen_input(pb_full_path, input_name, output_name, json_file_path):
  data = tensor2dict(pb_full_path)

  inputs = {}
  inputs[input_name] = data
  output_filters = [ output_name ]

  req = {}
  req["inputs"] = inputs
  req["outputFilter"] = output_filters

  with open(json_file_path, 'w') as outfile:
    json.dump(req, outfile)


def gen_output(pb_full_path, output_name, json_file_path):
  data = tensor2dict(pb_full_path)

  output = {}
  output[output_name] = data

  resp = {}
  resp["outputs"] = output

  with open(json_file_path, 'w') as outfile:
    json.dump(resp, outfile)


def gen_req_resp(model_zoo, test_data):
  opsets = [name for name in os.listdir(model_zoo) if os.path.isdir(os.path.join(model_zoo, name))]
  for opset in opsets:
    os.makedirs(os.path.join(test_data, opset), exist_ok=True)

    current_model_folder = os.path.join(model_zoo, opset)
    current_data_folder = os.path.join(test_data, opset)

    models = [name for name in os.listdir(current_model_folder) if os.path.isdir(os.path.join(current_model_folder, name))]
    for model in models:
      os.makedirs(os.path.join(current_data_folder, model), exist_ok=True)

      src_folder = os.path.join(current_model_folder, model)
      dst_folder = os.path.join(current_data_folder, model)

      shutil.copy2(os.path.join(src_folder, 'model.onnx'), dst_folder)
      iname, oname = get_io_name(os.path.join(src_folder, 'model.onnx'))
      model_test_data = [name for name in os.listdir(src_folder) if os.path.isdir(os.path.join(src_folder, name))]
      for test in model_test_data:
        src = os.path.join(src_folder, test)
        dst = os.path.join(dst_folder, test)
        os.makedirs(dst, exist_ok=True)
        gen_input(os.path.join(src, 'input_0.pb'), iname, oname, os.path.join(dst, 'request.json'))
        gen_output(os.path.join(src, 'output_0.pb'), oname, os.path.join(dst, 'response.json'))


if __name__ == '__main__':
  model_zoo = os.path.realpath(sys.argv[1])
  test_data = os.path.realpath(sys.argv[2])

  gen_req_resp(model_zoo, test_data)
