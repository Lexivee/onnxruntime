// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/logging/logging.h"
#include "core/common/logging/sinks/cerr_sink.h"
#include "core/framework/allocator.h"
#include "core/framework/session_options.h"
#include "core/session/environment.h"
#include "core/session/inference_session.h"

namespace onnxruntime {
namespace python {

using namespace onnxruntime;
using namespace onnxruntime::logging;

struct CustomOpLibrary {
  CustomOpLibrary(const char* library_path, OrtSessionOptions& ort_so);

  ~CustomOpLibrary();

  ORT_DISALLOW_COPY_ASSIGNMENT_AND_MOVE(CustomOpLibrary);

 private:
  void UnloadLibrary();

  std::string library_path_;
  void* library_handle_ = nullptr;
};

// Thin wrapper over internal C++ SessionOptions to accommodate custom op library management for the Python user
struct PySessionOptions : public SessionOptions {
  // Hold CustomOpLibrary resources so as to tie it to the life cycle of the InferenceSession needing it.
  std::vector<std::shared_ptr<CustomOpLibrary>> custom_op_libraries_;

  // Hold raw `OrtCustomOpDomain` pointers - it is upto the shared library to release the OrtCustomOpDomains
  // that was created when the library is unloaded
  std::vector<OrtCustomOpDomain*> custom_op_domains_;
};

// Thin wrapper over internal C++ InferenceSession to accommodate custom op library management for the Python user
struct PyInferenceSession {
  // Default ctor is present only to be invoked by the PyTrainingSession class
  explicit PyInferenceSession() {}

  explicit PyInferenceSession(Environment& env, const PySessionOptions& so, const std::string& arg, bool is_arg_file_name) {
    if (is_arg_file_name) {
      // Given arg is the file path. Invoke the corresponding ctor().
      sess_ = onnxruntime::make_unique<InferenceSession>(so, env, arg);
    } else {
      // Given arg is the model content as bytes. Invoke the corresponding ctor().
      std::istringstream buffer(arg);
      sess_ = onnxruntime::make_unique<InferenceSession>(so, env, buffer);
    }
  }

  void AddCustomOpLibraries(const std::vector<std::shared_ptr<CustomOpLibrary>>& custom_op_libraries) {
    if (custom_op_libraries.size() > 0) {
      custom_op_libraries_.reserve(custom_op_libraries_.size() + custom_op_libraries.size());
      for (size_t i = 0; i < custom_op_libraries.size(); ++i) {
        custom_op_libraries_.push_back(custom_op_libraries[i]);
      }
    }
  }

  virtual InferenceSession* GetSessionHandle() const { return sess_.get(); }

  virtual ~PyInferenceSession() {}

 private:
  // Hold CustomOpLibrary resources so as to tie it to the life cycle of the InferenceSession needing it.
  // NOTE: Declare this above `sess_` so that this is destructed AFTER the InferenceSession instance -
  // this is so that the custom ops held by the InferenceSession gets destroyed prior to the library getting unloaded
  // (if ref count of the shared_ptr reaches 0)
  std::vector<std::shared_ptr<CustomOpLibrary>> custom_op_libraries_;

  std::unique_ptr<InferenceSession> sess_;
};

inline const PySessionOptions& GetDefaultCPUSessionOptions() {
  static PySessionOptions so;
  return so;
}

inline AllocatorPtr& GetAllocator() {
  static AllocatorPtr alloc = std::make_shared<TAllocator>();
  return alloc;
}

class SessionObjectInitializer {
 public:
  typedef const PySessionOptions& Arg1;
  // typedef logging::LoggingManager* Arg2;
  static const std::string default_logger_id;
  operator Arg1() {
    return GetDefaultCPUSessionOptions();
  }

  // operator Arg2() {
  //   static LoggingManager default_logging_manager{std::unique_ptr<ISink>{new CErrSink{}},
  //                                                 Severity::kWARNING, false, LoggingManager::InstanceType::Default,
  //                                                 &default_logger_id};
  //   return &default_logging_manager;
  // }

  static SessionObjectInitializer Get() {
    return SessionObjectInitializer();
  }
};

Environment& GetEnv();

void InitializeSession(InferenceSession* sess, const std::vector<std::string>& provider_types);

}  // namespace python
}  // namespace onnxruntime
