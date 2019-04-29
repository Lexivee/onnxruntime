# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

file(GLOB_RECURSE onnxruntime_codegen_tvm_srcs CONFIGURE_DEPENDS
    "${ONNXRUNTIME_ROOT}/core/codegen/tvm/*.h"
    "${ONNXRUNTIME_ROOT}/core/codegen/tvm/*.cc"
)

#onnxruntime_codegen_tvm depends on onnxruntime framework
add_library(onnxruntime_codegen_tvm ${onnxruntime_codegen_tvm_srcs})
set_target_properties(onnxruntime_codegen_tvm PROPERTIES FOLDER "ONNXRuntime")
target_include_directories(onnxruntime_codegen_tvm PRIVATE ${ONNXRUNTIME_ROOT} ${TVM_INCLUDES})
onnxruntime_add_include_to_target(onnxruntime_codegen_tvm onnxruntime_common onnxruntime_framework gsl onnx onnx_proto protobuf::libprotobuf)
target_compile_options(onnxruntime_codegen_tvm PRIVATE ${DISABLED_WARNINGS_FOR_TVM})
# need onnx to build to create headers that this project includes
add_dependencies(onnxruntime_codegen_tvm ${onnxruntime_EXTERNAL_DEPENDENCIES})
