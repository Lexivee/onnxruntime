/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the MIT License.
 */
package ai.onnxruntime;

import ai.onnxruntime.OrtSession.Result;
import ai.onnxruntime.OrtSession.RunOptions;

import java.io.IOException;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;

/**
 * Wraps an ONNX training model and allows training and inference calls.
 *
 * <p>Allows the inspection of the model's input and output nodes. Produced by an {@link
 * OrtEnvironment}.
 *
 * <p>Most instance methods throw {@link IllegalStateException} if the session is closed and the
 * methods are called.
 */
public final class OrtTrainingSession implements AutoCloseable {

  static {
    try {
      OnnxRuntime.init();
    } catch (IOException e) {
      throw new RuntimeException("Failed to load onnx-runtime library", e);
    }
  }

  private final long nativeHandle;
  private final OrtAllocator allocator;
  private final OrtCheckpointState checkpoint;

  private final String trainPath;
  private final String evalPath;
  private final String optimizerPath;

  private final Set<String> trainInputNames;
  private final Set<String> trainOutputNames;

  private final Set<String> evalInputNames;
  private final Set<String> evalOutputNames;

  private boolean closed = false;

    /**
     * Constructs an {@code OrtTrainingSession}.
     *
     * <p>Note the guard on training being enabled is not present in this method, and it should only be called after
     * {@link OnnxRuntime#trainingEnabled} has been checked to be true.
     *
     * @param env The environment.
     * @param allocator The memory allocator.
     * @param options The session options.
     * @param checkpoint The checkpoint to load.
     * @param trainPath The path to the training model.
     * @param evalPath The path to the evaluation model.
     * @param optimizerPath The path to the optimizer model.
     * @throws OrtException If the native creation failed.
     */
  OrtTrainingSession(
      OrtEnvironment env,
      OrtAllocator allocator,
      OrtSession.SessionOptions options,
      OrtCheckpointState checkpoint,
      String trainPath,
      String evalPath,
      String optimizerPath)
      throws OrtException {
    this(
        createTrainingSession(
            OnnxRuntime.ortApiHandle,
            OnnxRuntime.ortTrainingApiHandle,
            env.nativeHandle,
            options.nativeHandle,
            checkpoint.nativeHandle,
            trainPath,
            evalPath,
            optimizerPath),
        allocator,
        checkpoint,
        trainPath,
        evalPath,
        optimizerPath);
  }

  /**
   * Wraps an OrtTrainingSession around the native session pointer.
   *
   * @param nativeHandle The native session pointer.
   * @param allocator The memory allocator.
   * @param trainPath The path on disk to the training model.
   * @param evalPath The path on disk to the evaluation model.
   * @param optimizerPath The path on disk to the optimizer model.
   */
  private OrtTrainingSession(
      long nativeHandle,
      OrtAllocator allocator,
      OrtCheckpointState checkpoint,
      String trainPath,
      String evalPath,
      String optimizerPath)
      throws OrtException {
    this.nativeHandle = nativeHandle;
    this.allocator = allocator;
    this.checkpoint = checkpoint;
    this.trainPath = trainPath;
    this.evalPath = evalPath;
    this.optimizerPath = optimizerPath;

    this.trainInputNames =
        new LinkedHashSet<>(
            Arrays.asList(
                getTrainInputNames(
                    OnnxRuntime.ortApiHandle,
                    OnnxRuntime.ortTrainingApiHandle,
                    nativeHandle,
                    allocator.handle)));
    this.trainOutputNames =
        new LinkedHashSet<>(
            Arrays.asList(
                getTrainOutputNames(
                    OnnxRuntime.ortApiHandle,
                    OnnxRuntime.ortTrainingApiHandle,
                    nativeHandle,
                    allocator.handle)));
    this.evalInputNames =
        new LinkedHashSet<>(
            Arrays.asList(
                getEvalInputNames(
                    OnnxRuntime.ortApiHandle,
                    OnnxRuntime.ortTrainingApiHandle,
                    nativeHandle,
                    allocator.handle)));
    this.evalOutputNames =
        new LinkedHashSet<>(
            Arrays.asList(
                getEvalOutputNames(
                    OnnxRuntime.ortApiHandle,
                    OnnxRuntime.ortTrainingApiHandle,
                    nativeHandle,
                    allocator.handle)));
  }

  /**
   * \brief Create a training session that can be used to begin or resume training.
   *
   * <p>This function creates a training session based on the env and session options provided that
   * can begin or resume training from a given checkpoint state for the given onnx models. The
   * checkpoint state represents the parameters of the training session which will be moved to the
   * device specified by the user through the session options (if necessary).
   *
   * <p>\param[in] env Environment to be used for the training session. \param[in] options Session
   * options that the user can customize for this training session. \param[in] checkpoint_state
   * Training states that the training session uses as a starting point for training. \param[in]
   * train_model_path Model to be used to perform training that can be generated using the offline
   * tooling library. \param[in] eval_model_path Model to be used to perform evaluation that can be
   * generated using the offline tooling library. \param[in] optimizer_model_path Model to be used
   * to the optimizer step for weight updates. The model can be generated using the offline tooling
   * library. \param[out] out Created training session.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(CreateTrainingSession, _In_ const OrtEnv* env, _In_ const OrtSessionOptions*
   * options, _Inout_ OrtCheckpointState* checkpoint_state, _In_ const ORTCHAR_T* train_model_path,
   * _In_ const ORTCHAR_T* eval_model_path, _In_ const ORTCHAR_T* optimizer_model_path, _Outptr_
   * OrtTrainingSession** out);
   */
  private static native long createTrainingSession(
      long apiHandle,
      long trainingHandle,
      long envHandle,
      long optionsHandle,
      long checkpointHandle,
      String trainPath,
      String evalPath,
      String optimizerPath);

  /** Checks if the OrtTrainingSession is closed, if so throws {@link IllegalStateException}. */
  private void checkClosed() {
    if (closed) {
      throw new IllegalStateException("Trying to use a closed OrtTrainingSession");
    }
  }

  @Override
  public void close() {
    if (!closed) {
      checkpoint.close();
      closeSession(OnnxRuntime.ortTrainingApiHandle, nativeHandle);
      closed = true;
    } else {
      throw new IllegalStateException("Trying to close an already closed OrtSession.");
    }
  }

  /**
   * Returns an ordered set of the train model input names.
   *
   * @return The training inputs.
   */
  public Set<String> getTrainInputNames() {
    return trainInputNames;
  }

  /**
   * Returns an ordered set of the train model output names.
   *
   * @return The training outputs.
   */
  public Set<String> getTrainOutputNames() {
    return trainOutputNames;
  }

  /**
   * Returns an ordered set of the eval model input names.
   *
   * @return The evaluation inputs.
   */
  public Set<String> getEvalInputNames() {
    return evalInputNames;
  }

  /**
   * Returns an ordered set of the eval model output names.
   *
   * @return The evaluation outputs.
   */
  public Set<String> getEvalOutputNames() {
    return evalOutputNames;
  }

  private native void closeSession(long trainingHandle, long nativeHandle);

  /**
   * Save out the training session state into the supplied checkpoint directory.
   *
   * @param outputPath Path to a checkpoint directory.
   * @param saveOptimizer Should the optimizer states be saved out.
   * @throws OrtException If the native call failed.
   */
  public void saveCheckpoint(Path outputPath, boolean saveOptimizer) throws OrtException {
    checkClosed();
    String outputStr = outputPath.toString();
    saveCheckpoint(
        OnnxRuntime.ortApiHandle,
        OnnxRuntime.ortTrainingApiHandle,
        nativeHandle,
        outputStr,
        saveOptimizer);
  }

  /**
   * \brief Save the training session states to a checkpoint directory on disk.
   *
   * <p>This function retrieves the training session states from the training session and serializes
   * them to a checkpoint directory on disk. This checkpoint can later be loaded by invoking
   * LoadCheckpoint to continue the training with the same states.
   *
   * <p>\param[in] checkpoint_path Path to the checkpoint directory \param[in] session The training
   * session from where the checkpoint states are to be retrieved. \param[in] save_optimizer_state
   * Boolean flag indicating whether or not to save the optimizer states to the checkpoint.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(SaveCheckpoint, _In_ const ORTCHAR_T* checkpoint_path, _In_ const
   * OrtTrainingSession* session, bool save_optimizer_state);
   */
  private native void saveCheckpoint(
      long apiHandle, long trainingHandle, long nativeHandle, String path, boolean saveOptimizer)
      throws OrtException;

  /**
   * \brief Retrieves the number of user outputs in the training model.
   *
   * <p>This function returns the number of outputs of the training model so that the user can
   * allocate space for the number of outputs when TrainStep is invoked.
   *
   * <p>\param[in] sess The training session which has working knowledge of the training model.
   * \param[out] out Number of user outputs in the training model.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(TrainingSessionGetTrainingModelOutputCount, _In_ const OrtTrainingSession*
   * sess, _Out_ size_t* out); ORT_API2_STATUS(TrainingSessionGetTrainingModelOutputName, _In_ const
   * OrtSession* sess, size_t index, _Inout_ OrtAllocator* allocator, _Outptr_ char** output);
   */
  private native String[] getTrainInputNames(
      long apiHandle, long trainingApiHandle, long nativeHandle, long allocatorHandle)
      throws OrtException;

  private native String[] getTrainOutputNames(
      long apiHandle, long trainingApiHandle, long nativeHandle, long allocatorHandle)
      throws OrtException;

  /**
   * \brief Retrieves the number of user outputs in the eval model.
   *
   * <p>This function returns the number of outputs of the eval model so that the user can allocate
   * space for the number of outputs when EvalStep is invoked.
   *
   * <p>\param[in] sess The training session which has working knowledge of the eval model.
   * \param[out] out Number of user outputs in the eval model.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(TrainingSessionGetEvalModelOutputCount, _In_ const OrtTrainingSession* sess,
   * _Out_ size_t* out); ORT_API2_STATUS(TrainingSessionGetEvalModelOutputName, _In_ const
   * OrtSession* sess, size_t index, _Inout_ OrtAllocator* allocator, _Outptr_ char** output);
   */
  private native String[] getEvalInputNames(
      long apiHandle, long trainingApiHandle, long nativeHandle, long allocatorHandle)
      throws OrtException;

  private native String[] getEvalOutputNames(
      long apiHandle, long trainingApiHandle, long nativeHandle, long allocatorHandle)
      throws OrtException;

  /**
   * \brief Reset the training model gradients to zero lazily.
   *
   * <p>This function sets the internal state of the training session such that the training model
   * gradients will be reset just before the new gradients are computed on the next invocation of
   * TrainStep.
   *
   * <p>\param[in] session The training session which has working knowledge of the eval model.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(ResetGrad, _Inout_ OrtTrainingSession* session);
   */

  /**
   * Ensures the gradients are reset to zero before the next call to {@link #trainStep}.
   *
   * <p>Note this is a lazy call, the gradients are cleared as part of running the next {@link
   * #trainStep} and not before.
   *
   * @throws OrtException If the native call failed.
   */
  public void lazyResetGrad() throws OrtException {
    checkClosed();
    lazyResetGrad(OnnxRuntime.ortApiHandle, OnnxRuntime.ortTrainingApiHandle, nativeHandle);
  }

  private native void lazyResetGrad(long apiHandle, long trainingHandle, long nativeHandle)
      throws OrtException;

  /**
   * Sets the RNG seed used by ONNX Runtime.
   *
   * <p>Note this setting is global across OrtTrainingSession instances.
   *
   * @param seed The RNG seed.
   * @throws OrtException If the native call failed.
   */
  public static void setSeed(long seed) throws OrtException {
    setSeed(OnnxRuntime.ortApiHandle, OnnxRuntime.ortTrainingApiHandle, seed);
  }

  private static native void setSeed(long apiHandle, long trainingHandle, long seed)
      throws OrtException;

  /**
   * \brief Computes the outputs and the gradients for the training model for the given inputs
   *
   * <p>This function performs a training step that computes the outputs and the gradients of the
   * training model for the given inputs. The train step is performed based on the training model
   * that was provided to the training session. The gradients computed are stored inside the
   * training session so they can be later consumed by the OptimizerStep function.
   *
   * <p>\param[in] sess The training session which has working knowledge of the eval model.
   * \param[in] run_options Run options for this training step. \param[in] inputs_len Number of user
   * inputs to the training model. \param[in] inputs The user inputs to the training model.
   * \param[in] outputs_len Number of user outputs expected from this training step. \param[out]
   * outputs User outputs computed by train step.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(TrainStep, _Inout_ OrtTrainingSession* sess, _In_opt_ const OrtRunOptions*
   * run_options, size_t inputs_len, _In_reads_(inputs_len) const OrtValue* const* inputs, size_t
   * outputs_len, _Inout_updates_all_(outputs_len) OrtValue** outputs);
   */
  public OrtSession.Result trainStep(Map<String, ? extends OnnxTensorLike> inputs)
      throws OrtException {
    return trainStep(inputs, trainOutputNames, null);
  }

  public OrtSession.Result trainStep(
      Map<String, ? extends OnnxTensorLike> inputs, OrtSession.RunOptions runOptions)
      throws OrtException {
    return trainStep(inputs, trainOutputNames, runOptions);
  }

  public OrtSession.Result trainStep(
      Map<String, ? extends OnnxTensorLike> inputs, Set<String> outputs) throws OrtException {
    return trainStep(inputs, outputs, null);
  }

  public OrtSession.Result trainStep(
      Map<String, ? extends OnnxTensorLike> inputs,
      Set<String> requestedOutputs,
      OrtSession.RunOptions runOptions)
      throws OrtException {
    checkClosed();
    /*
    if ((inputs.isEmpty() && (numInputs != 0)) || (inputs.size() > numInputs)) {
        throw new OrtException(
                "Unexpected number of inputs, expected [1," + numInputs + ") found " + inputs.size());
    }
     */
    if (requestedOutputs.isEmpty() || (requestedOutputs.size() > trainOutputNames.size())) {
      throw new OrtException(
          "Unexpected number of requestedOutputs, expected [1,"
              + trainOutputNames.size()
              + ") found "
              + requestedOutputs.size());
    }
    String[] inputNamesArray = new String[inputs.size()];
    long[] inputHandles = new long[inputs.size()];
    int i = 0;
    for (Map.Entry<String, ? extends OnnxTensorLike> t : inputs.entrySet()) {
      /*
      if (inputNames.contains(t.getKey())) {
       */
      inputNamesArray[i] = t.getKey();
      inputHandles[i] = t.getValue().getNativeHandle();
      i++;
      /*
      } else {
          throw new OrtException(
                  "Unknown input name " + t.getKey() + ", expected one of " + inputNames.toString());
      }
           */
    }
    String[] outputNamesArray = new String[requestedOutputs.size()];
    i = 0;
    for (String s : requestedOutputs) {
      if (trainOutputNames.contains(s)) {
        outputNamesArray[i] = s;
        i++;
      } else {
        throw new OrtException(
            "Unknown output name " + s + ", expected one of " + trainOutputNames.toString());
      }
    }
    long runOptionsHandle = runOptions == null ? 0 : runOptions.nativeHandle;

    OnnxValue[] outputValues =
        trainStep(
            OnnxRuntime.ortApiHandle,
            OnnxRuntime.ortTrainingApiHandle,
            nativeHandle,
            allocator.handle,
            inputNamesArray,
            inputHandles,
            inputNamesArray.length,
            outputNamesArray,
            outputNamesArray.length,
            runOptionsHandle);
    return new Result(outputNamesArray, outputValues);
  }

  private native OnnxValue[] trainStep(
      long apiHandle,
      long trainingApiHandle,
      long nativeHandle,
      long allocatorHandle,
      String[] inputNamesArray,
      long[] inputs,
      long numInputs,
      String[] outputNamesArray,
      long numOutputs,
      long runOptionsHandle);

  /**
   * \brief Computes the outputs for the eval model for the given inputs
   *
   * <p>This function performs an eval step that computes the outputs of the eval model for the
   * given inputs. The eval step is performed based on the eval model that was provided to the
   * training session.
   *
   * <p>\param[in] sess The training session which has working knowledge of the eval model.
   * \param[in] run_options Run options for this eval step. \param[in] inputs_len Number of user
   * inputs to the eval model. \param[in] inputs The user inputs to the eval model. \param[in]
   * outputs_len Number of user outputs expected from this eval step. \param[out] outputs User
   * outputs computed by eval step.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(EvalStep, _In_ const OrtTrainingSession* sess, _In_opt_ const OrtRunOptions*
   * run_options, size_t inputs_len, _In_reads_(inputs_len) const OrtValue* const* inputs, size_t
   * outputs_len, _Inout_updates_all_(outputs_len) OrtValue** outputs);
   */
  public OrtSession.Result evalStep(Map<String, ? extends OnnxTensorLike> inputs)
      throws OrtException {
    return evalStep(inputs, evalOutputNames, null);
  }

  public OrtSession.Result evalStep(
      Map<String, ? extends OnnxTensorLike> inputs, OrtSession.RunOptions runOptions)
      throws OrtException {
    return evalStep(inputs, evalOutputNames, runOptions);
  }

  public OrtSession.Result evalStep(
      Map<String, ? extends OnnxTensorLike> inputs, Set<String> outputs) throws OrtException {
    return evalStep(inputs, outputs, null);
  }

  public OrtSession.Result evalStep(
      Map<String, ? extends OnnxTensorLike> inputs,
      Set<String> requestedOutputs,
      OrtSession.RunOptions runOptions)
      throws OrtException {
    checkClosed();
    /*
    if ((inputs.isEmpty() && (numInputs != 0)) || (inputs.size() > numInputs)) {
        throw new OrtException(
                "Unexpected number of inputs, expected [1," + numInputs + ") found " + inputs.size());
    }
     */
    if (requestedOutputs.isEmpty() || (requestedOutputs.size() > evalOutputNames.size())) {
      throw new OrtException(
          "Unexpected number of requestedOutputs, expected [1,"
              + evalOutputNames.size()
              + ") found "
              + requestedOutputs.size());
    }
    String[] inputNamesArray = new String[inputs.size()];
    long[] inputHandles = new long[inputs.size()];
    int i = 0;
    for (Map.Entry<String, ? extends OnnxTensorLike> t : inputs.entrySet()) {
      /*
      if (inputNames.contains(t.getKey())) {
       */
      inputNamesArray[i] = t.getKey();
      inputHandles[i] = t.getValue().getNativeHandle();
      i++;
      /*
      } else {
          throw new OrtException(
                  "Unknown input name " + t.getKey() + ", expected one of " + inputNames.toString());
      }
           */
    }
    String[] outputNamesArray = new String[requestedOutputs.size()];
    i = 0;
    for (String s : requestedOutputs) {
      if (evalOutputNames.contains(s)) {
        outputNamesArray[i] = s;
        i++;
      } else {
        throw new OrtException(
            "Unknown output name " + s + ", expected one of " + evalOutputNames.toString());
      }
    }
    long runOptionsHandle = runOptions == null ? 0 : runOptions.nativeHandle;

    OnnxValue[] outputValues =
        evalStep(
            OnnxRuntime.ortApiHandle,
            OnnxRuntime.ortTrainingApiHandle,
            nativeHandle,
            allocator.handle,
            inputNamesArray,
            inputHandles,
            inputNamesArray.length,
            outputNamesArray,
            outputNamesArray.length,
            runOptionsHandle);
    return new Result(outputNamesArray, outputValues);
  }

  private native OnnxValue[] evalStep(
      long apiHandle,
      long trainingApiHandle,
      long nativeHandle,
      long allocatorHandle,
      String[] inputNamesArray,
      long[] inputs,
      long numInputs,
      String[] outputNamesArray,
      long numOutputs,
      long runOptionsHandle)
      throws OrtException;

  /**
   * \brief Sets the learning rate for this training session.
   *
   * <p>This function allows users to set the learning rate for the training session. The current
   * learning rate is maintained by the training session and can be overwritten by invoking this
   * function with the desired learning rate. This function should not be used when a valid learning
   * rate scheduler is registered. It should be used either to set the learning rate derived from a
   * custom learning rate scheduler or to set the learning rate constant to be used throughout the
   * training session. Please note that this function does not set the initial learning rate that
   * may be needed by the predefined learning rate schedulers. To set the initial learning rate for
   * learning rate schedulers, please look at the function `RegisterLinearLRScheduler`.
   *
   * <p>\param[in] sess The training session on which the learning rate needs to be set. \param[in]
   * learning_rate Desired learning rate to set.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(SetLearningRate, _Inout_ OrtTrainingSession* sess, _In_ float
   * learning_rate);
   */

  /**
   * Sets the learning rate for the training session.
   *
   * <p>Should be used only when there is no learning rate scheduler in the session. Not used to set
   * the initial learning rate for LR schedulers.
   *
   * @param learningRate The learning rate.
   * @throws OrtException If the call failed.
   */
  public void setLearningRate(float learningRate) throws OrtException {
    checkClosed();
    setLearningRate(
        OnnxRuntime.ortApiHandle, OnnxRuntime.ortTrainingApiHandle, nativeHandle, learningRate);
  }

  private native void setLearningRate(
      long apiHandle, long trainingApiHandle, long nativeHandle, float learningRate)
      throws OrtException;

  /**
   * \brief Gets the current learning rate for this training session.
   *
   * <p>This function allows users to get the learning rate for the training session. The current
   * learning rate is maintained by the training session
   *
   * <p>\param[in] sess The training session on which the learning rate needs to be set.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(GetLearningRate, _Inout_ OrtTrainingSession* sess, _Out_ float*
   * learning_rate);
   */

  /**
   * Gets the current learning rate for this training session.
   *
   * @return The current learning rate.
   * @throws OrtException If the call failed.
   */
  public float getLearningRate() throws OrtException {
    checkClosed();
    return getLearningRate(
        OnnxRuntime.ortApiHandle, OnnxRuntime.ortTrainingApiHandle, nativeHandle);
  }

  private native float getLearningRate(long apiHandle, long trainingApiHandle, long nativeHandle);

  /**
   * \brief Performs the weight updates for the trainable parameters using the optimizer model.
   *
   * <p>This function performs the weight update step that updates the trainable parameters such
   * that they take a step in the direction of their gradients. The optimizer step is performed
   * based on the optimizer model that was provided to the training session. The updated parameters
   * are stored inside the training session so that they can be used by the next TrainStep function
   * call.
   *
   * <p>\param[in] sess The training session which has working knowledge of the optimizer model.
   * \param[in] run_options Run options for this eval step.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(OptimizerStep, _Inout_ OrtTrainingSession* sess, _In_opt_ const
   * OrtRunOptions* run_options);
   */

  /**
   * Applies the gradient updates to the trainable parameters using the optimizer model.
   *
   * @throws OrtException If the native call failed.
   */
  public void optimizerStep() throws OrtException {
    optimizerStep(null);
  }

  /**
   * Applies the gradient updates to the trainable parameters using the optimizer model.
   *
   * <p>The run options can be used to control logging and to terminate the call early.
   *
   * @param runOptions Options for controlling the model execution.
   * @throws OrtException If the native call failed.
   */
  public void optimizerStep(OrtSession.RunOptions runOptions) throws OrtException {
    checkClosed();
    long runOptionsHandle = runOptions == null ? 0 : runOptions.nativeHandle;
    optimizerStep(
        OnnxRuntime.ortApiHandle, OnnxRuntime.ortTrainingApiHandle, nativeHandle, runOptionsHandle);
  }

  private native void optimizerStep(
      long apiHandle, long trainingApiHandle, long nativeHandle, long runOptionsHandle)
      throws OrtException;

  /**
   * \brief Registers the use of the Linear learning rate scheduler for the training session.
   *
   * <p>Register a Linear learning rate scheduler with the given learning rate scheduler parameters.
   * Specify the initial learning rate that should be used with this learning rate scheduler and
   * training session.
   *
   * <p>\param[in] sess The training session that should use the linear learning rate scheduler.
   * \param[in] warmup_step_count Warmup steps for LR warmup. \param[in] total_step_count Total step
   * count. \param[in] initial_lr The initial learning rate to be used by the training session.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(RegisterLinearLRScheduler, _Inout_ OrtTrainingSession* sess, _In_ const
   * int64_t warmup_step_count, _In_ const int64_t total_step_count, _In_ const float initial_lr);
   */

  /**
   * Registers a linear learning rate scheduler with linear warmup.
   *
   * @param warmupSteps The number of steps to increase the learning rate from zero to {@code
   *     initialLearningRate}.
   * @param totalSteps The total number of steps this scheduler operates over.
   * @param initialLearningRate The maximum learning rate.
   * @throws OrtException If the native call failed.
   */
  public void registerLinearLRScheduler(
      long warmupSteps, long totalSteps, float initialLearningRate) throws OrtException {
    registerLinearLRScheduler(
        OnnxRuntime.ortApiHandle,
        OnnxRuntime.ortTrainingApiHandle,
        nativeHandle,
        warmupSteps,
        totalSteps,
        initialLearningRate);
  }

  private native void registerLinearLRScheduler(
      long apiHandle,
      long trainingApiHandle,
      long nativeHandle,
      long warmupSteps,
      long totalSteps,
      float initialLearningRate)
      throws OrtException;

  /**
   * \brief Update the learning rate based on the registered learing rate scheduler.
   *
   * <p>Takes a scheduler step that updates the learning rate that is being used by the training
   * session. This function should typically be called before invoking the optimizer step for each
   * round, or as determined necessary to update the learning rate being used by the training
   * session. Please note that a valid predefined learning rate scheduler must be first registered
   * to invoke this function.
   *
   * <p>\param[in] sess The training session that has the registered learning rate scheduler.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(SchedulerStep, _Inout_ OrtTrainingSession* sess);
   */

  /**
   * Updates the learning rate based on the registered learning rate scheduler.
   *
   * @throws OrtException If the native call failed.
   */
  public void schedulerStep() throws OrtException {
    checkClosed();
    schedulerStep(OnnxRuntime.ortApiHandle, OnnxRuntime.ortTrainingApiHandle, nativeHandle);
  }

  private native void schedulerStep(long apiHandle, long trainingApiHandle, long nativeHandle)
      throws OrtException;

  /**
   * \brief Retrieves the size of all the parameters.
   *
   * <p>Calculates the size of all the parameters for the training session. When 'trainable_only' is
   * true, the size is calculated for trainable params only.
   *
   * <p>\param[in] sess The training session. \param[in] trainable_only Whether to skip
   * non-trainable parameters
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(GetParametersSize, _Inout_ OrtTrainingSession* sess, _Out_ size_t* out, bool
   * trainable_only);
   */

  /**
   * Calculates the size of all parameters in the training session.
   *
   * <p>When {@code trainableOnly} is true, it only computes the trainable parameter size.
   *
   * @param trainableOnly If true only return the size of the trainable parameters.
   * @return The parameter size.
   * @throws OrtException If the call failed public long getParametersSize(boolean trainableOnly)
   *     throws OrtException { checkClosed(); return getParametersSize(OnnxRuntime.ortApiHandle,
   *     OnnxRuntime.ortTrainingApiHandle, nativeHandle, trainableOnly); }
   *     <p>private native long getParametersSize(long apiHandle, long trainingApiHandle, long
   *     nativeHandle, boolean trainableOnly) throws OrtException;
   */

  /**
   * \brief Copy parameters onto contiguous buffer held by parameters_buffer
   *
   * <p>The parameters_buffer has to be of the size given by GetParametersSize api call, with
   * matching setting for 'trainable_only'. All the target parameters must be of the same datatype.
   * The OrtValue must be pre-allocated onto the desired device. This is a complementary function to
   * 'CopyBufferToParameters'. Parameter ordering is preserved. User is responsible for
   * allocating/freeing the 'parameters_buffer'.
   *
   * <p>\param[in] sess The training session. \param[in] trainable_only Whether to skip
   * non-trainable parameters \param[out] parameters_buffer The pre-allocated OrtValue buffer to
   * copy onto.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(CopyParametersToBuffer, _Inout_ OrtTrainingSession* sess, _Inout_ OrtValue*
   * parameters_buffer, bool trainable_only);
   */

  /**
   * \brief Copy parameter values from contiguous buffer held by parameters_buffer onto parameters
   *
   * <p>The parameters_buffer has to be of the size given by GetParametersSize api call, with
   * matching setting for 'trainable_only'. All the target parameters must be of the same datatype.
   * This is a complementary function to 'CopyBufferToParameters' and can be used to load updated
   * buffer values onto the parameters. Parameter ordering is preserved. User is responsible for
   * allocating/freeing the 'parameters_buffer'.
   *
   * <p>\param[in] sess The training session. \param[in] trainable_only Whether to skip
   * non-trainable parameters \param[out] parameters_buffer The pre-allocated OrtValue buffer to
   * copy from.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(CopyBufferToParameters, _Inout_ OrtTrainingSession* sess, _Inout_ OrtValue*
   * parameters_buffer, bool trainable_only);
   */

  /**
   * \brief Export a model that can be used for inferencing.
   *
   * <p>If the training session was provided with an eval model, the training session can generate
   * an inference model if it knows the inference graph outputs. The input inference graph outputs
   * are used to prune the eval model so that the output model's outputs align with the provided
   * outputs. The exported model is saved at the path provided and can be used for inferencing with
   * InferenceSession. Note that the function re-loads the eval model from the path provided to
   * CreateTrainingSession and expects that this path still be valid.
   *
   * <p>\param[in] sess The training session. \param[in] inference_model_path Path where the
   * inference model should be serialized to.
   *
   * <p>\snippet{doc} snippets.dox OrtStatus Return Value
   *
   * <p>ORT_API2_STATUS(ExportModelForInferencing, _Inout_ OrtTrainingSession* sess, _In_ const
   * ORTCHAR_T* inference_model_path, size_t graph_outputs_len, _In_reads_(graph_outputs_len) const
   * char* const* graph_output_names);
   */

  /**
   * Exports the evaluation model as a model suitable for inference, setting the desired nodes as
   * output nodes.
   *
   * <p>Note that this method reloads the evaluation model from the path provided to the training
   * session, and this path must still be valid.
   *
   * @param outputPath The path to write out the inference model.
   * @param outputNames The names of the output nodes.
   * @throws OrtException If the native call failed.
   */
  public void exportModelForInference(Path outputPath, String[] outputNames) throws OrtException {
    checkClosed();
    if (outputNames.length == 0) {
      throw new IllegalArgumentException("Requires at least one output name");
    }
    String outputStr = outputPath.toString();
    exportModelForInference(
        OnnxRuntime.ortApiHandle,
        OnnxRuntime.ortTrainingApiHandle,
        nativeHandle,
        outputStr,
        outputNames.length,
        outputNames);
  }

  private native void exportModelForInference(
      long apiHandle,
      long trainingApiHandle,
      long nativeHandle,
      String outputPath,
      long numOutputs,
      String[] outputNames)
      throws OrtException;

  /** Wrapper class for the checkpoint state. */
  static final class OrtCheckpointState implements AutoCloseable {
    final long nativeHandle;

    /**
     * Wraps an object around the checkpoint native handle.
     *
     * @param nativeHandle The pointer to the checkpoint.
     */
    OrtCheckpointState(long nativeHandle) {
      this.nativeHandle = nativeHandle;
    }

    /**
     * Loads a checkpoint from disk.
     *
     * @param checkpointPath The path to load
     * @return The checkpoint.
     * @throws OrtException If the checkpoint failed to load.
     */
    static OrtCheckpointState loadCheckpoint(Path checkpointPath) throws OrtException {
        String pathStr = checkpointPath.toString();
        return loadCheckpoint(pathStr);
    }

    /**
     * Loads a checkpoint from disk.
     *
     * @param checkpoint The path to load
     * @return The checkpoint.
     * @throws OrtException If the checkpoint failed to load.
     */
    static OrtCheckpointState loadCheckpoint(String checkpoint) throws OrtException {
      if (OnnxRuntime.trainingEnabled) {
        return new OrtCheckpointState(
            loadCheckpoint(OnnxRuntime.ortApiHandle, OnnxRuntime.ortTrainingApiHandle, checkpoint));
      } else {
        throw new IllegalStateException("Training is not enabled in this build of ONNX Runtime.");
      }
    }

    @Override
    public void close() {
      close(OnnxRuntime.ortTrainingApiHandle, nativeHandle);
    }

    /**
     * \brief Load a checkpoint state from directory on disk into checkpoint_state.
     *
     * <p>This function will parse a checkpoint directory, pull relevant files and load the training
     * states into the checkpoint_state. This checkpoint state can then be used to create the
     * training session by invoking CreateTrainingSession. By doing so, the training session will
     * resume training from the given checkpoint.
     *
     * <p>\param[in] checkpoint_path Path to the checkpoint directory \param[out] checkpoint_state
     * Checkpoint states that contains the states of the training session.
     *
     * <p>\snippet{doc} snippets.dox OrtStatus Return Value
     *
     * <p>ORT_API2_STATUS(LoadCheckpoint, _In_ const ORTCHAR_T* checkpoint_path, _Outptr_
     * OrtCheckpointState** checkpoint_state);
     */
    private static native long loadCheckpoint(long apiHandle, long trainingApiHandle, String path)
        throws OrtException;

    private native void close(long trainingApiHandle, long nativeHandle);
  }
}
