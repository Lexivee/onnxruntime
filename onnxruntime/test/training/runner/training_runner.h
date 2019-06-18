// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include <utility>
#include <vector>
#include "core/framework/ml_value.h"
#include "core/training/training_session.h"
#include "test/training/runner/training_util.h"
#include "core/graph/training/in_graph_training_optimizer.h"

namespace onnxruntime {
namespace training {

class TrainingRunner {
 public:
  struct AdamOptimizerParams {
    float alpha_;
    float beta_;
    float lambda_;
    float epsilon_;
  };

  struct Parameters {
    std::string model_path_;
    std::string model_with_loss_func_path_;          // To save the model after adding loss func.
    std::string model_with_training_graph_path_;     // To save the model after adding loss func and backward graph.
    std::string model_actual_running_graph_path_;    // To save the model with the actual running graph after transformations.
    std::string model_trained_path_;                 // To save the model after training.
    std::string model_trained_with_loss_func_path_;  // To save the model with loss func after training.
    LossFunctionInfo loss_func_info_;

    // The in-graph optimizer info.
    // It is the name of the optimizer OP.
    // If specified, every gradient output will be connected to a new optimizer node
    // who has the updated weights as new graph outputs.
    // For now all to-be-trained weights use the same optimizer type.
    std::string in_graph_optimizer_name_;

    // For some model, loss function's input "prediction" is not the model output.
    // So model_prediction_name must be specified.
    std::string model_prediction_name_;

    // The weights to train, exclusive with weights_not_to_train_.
    std::vector<std::string> weights_to_train_;

    // The weights not to train. If not empty, all the initializers not in the vector will be trained.
    // exclusive with weights_not_to_train_.
    std::vector<std::string> weights_not_to_train_;

    TrainingSession::ImmutableWeights immutable_weigths_;

    size_t batch_size_;
    size_t num_of_epoch_;

    // Optimizer Parameter
    float learning_rate_;
    AdamOptimizerParams adam_opt_params_;

    // When doing evaluation, some number of test samples will be selected to run evaluation.
    size_t num_of_samples_for_evaluation_;

    // error_function_ is called when evaluating the error for a single sample.
    std::function<void(const MLValue& /*predict*/, const MLValue& /*label*/, const MLValue& /*loss*/)> error_function_;

    // post_evaluation_callback_ is called when a batch of evaluation is done.
    std::function<void(size_t /*num_of_test_sample_run*/)> post_evaluation_callback_;

#ifdef USE_CUDA
    // Use CUDA providers or not.
    // TODO: support a list of providers.
    bool use_cuda_ = false;
#endif

    int world_rank_ = 0;
  };

  TrainingRunner(DataSet& trainingData, DataSet& testData, const Parameters& params);

  common::Status Initialize();

  common::Status Run();

 private:
  Status TrainingLoop();
  Status EndTraining();
  Status Evaluate(InferenceSession& session, bool use_full_set = false);
  Status LoadAndEvaluate(const std::string& model_path);
  Status SetupOptimizerParams(std::vector<in_graph_optimizer::OptimizerInfo>& infos);

  DataSet& training_data_;
  DataSet& test_data_;
  Parameters params_;
  TrainingSession session_;
};

}  // namespace training
}  // namespace onnxruntime
