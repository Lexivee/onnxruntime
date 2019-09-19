// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "cxxopts.hpp"
#include "core/common/common.h"
#include "core/common/logging/logging.h"
#include "core/common/logging/sinks/clog_sink.h"
#include "core/session/environment.h"
#include "core/training/training_session.h"
#include "core/training/tensorboard/event_writer.h"
#include "core/training/mpi_setup.h"
#include "test/training/runner/training_runner.h"
#include "test/training/runner/training_util.h"
#include "test/training/runner/data_loader.h"

using namespace onnxruntime;
using namespace onnxruntime::training;
using namespace onnxruntime::training::tensorboard;
using namespace std;

Status ParseArguments(int argc, char* argv[], TrainingRunner::Parameters& params) {
  cxxopts::Options options("BERT Training", "Main Program to train BERT");
  // clang-format off
  options
    .add_options()
      ("model_name", "model to be trained", cxxopts::value<std::string>())
      ("train_data_dir", "Input ONNX example files (can be a glob or comma separated).",
        cxxopts::value<std::string>()->default_value("bert_data/train"))
      ("test_data_dir", "Input ONNX example files (can be a glob or comma separated).",
        cxxopts::value<std::string>()->default_value("bert_data/test"))
      ("output_dir", "The output directory where the model checkpoints will be written.",
        cxxopts::value<std::string>())
      ("log_dir", "The directory to write tensorboard events.",
        cxxopts::value<std::string>()->default_value(""))
      ("num_of_epoch", "Num of epoch", cxxopts::value<int>()->default_value("1"))
      ("train_batch_size", "Total batch size for training.", cxxopts::value<int>())
      ("eval_batch_size", "Total batch size for eval.", cxxopts::value<int>())
      ("learning_rate", "The initial learning rate for the optimizer.", cxxopts::value<float>()->default_value("5e-5"))
      ("num_train_steps", "Number of training steps.", cxxopts::value<int>()->default_value("100000"))
      ("num_warmup_steps", "Number of warmup steps.", cxxopts::value<int>()->default_value("10000"))
      ("evaluation_period",
        "How many training steps to make before making an evaluation.",
        cxxopts::value<size_t>()->default_value("100"))
      ("gradient_accumulation_steps", "The number of gradient accumulation steps before performing a backward/update pass.",
        cxxopts::value<int>()->default_value("1"))
      ("save_checkpoint_steps", "How often to save the model checkpoint.", cxxopts::value<int>()->default_value("1000"))
      ("iterations_per_loop", "How many steps to make in each estimator call.", cxxopts::value<int>()->default_value("1000"))
      ("max_eval_steps", "Maximum number of eval steps.", cxxopts::value<int>()->default_value("100"))
      ("use_mixed_precision", "Whether to use a mix of fp32 and fp16 arithmetic on GPU.", cxxopts::value<bool>()->default_value("false"))
      ("use_fp16_moments", "Whether to use fp16 version of moments.", cxxopts::value<bool>()->default_value("false"))
      ("use_fp16_initializer", "FP16 weights will be created. Otherwise, cast nodes will be inserted for converting weights from FP32 to FP16",
        cxxopts::value<bool>()->default_value("true"))
      ("use_profiler", "Collect runtime profile data during this training run.", cxxopts::value<bool>()->default_value("false"))
      ("max_profile_records", "Maximum number of runtime profile data records to collect.",
          cxxopts::value<size_t>()->default_value(to_string(profiling::Profiler::DEFAULT_MAX_PROFILER_EVENTS)))
      ("mode", "mode for running, can be one of [train|perf]", cxxopts::value<std::string>()->default_value("train"))
      ("num_of_perf_samples", "Num of samples to run for the perf test", cxxopts::value<int>()->default_value("100"))
      ("perf_warm_up_iters", "Num of warm-up iterations to run before the perf test", cxxopts::value<int>()->default_value("10"))
      ("max_seq_length",
        "The maximum total input sequence length after WordPiece tokenization. "
        "Sequences longer than this will be truncated, and sequences shorter "
        "than this will be padded. Must match data generation.", cxxopts::value<int>()->default_value("512"))
      ("max_predictions_per_seq",
        "Maximum number of masked LM predictions per sequence. "
        "Must match data generation.", cxxopts::value<int>()->default_value("80"))
      ("optimizer", "Adam or Lamb", cxxopts::value<std::string>()->default_value("Adam"));
  // clang-format on

  try {
    auto flags = options.parse(argc, argv);

    params.model_name = flags["model_name"].as<std::string>();
    params.learning_rate = flags["learning_rate"].as<float>();
    params.num_of_epoch = flags["num_of_epoch"].as<int>();
    params.num_of_perf_samples = flags["num_of_perf_samples"].as<int>();
    params.perf_warm_up_iters = flags["perf_warm_up_iters"].as<int>();
    params.batch_size = flags["train_batch_size"].as<int>();
    if (flags.count("eval_batch_size")) {
      params.eval_batch_size = flags["eval_batch_size"].as<int>();
    } else {
      params.eval_batch_size = params.batch_size;
    }

    params.gradient_accumulation_steps = flags["gradient_accumulation_steps"].as<int>();
    if (params.gradient_accumulation_steps < 1) {
      return Status(ONNXRUNTIME, INVALID_ARGUMENT, "Invalid gradient_accumulation_steps parameter: should be >= 1");
    }

    params.evaluation_period = flags["evaluation_period"].as<size_t>();
    params.use_profiler = flags.count("use_profiler") > 0;
    params.max_profile_records = flags["max_profile_records"].as<size_t>();

    auto train_data_dir = flags["train_data_dir"].as<std::string>();
    auto test_data_dir = flags["test_data_dir"].as<std::string>();
    auto log_dir = flags["log_dir"].as<std::string>();
    params.train_data_dir.assign(train_data_dir.begin(), train_data_dir.end());
    params.test_data_dir.assign(test_data_dir.begin(), test_data_dir.end());
    params.log_dir.assign(log_dir.begin(), log_dir.end());

    std::string mode = flags["mode"].as<std::string>();
    if (mode == "perf" || mode == "train") {
      params.is_perf_test = mode == "perf";
    } else {
      return Status(ONNXRUNTIME, INVALID_ARGUMENT, "Incorrect command line for mode: it must be one of [perf|train]");
    }

    params.use_mixed_precision = flags["use_mixed_precision"].as<bool>();
    if (params.use_mixed_precision) {
      printf("Mixed precision training is enabled.\n");
    }
    params.use_fp16_moments = flags["use_fp16_moments"].as<bool>();
    if (params.use_fp16_moments) {
      printf("Using fp16 version of moments.\n");
    }
    params.use_fp16_initializer = flags["use_fp16_initializer"].as<bool>();
    if (params.use_mixed_precision && params.use_fp16_initializer) {
      printf("FP16 initializer is enabled.\n");
    }

    std::string optimizer_name = flags["optimizer"].as<std::string>();
    if (optimizer_name == "adam" || optimizer_name == "Adam") {
      params.training_optimizer_name = "AdamOptimizer";
    } else if (optimizer_name == "lamb" || optimizer_name == "Lamb") {
      params.training_optimizer_name = "LambOptimizer";
    } else {
      return Status(ONNXRUNTIME, INVALID_ARGUMENT, "Incorrect optimizer type: it must be one of [Adam|Lamb]");
    }
  } catch (const exception& e) {
    const std::string msg = "Failed to parse the command line arguments";
    cerr << msg << ": " << e.what() << "\n"
         << options.help() << "\n";
    return Status(ONNXRUNTIME, INVALID_ARGUMENT, msg);
  }
  return Status::OK();
}

// NOTE: these variables need to be alive when the error_function is called.
float total_loss = 0.0f;
float mlm_loss = 0.0f;
float nsp_loss = 0.0f;
std::vector<std::string> summary_loss;

void setup_training_params(TrainingRunner::Parameters& params) {
  params.model_path = params.model_name + ".onnx";
  params.model_with_loss_func_path = params.model_name + "_with_cost.onnx";
  params.model_with_training_graph_path = params.model_name + "_bw.onnx";
  params.model_actual_running_graph_path = params.model_name + "_bw_running.onnx";
  params.model_trained_path = params.model_name + "_trained.onnx";
  params.model_trained_with_loss_func_path = params.model_name + "_with_cost_trained.onnx";

  params.loss_func_info = LossFunctionInfo(OpDef("BertLoss", kOnnxDomain),
                                           "total_loss",
                                           {/*prediction_masked_lm*/ "output1",
                                            /*prediction_next_sentence*/ "output2",
                                            /*masked_lm_positions*/ "masked_lm_positions",
                                            /*masked_lm_ids*/ "masked_lm_ids",
                                            /*masked_lm_weights*/ "masked_lm_weights",
                                            /*next_sentence_labels*/ "next_sentence_labels",
                                            /*mlm_loss*/ "mlm_loss",
                                            /*nsp_loss*/ "nsp_loss",
                                            /*batch_size*/ std::to_string(params.batch_size),
                                            /*max_sequence_len*/ std::to_string(512),
                                            /*max_predictions_per_sequence*/ std::to_string(80),
                                            /*summary_loss*/ "summary"});
  params.weights_not_to_train = {
      "position_01",            // Slice's dat input
      "op_min_ends_expand_10",  //op_min_ends_expand_10
  };
  params.fetch_names = {"total_loss", "mlm_loss", "nsp_loss", "summary"};

  params.immutable_weights = {
      {"Div", {{1, 8.0f}, {1, 1.4142135381698608f}}},
      {"Add", {{1, 1.0f}, {1, 9.999999960041972e-13f}}},
      {"Mul", {{1, 0.5f}, {1, -10000.0f}}},
      {"Sub", {{0, 1.0f}}}};

  params.optimizer_attributes = {
      {"alpha", 0.9f},
      {"beta", 0.999f},
      {"lambda", 0.0f},
      {"epsilon", 1e-6f},
  };

  params.shuffle_data = false;

  // name_in_data_file -> name_in_model
  params.input_name_map = {
      {"input_ids", "input1"},
      {"segment_ids", "input2"},
      {"input_mask", "input3"},
      {"masked_lm_positions", "masked_lm_positions"},
      {"masked_lm_ids", "masked_lm_ids"},
      {"masked_lm_weights", "masked_lm_weights"},
      {"next_sentence_label", "next_sentence_labels"}};

  params.use_cuda = true;

  params.skip_evaluation = params.is_perf_test;

  params.error_function = [params](const std::vector<std::string>& /*feed_names*/,
                                   const std::vector<OrtValue>& /*feeds*/,
                                   const std::vector<std::string>& fetch_names,
                                   const std::vector<OrtValue>& fetches) {
    const Tensor& total_loss_t = fetches[0].Get<Tensor>();
    const Tensor& mlm_loss_t = fetches[1].Get<Tensor>();
    const Tensor& nsp_loss_t = fetches[2].Get<Tensor>();
    const Tensor& summary_loss_t = fetches[3].Get<Tensor>();

    const float* total_loss_val = total_loss_t.template Data<float>();
    const float* mlm_loss_val = mlm_loss_t.template Data<float>();
    const float* nsp_loss_val = nsp_loss_t.template Data<float>();
    const std::string* summary_loss_val = summary_loss_t.template Data<std::string>();

    total_loss += *total_loss_val;
    mlm_loss += *mlm_loss_val;
    nsp_loss += *nsp_loss_val;
    summary_loss.push_back(*summary_loss_val);

    if (params.dump_fetches) {
      ofstream ofs("fetches_dump.txt");
      for (size_t i = 0; i < fetch_names.size(); ++i) {
        TrainingUtil::PrintTensor(fetch_names[i], fetches[i].Get<Tensor>(), ofs);
      }
      ofs.close();
    }
  };

  std::shared_ptr<EventWriter> tensorboard;
  if (!params.log_dir.empty() && params.mpi_context.world_rank == 0)
    tensorboard = std::make_shared<EventWriter>(params.log_dir);

  params.post_evaluation_callback = [tensorboard](size_t num_samples, size_t step) {
    float average_total_loss = total_loss / float(num_samples);
    float average_mlm_loss = mlm_loss / float(num_samples);
    float average_nsp_loss = nsp_loss / float(num_samples);

    if (tensorboard != nullptr) {
      for (const std::string& summary : summary_loss)
        tensorboard->AddSummary(summary, step);
    }

    printf("Step: %zu, #examples: %d, total_loss: %0.04f, mlm_loss: %0.04f, nsp_loss: %0.04f \n\n",
           step,
           static_cast<int>(num_samples),
           average_total_loss,
           average_mlm_loss,
           average_nsp_loss);
    total_loss = 0.0f;
    mlm_loss = 0.0f;
    nsp_loss = 0.0f;
    summary_loss.clear();
  };
}

int main(int argc, char* argv[]) {
#ifndef USE_CUDA
  printf("BERT training is not supported in non-CUDA build. ");
#endif

  TrainingRunner::Parameters params;
  RETURN_IF_FAIL(ParseArguments(argc, argv, params));
  setup_training_params(params);

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

// setup horovod
#ifdef USE_HOROVOD
  params.mpi_context = setup_horovod();
#endif

  // start training session
  std::unique_ptr<TrainingRunner> runner;
  if (params.is_perf_test) {
    // setup fake data
    int batch_size = static_cast<int>(params.batch_size);
    int max_seq_len_in_batch = 512;
    std::vector<std::string> tensor_names = {"input1",
                                             "input2",
                                             "input3",
                                             "masked_lm_positions",
                                             "masked_lm_ids",
                                             "masked_lm_weights",
                                             "next_sentence_labels"};
    std::vector<TensorShape> tensor_shapes = {{batch_size, max_seq_len_in_batch},
                                              {batch_size, max_seq_len_in_batch},
                                              {batch_size, max_seq_len_in_batch},
                                              {batch_size, 80},
                                              {batch_size, 80},
                                              {batch_size, 80},
                                              {batch_size}};
    std::vector<onnx::TensorProto_DataType> tensor_types = {onnx::TensorProto_DataType_INT64,
                                                            onnx::TensorProto_DataType_INT64,
                                                            onnx::TensorProto_DataType_INT64,
                                                            onnx::TensorProto_DataType_INT64,
                                                            onnx::TensorProto_DataType_INT64,
                                                            onnx::TensorProto_DataType_FLOAT,
                                                            onnx::TensorProto_DataType_INT64};

    auto random_perf_data = std::make_shared<RandomDataSet>(params.num_of_perf_samples, tensor_names, tensor_shapes, tensor_types);
    auto random_perf_data_loader = std::make_shared<SingleDataLoader>(random_perf_data, tensor_names);
    runner = std::make_unique<TrainingRunner>(random_perf_data_loader, random_perf_data_loader, params);

  } else {
    const size_t max_num_files_preload = 2;

    auto training_data_loader = std::make_shared<DataLoader>(params.input_name_map,
                                                             params.train_data_dir,
                                                             max_num_files_preload,
                                                             params.mpi_context.world_rank,
                                                             params.mpi_context.world_size);
    RETURN_IF_FAIL(training_data_loader->InitialPreLoadAsync());

    // Evaluation is only done in device #0
    std::shared_ptr<DataLoader> test_data_loader;
    if (params.mpi_context.world_rank == 0) {
      test_data_loader = std::make_shared<DataLoader>(params.input_name_map,
                                                      params.test_data_dir,
                                                      max_num_files_preload);
      RETURN_IF_FAIL(test_data_loader->InitialPreLoadAsync());
    }

    runner = std::make_unique<TrainingRunner>(training_data_loader, test_data_loader, params);
  }

  RETURN_IF_FAIL(runner->Initialize());
  RETURN_IF_FAIL(runner->Run());

#ifdef USE_HOROVOD
  shutdown_horovod();
#endif
}
