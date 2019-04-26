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

#include "core/providers/openvino/openvino_node.h"

namespace openvino_ep {

void OpenVINONode::CreateNormLayer(
    std::shared_ptr<InferenceEngine::Builder::Network>& builder,
    std::map<const onnxruntime::Node*, std::shared_ptr<OpenVINONode>>& onnx_openvino_map,
    std::map<std::string, std::shared_ptr<OpenVINONode>>& openvino_io_map) {

  auto norm_layer =
      std::make_shared<InferenceEngine::Builder::NormLayer>(
          onnx_node_->Name());

  //
  // *** Set inputs ***
  //
  auto formal_params = onnx_node_->Op()->inputs();

  for (size_t i = 0; i < formal_params.size(); i++) {
    auto formal_name = formal_params[i].GetName();

    if (formal_name == "X") {

      // Set Input info
      std::shared_ptr<OpenVINONode> in_ov_node = nullptr;

      if (node_connects_to_graph_inputs_) {
        auto input_name = input_defs_[i]->Name();
        in_ov_node = openvino_io_map[input_name];
      } else {
        in_ov_node = onnx_openvino_map[&(input_edges_[0].GetNode())];
      }
      InferenceEngine::idx_t in_port = 0;
      input_connections_.push_back( { in_ov_node, in_port });

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
    if (formal_name == "Y") {

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

  // set alpha
  norm_layer->setAcrossMaps(true);
  auto alpha = attributes["alpha"].f();
  norm_layer->setAlpha(alpha);

  // set beta
  auto beta = attributes["beta"].f();
  norm_layer->setBeta(beta);

  // set bias
//   auto bias = attributes["bias"].f();
  // needs feedback
  //norm_layer->

  // set size
  auto size = attributes["size"].i();
  norm_layer->setSize(size);


  layerID_ = builder->addLayer(*norm_layer);
}
} // namespce openvino_ep
