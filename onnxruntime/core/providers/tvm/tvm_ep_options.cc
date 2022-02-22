// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <unordered_set>
#include <regex>

#include "core/common/common.h"
#include "core/common/cpuid_info.h"
#include "core/framework/provider_options_utils.h"

// TODO(vvchernov): transfer to ONNX runtime logger
#include "tvm/runtime/logging.h"

#include "tvm_ep_options.h"


namespace onnxruntime {
namespace tvm {
namespace provider_option_names {
constexpr const char* kExecutor = "executor";
constexpr const char* kTarget = "target";
constexpr const char* kTargetHost = "target_host";
constexpr const char* kOptLevel = "opt_level";
constexpr const char* kFreezeWeights = "freeze_weights";
constexpr const char* kToNHWC = "to_nhwc";
constexpr const char* kTuningFilePath = "tuning_file_path";
constexpr const char* kTuningType = "tuning_type";
constexpr const char* kInputNames = "input_names";
constexpr const char* kInputShapes = "input_shapes";

static const std::unordered_set<std::string> valid_keys {
  std::string{kExecutor},
  std::string{kTarget},
  std::string{kTargetHost},
  std::string{kOptLevel},
  std::string{kFreezeWeights},
  std::string{kToNHWC},
  std::string{kTuningFilePath},
  std::string{kTuningType},
  std::string{kInputNames},
  std::string{kInputShapes}
};

}  // namespace provider_option_names
}  // namespace tvm

size_t split(const std::string &src, std::vector<std::string> &dst, char ch) {
  dst.clear();

  size_t pos = src.find( ch );
  size_t initialPos = 0;
  while( pos != std::string::npos ) {
      dst.push_back( src.substr( initialPos, pos - initialPos ) );
      initialPos = pos + 1;

      pos = src.find( ch, initialPos );
  }
  dst.push_back( src.substr( initialPos, std::min( pos, src.size() ) - initialPos + 1 ) );

  return dst.size();
}

TvmEPOptions TvmEPOptions::FromOptionsString(const char* opt_str) {
  std::string settings{opt_str};
  ProviderOptions options;
  if (!settings.empty()) {
    const std::string& str = settings;

    // tokenize settings
    std::regex reg("\\s*,\\s*");
    std::sregex_token_iterator iter(str.begin(), str.end(), reg, -1);
    std::sregex_token_iterator iter_end;
    std::vector<std::string> pairs(iter, iter_end);

    ORT_ENFORCE(pairs.size() > 0);

    for(const auto& pair : pairs) {
      auto pos_colon = pair.find(':');
      ORT_ENFORCE(pos_colon != std::string::npos, "Invalid key value pair.");
      std::string key = pair.substr(0, pos_colon);
      std::string value = pair.substr(pos_colon + 1);

      // trim leading and trailing spaces from key/value
      key = whitespace_trimming(key);
      value = whitespace_trimming(value);

      // Check keys of obtained options
      if (tvm::provider_option_names::valid_keys.count(key) == 0) {
        ORT_NOT_IMPLEMENTED("TvmOptions: unknown option (", key, ")");
      }

      options[key] = value;
    }
  }

  return FromProviderOptions(options);
}

std::string TvmEPOptions::whitespace_trimming(const std::string& str) {
  const std::string WHITESPACE = " \n\r\t\f\v";
  size_t start = str.find_first_not_of(WHITESPACE);
  if (start == std::string::npos) {
    return "";
  } else {
    size_t end = str.find_last_not_of(WHITESPACE);
    ORT_ENFORCE(end != std::string::npos);
    return str.substr(start, end + 1);
  }
}

TvmEPOptions TvmEPOptions::FromProviderOptions(const ProviderOptions& pr_options) {
  TvmEPOptions options{};

  std::string input_names, input_shapes;
  ORT_THROW_IF_ERROR(
      ProviderOptionsParser{}
          .AddAssignmentToReference(tvm::provider_option_names::kExecutor, options.executor)
          .AddAssignmentToReference(tvm::provider_option_names::kTarget, options.target)
          .AddAssignmentToReference(tvm::provider_option_names::kTargetHost, options.target_host)
          .AddAssignmentToReference(tvm::provider_option_names::kOptLevel, options.opt_level)
          .AddAssignmentToReference(tvm::provider_option_names::kFreezeWeights, options.freeze_weights)
          .AddAssignmentToReference(tvm::provider_option_names::kToNHWC, options.to_nhwc)
          .AddAssignmentToReference(tvm::provider_option_names::kTuningFilePath, options.tuning_file_path)
          .AddAssignmentToReference(tvm::provider_option_names::kTuningType, options.tuning_type)
          .AddAssignmentToReference(tvm::provider_option_names::kInputNames, input_names)
          .AddAssignmentToReference(tvm::provider_option_names::kInputShapes, input_shapes)
          .Parse(pr_options));

  options.optionsPostprocess(input_names, input_shapes);

  return options;
}

void TvmEPOptions::optionsPostprocess(const std::string& names, const std::string& shapes) {
  setInputShapes(names, shapes);
  targetPostprocess();
  targetHostPostprocess();
  optLevelPostprocess();

  printOptions(names, shapes);
}

bool TvmEPOptions::checkGPUTarget() const {
  bool check = (
    target.find("cuda") != std::string::npos ||
    target.find("opencl") != std::string::npos ||
    target.find("metal") != std::string::npos ||
    target.find("vulkan") != std::string::npos
  );
  return check;
}

void TvmEPOptions::setInputShapes(const std::string& names, const std::string& shapes) {
  if (names.empty() && shapes.empty())
    return;
  ORT_ENFORCE(!names.empty() && !shapes.empty(), "Both provider options \"input_names\" and \"input_shapes\" should be empty or full");

  std::vector<std::string> name_set;
  std::string trimmed_names = TvmEPOptions::whitespace_trimming(names);
  size_t inp_tensors_num = split(trimmed_names, name_set, ' ');
  ORT_ENFORCE(inp_tensors_num, "There is no any input tensor names!");

  std::string trimmed_shapes = TvmEPOptions::whitespace_trimming(shapes);
  size_t end_pos = trimmed_shapes.find_last_of(']');
  ORT_ENFORCE(end_pos != std::string::npos, "Invalid string for input shapes. Symbol ] is not found");
  ORT_ENFORCE(end_pos == (trimmed_shapes.size() - 1),
              "Invalid string for input shapes. Symbol ] should be last after whitespace trimming");

  std::vector<std::string> shape_set;
  split(trimmed_shapes, shape_set, ']');
  shape_set.pop_back();
  ORT_ENFORCE( shape_set.size() == inp_tensors_num,
              "Number of shapes is not the same as number of input tensor names");

  for (size_t i = 0; i < inp_tensors_num; ++i) {
    size_t pos = shape_set[i].find('[');
    ORT_ENFORCE(pos != std::string::npos, "There is no symbol [ as pair for ]");
    std::string numbers = shape_set[i].substr(pos + 1);
    std::vector<std::string> number_set;
    ORT_ENFORCE(split(numbers, number_set, ' '), "There is no any number between [ and ] symbols");

    std::vector<int64_t> dims;
    for(const auto& number : number_set) {
      dims.push_back(std::stoi(number));
    }

    input_shapes[name_set[i]] = dims;
  }
}

void TvmEPOptions::targetPostprocess() {
  if(target == tvm::cpu_target_str ||
     target == tvm::llvm_target_str) {
    ProcessCPUTarget();
  } else if(target == tvm::gpu_target_str) {
    ProcessGPUTarget();
  } else if(target.empty()) {
    ORT_NOT_IMPLEMENTED("target option is empty!");
  } else {
    // TODO(vvchernov): extend mechanism of auto-definition of target
    // target is gotten from option set up by client
  }
}

void TvmEPOptions::ProcessCPUTarget() {
  const auto& cpu_id_info = CPUIDInfo::GetCPUIDInfo();
  // auto detect from CPU ID
  if (cpu_id_info.HasAVX512Skylake()) {
    target = tvm::cpu_targets::LLVM_TARGET_SKYLAKE_AVX512;
  } else if (cpu_id_info.HasAVX512f()) {
    target = tvm::cpu_targets::LLVM_TARGET_AVX512;
  } else if (cpu_id_info.HasAVX2()) {
    target = tvm::cpu_targets::LLVM_TARGET_AVX2;
  } else if (cpu_id_info.HasAVX()) {
    target = tvm::cpu_targets::LLVM_TARGET_AVX;
  } else  {
    // TODO(vvchernov): extend mechanism of auto-definition of cpu target
    target = tvm::llvm_target_str;
  }
}

void TvmEPOptions::ProcessGPUTarget() {
  ORT_NOT_IMPLEMENTED("GPU target auto-defenition is not implemented now!");
}

void TvmEPOptions::targetHostPostprocess() {
  if((target_host == tvm::cpu_target_str ||
      target_host == tvm::llvm_target_str) &&
      target_host != target) {
    target_host = target;
  } else if (target_host.empty()) {
    target_host = target;
  } else {
    // TODO(vvchernov): extend mechanism of auto-definition of target host
    // target host is gotten from option set up by client
  }
}

void TvmEPOptions::optLevelPostprocess() {
  if(opt_level < 1) {
    opt_level = tvm::default_opt_level;
  }
}

void TvmEPOptions::printOptions(const std::string& names, const std::string& shapes) const {
  // TODO(vvchernov): check by Alexey
  // LOGS(*GetLogger(), INFO) << "TVM EP options:\n" <<
  LOG(INFO) << "TVM EP options:\n" <<
  "executor type: " << executor << "\n" <<
  "target: " << target << "\n" <<
  "target_host: " << target_host << "\n" <<
  "opt level: " << opt_level << "\n" <<
  "freeze weights: " << freeze_weights << "\n" <<
  "tuning file path: " << tuning_file_path << "\n" <<
  "tuning type: " << tuning_type << "\n" <<
  "convert layout to NHWC: " << to_nhwc << "\n" <<
  "input tensor names: " << names << "\n" <<
  "input tensor shapes: " << shapes;
}

}  // namespace onnxruntime
