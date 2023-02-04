// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.


#include "core/framework/provider_options.h"
#include "tensorrt_execution_provider_custom_ops.h"
#include "tensorrt_execution_provider.h"
#include <NvInferRuntime.h>
#include <NvInferPlugin.h>
#include <unordered_set>

namespace onnxruntime {
extern TensorrtLogger& GetTensorrtLogger();

// This is the helper function to get the plugin fields that is currently being used
void IterateTensorRTPluginFields(const nvinfer1::PluginFieldCollection* plugin_field_collection) {
  if (plugin_field_collection == nullptr) {
    return;
  }
  LOGS_DEFAULT(VERBOSE) << "plugin fields:";
  for (int i = 0; i < plugin_field_collection->nbFields; ++i) {
    auto plugin_field = plugin_field_collection->fields[i];
    std::string plugin_field_name(plugin_field.name);
    LOGS_DEFAULT(VERBOSE) << "\t" << plugin_field_name ;
  }  
}

/*
 * Create custom op domain list for TRT plugins.
 *
 * There are several TRT plugins registered as onnx schema op through contrib op with ONNX domain, for example, 
 * EfficientNMS_TRT, MultilevelCropAndResize_TRT, PyramidROIAlign_TRT and DisentangledAttention_TRT.
 * In order not to break the old models using those TRT plugins which were registered with ONNX domain and maintain backward compatible,
 * we need to keep those old/legacy TRT plugins with ONNX domain. Moving forward, any newly added TRT plugin nodes should be registered with
 * "trt.plugins" domain.
 *
 * Note: Current TRT plugin doesn't have APIs to get number of inputs/outputs of the plugin.
 * So, TensorRTCustomOp uses variadic inputs/outputs to pass ONNX graph validation.
 */
common::Status CreateTensorRTCustomOpDomainList(std::vector<OrtProviderCustomOpDomain*>& custom_op_domain_list) {
  std::unique_ptr<OrtProviderCustomOpDomain> custom_op_domain = std::make_unique<OrtProviderCustomOpDomain>();
  custom_op_domain->domain_ = "trt.plugins";
  // create legacy custom op domain for those registered with contrib op with ONNX domain
  std::unique_ptr<OrtProviderCustomOpDomain> legacy_custom_op_domain = std::make_unique<OrtProviderCustomOpDomain>();
  legacy_custom_op_domain->domain_ = kOnnxDomain;

  LOGS_DEFAULT(VERBOSE) << "[TensorRT EP] Get all registered TRT plugins from registry.";
  TensorrtLogger trt_logger = GetTensorrtLogger();
  initLibNvInferPlugins(&trt_logger, "");

  int num_plugin_creator = 0;
  auto plugin_creators = getPluginRegistry()->getPluginCreatorList(&num_plugin_creator);
  std::unordered_set<std::string> registered_plugin_names;
  std::unordered_set<std::string> legacy_trt_contrib_ops = {"EfficientNMS_TRT", "MultilevelCropAndResize_TRT", "PyramidROIAlign_TRT", "DisentangledAttention_TRT", "CustomVisionTransformerINT8Plugin", "CustomVisionTransformerPlugin"};

  for (int i = 0; i < num_plugin_creator; i++) {
    auto plugin_creator = plugin_creators[i];
    std::string plugin_name(plugin_creator->getPluginName());
    LOGS_DEFAULT(VERBOSE) << "[TensorRT EP] " << plugin_name << ", version : " << plugin_creator->getPluginVersion();

    //auto plugin_field_collection = plugin_creator->getFieldNames();
    //IterateTensorRTPluginFields(plugin_field_collection);

    // plugin has different versions and we only register once
    if (registered_plugin_names.find(plugin_name) != registered_plugin_names.end()) {
      continue;
    }

    std::unique_ptr<TensorRTCustomOp> trt_custom_op = std::make_unique<TensorRTCustomOp>(onnxruntime::kTensorrtExecutionProvider, nullptr);
    trt_custom_op->SetName(plugin_creator->getPluginName());
    custom_op_domain->custom_ops_.push_back(trt_custom_op.release());

    if (legacy_trt_contrib_ops.find(plugin_name) != legacy_trt_contrib_ops.end()) {
      std::unique_ptr<TensorRTCustomOp> legacy_trt_custom_op = std::make_unique<TensorRTCustomOp>(onnxruntime::kTensorrtExecutionProvider, nullptr);
      legacy_trt_custom_op->SetName(plugin_creator->getPluginName());
      legacy_custom_op_domain->custom_ops_.push_back(legacy_trt_custom_op.release());
    }

    registered_plugin_names.insert(plugin_name);
  }
  custom_op_domain_list.push_back(legacy_custom_op_domain.release());
  custom_op_domain_list.push_back(custom_op_domain.release());

  return common::Status::OK();
}

void ReleaseTensorRTCustomOpDomain(OrtProviderCustomOpDomain* domain) {
  if (domain != nullptr) {
    for (auto ptr : domain->custom_ops_) {
      if (ptr != nullptr) {
        delete ptr;
      }
    }
    delete domain;
  }
}

void ReleaseTensorRTCustomOpDomainList(std::vector<OrtProviderCustomOpDomain*>& custom_op_domain_list) {
  for (auto ptr : custom_op_domain_list) {
    ReleaseTensorRTCustomOpDomain(ptr);
  }
}

}  // namespace onnxruntime
