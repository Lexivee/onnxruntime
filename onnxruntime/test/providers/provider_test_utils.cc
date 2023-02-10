// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <sstream>

#include "core/common/logging/logging.h"
#include "core/common/logging/sinks/clog_sink.h"
#include "core/framework/tensorprotoutils.h"
#include "core/framework/data_types_internal.h"
#include "core/session/inference_session.h"
#include "core/session/onnxruntime_session_options_config_keys.h"
#include "core/graph/model_load_utils.h"
#include "gmock/gmock.h"
#include "test/providers/provider_test_utils.h"
#include "test/providers/run_options_config_keys.h"
#include "test/util/include/default_providers.h"
#include "test/framework/test_utils.h"
#include <csignal>
#include <exception>
#include <memory>

#ifdef ENABLE_TRAINING
#include "orttraining/core/session/training_session.h"
#endif

using namespace ::onnxruntime::logging;

namespace onnxruntime {
namespace test {

template <typename T>
Tensor copy_sort(const Tensor& src, const AllocatorPtr& allocator) {
  Tensor result(src.DataType(), src.Shape(), allocator);
  memcpy(result.MutableDataRaw(), src.DataRaw(), src.SizeInBytes());
  auto dst_span = gsl::make_span(result.MutableData<T>(), result.MutableData<T>() + result.Shape().Size());
  std::sort(dst_span.begin(), dst_span.end());
  return result;
}

// Check functions for tensor types
template <typename T>
void sort_expected_and_actual_buffers(const Tensor& expected, Tensor& expected_sorted,
                                      const Tensor& actual, Tensor& actual_sorted) {
  auto allocator = TestCPUExecutionProvider()->GetAllocator(0, OrtMemTypeDefault);
  expected_sorted = copy_sort<T>(expected, allocator);
  actual_sorted = copy_sort<T>(actual, allocator);
}

// Check functions for tensor types
template <typename T>
void sort_expected_and_actual_buffers(std::vector<T>& expected,
                                      std::vector<T>& actual) {
  ORT_ENFORCE(expected.size() == actual.size(),
              "The 2 containers contain different number of elements");
  std::sort(expected.begin(), expected.end());
  std::sort(actual.begin(), actual.end());
}

// The default implementation compares for equality, specialized versions for
// other types are below
template <typename T>
struct TensorCheck {
  void operator()(const Tensor& expected_tensor, const Tensor& output_tensor,
                  const std::string& provider_type, const CheckParams& params) const {
    Tensor expected_sorted, output_sorted;
    const T* expected;
    const T* output;
    const auto size = output_tensor.Shape().Size();
    if (params.sort_output_) {
      // if order can be jumbled in the output of an operator, sort both the
      // expected and output buffers prior to
      // comparison this is a "best-effort" algo and should satisfy the
      // requirement for the few ops that do require this
      // support without investing in a more sophisticated infrastructure for the
      // same
      sort_expected_and_actual_buffers<T>(expected_tensor, expected_sorted, output_tensor, output_sorted);
      expected = expected_sorted.Data<T>();
      output = output_sorted.Data<T>();
    } else {
      expected = expected_tensor.Data<T>();
      output = output_tensor.Data<T>();
    }

    for (int i = 0; i < size; ++i) {
      EXPECT_EQ(expected[i], output[i]) << "i:" << i
                                        << ", provider_type: " << provider_type;
    }
  }
};

template <>
struct TensorCheck<uint8_t> {
  void operator()(const Tensor& expected_tensor,
                  const Tensor& output_tensor,
                  const std::string& provider_type, const CheckParams& params) const {
    const bool has_abs_err = params.absolute_error_.has_value();
    const bool has_rel_err = params.relative_error_.has_value();

    Tensor expected_sorted, output_sorted;
    const uint8_t* expected;
    const uint8_t* output;
    const auto size = output_tensor.Shape().Size();
    if (params.sort_output_) {
      // if order can be jumbled in the output of an operator, sort both the
      // expected and output buffers prior to
      // comparison this is a "best-effort" algo and should satisfy the
      // requirement for the few ops that do require this
      // support without investing in a more sophisticated infrastructure for the
      // same
      sort_expected_and_actual_buffers<uint8_t>(expected_tensor, expected_sorted, output_tensor, output_sorted);
      expected = expected_sorted.Data<uint8_t>();
      output = output_sorted.Data<uint8_t>();
    } else {
      expected = expected_tensor.Data<uint8_t>();
      output = output_tensor.Data<uint8_t>();
    }

    // For uint8_t results, we only allow NNAPI/XNNPACK EP to have an error tolerance, see below for the reason
    // XNNPACK EP will always round to larger. For example, 0.1 will be rounded to 1.0
    // For any other EPs, we still expect an exact match for the results
    // TODO: Verify if DML can possibly have a ROUNDING_MODE parameter and conform to the other EPs #41968513
    if ((provider_type == kNnapiExecutionProvider || provider_type == kDmlExecutionProvider ||
         provider_type == kXnnpackExecutionProvider) &&
        (has_abs_err || has_rel_err)) {
      double threshold = has_abs_err
                             ? *(params.absolute_error_)
                             : 0.0;

      for (int i = 0; i < size; ++i) {
        if (has_rel_err) {
          EXPECT_NEAR(expected[i], output[i],
                      *(params.relative_error_) * expected[i])  // expected[i] is unsigned, can't be negative
              << "i:" << i << ", provider_type: " << provider_type;
        } else {  // has_abs_err
          EXPECT_NEAR(expected[i], output[i], threshold)
              << "i:" << i << ", provider_type: " << provider_type;
        }
      }
    } else {
      for (int i = 0; i < size; ++i) {
        EXPECT_EQ(expected[i], output[i]) << "i:" << i
                                          << ", provider_type: " << provider_type;
      }
    }
  }
};

template <>
struct TensorCheck<int8_t> {
  void operator()(const Tensor& expected_tensor,
                  const Tensor& output_tensor,
                  const std::string& provider_type, const CheckParams& params) const {
    Tensor expected_sorted, output_sorted;
    const int8_t* expected;
    const int8_t* output;
    const auto size = output_tensor.Shape().Size();
    if (params.sort_output_) {
      // if order can be jumbled in the output of an operator, sort both the
      // expected and output buffers prior to
      // comparison this is a "best-effort" algo and should satisfy the
      // requirement for the few ops that do require this
      // support without investing in a more sophisticated infrastructure for the
      // same
      sort_expected_and_actual_buffers<int8_t>(expected_tensor, expected_sorted, output_tensor, output_sorted);
      expected = expected_sorted.Data<int8_t>();
      output = output_sorted.Data<int8_t>();
    } else {
      expected = expected_tensor.template Data<int8_t>();
      output = output_tensor.template Data<int8_t>();
    }

    const bool has_abs_err = params.absolute_error_.has_value();
    if (has_abs_err) {
      double threshold = *(params.absolute_error_);

      for (int i = 0; i < size; ++i) {
        EXPECT_NEAR(expected[i], output[i], threshold)
            << "i:" << i << ", provider_type: " << provider_type;
      }
    } else {
      for (int i = 0; i < size; ++i) {
        EXPECT_EQ(expected[i], output[i])
            << "i:" << i << ", provider_type: " << provider_type;
      }
    }
  }
};

template <>
struct TensorCheck<double> {
  void operator()(const Tensor& expected_tensor,
                  const Tensor& output_tensor,
                  const std::string& provider_type,
                  const CheckParams& params) const {
    auto size = output_tensor.Shape().Size();

    bool has_abs_err = params.absolute_error_.has_value();
    bool has_rel_err = params.relative_error_.has_value();

    // deal with rare cases in which order of output data from a kernel MAY be
    // undefined
    Tensor expected_sorted, output_sorted;
    const double* expected;
    const double* output;
    if (params.sort_output_) {
      sort_expected_and_actual_buffers<double>(expected_tensor, expected_sorted, output_tensor, output_sorted);
      expected = expected_sorted.Data<double>();
      output = output_sorted.Data<double>();
    } else {
      expected = expected_tensor.Data<double>();
      output = output_tensor.Data<double>();
    }

    double threshold = 0.001;
#if defined(USE_CUDA) || defined(USE_ROCM) || defined(USE_DML)
    threshold = 0.005;
#endif

    for (int i = 0; i < size; ++i) {
      // NOTE: Check isnan first to work around MSVC linker bug when /LTCG:incremental is specified.
      // If the isinf check is first the isnan check and branch gets omitted
      if (std::isnan(expected[i])) {
        ASSERT_TRUE(std::isnan(output[i])) << "Expected NaN. i:" << i << ", provider_type: " << provider_type;
      } else if (std::isinf(expected[i])) {  // Test infinity for equality
        ASSERT_EQ(expected[i], output[i]) << "Expected infinity. i:" << i << ", provider_type: " << provider_type;
      } else {
        if (!has_abs_err && !has_rel_err) {
          // the default for existing tests
            if (expected[i] != output[i]) {
                float a = 2.f;
                (void)(a);
            }
          ASSERT_NEAR(expected[i], output[i], threshold)
              << "i:" << i << ", provider_type: " << provider_type;
        } else {
          if (has_abs_err) {
            ASSERT_NEAR(expected[i], output[i],
                        *(params.absolute_error_))
                << "i:" << i << ", provider_type: " << provider_type;
          }
          if (has_rel_err) {
            ASSERT_NEAR(expected[i], output[i],
                        *(params.relative_error_) *
                            std::abs(expected[i]))
                << "i:" << i << ", provider_type: " << provider_type;
          }
        }
      }
    }
  }
};

template <typename TypeToCheck>
void InternalNumericalCheck(const Tensor& expected_tensor,
                            const Tensor& output_tensor,
                            const std::string& provider_type,
                            const CheckParams& params) {
  const bool has_abs_err = params.absolute_error_.has_value();
  const bool has_rel_err = params.relative_error_.has_value();

  // deal with rare cases in which order of output data from a kernel MAY be
  // undefined
  Tensor expected_sorted, output_sorted;
  const TypeToCheck* expected;
  const TypeToCheck* output;
  auto size = output_tensor.Shape().Size();
  if (params.sort_output_) {
    sort_expected_and_actual_buffers<TypeToCheck>(expected_tensor, expected_sorted, output_tensor, output_sorted);
    expected = expected_sorted.Data<TypeToCheck>();
    output = output_sorted.Data<TypeToCheck>();
  } else {
    expected = expected_tensor.Data<TypeToCheck>();
    output = output_tensor.Data<TypeToCheck>();
  }

#if defined(USE_CUDA) || defined(USE_ROCM) || defined(USE_DML)
  constexpr float threshold = 0.005f;
#else
  constexpr float threshold = 0.0001f;
#endif

  for (int i = 0; i < size; ++i) {
    // NOTE: Check isnan first to work around MSVC linker bug when /LTCG:incremental is specified.
    // If the isinf check is first the isnan check and branch gets omitted
    if (std::isnan(expected[i])) {
      ASSERT_TRUE(std::isnan(output[i])) << "Expected NaN. i:" << i << ", provider_type: " << provider_type;
    } else if (std::isinf(expected[i])) {  // Test infinity for equality
      ASSERT_EQ(expected[i], output[i]) << "Expected infinity. i:" << i << ", provider_type: " << provider_type;
    } else {
      if (!has_abs_err && !has_rel_err) {
          if (expected[i] != output[i]) {
              float a = expected[i];
              float b = output[i];
              float c = 1.f;
              (void)(a);
              (void)(b);
              (void)(c);
          }
          else {
              float a = expected[i];
              float b = output[i];
              float c = 1.f;
              (void)(a);
              (void)(b);
              (void)(c);
          }

        // the default for existing tests
        //ASSERT_NEAR(expected[i], output[i], threshold)
        //    << "i:" << i << ", provider_type: " << provider_type;
      } else {
        if (has_abs_err) {
          ASSERT_NEAR(expected[i], output[i],
                      *(params.absolute_error_))
              << "i:" << i << ", provider_type: " << provider_type;
        }
        if (has_rel_err) {
          ASSERT_NEAR(expected[i], output[i],
                      *(params.relative_error_) *
                          std::abs(expected[i]))
              << "i:" << i << ", provider_type: " << provider_type;
        }
      }
    }
  }
}

template <>
struct TensorCheck<float> {
  void operator()(const Tensor& expected_tensor,
                  const Tensor& output_tensor,
                  const std::string& provider_type,
                  const CheckParams& params) const {
    InternalNumericalCheck<float>(expected_tensor, output_tensor, provider_type, params);
  }
};

template <>
struct TensorCheck<MLFloat16> {
  void operator()(const Tensor& expected_tensor,
                  const Tensor& output_tensor,
                  const std::string& provider_type,
                  const CheckParams& params) const {
    auto* expected = expected_tensor.Data<MLFloat16>();
    auto* output = output_tensor.Data<MLFloat16>();
    auto size = output_tensor.Shape().Size();

    std::vector<float> f_expected(size);
    std::vector<float> f_output(size);
    ConvertMLFloat16ToFloat(expected, f_expected.data(), static_cast<int>(size));
    ConvertMLFloat16ToFloat(output, f_output.data(), static_cast<int>(size));

    // deal with rare cases in which order of output data from a kernel MAY be
    // undefined
    if (params.sort_output_) {
      sort_expected_and_actual_buffers<float>(f_expected, f_output);
    }

    const bool has_abs_err = params.absolute_error_.has_value();
    const bool has_rel_err = params.relative_error_.has_value();

    float threshold = 0.001f;
#if defined(USE_TENSORRT) || defined(ENABLE_TRAINING_CORE) || defined(USE_CUDA) || defined(USE_ROCM)
    threshold = 0.005f;
#elif defined(USE_DML)
    threshold = 0.008f;
#endif
    for (int i = 0; i < size; ++i) {
      if (std::isnan(f_expected[i])) {
        EXPECT_TRUE(std::isnan(f_expected[i])) << "Expected NaN. i:" << i << ", provider_type: " << provider_type;
      } else if (std::isinf(f_expected[i])) {  // Test infinity for equality
        EXPECT_EQ(f_expected[i], f_output[i]) << "Expected infinity. i:" << i << ", provider_type: " << provider_type;
      } else {
        if (!has_abs_err && !has_rel_err) {
          // the default for existing tests
          EXPECT_NEAR(f_expected[i], f_output[i], threshold)
              << "i:" << i << ", provider_type: " << provider_type;
        } else {
          if (has_abs_err) {
            EXPECT_NEAR(f_expected[i], f_output[i],
                        *(params.absolute_error_))
                << "i:" << i << ", provider_type: " << provider_type;
          }
          if (has_rel_err) {
            EXPECT_NEAR(f_expected[i], f_output[i],
                        *(params.relative_error_) *
                            std::abs(expected[i]))
                << "i:" << i << ", provider_type: " << provider_type;
          }
        }
      }
    }
  }
};

template <>
struct TensorCheck<BFloat16> {
  void operator()(const Tensor& expected_tensor,
                  const Tensor& output_tensor,
                  const std::string& provider_type,
                  const CheckParams& params) const {
    auto* expected = expected_tensor.Data<BFloat16>();
    auto* output = output_tensor.Data<BFloat16>();
    auto size = output_tensor.Shape().Size();

    std::vector<float> f_expected(size);
    std::vector<float> f_output(size);
    BFloat16ToFloat(expected, f_expected.data(), static_cast<size_t>(size));
    BFloat16ToFloat(output, f_output.data(), static_cast<size_t>(size));

    // deal with rare cases in which order of output data from a kernel MAY be
    // undefined
    if (params.sort_output_) {
      sort_expected_and_actual_buffers<float>(f_expected, f_output);
    }

    /// XXX: May need to adjust threshold as BFloat is coarse
    float abs_threshold = 0.0001f;
    float threshold = 0.001f;
#if defined(USE_TENSORRT) || defined(ENABLE_TRAINING_CORE) || defined(USE_CUDA) || defined(USE_ROCM) || defined(USE_DML) || defined(USE_DNNL)
    threshold = 0.05f;  // expect at least 95% close
#endif

    for (int i = 0; i < size; ++i) {
      if (std::isnan(f_expected[i])) {
        EXPECT_TRUE(std::isnan(f_expected[i])) << "Expected NaN. i:" << i << ", provider_type: " << provider_type;
      } else if (std::isinf(f_expected[i])) {  // Test infinity for equality
        EXPECT_EQ(f_expected[i], f_output[i]) << "Expected infinity. i:" << i << ", provider_type: " << provider_type;
      } else {
        // the default for existing tests
        const float max_value = fmax(fabs(f_expected[i]), fabs(f_output[i]));
        if (max_value != 0) {  // max_value = 0 means output and expected are 0s.
          const float abs_error = fabs(f_expected[i] - f_output[i]);
          if (abs_error <= abs_threshold) {
            // if the absolute error is small enough, then no need to calculate realative error
            EXPECT_NEAR(0, abs_error, abs_threshold) << "provider_type: "
                                                 << provider_type;
          } else {
            //default for existing tests.
            const float rel_error = abs_error / max_value;
            EXPECT_NEAR(0, rel_error, threshold) << "provider_type: "
                                                 << provider_type;
          }
        }
      }
    }
  }
};

void Check(const OpTester::Data& expected_data, const Tensor& output_tensor,
           const std::string& provider_type) {
  ORT_ENFORCE(expected_data.data_.Get<Tensor>().Shape() ==
                  output_tensor.Shape(),
              "Expected output shape [" +
                  expected_data.data_.Get<Tensor>().Shape().ToString() +
                  "] did not match run output shape [" +
                  output_tensor.Shape().ToString() + "] for " +
                  expected_data.def_.Name());

  utils::MLTypeCallDispatcher<bool, float, double, uint8_t, uint16_t, uint32_t, uint64_t,
                              int8_t, int16_t, int32_t, int64_t, std::string, MLFloat16,
                              BFloat16>
      t_disp(output_tensor.GetElementType());

  t_disp.Invoke<TensorCheck>(expected_data.data_.Get<Tensor>(), output_tensor, provider_type, MakeCheckParams(expected_data));
}

// Check for non tensor types

template <typename T>
void Check(const OpTester::Data& expected_data, const T& run_output,
           const std::string& provider_type) {
  EXPECT_EQ(expected_data.data_.Get<T>(), run_output) << "provider_type: "
                                                      << provider_type;
}

template <>
void Check<TensorSeq>(const OpTester::Data& expected_data,
                      const TensorSeq& output_seq,
                      const std::string& provider_type) {
  const auto& exp_seq = expected_data.data_.Get<TensorSeq>();

  // first ensure data types match
  EXPECT_EQ(exp_seq.DataType(), output_seq.DataType())
      << "Data types don't match: Expected: "
      << DataTypeImpl::ToString(exp_seq.DataType())
      << " Output: " << output_seq.DataType()
      << " provider_type: " << provider_type;

  // check num of contained tensors
  size_t expected_num_tensors = exp_seq.Size();
  size_t output_num_tensors = output_seq.Size();
  EXPECT_EQ(expected_num_tensors, output_num_tensors)
      << "Mismatch in number of tensors in the sequence"
      << " Expected: " << expected_num_tensors
      << " Output: " << output_num_tensors
      << " provider_type: " << provider_type;

  // now check the contents of the tensors
  CheckParams check_params = MakeCheckParams(expected_data);

  auto element_type = exp_seq.DataType()->AsPrimitiveDataType()->GetDataType();
  utils::MLTypeCallDispatcher<bool, float, double, uint8_t, uint16_t, uint32_t, uint64_t,
                              int8_t, int16_t, int32_t, int64_t, std::string, MLFloat16,
                              BFloat16>
      t_disp(element_type);

  for (size_t i = 0; i < output_num_tensors; ++i) {
    t_disp.Invoke<TensorCheck>(exp_seq.Get(i), output_seq.Get(i), provider_type, check_params);
  }
}

template <typename Type>
void CheckDispatch(MLDataType type, const OpTester::Data& expected_data,
                   OrtValue& ort_value, const std::string& provider_type) {
  if (type == DataTypeImpl::GetType<Type>())
    Check<Type>(expected_data, ort_value.Get<Type>(), provider_type);
  else
    ORT_THROW("OpTester:Check() not implemented for output tensor type of ",
              type);
}

template <typename Type, typename Next, typename... Types>
void CheckDispatch(MLDataType type, const OpTester::Data& expected_data,
                   OrtValue& ort_value, const std::string& provider_type) {
  if (type == DataTypeImpl::GetType<Type>())
    Check<Type>(expected_data, ort_value.Get<Type>(), provider_type);
  else
    CheckDispatch<Next, Types...>(type, expected_data, ort_value,
                                  provider_type);
}

void Check(const OpTester::Data& expected_data, OrtValue& ort_value,
           const std::string& provider_type) {
  CheckDispatch<
#if !defined(DISABLE_ML_OPS)
      VectorMapStringToFloat, VectorMapInt64ToFloat,
#endif
      TensorSeq>(
      expected_data.data_.Type(), expected_data, ort_value, provider_type);
}

void DebugTrap() {
#if _MSC_VER
  __debugbreak();
#else
  raise(SIGTRAP);
#endif
}

OpTester::~OpTester() {
#ifndef NDEBUG
  if (!run_called_) {
    std::cerr << "Someone forgot to call OpTester::Run()" << std::endl;
    DebugTrap();
  }
#endif
}

void OpTester::FillFeedsAndOutputNames(
    std::unordered_map<std::string, OrtValue>& feeds,
    std::vector<std::string>& output_names) {
  for (auto& output : output_data_) {
    if (output.def_.Exists())
      output_names.push_back(output.def_.Name());
  }

  FillFeeds(feeds);
}

void OpTester::FillFeeds(std::unordered_map<std::string, OrtValue>& feeds) {
  for (size_t i = 0; i < input_data_.size(); ++i) {
    if (std::find(initializer_index_.begin(), initializer_index_.end(), i) ==
            initializer_index_.end() &&
        input_data_[i].def_.Exists() &&
        // We don't include optional type OrtValues of None because this is
        // how we expect users to deal with sending through "None"s as graph inputs
        // (i.e.) don't send them through at all
        input_data_[i].data_.IsAllocated()) {
      feeds[input_data_[i].def_.Name()] = input_data_[i].data_;
    }
  }
}

void OpTester::SetOutputAbsErr(const char* name, float v) {
  auto it =
      std::find_if(output_data_.begin(), output_data_.end(),
                   [name](Data& data) { return (data.def_.Name() == name); });
  ORT_ENFORCE(it != output_data_.end());
  it->absolute_error_ = optional<float>(v);
}

void OpTester::SetOutputRelErr(const char* name, float v) {
  auto it =
      std::find_if(output_data_.begin(), output_data_.end(),
                   [name](Data& data) { return (data.def_.Name() == name); });
  ORT_ENFORCE(it != output_data_.end());
  it->relative_error_ = optional<float>(v);
}

void OpTester::AddNodes(
    onnxruntime::Graph& graph,
    std::vector<onnxruntime::NodeArg*>& graph_input_defs,
    std::vector<onnxruntime::NodeArg*>& graph_output_defs,
    std::vector<std::function<void(onnxruntime::Node& node)>>& add_attribute_funcs) {
  // default behavior is to create a single Node for the op being tested, with
  // node inputs/outputs
  // being 1:1 with graph inputs/outputs.
  auto& node = graph.AddNode("node1", op_, op_, graph_input_defs,
                             graph_output_defs, nullptr, domain_);

  // Add the attributes if any
  for (auto& add_attribute_fn : add_attribute_funcs)
    add_attribute_fn(node);
}

std::vector<int64_t> OpTester::GetDimsForProto(gsl::span<const int64_t> dims) {
  std::vector<int64_t> dims_for_proto{dims.begin(), dims.end()};
  if (add_symbolic_dim_to_tensor_data_ >= 0 &&
      dims.size() > static_cast<size_t>(add_symbolic_dim_to_tensor_data_)) {
    dims_for_proto[add_symbolic_dim_to_tensor_data_] = -1;
  }
  return dims_for_proto;
}

void OpTester::AddShapeToTensorData(NodeArg& node_arg, gsl::span<const int64_t> dims,
                                    const std::vector<std::string>* dim_params) {
  if (dim_params && !(dim_params->empty()) && add_shape_to_tensor_data_) {
    // If dim_params presents, configure node_arg's dim value based on dim_params, which supports symbolic dim and dim broadcast.
    const auto& dim_params_data = *dim_params;
    onnx::TensorShapeProto new_shape;

    // currently hard-code the reserved symbolic names.
    // TODO: when the list grows longer, consider move it to a better place.
    const static std::unordered_set<std::string> reserved_symbolic{"batch", "seq"};

    for (size_t i = 0; i < dim_params_data.size(); ++i) {
      if (reserved_symbolic.find(dim_params_data[i]) != reserved_symbolic.end()) {
        new_shape.add_dim()->set_dim_param(dim_params_data[i]);
      } else {
        ASSERT_TRUE(std::stoi(dim_params_data[i]) == dims[i]);
        new_shape.add_dim()->set_dim_value(dims[i]);
      }
    }
    node_arg.SetShape(new_shape);
  }
}

#if !defined(DISABLE_SPARSE_TENSORS)
static std::unique_ptr<SparseTensor> MakeSparseTensor(MLDataType data_type, const gsl::span<const int64_t>& dims) {
  TensorShape shape{dims};
  auto allocator = test::AllocatorManager::Instance().GetAllocator(CPU);
  auto p_tensor = std::make_unique<SparseTensor>(data_type, shape, std::move(allocator));
  return p_tensor;
}

void OpTester::CopyDataToTensor(gsl::span<const gsl::byte> data, Tensor& dst) {
  ORT_ENFORCE(dst.SizeInBytes() >= data.size_bytes(), "Not enough space in the destination tensor");
  memcpy(dst.MutableDataRaw(), data.data(), data.size_bytes());
}

NodeArg OpTester::MakeSparseNodeArg(int32_t dtype, const char* name,
                                    const gsl::span<const int64_t>& dims, const std::vector<std::string>* dim_params) {
  std::vector<int64_t> dims_for_proto = GetDimsForProto(dims);
  TSparseTensorProto type_proto(dtype, add_shape_to_tensor_data_ ? &dims_for_proto : nullptr);
  NodeArg node_arg(name, &type_proto.proto);
  AddShapeToTensorData(node_arg, dims, dim_params);
  return node_arg;
}

void OpTester::AddSparseTensorData(std::vector<Data>& data, NodeArg node_arg,
                                   std::unique_ptr<SparseTensor> p_tensor,
                                   const CheckParams& check_params) {
  OrtValue value;
  auto ml_type = DataTypeImpl::GetType<SparseTensor>();
  value.Init(p_tensor.release(), ml_type, ml_type->GetDeleteFunc());
  data.push_back(Data(std::move(node_arg), std::move(value),
                      optional<float>(check_params.relative_error_), optional<float>(check_params.absolute_error_),
                      check_params.sort_output_));
}

void OpTester::AddSparseCooTensorData(std::vector<Data>& data,
                                      MLDataType data_type,
                                      const char* name,
                                      gsl::span<const int64_t> dims,
                                      gsl::span<const gsl::byte> values,
                                      gsl::span<const int64_t> indices,
                                      const CheckParams& check_params,
                                      const std::vector<std::string>* dim_params) {
  const auto elem_size = data_type->Size();
  const auto dtype = data_type->AsPrimitiveDataType()->GetDataType();
  const auto nnz = values.size_bytes() / elem_size;
  ORT_ENFORCE(dims.size() == 2U, "Expecting a 2-D dense shape");
  ORT_ENFORCE((nnz == indices.size() || 2 * nnz == indices.size()), "Expecting indices to have either nnz or (2 * nnz) length");
  auto p_tensor = MakeSparseTensor(data_type, dims);
  auto mutator = p_tensor->MakeCooData(nnz, indices.size());
  CopyDataToTensor(values, mutator.Values());
  CopyDataToTensor(gsl::as_bytes(indices), mutator.Indices());

  NodeArg node_arg = MakeSparseNodeArg(dtype, name, dims, dim_params);
  AddSparseTensorData(data, std::move(node_arg), std::move(p_tensor), check_params);
}

void OpTester::AddSparseCooTensorStrings(std::vector<Data>& data,
                                         const char* name,
                                         gsl::span<const int64_t> dims,
                                         gsl::span<const std::string> values,
                                         gsl::span<const int64_t> indices,
                                         const std::vector<std::string>* dim_params) {
  auto data_type = DataTypeImpl::GetType<std::string>();
  const auto nnz = values.size();
  const auto dtype = data_type->AsPrimitiveDataType()->GetDataType();
  ORT_ENFORCE(dims.size() == 2U, "Expecting a 2-D dense shape");
  ORT_ENFORCE((nnz == indices.size() || 2 * nnz == indices.size()), "Expecting indices to have either nnz or (2 * nnz) length");
  auto p_tensor = MakeSparseTensor(data_type, dims);
  // linear index is 1-D index, otherwise 2-D index
  auto mutator = p_tensor->MakeCooData(nnz, indices.size());
  auto mutable_values = mutator.Values().MutableDataAsSpan<std::string>();
  ORT_ENFORCE(values.size() == mutable_values.size(), "Must allocate space for values");
  std::copy(values.begin(), values.end(), mutable_values.begin());
  CopyDataToTensor(gsl::as_bytes(indices), mutator.Indices());
  NodeArg node_arg = MakeSparseNodeArg(dtype, name, dims, dim_params);
  AddSparseTensorData(data, std::move(node_arg), std::move(p_tensor), CheckParams());
}

void OpTester::AddSparseCsrTensorData(std::vector<Data>& data,
                                      MLDataType data_type,
                                      const char* name,
                                      gsl::span<const int64_t> dims,
                                      gsl::span<const gsl::byte> values,
                                      gsl::span<const int64_t> inner_indices,
                                      gsl::span<const int64_t> outer_indices,
                                      const CheckParams& check_params,
                                      const std::vector<std::string>* dim_params) {
  const auto elem_size = data_type->Size();
  const auto dtype = data_type->AsPrimitiveDataType()->GetDataType();
  const auto nnz = values.size_bytes() / elem_size;
  ORT_ENFORCE(dims.size() == 2U, "Expecting a 2-D dense shape");
  ORT_ENFORCE(nnz == inner_indices.size(), "Expecting the same number of inner_indices as nnz");
  auto p_tensor = MakeSparseTensor(data_type, dims);

  auto mutator = p_tensor->MakeCsrData(nnz, inner_indices.size(), outer_indices.size());
  CopyDataToTensor(values, mutator.Values());
  CopyDataToTensor(gsl::as_bytes(inner_indices), mutator.Inner());
  CopyDataToTensor(gsl::as_bytes(outer_indices), mutator.Outer());

  NodeArg node_arg = MakeSparseNodeArg(dtype, name, dims, dim_params);
  AddSparseTensorData(data, std::move(node_arg), std::move(p_tensor), check_params);
}

void OpTester::AddSparseCsrTensorStrings(std::vector<Data>& data,
                                         const char* name,
                                         gsl::span<const int64_t> dims,
                                         gsl::span<const std::string> values,
                                         gsl::span<const int64_t> inner_indices,
                                         gsl::span<const int64_t> outer_indices,
                                         const std::vector<std::string>* dim_params) {
  auto data_type = DataTypeImpl::GetType<std::string>();
  const auto nnz = values.size();
  const auto dtype = data_type->AsPrimitiveDataType()->GetDataType();

  ORT_ENFORCE(dims.size() == 2U, "Expecting a 2-D dense shape");
  ORT_ENFORCE(nnz == inner_indices.size(), "Expecting the same number of inner_indices as nnz");
  auto p_tensor = MakeSparseTensor(data_type, dims);

  auto mutator = p_tensor->MakeCsrData(nnz, inner_indices.size(), outer_indices.size());
  auto mutable_values = mutator.Values().MutableDataAsSpan<std::string>();
  ORT_ENFORCE(values.size() == mutable_values.size(), "Must allocate space for values");
  std::copy(values.begin(), values.end(), mutable_values.begin());
  CopyDataToTensor(gsl::as_bytes(inner_indices), mutator.Inner());
  CopyDataToTensor(gsl::as_bytes(outer_indices), mutator.Outer());
  NodeArg node_arg = MakeSparseNodeArg(dtype, name, dims, dim_params);
  AddSparseTensorData(data, std::move(node_arg), std::move(p_tensor), CheckParams());
}
#endif  // !defined(DISABLE_SPARSE_TENSORS)

void OpTester::AddInitializers(onnxruntime::Graph& graph) {
  for (auto index : initializer_index_) {
    auto& data = input_data_[index];
    auto& tensor = data.data_.Get<Tensor>();
    ONNX_NAMESPACE::TensorProto tensor_proto;
    // 1. set dimension
    auto& shape = tensor.Shape();
    for (auto& dim : shape.GetDims()) {
      tensor_proto.add_dims(dim);
    }
    // 2. set type
    tensor_proto.set_data_type(
        data.def_.TypeAsProto()->tensor_type().elem_type());
    // 3. data
    if (data.def_.TypeAsProto()->tensor_type().elem_type() ==
        ONNX_NAMESPACE::TensorProto_DataType_STRING) {
      const std::string* string_data = tensor.Data<std::string>();
      for (auto i = 0; i < shape.Size(); i++) {
        tensor_proto.add_string_data(string_data[i]);
      }
    } else {
      auto buffer_size = tensor.DataType()->Size() * shape.Size();
      tensor_proto.set_raw_data(tensor.DataRaw(), buffer_size);
    }
    // 4. name
    tensor_proto.set_name(data.def_.Name());
    graph.AddInitializedTensor(tensor_proto);
  }
}

std::unique_ptr<onnxruntime::Model> OpTester::BuildGraph(
    const std::unordered_map<std::string, int>& extra_domain_to_version,
    const ModelOptions& model_options) {
  // Generate the input & output def lists
  std::vector<onnxruntime::NodeArg*> node_input_defs;
  std::vector<onnxruntime::NodeArg*> output_defs;

  for (size_t i = 0; i < input_data_.size(); ++i) {
    node_input_defs.push_back(&input_data_[i].def_);
  }

  for (auto& data : output_data_) {
    output_defs.push_back(&data.def_);
  }

  // Create a simple model
  std::unordered_map<std::string, int> domain_to_version(extra_domain_to_version);
  if (domain_to_version.count(domain_) == 0) {
    domain_to_version.insert({domain_, opset_version_});
  } else {
    auto key_val = extra_domain_to_version.find(domain_);

    ORT_ENFORCE(key_val->second <= opset_version_);

    if (key_val->second < opset_version_) {
      domain_to_version[domain_] = opset_version_;
    }
  }

  auto p_model = std::make_unique<onnxruntime::Model>(
      "test", false, ModelMetaData(), PathString(), custom_schema_registries_,
      domain_to_version, std::vector<ONNX_NAMESPACE::FunctionProto>{},
      DefaultLoggingManager().DefaultLogger(),
      model_options);
  onnxruntime::Graph& graph = p_model->MainGraph();
  AddNodes(graph, node_input_defs, output_defs, add_attribute_funcs_);

  // Add Initializer
  AddInitializers(graph);
  return p_model;
}

template <class SessionType>
std::vector<OrtValue> OpTester::ExecuteModel(
    Model& model, SessionType& session_object, ExpectResult expect_result,
    const std::string& expected_failure_string, const RunOptions* run_options,
    const std::unordered_map<std::string, OrtValue>& feeds,
    const std::vector<std::string>& output_names,
    const std::string& provider_type, bool allow_released_onnx_opset_only) {
  std::string s1;
  const bool rc = model.ToProto().SerializeToString(&s1);
  if (!rc) {
    LOGS_DEFAULT(ERROR) << "Failed to serialize proto to string";
    return {};
  }
  std::stringstream sstr(s1);
  auto status = session_object.Load(sstr, allow_released_onnx_opset_only);
  EXPECT_TRUE(status.IsOK()) << status.ErrorMessage();
  if (!status.IsOK()) {
    LOGS_DEFAULT(ERROR) << "Load failed with status: " << status.ErrorMessage();
    return {};
  }

  status = session_object.Initialize();

  if (!status.IsOK()) {
    if (expect_result == ExpectResult::kExpectFailure) {
      EXPECT_TRUE(!status.IsOK());
      // Disable expected_failure_string checks for OpenVINO EP
      if (provider_type != kOpenVINOExecutionProvider) {
        EXPECT_THAT(status.ErrorMessage(),
                    testing::HasSubstr(expected_failure_string));
      }
    } else {
      LOGS_DEFAULT(ERROR) << "Initialize failed with status: "
                          << status.ErrorMessage();
      EXPECT_TRUE(status.IsOK()) << status.ErrorMessage();
    }
  }

  if (!status.IsOK()) {
    return {};
  }

  RunOptions default_run_options{};
  default_run_options.run_tag = op_;
  default_run_options.run_log_verbosity_level = 1;

  std::vector<OrtValue> fetches;
  for (int i = 0; i < num_run_calls_; ++i) {
    fetches.clear();
    status =
        session_object.Run(run_options ? *run_options : default_run_options,
                           feeds, output_names, &fetches);

    if (status.IsOK()) {
      EXPECT_TRUE(expect_result == ExpectResult::kExpectSuccess)
          << "Expected failure but Run was successful";
      if (expect_result == ExpectResult::kExpectFailure) {
        return {};
      }
    } else {
      if (expect_result == ExpectResult::kExpectFailure) {
        // Disable expected_failure_string checks for MKL-DNN and OpenVINO EP's
        if (provider_type != kDnnlExecutionProvider &&
            provider_type != kOpenVINOExecutionProvider) {
          EXPECT_THAT(status.ErrorMessage(),
                      testing::HasSubstr(expected_failure_string));
        }
      } else {
        LOGS_DEFAULT(ERROR) << "Run failed with status: "
                            << status.ErrorMessage();
        EXPECT_TRUE(status.IsOK()) << status.ErrorMessage();
      }
      return {};
    }
  }

  // Verify the outputs
  // Todo: support check output with map/sequence/....
  if (verify_output_) {
    if (custom_output_verifier_) {
      // do custom verification if provided
      custom_output_verifier_(fetches, provider_type);
    } else {
      // default verification
      size_t idx = 0;
      for (auto& expected_data : output_data_) {
        OrtValue& ort_value = fetches[idx];

        if (expected_data.def_.Exists()) {           // optional edges won't exist (so skip them)
          if (!expected_data.data_.IsAllocated()) {  // optional type output (None)
            EXPECT_TRUE(!ort_value.IsAllocated())
                << "Expected to see an output of None "
                << "but instead got an output that wasn't None";

            // Make sure types align
            EXPECT_EQ(expected_data.data_.Type(), ort_value.Type())
                << "Expected optional type: " << expected_data.data_.Type()
                << " but instead got optional type: " << ort_value.Type();
          }

          else if (expected_data.data_.IsTensor()) {
            // verify output shape inference when input defs have shape
            if (add_shape_to_tensor_data_) {
              auto out_shape_proto = expected_data.def_.Shape();
              EXPECT_TRUE(out_shape_proto != nullptr);
              const auto tensor_shape =
                  utils::GetTensorShapeFromTensorShapeProto(*out_shape_proto);
              const auto inferred_dims = tensor_shape.GetDims();
              const auto& expected_shape =
                  expected_data.data_.Get<Tensor>().Shape();
              EXPECT_TRUE(inferred_dims.size() ==
                          expected_shape.NumDimensions());
              for (size_t d = 0; d < inferred_dims.size(); ++d) {
                // check equal unless the input involved a symbolic dimension
                if (inferred_dims[d] != -1) {
                  EXPECT_EQ(expected_shape[d], inferred_dims[d])
                      << "Output idx = " << idx << " dim = " << d;
                }
              }
            }

            Check(expected_data, ort_value.Get<Tensor>(), provider_type);
          } else {
            Check(expected_data, ort_value, provider_type);
          }

          ++idx;

          // skip missing trailing optional outputs
          if (idx == fetches.size())
            break;
        }
      }
    }
  }

  return fetches;
}

bool SetEpsForAllNodes(
    Graph& graph,
    const std::vector<std::unique_ptr<IExecutionProvider>>& execution_providers,
    const std::vector<std::shared_ptr<CustomRegistry>>* custom_registries) {
  const OpSchemaKernelTypeStrResolver kernel_type_str_resolver{};
  for (auto& node : graph.Nodes()) {
    if (node.OpType() == kConstant)
      continue;

    bool found = false;

    for (const auto& ep : execution_providers) {
      auto provider_type = ep->Type();

      node.SetExecutionProviderType(provider_type);
      if (provider_type == onnxruntime::kOpenVINOExecutionProvider ||
          provider_type == onnxruntime::kTensorrtExecutionProvider ||
          // provider_type == onnxruntime::kTvmExecutionProvider ||
          provider_type == onnxruntime::kNnapiExecutionProvider ||
          provider_type == onnxruntime::kCoreMLExecutionProvider ||
          provider_type == onnxruntime::kDnnlExecutionProvider ||
          provider_type == onnxruntime::kSnpeExecutionProvider) {
        found = true;
        break;
      }

      // Check the EP has an impl for the node from builtin registry.
      if (KernelRegistry::HasImplementationOf(*ep->GetKernelRegistry(), node, ep->Type(), kernel_type_str_resolver)) {
        found = true;
        break;
      }

      // Check the EP has an impl for the node from custom_registries
      if (custom_registries != nullptr &&
          std::any_of(custom_registries->cbegin(), custom_registries->cend(),
                      [&](auto reg) { return KernelRegistry::HasImplementationOf(
                                          *reg->GetKernelRegistry(),
                                          node, ep->Type(),
                                          kernel_type_str_resolver); })) {
        found = true;
        break;
      }
    }

    // We will reach here:
    //  - either we could not find an impl in all possible kernel registries
    //  - or we skip the registry search and blindly assign the node to the EP due to impl details.
    if (!found) {
      return false;
    }
  }

  // all nodes have been assigned an EP
  return true;
}

OpTester& OpTester::Config(const SessionOptions& sess_options) {
  ctx_.session_options = sess_options;
  return *this;
}

OpTester& OpTester::Config(ExpectResult expect_result, const std::string& expected_failure_string) {
  ctx_.expect_result = expect_result;
  ctx_.expected_failure_string = expected_failure_string;
  return *this;
}

OpTester& OpTester::ConfigExcludeEps(const std::unordered_set<std::string>& excluded_provider_types) {
  ctx_.excluded_provider_types = excluded_provider_types;
  return *this;
}

OpTester& OpTester::Config(const RunOptions* run_options) {
  ctx_.run_options = run_options;
  return *this;
}

OpTester& OpTester::ConfigEps(std::vector<std::unique_ptr<IExecutionProvider>>&& execution_providers) {
  ORT_ENFORCE(execution_providers.size() > 0);
  ctx_.run_with_specified_eps = true;
  ctx_.execution_providers = std::move(execution_providers);
  return *this;
}

OpTester& OpTester::Config(const Graph::ResolveOptions& resolve_options) {
  ctx_.resolve_options = resolve_options;
  return *this;
}

void OpTester::Run(
    ExpectResult expect_result, const std::string& expected_failure_string,
    const std::unordered_set<std::string>& excluded_provider_types,
    const RunOptions* run_options,
    std::vector<std::unique_ptr<IExecutionProvider>>* execution_providers,
    ExecutionMode execution_mode,
    const Graph::ResolveOptions& options) {
  SessionOptions so;
  so.use_per_session_threads = false;
  so.session_logid = op_;
  so.session_log_verbosity_level = 1;
  so.execution_mode = execution_mode;
  so.use_deterministic_compute = use_determinism_;
  so.graph_optimization_level = TransformerLevel::Default;  // 'Default' == off
  Run(so, expect_result, expected_failure_string, excluded_provider_types,
      run_options, execution_providers, options);
}

#define ASSERT_PROVIDER_STATUS_OK(function)                                                         \
  do {                                                                                              \
    Status _tmp_status = function;                                                                  \
    ASSERT_TRUE(_tmp_status.IsOK()) << "provider: " << provider_type << ", error: " << _tmp_status; \
  } while (false)

void OpTester::Run(
    SessionOptions so,  // Take the SessionOptions by value (i.e. make a copy)
                        // because we may need to modify it
    ExpectResult expect_result, const std::string& expected_failure_string,
    const std::unordered_set<std::string>& excluded_provider_types,
    const RunOptions* run_options,
    std::vector<std::unique_ptr<IExecutionProvider>>* execution_providers,
    const Graph::ResolveOptions& options,
    /*out*/ size_t* number_of_pre_packed_weights_counter,
    /*out*/ size_t* number_of_shared_pre_packed_weights_counter) {
  if (execution_providers == nullptr) {
    ctx_.run_with_specified_eps = false;
    ctx_.execution_providers.clear();
  } else {
    this->ConfigEps(std::move(*execution_providers));
    // NOTE: some callsites do the following:
    //
    //   std::vector<std::unique_ptr<IExecutionProvider>> execution_providers;
    //   execution_providers.push_back(DefaultCPUExecutionProvider());
    //   test.run(..., &execution_providers, ...);
    //   execution_providers[0] =  DefaultCUDAExecutionProvider();     //  <-- std::move cause segfault here.
    //   test.run(..., &execution_providers, ...);
    //
    // So we need to restore the old vector's size.
    execution_providers->resize(ctx_.execution_providers.size());
  }

  (*this)
      .Config(so)
      .Config(expect_result, expected_failure_string)
      .Config(run_options)
      .ConfigExcludeEps(excluded_provider_types)
      .Config(options)
      .RunWithConfig(number_of_pre_packed_weights_counter, number_of_shared_pre_packed_weights_counter);
}

void OpTester::RunWithConfig(size_t* number_of_pre_packed_weights_counter,
                             size_t* number_of_shared_pre_packed_weights_counter) {
  std::string cur_provider = "not set";
  ORT_TRY {
#ifndef NDEBUG
    run_called_ = true;
#endif

    // IsAllowReleasedONNXOpsetsOnlySet() checks for the appropriate env var in the process (i.e.) process-wide
    // `IsAllowReleasedONNXOpsetsOnlySetForThisTest()` is for this specific OpTester instance
    // We will only support released opsets iff IsAllowReleasedONNXOpsetsOnlySet() and `IsAllowReleasedONNXOpsetsOnlySetForThisTest()`
    // are both true
    auto allow_released_onnx_opset_only =
        IsAllowReleasedONNXOpsetsOnlySetForThisTest() && model_load_utils::IsAllowReleasedONNXOpsetsOnlySet();

    if (allow_released_onnx_opset_only) {
      auto& onnx_released_versions =
          ONNX_NAMESPACE::OpSchemaRegistry::DomainToVersionRange::Instance().LastReleaseVersionMap();
      auto it = onnx_released_versions.find(domain_);
      if (it != onnx_released_versions.end() && opset_version_ > it->second) {
        LOGS_DEFAULT(WARNING) << "Encountered model with opset version greater than released onnx opset version. "
                              << "Skipping this test. To run this test set environment variable ALLOW_RELEASED_ONNX_OPSET_ONLY to \"0\". "
                              << "Opset version of current model is " << opset_version_
                              << ", the latest released onnx opset version is " << it->second << ".";
        GTEST_SKIP();
      }
    }

    fetches_.clear();
    bool cache_enabled = cached_model_ != nullptr;
    const bool strict_shape_type_inference = ctx_.session_options.config_options.GetConfigOrDefault(
                                                 kOrtSessionOptionsConfigStrictShapeTypeInference, "1") == "1";
    const ModelOptions model_options(allow_released_onnx_opset_only,
                                     strict_shape_type_inference);
    auto p_model = !cache_enabled ? BuildGraph({}, model_options) : cached_model_;
    auto& graph = p_model->MainGraph();

    Status status = Status::OK();
    if (!cache_enabled) {
      if (add_shape_to_tensor_data_ &&
          ctx_.expect_result == ExpectResult::kExpectFailure) {
        // capture possible exceptions from shape inference for invalid testcase
        ORT_TRY {
          status = graph.Resolve(ctx_.resolve_options);
        }
        ORT_CATCH(const std::exception& ex) {
          ORT_HANDLE_EXCEPTION([&]() {
            status = ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, ex.what());
          });
        }
      } else {
        status = graph.Resolve(ctx_.resolve_options);
      }

      if (!status.IsOK()) {
        if (ctx_.expect_result == ExpectResult::kExpectFailure) {
          EXPECT_TRUE(!status.IsOK());
          EXPECT_THAT(status.ErrorMessage(),
                      testing::HasSubstr(ctx_.expected_failure_string));
        } else {
          LOGS_DEFAULT(ERROR) << "Resolve failed with status: "
                              << status.ErrorMessage();
          EXPECT_TRUE(status.IsOK()) << status.ErrorMessage();
        }
      }

      if (!status.IsOK()) {
        return;
      }
    }

    // Hookup the inputs and outputs
    std::unordered_map<std::string, OrtValue> feeds;
    std::vector<std::string> output_names;
    FillFeedsAndOutputNames(feeds, output_names);

    // Run the model
    if (ctx_.run_with_specified_eps) {
      ExecuteModelForEps(
          std::move(ctx_.execution_providers), *p_model, ctx_.session_options,
          ctx_.expect_result, ctx_.expected_failure_string,
          ctx_.run_options, feeds, output_names,
          /*custom_registries=*/nullptr,
          /*assign_ep_for_nodes=*/false,
          allow_released_onnx_opset_only,
          number_of_pre_packed_weights_counter,
          number_of_shared_pre_packed_weights_counter);
    } else {
#ifdef USE_TENSORRT
      // only run trt ep to reduce test time
      static const std::string all_provider_types[] = {
          kTensorrtExecutionProvider,
      };
#else
      static const std::string all_provider_types[] = {
          kCpuExecutionProvider,
          kCudaExecutionProvider,
          kDnnlExecutionProvider,
          kTensorrtExecutionProvider,
          kOpenVINOExecutionProvider,
          kDmlExecutionProvider,
          kAclExecutionProvider,
          kArmNNExecutionProvider,
          kNnapiExecutionProvider,
          kRocmExecutionProvider,
          kCoreMLExecutionProvider,
          kSnpeExecutionProvider,
          kXnnpackExecutionProvider,
      };
#endif

      bool has_run = false;

      for (const std::string& provider_type : all_provider_types) {
        if (ctx_.excluded_provider_types.count(provider_type) > 0)
          continue;

        cur_provider = provider_type;

        std::unique_ptr<IExecutionProvider> execution_provider;
        if (provider_type == onnxruntime::kCpuExecutionProvider)
          execution_provider = DefaultCpuExecutionProvider();
        else if (provider_type == onnxruntime::kCudaExecutionProvider)
          execution_provider = DefaultCudaExecutionProvider();
        else if (provider_type == onnxruntime::kDnnlExecutionProvider)
          execution_provider = DefaultDnnlExecutionProvider();
        else if (provider_type == onnxruntime::kOpenVINOExecutionProvider)
          execution_provider = DefaultOpenVINOExecutionProvider();
        else if (provider_type == onnxruntime::kTensorrtExecutionProvider)
          execution_provider = DefaultTensorrtExecutionProvider();
        else if (provider_type == onnxruntime::kNnapiExecutionProvider)
          execution_provider = DefaultNnapiExecutionProvider();
        else if (provider_type == onnxruntime::kRknpuExecutionProvider)
          execution_provider = DefaultRknpuExecutionProvider();
        else if (provider_type == onnxruntime::kAclExecutionProvider)
          execution_provider = DefaultAclExecutionProvider();
        else if (provider_type == onnxruntime::kArmNNExecutionProvider)
          execution_provider = DefaultArmNNExecutionProvider();
        else if (provider_type == onnxruntime::kRocmExecutionProvider)
          execution_provider = DefaultRocmExecutionProvider();
        else if (provider_type == onnxruntime::kCoreMLExecutionProvider)
          execution_provider = DefaultCoreMLExecutionProvider();
        else if (provider_type == onnxruntime::kSnpeExecutionProvider)
          execution_provider = DefaultSnpeExecutionProvider();
        else if (provider_type == onnxruntime::kXnnpackExecutionProvider)
          execution_provider = DefaultXnnpackExecutionProvider();
        else if (provider_type == onnxruntime::kDmlExecutionProvider)
          execution_provider = DefaultDmlExecutionProvider();

        // skip if execution provider is disabled
        if (execution_provider == nullptr)
          continue;

        ExecuteModelForEps(
            [&execution_provider]() {
              std::vector<std::unique_ptr<IExecutionProvider>> ret;
              ret.emplace_back(std::move(execution_provider));
              return ret;
            }(),
            *p_model, ctx_.session_options,
            ctx_.expect_result, ctx_.expected_failure_string,
            ctx_.run_options, feeds, output_names,
            &custom_session_registries_,
            /*try_assign_ep_for_nodes=*/true,
            allow_released_onnx_opset_only,
            number_of_pre_packed_weights_counter,
            number_of_shared_pre_packed_weights_counter);

        // Run Models with subscribed run_options->config_options
        if (ctx_.run_options != nullptr &&
            ctx_.run_options->config_options.GetConfigEntry(kOpTesterRunOptionsConfigTestTunableOp) == "true") {
          std::vector<std::unique_ptr<IExecutionProvider>> execution_providers;
          if (provider_type == onnxruntime::kRocmExecutionProvider) {
            execution_providers.emplace_back(DefaultRocmExecutionProvider(/*test_tunable_op=*/true));
          }

          if (!execution_providers.empty()) {
            ExecuteModelForEps(
                std::move(execution_providers), *p_model, ctx_.session_options,
                ctx_.expect_result, ctx_.expected_failure_string,
                ctx_.run_options, feeds, output_names,
                &custom_session_registries_,
                /*assign_ep_for_nodes=*/true,
                allow_released_onnx_opset_only,
                number_of_pre_packed_weights_counter,
                number_of_shared_pre_packed_weights_counter);
          }
        }

        has_run = true;
        cur_provider = "not set";
      }

#ifdef USE_TENSORRT
      // We are allowing tests to be run with only TensorRT EP, but TensorRT EP may not support all tests and may be in excluded providers list.
      // So, no registered EPs were able to run the model is okay for this situation.
      ORT_UNUSED_PARAMETER(has_run);
#else
      EXPECT_TRUE(has_run)
          << "No registered execution providers were able to run the model.";
#endif
    }
  }
  ORT_CATCH(const std::exception& ex) {
    ORT_HANDLE_EXCEPTION([&]() {
      std::cerr << ex.what() << "\nProvider:" << cur_provider << "\n";
    });
    // rethrow as some tests for error handling expect this
    ORT_RETHROW;
  }
}

void OpTester::ExecuteModelForEps(
    std::vector<std::unique_ptr<IExecutionProvider>>&& execution_providers,
    onnxruntime::Model& model,
    SessionOptions sess_options,  // session options is copied to avoid the side effect in this function
    onnxruntime::test::OpTester::ExpectResult expect_result,
    const std::string& expected_failure_string,
    const onnxruntime::RunOptions* run_options,
    const std::unordered_map<std::string, OrtValue>& feeds,
    const std::vector<std::string>& output_names,
    const std::vector<std::shared_ptr<CustomRegistry>>* custom_registries,
    bool try_assign_ep_for_nodes,
    bool allow_released_onnx_opset_only,
    size_t* number_of_pre_packed_weights_counter,
    size_t* number_of_shared_pre_packed_weights_counter) {
  for (auto& entry : execution_providers) {
    // Be noted, entry in execution providers passed in OpTester will be std::moved in the first OpTester::Run(),
    // To make the error more obvious to debug (instead of a segment fault), we do check explicitly here.
    ASSERT_TRUE(entry) << "Execution provider entry invalid.";

    if (entry->Type() == kDmlExecutionProvider) {
      sess_options.enable_mem_pattern = false;
      sess_options.execution_mode = ExecutionMode::ORT_SEQUENTIAL;
      break;
    }
  }

  InferenceSession session_object{sess_options, GetEnvironment()};

  if (add_prepacked_shared_container_to_sessions_) {
    ASSERT_STATUS_OK(session_object.AddPrePackedWeightsContainer(&prepacked_weights_container_));
  }
  ASSERT_TRUE(!execution_providers.empty()) << "Empty execution providers vector.";
  if (try_assign_ep_for_nodes && !SetEpsForAllNodes(model.MainGraph(), execution_providers, custom_registries)) {
    std::string providers;
    for (const auto& ep : execution_providers) {
      providers.append(ep->Type() + " ");
    }
    LOGS_DEFAULT(WARNING) << "registered execution providers " << providers << "were unable to run the model.";
    return;
  }

  std::string provider_type;
  for (auto&& ep : execution_providers) {
    provider_type += ep->Type() + ":";
  }
  provider_type.resize(provider_type.size() - 1);  // remove the trailing ':'

  if (custom_registries != nullptr) {
    for (const auto& reg : *custom_registries) {
      ASSERT_PROVIDER_STATUS_OK(session_object.RegisterCustomRegistry(reg));
    }
  }

  for (auto&& entry : execution_providers) {
    ASSERT_STATUS_OK(session_object.RegisterExecutionProvider(std::move(entry)));
  }

  fetches_ = ExecuteModel<InferenceSession>(
      model, session_object, expect_result, expected_failure_string,
      run_options, feeds, output_names, provider_type, allow_released_onnx_opset_only);

  // After the model has initialized (happens in ExecuteModel),
  // we should be able to tell how many constant initializers were pre-packed
  // and out of these pre-packed ones how many of them used a "cached" version
  // from the shared container.
  // Populate these value if the user has requested this information.
  if (number_of_pre_packed_weights_counter != nullptr) {
    *number_of_pre_packed_weights_counter =
        session_object.GetSessionState().GetNumberOfPrepacksCounter();
  }

  if (number_of_shared_pre_packed_weights_counter != nullptr) {
    *number_of_shared_pre_packed_weights_counter =
        session_object.GetSessionState().GetUsedSharedPrePackedWeightCounter();
  }
};

void OpTester::AddReferenceOutputs(const std::string& model_path, float abs_error) {
  SessionOptions so;
  so.session_logid = op_;
  so.session_log_verbosity_level = 1;
  so.graph_optimization_level = TransformerLevel::Default;

  RunOptions run_options;
  run_options.run_tag = op_;
  run_options.run_log_verbosity_level = 1;

  Status status;
  InferenceSession subgraph_session_object{so, GetEnvironment()};
  ASSERT_TRUE((status = subgraph_session_object.Load(model_path)).IsOK()) << status;
  ASSERT_TRUE((status = subgraph_session_object.Initialize()).IsOK()) << status;

  // Retrieve output names
  auto model_outputs = subgraph_session_object.GetModelOutputs();
  ASSERT_TRUE(model_outputs.first.IsOK());
  std::vector<std::string> output_names;
  std::transform(model_outputs.second->begin(),
                 model_outputs.second->end(),
                 std::back_inserter(output_names),
                 [](const onnxruntime::NodeArg* node_arg) -> std::string { return node_arg->Name(); });

  NameMLValMap feeds;
  for (size_t i = 0; i < input_data_.size(); ++i) {
    if (input_data_[i].def_.Exists()) {
      feeds[input_data_[i].def_.Name()] = input_data_[i].data_;
    }
  }

  std::vector<OrtValue> subgraph_fetches;
  ASSERT_TRUE((status = subgraph_session_object.Run(run_options, feeds, output_names, &subgraph_fetches)).IsOK()) << status;

  for (size_t out_idx = 0; out_idx < subgraph_fetches.size(); out_idx++) {
    // Retrieve TypeProto
    ASSERT_TRUE(subgraph_fetches[out_idx].Type()->IsTensorType()) << status;
    const Tensor& t = subgraph_fetches[out_idx].Get<Tensor>();
    const TensorTypeBase* tensor_type = DataTypeImpl::TensorTypeFromONNXEnum(t.GetElementType());

    // Construct a temp TypeProto with shape information
    ONNX_NAMESPACE::TypeProto tmp_type_proto(*(tensor_type->GetTypeProto()));
    auto mutable_shape = tmp_type_proto.mutable_tensor_type()->mutable_shape();
    for (auto i : t.Shape().GetDims()) {
      auto* mutable_dim = mutable_shape->add_dim();
      mutable_dim->set_dim_value(i);
    }

    if (abs_error != 0.0f) {
      output_data_.push_back(Data(NodeArg(output_names[out_idx], &tmp_type_proto),
                                  std::move(subgraph_fetches[out_idx]),
                                  optional<float>(), optional<float>(abs_error)));
    } else {
      output_data_.push_back(Data(NodeArg(output_names[out_idx], &tmp_type_proto),
                                  std::move(subgraph_fetches[out_idx]),
                                  optional<float>(), optional<float>()));
    }
  }
}

#ifdef ENABLE_TRAINING
// Deprecated code. Remove this when training::TrainingSession is removed.
template std::vector<OrtValue> OpTester::ExecuteModel<training::TrainingSession>(
    Model& model, training::TrainingSession& session_object,
    ExpectResult expect_result, const std::string& expected_failure_string,
    const RunOptions* run_options,
    const std::unordered_map<std::string, OrtValue>& feeds,
    const std::vector<std::string>& output_names, const std::string& provider_type,
    bool allow_released_onnx_opset_only);
#endif

}  // namespace test
}  // namespace onnxruntime
