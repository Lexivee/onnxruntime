// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include <Python.h>
#include "core/platform/env.h"

namespace onnxruntime {
namespace language_interop_ops {
namespace torch {
#ifndef NDEBUG

using AddressInfos = std::unordered_map<void*, std::vector<std::string>>;

class RefCountTracker {
 public:
  enum ObjCategory {
    ForwardArgs,
    ReturnValues,
    AutoGradContext,
  };

  static RefCountTracker& GetInstance() {
    static RefCountTracker tracker;
    return tracker;
  }
  void TrackPyObject(RefCountTracker::ObjCategory category, PyObject* py_obj, std::string log_tag);
  void DumpDetails(std::string phase_name);
  void Reset();

 private:
  RefCountTracker();
  const char* ObjCategoryToString(int enumVal) {
    static const char* enum_strings[] = {"ForwardArgs", "ReturnValues", "AutoGradContext"};
    return enum_strings[enumVal];
  }

  AddressInfos forward_arg_addresses_;
  AddressInfos return_value_addresses_;
  AddressInfos auto_grad_addresses_;
  std::unordered_map<RefCountTracker::ObjCategory, AddressInfos> addr_info_map_;
};
#endif
}  // namespace torch
}  // namespace language_interop_ops
}  // namespace onnxruntime
