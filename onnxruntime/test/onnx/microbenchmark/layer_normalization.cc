#ifdef _WIN32

#include "core/platform/threadpool.h"
#include "core/util/thread_utils.h"
#include <benchmark/benchmark.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "core/framework/allocator.h"
#include "core/framework/config_options.h"
#include "core/framework/data_transfer_manager.h"
#include "core/framework/op_kernel_info.h"
#include "core/framework/ort_value_name_idx_map.h"
#include "core/platform/windows/env.h"
#include "core/providers/cpu/nn/layer_norm_impl.h"
#include "core/providers/cpu/cpu_provider_factory.h"
#include "core/providers/cpu/cpu_provider_factory_creator.h"
#include "core/util/thread_utils.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

using namespace onnxruntime;

namespace {

static const std::vector<int64_t> dims{1, 256, 1024};
static const size_t num_elems = dims[0] * dims[1] * dims[2];
static const std::vector<float> float_vals(num_elems, 1.0f);
static const std::vector<MLFloat16> MLFloat16_vals(num_elems, MLFloat16(1.0f));

} // namespace

template <typename T>
const T* getVector();

template <>
const float* getVector<float>() {
    return float_vals.data();
}

template <>
const MLFloat16* getVector<MLFloat16>() {
    return MLFloat16_vals.data();
}


template<typename T, typename U>
static void BM_LayerNormalization(benchmark::State& state) {
  bool simplified = false;
  const float epsilon = 1e-05f;
  int64_t axis = 1;

  onnxruntime::Node node;
  // Required by LayerNormImpl constructor
  node.AddAttribute("axis", axis);
  node.AddAttribute("epsilon", epsilon);

  KernelDef kernel_def;
  std::unique_ptr<IExecutionProvider> execution_provider = CPUProviderFactoryCreator::Create(true)->CreateProvider();
  std::unordered_map<int, OrtValue> constant_initialized_tensors;
  OrtValueNameIdxMap mlvalue_name_idx_map;
  DataTransferManager data_transfer_mgr;
  AllocatorMap allocators;
  ConfigOptions config_options;

  OpKernelInfo op_kernel_info(node, kernel_def, *execution_provider, constant_initialized_tensors, mlvalue_name_idx_map,
    data_transfer_mgr, allocators, config_options);

  LayerNormImpl layer_norm_impl(op_kernel_info);

  TensorShape x_shape(dims);
  TensorShape scale_shape(dims);
  TensorShape bias_shape(dims);

  const T* x_data = getVector<T>();
  const T* scale_data = getVector<T>();
  const T* bias_data = getVector<T>();

  T* Y_data = static_cast<T*>(malloc(num_elems * sizeof(T)));
  U* mean_data = static_cast<U*>(malloc(num_elems * sizeof(U)));
  U* inv_std_dev_data = static_cast<U*>(malloc(num_elems * sizeof(U)));

  OrtThreadPoolParams tp_params;
  tp_params.name = ORT_TSTR("intra-op");
  std::unique_ptr<concurrency::ThreadPool> thread_pool = concurrency::CreateThreadPool(
    &Env::Default(), tp_params, concurrency::ThreadPoolType::INTRA_OP);

  for (auto _ : state) {
    auto status = layer_norm_impl.ComputeWithoutContext(x_data, x_shape, scale_data, scale_shape, bias_data, bias_shape,
      Y_data, mean_data, inv_std_dev_data, thread_pool.get(), axis, epsilon, simplified);

    if (! status.IsOK())
    {
       std::cout << "ComputeWithoutContext status not OK: " << status.ErrorMessage() << std::endl;
       break;
    }
  }
}


BENCHMARK(BM_LayerNormalization<float, float>)
    ->Arg(1)
    ->UseRealTime()
    ->Unit(benchmark::TimeUnit::kMicrosecond);

BENCHMARK(BM_LayerNormalization<MLFloat16, MLFloat16>)
    ->Arg(1)
    ->UseRealTime()
    ->Unit(benchmark::TimeUnit::kMicrosecond);

#endif
