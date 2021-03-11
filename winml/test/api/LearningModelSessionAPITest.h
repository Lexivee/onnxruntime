// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "test.h"

struct LearningModelSessionAPITestsApi {
  SetupClass LearningModelSessionAPITestsClassSetup;
  VoidTest CreateSessionDeviceDefault;
  VoidTest CreateSessionDeviceCpu;
  VoidTest CreateSessionWithModelLoadedFromStream;
  VoidTest CreateSessionDeviceDirectX;
  VoidTest CreateSessionDeviceDirectXHighPerformance;
  VoidTest CreateSessionDeviceDirectXMinimumPower;
  VoidTest AdapterIdAndDevice;
  VoidTest EvaluateFeatures;
  VoidTest EvaluateFeaturesAsync;
  VoidTest EvaluationProperties;
  VoidTest CreateSessionWithCastToFloat16InModel;
  VoidTest CreateSessionWithFloat16InitializersInModel;
  VoidTest EvaluateSessionAndCloseModel;
  VoidTest OverrideNamedDimension;
  VoidTest CloseSession;
  VoidTest SetIntraOpNumThreads;
  VoidTest ModelBuilding_Gemm;
  VoidTest ModelBuilding_StandardDeviationNormalization;
  VoidTest ModelBuilding_DynamicMatmul;
  VoidTest ModelBuilding_ConstantMatmul;
  VoidTest ModelBuilding_DiscreteFourierTransform;
  VoidTest ModelBuilding_DiscreteFourierTransformInverseIdentity;
  VoidTest ModelBuilding_HannWindow;
  VoidTest ModelBuilding_HammingWindow;
  VoidTest ModelBuilding_BlackmanWindow;
  VoidTest ModelBuilding_STFT;
  VoidTest ModelBuilding_MelSpectrogramOnThreeToneSignal;
  VoidTest ModelBuilding_MelWeightMatrix;
};
const LearningModelSessionAPITestsApi& getapi();

WINML_TEST_CLASS_BEGIN(LearningModelSessionAPITests)
WINML_TEST_CLASS_SETUP_CLASS(LearningModelSessionAPITestsClassSetup)
WINML_TEST_CLASS_BEGIN_TESTS
WINML_TEST(LearningModelSessionAPITests, CreateSessionDeviceDefault)
WINML_TEST(LearningModelSessionAPITests,CreateSessionDeviceCpu)
WINML_TEST(LearningModelSessionAPITests,CreateSessionWithModelLoadedFromStream)
WINML_TEST(LearningModelSessionAPITests,EvaluateFeatures)
WINML_TEST(LearningModelSessionAPITests,EvaluateFeaturesAsync)
WINML_TEST(LearningModelSessionAPITests,EvaluationProperties)
WINML_TEST(LearningModelSessionAPITests,EvaluateSessionAndCloseModel)
WINML_TEST(LearningModelSessionAPITests, CreateSessionDeviceDirectX)
WINML_TEST(LearningModelSessionAPITests, CreateSessionDeviceDirectXHighPerformance)
WINML_TEST(LearningModelSessionAPITests, CreateSessionDeviceDirectXMinimumPower)
WINML_TEST(LearningModelSessionAPITests, CreateSessionWithCastToFloat16InModel)
WINML_TEST(LearningModelSessionAPITests, CreateSessionWithFloat16InitializersInModel)
WINML_TEST(LearningModelSessionAPITests, AdapterIdAndDevice)
WINML_TEST(LearningModelSessionAPITests, OverrideNamedDimension)
WINML_TEST(LearningModelSessionAPITests, CloseSession)
WINML_TEST(LearningModelSessionAPITests, SetIntraOpNumThreads)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_Gemm)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_StandardDeviationNormalization)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_DynamicMatmul)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_ConstantMatmul)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_DiscreteFourierTransform)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_DiscreteFourierTransformInverseIdentity)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_HannWindow)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_HammingWindow)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_BlackmanWindow)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_STFT)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_MelSpectrogramOnThreeToneSignal)
WINML_TEST(LearningModelSessionAPITests, ModelBuilding_MelWeightMatrix)
WINML_TEST_CLASS_END()
