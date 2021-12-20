# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

file(GLOB onnxruntime_eager_srcs CONFIGURE_DEPENDS
    "${ONNXRUNTIME_INCLUDE_DIR}/core/eager/*.h"
    "${ONNXRUNTIME_ROOT}/core/eager/*.cc"
    )

source_group(TREE ${REPO_ROOT} FILES ${onnxruntime_eager_srcs})

add_library(onnxruntime_eager ${onnxruntime_eager_srcs})
if(MSVC AND onnxruntime_ENABLE_EAGER_MODE)
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_aten.cpp" PROPERTIES COMPILE_FLAGS "/wd4100 /wd4458")
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_customops.g.cpp" PROPERTIES COMPILE_FLAGS "/wd4100")
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_backends.cpp" PROPERTIES COMPILE_FLAGS "/wd4100")
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_hooks.cpp" PROPERTIES COMPILE_FLAGS "/wd4100")
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_eager.cpp" PROPERTIES COMPILE_FLAGS "/wd4100")
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_log.cpp" PROPERTIES COMPILE_FLAGS "/wd4100 /wd4324")
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_guard.cpp" PROPERTIES COMPILE_FLAGS "/wd4100")
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_tensor.cpp" PROPERTIES COMPILE_FLAGS "/wd4100 /wd4458 /wd4127")
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_ops.cpp" PROPERTIES COMPILE_FLAGS "/wd4100")
  set_source_files_properties("${ORTTRAINING_ROOT}/orttraining/eager/ort_util.cpp" PROPERTIES COMPILE_FLAGS "/wd4100")
endif()
install(DIRECTORY ${PROJECT_SOURCE_DIR}/../include/onnxruntime/core/eager  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/onnxruntime/core)
onnxruntime_add_include_to_target(onnxruntime_eager onnxruntime_common onnxruntime_framework onnxruntime_optimizer onnxruntime_graph onnx onnx_proto ${PROTOBUF_LIB} flatbuffers)
if(onnxruntime_ENABLE_INSTRUMENT)
  target_compile_definitions(onnxruntime_eager PUBLIC ONNXRUNTIME_ENABLE_INSTRUMENT)
endif()
target_include_directories(onnxruntime_eager PRIVATE ${ONNXRUNTIME_ROOT} ${eigen_INCLUDE_DIRS} ${ONNXRUNTIME_INCLUDE_DIR})
add_dependencies(onnxruntime_eager ${onnxruntime_EXTERNAL_DEPENDENCIES})
set_target_properties(onnxruntime_eager PROPERTIES FOLDER "ONNXRuntime")
if (onnxruntime_ENABLE_TRAINING)
  target_include_directories(onnxruntime_session PRIVATE ${ORTTRAINING_ROOT})
endif()


