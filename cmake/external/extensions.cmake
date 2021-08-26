# add compile definition to enable custom operators in onnxruntime-extensions
add_compile_definitions(ENABLE_EXTENSION_CUSTOM_OPS)

# set options for onnxruntime-extensions
set(OCOS_ENABLE_CTEST OFF CACHE INTERNAL "")
set(OCOS_ENABLE_STATIC_LIB ON CACHE INTERNAL "")
set(OCOS_ENABLE_SPM_TOKENIZER OFF CACHE INTERNAL "")

# disable exceptions
if (onnxruntime_DISABLE_EXCEPTIONS)
set(OCOS_ENABLE_CPP_EXCEPTIONS OFF CACHE INTERNAL "")
endif()

# customize operators used
if (Donnxruntime_REDUCED_OPS_BUILD)
set(OCOS_ENABLE_SELECTED_OPLIST ON CACHE INTERNAL "")
endif()

if (onnxruntime_EXTENSIONS_PATH)
  add_subdirectory(${onnxruntime_EXTENSIONS_PATH} ${onnxruntime_EXTENSIONS_PATH} EXCLUDE_FROM_ALL)
else()
  add_subdirectory(external/onnxruntime-extensions EXCLUDE_FROM_ALL)
endif()

# target library or executable are defined in CMakeLists.txt of onnxruntime-extensions
target_include_directories(ocos_operators PRIVATE ${RE2_INCLUDE_DIR} external/json/include)
target_include_directories(ortcustomops PUBLIC ${onnxruntime_EXTENSIONS_PATH}/shared)