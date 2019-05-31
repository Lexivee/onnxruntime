// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/common/logging/logging.h"
#include "core/common/logging/sinks/clog_sink.h"
#include "core/framework/environment.h"
#include "core/training/training_optimizer.h"
#include "core/training/training_session.h"
#include "core/training/weight_updater.h"
#include "test/training/poc/mnist_data_provider.h"
#include "test/training/runner/training_runner.h"
#include "test/training/runner/training_util.h"

#ifdef USE_HOROVOD
#include "core/graph/training/horovod_adapters.h"
#include <mpi.h>
#endif

#include <condition_variable>
#include <mutex>
#include <tuple>

using namespace onnxruntime;
using namespace onnxruntime::training;
using namespace std;

using namespace onnxruntime;

const static int NUM_OF_EPOCH = 2;
const static float LEARNING_RATE = 0.1f;
const static int BATCH_SIZE = 100;
const static int NUM_CLASS = 10;
const static int NUM_SAMPLES_FOR_EVALUATION = 100;
const static vector<int64_t> IMAGE_DIMS = {1, 784};  //{1, 1, 28, 28} for mnist_conv
const static vector<int64_t> LABEL_DIMS = {1, 10};
const static std::string MNIST_DATA_PATH = "mnist_data";

int validate_params(int argc, char* args[]) {
  if (argc < 2) {
    printf("Incorrect command line for %s\n", args[0]);
#ifdef USE_CUDA
    printf("usage: exe_name model_name [gpu] [optional:world_rank]\n");
#else
    printf("usage: exe_name model_name\n");
#endif
    return -1;
  }
  return 0;
}

#ifdef USE_HOROVOD
std::pair<int, int> setup_horovod() {
  using namespace horovod::common;
  // setup MPI amd horovod
  MPI_Init(0, 0);

  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  int* ranks = (int*)malloc(sizeof(int) * world_size);

  MPI_Allgather(&world_rank, 1, MPI_INT, ranks, 1, MPI_INT, MPI_COMM_WORLD);

  horovod_init(ranks, world_size);

  return {world_rank, world_size};
}

void shutdown_horovod() {
  horovod::common::horovod_shutdown();
  MPI_Finalize();
}

#endif

// NOTE: these variables need to be alive when the error_function is called.
int true_count = 0;
float total_loss = 0.0f;

void setup_training_params(std::string& model_name, TrainingRunner::Parameters& params) {
  params.model_path_ = model_name + ".onnx";
  params.model_with_loss_func_path_ = model_name + "_with_cost.onnx";
  params.model_with_training_graph_path_ = model_name + "_bw.onnx";
  params.model_actual_running_graph_path_ = model_name + "_bw_running.onnx";
  params.model_trained_path_ = model_name + "_trained.onnx";
  params.model_trained_with_loss_func_path_ = model_name + "_with_cost_trained.onnx";
  params.loss_func_info_ = {"SoftmaxCrossEntropy", "predictions", "labels", "loss", kMSDomain};
  params.model_prediction_name_ = "predictions";
  //params.weights_to_train_ = {"W1", "W2", "W3", "B1", "B2", "B3"};
  params.weights_not_to_train_ = {""};
  params.batch_size_ = BATCH_SIZE;
  params.num_of_epoch_ = NUM_OF_EPOCH;
#ifdef USE_CUDA
  // TODO: This should be done in SGD optimizer. Will refactor when optimizing the kernel.
  // Adding another cuda kernel call for this division seems wasteful currently.
  params.learning_rate_ = LEARNING_RATE / BATCH_SIZE;
  params.in_graph_optimizer_name_ = params.use_cuda_ ? "SGDOptimizer" : "";
#else
  params.learning_rate_ = LEARNING_RATE;
#endif
  params.num_of_samples_for_evaluation_ = NUM_SAMPLES_FOR_EVALUATION;

  params.error_function_ = [](const MLValue& predict, const MLValue& label, const MLValue& loss) {
    const Tensor& predict_t = predict.Get<Tensor>();
    const Tensor& label_t = label.Get<Tensor>();
    const Tensor& loss_t = loss.Get<Tensor>();

    const float* prediction_data = predict_t.template Data<float>();
    const float* label_data = label_t.template Data<float>();
    const float* loss_data = loss_t.template Data<float>();

    const TensorShape predict_shape = predict_t.Shape();
    const TensorShape label_shape = label_t.Shape();
    const TensorShape loss_shape = loss_t.Shape();
    ORT_ENFORCE(predict_shape == label_shape);

    int64_t batch_size = predict_shape[0];
    for (int n = 0; n < batch_size; ++n) {
      auto max_class_index = std::distance(prediction_data,
                                           std::max_element(prediction_data, prediction_data + NUM_CLASS));

      if (static_cast<int>(label_data[max_class_index]) == 1) {
        true_count++;
      }

      prediction_data += predict_shape.SizeFromDimension(1);
      label_data += label_shape.SizeFromDimension(1);
    }
    total_loss += *loss_data;
  };

  params.post_evaluation_callback_ = [](size_t num_samples) {
    float precision = float(true_count) / num_samples;
    float average_loss = total_loss / float(num_samples);
    printf("#examples: %d, #correct: %d, precision: %0.04f, loss: %0.04f \n\n",
           static_cast<int>(num_samples),
           true_count,
           precision,
           average_loss);
    true_count = 0;
    total_loss = 0.0f;
  };
}

int main(int argc, char* args[]) {
  if (validate_params(argc, args) == -1) return -1;

  // setup logger
  string default_logger_id{"Default"};
  logging::LoggingManager default_logging_manager{unique_ptr<logging::ISink>{new logging::CLogSink{}},
                                                  logging::Severity::kWARNING,
                                                  false,
                                                  logging::LoggingManager::InstanceType::Default,
                                                  &default_logger_id};

  // setup onnxruntime env
  unique_ptr<Environment> env;
  ORT_ENFORCE(Environment::Create(env).IsOK());

  // setup training params
  TrainingRunner::Parameters params;
  std::string model_name = args[1];
#ifdef USE_CUDA
  params.use_cuda_ = (argc > 2 && string(args[2]) == "gpu");
#endif
  setup_training_params(model_name, params);

  // setup horovod
  int device_id = 0,
      device_count = 1;

#ifdef USE_HOROVOD
  std::tie(device_id, device_count) = setup_horovod();
#endif

#ifdef USE_CUDA
  params.learning_rate_ /= device_count;
  params.world_rank_ = device_id;
  if (params.use_cuda_) {
    printf("Using cuda device #%d \n", params.world_rank_);
  }
#endif

  // setup data
  DataSet trainingData({"X", "labels"});
  DataSet testData({"X", "labels"});
  PrepareMNISTData(MNIST_DATA_PATH, IMAGE_DIMS, LABEL_DIMS, trainingData, testData, device_id /* shard_to_load */, device_count /* total_shards */);

  // start training session
  TrainingRunner runner(trainingData, testData, params);
  RETURN_IF_FAIL(runner.Initialize());
  RETURN_IF_FAIL(runner.Run());

#ifdef USE_HOROVOD
  shutdown_horovod();
#endif
}
