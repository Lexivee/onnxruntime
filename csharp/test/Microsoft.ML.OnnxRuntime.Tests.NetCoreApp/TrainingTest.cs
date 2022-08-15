﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

using Microsoft.ML.OnnxRuntime.Tensors;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Xunit;
using Xunit.Abstractions;

// This runs in a separate package built from EndToEndTests
// and for this reason it can not refer to non-public members
// of Onnxruntime package
namespace Microsoft.ML.OnnxRuntime.Tests
{
    public partial class TrainingTest
    {
        private readonly ITestOutputHelper output;

        public TrainingTest(ITestOutputHelper o)
        {
            this.output = o;
        }

        [Fact(DisplayName = "TestLoadCheckpoint")]
        public void TestLoadCheckpoint()
        {
            using (var opt = new CheckpointState())
            {
                Assert.NotNull(opt);
                string path = Path.Combine(Directory.GetCurrentDirectory(), "checkpoint.ckpt");
                opt.LoadCheckpoint(path);
            }
        }

        [Fact(DisplayName = "TestCreateTrainingSession")]
        public void TestCreateTrainingSession()
        {
            using (var state = new CheckpointState())
            {
                Assert.NotNull(state);
                string checkpointPath = Path.Combine(Directory.GetCurrentDirectory(), "checkpoint.ckpt");
                state.LoadCheckpoint(checkpointPath);
                string trainingPath = Path.Combine(Directory.GetCurrentDirectory(), "training_model.onnx");

                var trainingSession = new TrainingSession(state, trainingPath);
            }
        }

        [Fact(DisplayName = "TestTrainingSessionTrainStep")]
        public void TestTrainingSessionTrainStep()
        {
            using (var state = new CheckpointState())
            {
                Assert.NotNull(state);
                string checkpointPath = Path.Combine(Directory.GetCurrentDirectory(), "checkpoint.ckpt");
                state.LoadCheckpoint(checkpointPath);
                string trainingPath = Path.Combine(Directory.GetCurrentDirectory(), "training_model.onnx");

                var trainingSession = new TrainingSession(state, trainingPath);

                float[] expectedOutput = TestDataLoader.LoadTensorFromFile("loss_1.out");
                var expectedOutputDimensions = new int[] { 1 };
                float[] input = TestDataLoader.LoadTensorFromFile("input-0.in");
                Int32[] labels = { 1, 1 };

                // Run inference with pinned inputs and pinned outputs
                using (DisposableListTest<FixedBufferOnnxValue> pinnedInputs = new DisposableListTest<FixedBufferOnnxValue>(),
                                                            pinnedOutputs = new DisposableListTest<FixedBufferOnnxValue>())
                {
                    var memInfo = OrtMemoryInfo.DefaultInstance; // CPU

                    // Create inputs
                    long[] inputShape = { 2, 784 };
                    var byteSize = inputShape.Aggregate(1L, (a, b) => a * b) * sizeof(float);
                    pinnedInputs.Add(FixedBufferOnnxValue.CreateFromMemory<float>(memInfo, input,
                        TensorElementType.Float, inputShape, byteSize));

                    long[] labelsShape = { 2 };
                    byteSize = labelsShape.Aggregate(1L, (a, b) => a * b) * sizeof(Int32);
                    pinnedInputs.Add(FixedBufferOnnxValue.CreateFromMemory<Int32>(memInfo, labels,
                        TensorElementType.Int32, labelsShape, byteSize));


                    // Prepare output buffer
                    long[] outputShape = { };
                    byteSize = outputShape.Aggregate(1L, (a, b) => a * b) * sizeof(float);
                    float[] outputBuffer = new float[expectedOutput.Length];
                    pinnedOutputs.Add(FixedBufferOnnxValue.CreateFromMemory<float>(memInfo, outputBuffer,
                        TensorElementType.Float, outputShape, byteSize));

                    trainingSession.TrainStep(pinnedInputs, pinnedOutputs);
                    Assert.Equal(expectedOutput, outputBuffer, new FloatComparer());
                }
            }
        }

        void RunTrainStep(TrainingSession trainingSession)
        {
            float[] expectedOutput = TestDataLoader.LoadTensorFromFile("loss_1.out");
            var expectedOutputDimensions = new int[] { 1 };
            float[] input = TestDataLoader.LoadTensorFromFile("input-0.in");
            Int32[] labels = { 1, 1 };

            // Run inference with pinned inputs and pinned outputs
            using (DisposableListTest<FixedBufferOnnxValue> pinnedInputs = new DisposableListTest<FixedBufferOnnxValue>())
            {
                var memInfo = OrtMemoryInfo.DefaultInstance; // CPU

                // Create inputs
                long[] inputShape = { 2, 784 };
                var byteSize = inputShape.Aggregate(1L, (a, b) => a * b) * sizeof(float);
                pinnedInputs.Add(FixedBufferOnnxValue.CreateFromMemory<float>(memInfo, input,
                    TensorElementType.Float, inputShape, byteSize));

                long[] labelsShape = { 2 };
                byteSize = labelsShape.Aggregate(1L, (a, b) => a * b) * sizeof(Int32);
                pinnedInputs.Add(FixedBufferOnnxValue.CreateFromMemory<Int32>(memInfo, labels,
                    TensorElementType.Int32, labelsShape, byteSize));

                var outputs = trainingSession.TrainStep(pinnedInputs);
                var outputBuffer = outputs.ElementAtOrDefault(0);

                Assert.Equal("542.loss", outputBuffer.Name);
                Assert.Equal(OnnxValueType.ONNX_TYPE_TENSOR, outputBuffer.ValueType);
                Assert.Equal(TensorElementType.Float, outputBuffer.ElementType);

                var outLabelTensor = outputBuffer.AsTensor<float>();
                Assert.NotNull(outLabelTensor);
                Assert.Equal(expectedOutput, outLabelTensor, new FloatComparer());
            }
        }

        [Fact(DisplayName = "TestTrainingSessionTrainStepOrtOutput")]
        public void TestTrainingSessionTrainStepOrtOutput()
        {
            using (var state = new CheckpointState())
            {
                Assert.NotNull(state);
                string checkpointPath = Path.Combine(Directory.GetCurrentDirectory(), "checkpoint.ckpt");
                state.LoadCheckpoint(checkpointPath);
                string trainingPath = Path.Combine(Directory.GetCurrentDirectory(), "training_model.onnx");

                var trainingSession = new TrainingSession(state, trainingPath);

                RunTrainStep(trainingSession);
            }
        }


        [Fact(DisplayName = "TestSaveCheckpoint")]
        public void TestSaveCheckpoint()
        {
            using (var state = new CheckpointState())
            {
                Assert.NotNull(state);
                string path = Path.Combine(Directory.GetCurrentDirectory(), "checkpoint.ckpt");
                state.LoadCheckpoint(path);
                string trainingPath = Path.Combine(Directory.GetCurrentDirectory(), "training_model.onnx");
                var trainingSession = new TrainingSession(state, trainingPath);
                string savedCheckpointPath = Path.Combine(Directory.GetCurrentDirectory(), "saved_checkpoint.ckpt");
                trainingSession.SaveCheckpoint(savedCheckpointPath, false);
                state.LoadCheckpoint(savedCheckpointPath);
                var trainingSession_2 = new TrainingSession(state, trainingPath);
                RunTrainStep(trainingSession_2);
            }
        }

        [Fact(DisplayName = "TestTrainingSessionOptimizerStep")]
        public void TestTrainingSessionOptimizerStep()
        {
            using (var state = new CheckpointState())
            {
                Assert.NotNull(state);
                string checkpointPath = Path.Combine(Directory.GetCurrentDirectory(), "checkpoint.ckpt");
                state.LoadCheckpoint(checkpointPath);
                string trainingPath = Path.Combine(Directory.GetCurrentDirectory(), "training_model.onnx");
                string optimizerPath = Path.Combine(Directory.GetCurrentDirectory(), "adamw.onnx");

                var trainingSession = new TrainingSession(state, trainingPath, optimizerPath);

                float[] expectedOutput_1 = TestDataLoader.LoadTensorFromFile("loss_1.out");
                float[] expectedOutput_2 = TestDataLoader.LoadTensorFromFile("loss_2.out");
                var expectedOutputDimensions = new int[] { 1 };
                float[] input = TestDataLoader.LoadTensorFromFile("input-0.in");
                Int32[] labels = { 1, 1 };

                // Run train step with pinned inputs and pinned outputs
                using (DisposableListTest<FixedBufferOnnxValue> pinnedInputs = new DisposableListTest<FixedBufferOnnxValue>(),
                                                            pinnedOutputs = new DisposableListTest<FixedBufferOnnxValue>())
                {
                    var memInfo = OrtMemoryInfo.DefaultInstance; // CPU

                    // Create inputs
                    long[] inputShape = { 2, 784 };
                    var byteSize = inputShape.Aggregate(1L, (a, b) => a * b) * sizeof(float);
                    pinnedInputs.Add(FixedBufferOnnxValue.CreateFromMemory<float>(memInfo, input,
                        TensorElementType.Float, inputShape, byteSize));

                    long[] labelsShape = { 2 };
                    byteSize = labelsShape.Aggregate(1L, (a, b) => a * b) * sizeof(Int32);
                    pinnedInputs.Add(FixedBufferOnnxValue.CreateFromMemory<Int32>(memInfo, labels,
                        TensorElementType.Int32, labelsShape, byteSize));


                    // Prepare output buffer
                    long[] outputShape = { };
                    byteSize = outputShape.Aggregate(1L, (a, b) => a * b) * sizeof(float);
                    float[] outputBuffer = new float[expectedOutput_1.Length];
                    pinnedOutputs.Add(FixedBufferOnnxValue.CreateFromMemory<float>(memInfo, outputBuffer,
                        TensorElementType.Float, outputShape, byteSize));

                    trainingSession.TrainStep(pinnedInputs, pinnedOutputs);
                    Assert.Equal(expectedOutput_1, outputBuffer, new FloatComparer());

                    trainingSession.ResetGrad();

                    trainingSession.TrainStep(pinnedInputs, pinnedOutputs);
                    Assert.Equal(expectedOutput_1, outputBuffer, new FloatComparer());

                    trainingSession.OptimizerStep();

                    trainingSession.TrainStep(pinnedInputs, pinnedOutputs);
                    Assert.Equal(expectedOutput_2, outputBuffer, new FloatComparer());
                }

                trainingSession.Dispose();
            }
        }

        [Fact(DisplayName = "TestTrainingSessionSaveCheckpoint")]
        public void TestTrainingSessionSaveCheckpoint()
        {
            using (var state = new CheckpointState())
            {
                Assert.NotNull(state);
                string checkpointPath = Path.Combine(Directory.GetCurrentDirectory(), "checkpoint.ckpt");
                state.LoadCheckpoint(checkpointPath);
                string trainingPath = Path.Combine(Directory.GetCurrentDirectory(), "training_model.onnx");

                var trainingSession = new TrainingSession(state, trainingPath);

                string savedCheckpointPath = Path.Combine(Directory.GetCurrentDirectory(), "saved_checkpoint.ckpt");
                trainingSession.SaveCheckpoint(savedCheckpointPath);
                trainingSession.Dispose();

                var loadedState = new CheckpointState();
                loadedState.LoadCheckpoint(savedCheckpointPath);
                var newTrainingSession = new TrainingSession(loadedState, trainingPath);


                float[] expectedOutput_1 = TestDataLoader.LoadTensorFromFile("loss_1.out");
                var expectedOutputDimensions = new int[] { 1 };
                float[] input = TestDataLoader.LoadTensorFromFile("input-0.in");
                Int32[] labels = { 1, 1 };

                // Run train step with pinned inputs and pinned outputs
                using (DisposableListTest<FixedBufferOnnxValue> pinnedInputs = new DisposableListTest<FixedBufferOnnxValue>(),
                                                            pinnedOutputs = new DisposableListTest<FixedBufferOnnxValue>())
                {
                    var memInfo = OrtMemoryInfo.DefaultInstance; // CPU

                    // Create inputs
                    long[] inputShape = { 2, 784 };
                    var byteSize = inputShape.Aggregate(1L, (a, b) => a * b) * sizeof(float);
                    pinnedInputs.Add(FixedBufferOnnxValue.CreateFromMemory<float>(memInfo, input,
                        TensorElementType.Float, inputShape, byteSize));

                    long[] labelsShape = { 2 };
                    byteSize = labelsShape.Aggregate(1L, (a, b) => a * b) * sizeof(Int32);
                    pinnedInputs.Add(FixedBufferOnnxValue.CreateFromMemory<Int32>(memInfo, labels,
                        TensorElementType.Int32, labelsShape, byteSize));


                    // Prepare output buffer
                    long[] outputShape = { };
                    byteSize = outputShape.Aggregate(1L, (a, b) => a * b) * sizeof(float);
                    float[] outputBuffer = new float[expectedOutput_1.Length];
                    pinnedOutputs.Add(FixedBufferOnnxValue.CreateFromMemory<float>(memInfo, outputBuffer,
                        TensorElementType.Float, outputShape, byteSize));

                    newTrainingSession.TrainStep(pinnedInputs, pinnedOutputs);
                    Assert.Equal(expectedOutput_1, outputBuffer, new FloatComparer());
                }

                newTrainingSession.Dispose();
            }
        }

        internal class FloatComparer : IEqualityComparer<float>
        {
            private float atol = 1e-3f;
            private float rtol = 1.7e-2f;

            public bool Equals(float x, float y)
            {
                return Math.Abs(x - y) <= (atol + rtol * Math.Abs(y));
            }
            public int GetHashCode(float x)
            {
                return x.GetHashCode();
            }
        }
    }
}
