// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <functional>

#include "core/session/onnxruntime_lite_custom_op.h"
#include "core/session/onnxruntime_c_api.h"
#include "core/framework/ortdevice.h"
#include "core/framework/stream_handles.h"
#include <climits>

namespace Ort {
namespace Custom {
struct ExternalKernelDef {
  std::unique_ptr<OrtLiteCustomOp> custom_op_;
  std::string domain_;
  int op_since_version_start_ = 1;
  int op_since_version_end_ = INT_MAX;
  ExternalKernelDef(OrtLiteCustomOp* op, std::string domain, int op_version_start, int op_version_end) {
    custom_op_ = std::unique_ptr<OrtLiteCustomOp>(op);
    domain_ = domain;
    op_since_version_start_ = op_version_start;
    op_since_version_end_ = op_version_end;
  }
};

template <typename... Args>
ExternalKernelDef* CreateExternalKernelDef(const char* op_name, const char* execution_provider, void (*custom_compute_fn)(Args...),
                                          const char* domain, int op_since_version_start, int op_since_version_end = INT_MAX) {
  OrtLiteCustomOp* op = CreateLiteCustomOp(op_name, execution_provider, custom_compute_fn);
  return std::make_unique<ExternalKernelDef>(op, domain, op_since_version_start, op_since_version_end).release();
}

}
}

namespace onnxruntime{
    class CustomExecutionProvider{
        public:
        CustomExecutionProvider() { default_device_ = OrtDevice(); };
        virtual ~CustomExecutionProvider() = default;

        std::vector<OrtAllocator*>& GetAllocators() { return allocators_; }
        //std::vector<std::unique_ptr<Ort::Custom::OrtLiteCustomOp>>& GetKernelDefinitions() { return kernel_definitions_; }
        size_t GetKernelDefinitionCount() { return kernel_definitions_.size(); }
        Ort::Custom::ExternalKernelDef* GetKernelDefinition(size_t index) {
            if (index >= kernel_definitions_.size()) return nullptr;
            return kernel_definitions_[index].get();
        }
        std::string& GetType() { return type_; }
        OrtDevice& GetDevice() { return default_device_; }

        virtual bool CanCopy(const OrtDevice&, const OrtDevice&) { return false; }
        //virtual void MemoryCpy(OrtValue&, const OrtValue&) {}
        virtual void MemoryCpy(Ort::UnownedValue&, Ort::ConstValue const&) {}
        virtual void RegisterStreamHandlers(IStreamCommandHandleRegistry&, std::map<OrtDevice, OrtAllocator*>&) const {}

        protected:
        std::vector<OrtAllocator*> allocators_;
        //std::vector<std::unique_ptr<Ort::Custom::OrtLiteCustomOp>> kernel_definitions_;
        std::vector<std::unique_ptr<Ort::Custom::ExternalKernelDef>> kernel_definitions_;
        std::string type_;
        OrtDevice default_device_;
    };
}
