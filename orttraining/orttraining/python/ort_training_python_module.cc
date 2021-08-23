// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "python/onnxruntime_pybind_exceptions.h"
#include "python/onnxruntime_pybind_mlvalue.h"
#include "python/onnxruntime_pybind_state_common.h"

#include "core/common/logging/logging.h"
#include "core/common/logging/severity.h"

#include "core/platform/env.h"
#include "core/session/provider_bridge_ort.h"
#include <unordered_map>
#include <cstdlib>

namespace onnxruntime {
namespace python {
namespace py = pybind11;

using namespace onnxruntime::logging;

std::unique_ptr<IExecutionProvider> CreateExecutionProviderInstance(
  InferenceSession* sess,
  const std::string& type,
  const ProviderOptionsMap& provider_options_map);


#ifdef USE_CUDA
const CUDAExecutionProviderInfo GetCudaExecutionProviderInfo(ProviderInfo_CUDA* cuda_provider_info,
                                                             const ProviderOptionsMap& provider_options_map);
#endif

#ifdef USE_ROCM
const ROCMExecutionProviderInfo GetROCMExecutionProviderInfo(const ProviderOptionsMap& provider_options_map);
#endif

void addGlobalMethods(py::module& m, Environment& env);
void addObjectMethods(py::module& m, Environment& env, ExecutionProviderRegistrationFn ep_registration_fn);
void addObjectMethodsForTraining(py::module& m, ExecutionProviderRegistrationFn ep_registration_fn);
void addObjectMethodsForEager(py::module& m);
void InitArray();

using ExecutionProviderMap = std::unordered_map<std::string, std::shared_ptr<IExecutionProvider> >;

bool GetDyanmicExecutionProviderHash(
    const std::string& ep_shared_lib_path,
    const ProviderOptions& provider_options,
    size_t& hash,
    const std::string& entry_symbol_name = "ProviderHashFunc") {
  void* handle;
  auto error = Env::Default().LoadDynamicLibrary(ep_shared_lib_path, false, &handle);
  if (!error.IsOK()) {
    throw std::runtime_error(error.ErrorMessage());
  }

  try{
    size_t (*PGetProviderHash)(const void*) = nullptr;
    OrtPybindThrowIfError(Env::Default().GetSymbolFromLibrary(handle, entry_symbol_name, (void**)&PGetProviderHash));

    if (PGetProviderHash){
      hash = PGetProviderHash(&provider_options);
      return true;
    }
    return false;
  }
  catch(...){
    // there is no ProvideHashFunc provide in the shared lib, which means it doesn't support cache
    return false;
  }
}

bool GetProviderInstanceHash(const std::string& type,
                            const ProviderOptionsMap& provider_options_map,
                            size_t& hash){
  // for built-in execution provider, currently only cpu / cuda / rocm support hash.
  if (type == kCpuExecutionProvider){
    // for CPU, only 1 instance
    hash = 0;
    return true;
  }
  else if (type == kCudaExecutionProvider){
#ifdef USE_CUDA
  if(auto* cuda_provider_info = TryGetProviderInfo_CUDA()){
    const CUDAExecutionProviderInfo info = GetCudaExecutionProviderInfo(cuda_provider_info,
                                                                        provider_options_map);
    hash = info.hash();
    return true;
  }
#endif
  }
  else if (type == kRocmExecutionProvider){
#ifdef USE_ROCM
    const ROCMExecutionProviderInfo info = GetROCMExecutionProviderInfo(provider_options_map);
    hash = info.hash()
    return true;
#endif
  }
  else{
    const auto it = provider_options_map.find(type);
    if (it != provider_options_map.end()) {
      auto shared_lib_path_it = it->second.find(kExecutionProviderSharedLibraryPath);
      if (shared_lib_path_it != it->second.end()) {
        // this is an EP with dynamic loading
        // construct the provider option
        ProviderOptions provider_options;
        std::string entry_symbol = kDefaultExecutionProviderEntry;
        for (auto option : it->second) {
          if (option.first == kExecutionProviderSharedLibraryEntry){
            entry_symbol = option.second;
          }
          else if (option.first != kExecutionProviderSharedLibraryPath){
            provider_options.insert(option);
          }
        }
        return GetDyanmicExecutionProviderHash(shared_lib_path_it->second, provider_options, hash);
      }
    }
  }
  return false;
}

class ORTTrainingPythonEnv{
public:
  ORTTrainingPythonEnv(){
    OrtPybindThrowIfError(Environment::Create(std::make_unique<LoggingManager>(
                                                  std::unique_ptr<ISink>{new CLogSink{}},
                                                  Severity::kWARNING, false, LoggingManager::InstanceType::Default,
                                                  &SessionObjectInitializer::default_logger_id),
                                              ort_env_));
  }

  Environment& GetORTEnv(){
    return *ort_env_;
  }

  std::shared_ptr<IExecutionProvider> GetExecutionProviderInstance(const std::string& provider_type,
                                                                   size_t hash){
    auto it = execution_provider_instances_.find(GetExecutionProviderMapKey(provider_type, hash));
    return it == execution_provider_instances_.end() ? nullptr : it->second;
  }

  void AddExecutionProvider(const std::string& provider_type,
                            size_t hash,
                            std::unique_ptr<IExecutionProvider> execution_provider){
    execution_provider_instances_.insert({GetExecutionProviderMapKey(provider_type, hash),
                                          std::move(execution_provider)});
  }

private:
  std::string GetExecutionProviderMapKey(const std::string& provider_type,
                                         size_t hash){
    std::string key(provider_type);
    key.append(std::to_string(hash));
    return key;
  }

  std::unique_ptr<Environment> ort_env_;
  ExecutionProviderMap execution_provider_instances_;
};

static std::unique_ptr<ORTTrainingPythonEnv> ort_training_env;

void InitializeTrainingEnv() {
  auto initialize = [&]() {
    static bool initialized = false;
    if (initialized) {
      return;
    }
    // Initialization of the module
    InitArray();
    Env::Default().GetTelemetryProvider().SetLanguageProjection(OrtLanguageProjection::ORT_PROJECTION_PYTHON);
    ort_training_env = std::make_unique<ORTTrainingPythonEnv>();
    initialized = true;
  };
  initialize();
}

ORTTrainingPythonEnv& GetTrainingEnv() {
  if (!ort_training_env) {
    InitializeTrainingEnv();
  }
  return *ort_training_env;
}

Environment& GetTrainingORTEnv() {
  if (!ort_training_env) {
    InitializeTrainingEnv();
  }
  return ort_training_env->GetORTEnv();
}

void ORTTrainingRegisterExecutionProviders(InferenceSession* sess, const std::vector<std::string>& provider_types,
                                       const ProviderOptionsMap& provider_options_map) {
  // search in environment
  ORTTrainingPythonEnv& training_env = GetTrainingEnv();
  for (auto provider_type : provider_types){
    size_t hash;
    if (GetProviderInstanceHash(provider_type, provider_options_map, hash)){
      auto cached_provider_instance = training_env.GetExecutionProviderInstance(provider_type, hash);
      if (!cached_provider_instance){
        auto ep = CreateExecutionProviderInstance(sess, provider_type, provider_options_map);
        if (ep){
          training_env.AddExecutionProvider(provider_type, hash, std::move(ep));
          cached_provider_instance = training_env.GetExecutionProviderInstance(provider_type, hash);
        }
      }
      if (cached_provider_instance)
        OrtPybindThrowIfError(sess->RegisterExecutionProvider(cached_provider_instance));
    }
    else{
      // the EP doesn't support cache, register the instance to session
      auto ep = CreateExecutionProviderInstance(sess, provider_type, provider_options_map);
      if (ep)
        OrtPybindThrowIfError(sess->RegisterExecutionProvider(std::move(ep)));
    }
  }
}

PYBIND11_MODULE(onnxruntime_pybind11_state, m) {
  m.doc() = "pybind11 stateful interface to ORTTraining";
  RegisterExceptions(m);
  
  Environment& env = GetTrainingORTEnv();
  addGlobalMethods(m, env);
  addObjectMethods(m, env, ORTTrainingRegisterExecutionProviders);
  addOrtValueMethods(m);
  addSparseTensorMethods(m);
  addIoBindingMethods(m);

#if !defined(__APPLE__) && \
    (!defined(ORT_MINIMAL_BUILD) || defined(ORT_EXTENDED_MINIMAL_BUILD) || defined(ORT_MINIMAL_BUILD_CUSTOM_OPS))
  Ort::SessionOptions tmp_options;
  if (!InitProvidersSharedLibrary()) {
    const logging::Logger& default_logger = logging::LoggingManager::DefaultLogger();
    LOGS(default_logger, WARNING) << "Init provider bridge failed.";
  }
#endif
  
  addObjectMethodsForTraining(m, ORTTrainingRegisterExecutionProviders);
#ifdef ENABLE_EAGER_MODE
  addObjectMethodsForEager(m);
#endif

  // clean the ort training environment when python interpreter exit
  // otherwise the global var will be de-constrcut after user main.
  // the order of ort training environment deconstruction and cudart
  // deconstruction is not stable, which will lead to crash.
  auto atexit = py::module_::import("atexit");
  atexit.attr("register")(py::cpp_function([]() {
    ort_training_env = nullptr;
  }));
}

}
}