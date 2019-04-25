#include <stddef.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>

#include <inference_engine.hpp>
#include <ie_builders.hpp>

#include "core/graph/graph.h"

#include "core/providers/openvino/openvino_graph.h"

namespace openvino_ep {
void OpenVINONode::CreateConcatLayer(
    std::shared_ptr<InferenceEngine::Builder::Network>& builder,
    std::map<const onnxruntime::Node*, std::shared_ptr<OpenVINONode>>& onnx_openvino_map,
    std::map<std::string, std::shared_ptr<OpenVINONode>>& openvino_io_map) {

  auto concat_layer =
      std::make_shared<InferenceEngine::Builder::ConcatLayer>(
          "Concat");

  //
  // *** Set inputs ***
  //
  auto formal_params = onnx_node_->Op()->inputs();
  std::cout << "Formal params size is " << formal_params.size() << std::endl;


  for (size_t i = 0; i < formal_params.size(); i++) {
    auto formal_name = formal_params[i].GetName();

    if (formal_name == "inputs") {

        for(int j=0; j < input_defs_.size(); j++){

            std::shared_ptr<OpenVINONode> in_ov_node = nullptr;

            if (node_connects_to_graph_inputs_) {
                auto input_name = input_defs_[j]->Name();
                in_ov_node = openvino_io_map[input_name];
            } else {
                in_ov_node = onnx_openvino_map[&(input_edges_[j].GetNode())];
            }
           InferenceEngine::idx_t in_port = j;
           input_connections_.push_back( { in_ov_node, in_port });
        }

      // Set Input info

    } else {
      std::stringstream msg;
      msg << "Node: " << onnx_node_->Name() << "| Param: "
          << formal_name.c_str() << "not found";
      throw msg.str();

    }
  }

  //
  // *** Set Outputs ***
  //
  formal_params = onnx_node_->Op()->outputs();
  for (size_t i = 0; i < formal_params.size(); i++) {
    auto formal_name = formal_params[i].GetName();
    if (formal_name == "concat_result") {

      std::shared_ptr<OpenVINONode> out_ov_node = nullptr;
      if (node_connects_to_graph_outputs_) {
        auto output_name = output_defs_[i]->Name();
        out_ov_node = openvino_io_map[output_name];
      } else {
        out_ov_node = onnx_openvino_map[&(output_edges_[0].GetNode())];
      }
      InferenceEngine::idx_t out_port = 0;
      output_connections_.push_back( { out_ov_node, out_port });

    } else {
      std::stringstream msg;
      msg << "Node: " << onnx_node_->Name() << "| Param: " << formal_name
          << "not found";
      throw msg.str();
    }
  }

  //
  // *** Set attributes ***
  //
  auto attributes = onnx_node_->GetAttributes();


  // set axis
  auto axis = attributes["axis"].i();
  concat_layer->setAxis(axis);


  layerID_ = builder->addLayer(*concat_layer);
  std::cout << "Concat done " << std::endl;
}
} // namespce openvino_ep
