// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <random>
#include "test/common/tensor_op_test_utils.h"
#include "test/common/cuda_op_test_utils.h"
#include "test/framework/test_utils.h"
#include "test/providers/provider_test_utils.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

using namespace std;

namespace onnxruntime {
namespace test {

TEST(SkipGroupNormTest, SkipGroupNorm_with_bias) {
  constexpr int64_t B = 2;
  constexpr int64_t C = 16;
  constexpr int64_t H = 2;
  constexpr int64_t W = 2;

  std::vector<int64_t> dims_nhwc{B, H, W, C};
  std::vector<float> input_data_nhwc = {
      -0.768555f, 1.575195f, -0.698242f, 1.587891f, 0.371826f, -0.280029f, -1.328125f, 0.127197f,
      -0.197144f, 0.982422f, -0.671387f, -1.925781f, 1.800781f, -0.020218f, -0.782227f, 1.291992f,
      -0.935059f, 1.782227f, -0.674316f, -1.943359f, -0.218994f, 0.054138f, -1.539062f, -0.546387f,
      -2.160156f, 1.195312f, 1.653320f, -0.674316f, 0.224731f, -0.093262f, 1.160156f, -0.389404f,
      1.748047f, 0.766113f, 0.234375f, 0.011177f, -0.055847f, -0.930664f, -0.490234f, -0.655762f,
      -0.382568f, -0.554688f, 0.910645f, -0.227295f, 1.687500f, 0.028397f, -0.241699f, -0.480957f,
      -0.355713f, -2.095703f, -0.443359f, -0.126221f, -0.815918f, 0.792969f, -0.450439f, -0.952148f,
      -1.174805f, 0.242798f, 0.138550f, -0.237061f, -0.994141f, 0.346436f, 0.147705f, 0.125854f,
      -0.517090f, 0.253906f, 0.400146f, -0.540039f, -0.788574f, 0.146606f, -0.409668f, 0.281982f,
      1.444336f, 0.044434f, -0.366699f, 2.250000f, -0.453613f, -0.652344f, 1.828125f, -0.244751f,
      0.307129f, -0.051361f, 0.106384f, 0.844727f, 1.648438f, -0.904785f, -0.353760f, 0.510742f,
      0.074829f, -0.311279f, 0.274902f, 1.594727f, 1.367188f, 0.098755f, 0.043304f, -0.207397f,
      0.068298f, -0.601074f, 0.083008f, 0.264893f, -0.659180f, -0.216797f, -0.086548f, -0.683594f,
      -0.964844f, -2.591797f, -0.817383f, -0.461914f, -1.840820f, -0.712402f, -0.052094f, -0.583008f,
      1.114258f, 0.190308f, 1.087891f, 0.005146f, 1.041992f, 1.363281f, -0.273682f, -0.465576f,
      -0.027618f, 1.345703f, 0.789551f, -0.015991f, 0.401611f, 0.726562f, 0.598633f, 0.133667f};

  std::vector<float> gamma_data = {
      0.241255f, 0.556660f, -0.835532f, 0.564596f, -1.338308f, -0.278924f, 0.357326f, -1.745484f,
      0.277184f, 0.101415f, -0.018637f, -0.526188f, -0.011698f, -2.349411f, 0.206578f, 0.357679f};

  std::vector<float> beta_data = {
      -1.194839f, 0.209146f, -0.677225f, -0.547338f, 1.275685f, -1.099577f, 0.470916f, 0.293907f,
      -1.094209f, 2.350204f, -1.633769f, 0.248753f, -0.180166f, 0.365134f, -0.555731f, 1.843083f};

  std::vector<float> skip_data_nhwc = {
      0.892578f, -0.471924f, -0.423096f, 1.277344f, 0.257080f, -1.366211f, 1.552734f, 0.441406f,
      -0.033142f, -0.059418f, 1.536133f, -0.225464f, 1.472656f, 0.591309f, -0.386230f, -2.197266f,
      0.089600f, -0.256592f, -1.873047f, 0.916992f, 0.392090f, 0.015526f, -0.949219f, 0.566895f,
      -0.220459f, 1.262695f, -0.437744f, -2.283203f, -0.264893f, -0.660156f, 2.353516f, 1.992188f,
      0.865723f, -0.854004f, -1.014648f, 0.899414f, -1.041016f, 1.378906f, -0.075073f, -2.541016f,
      -0.883789f, -0.428711f, 0.981934f, -0.072754f, 2.214844f, 0.658203f, 0.170166f, -1.727539f,
      -0.672363f, -1.373047f, 0.318115f, 0.422363f, 0.260742f, -0.547852f, 0.545898f, -0.155762f,
      0.679688f, 2.861328f, -0.300781f, -0.504883f, 1.548828f, 0.353760f, -0.387695f, -1.595703f,
      -0.170166f, -0.002897f, 0.273193f, -0.383545f, -1.082031f, -0.894043f, -1.048828f, -0.044708f,
      0.049286f, 0.220215f, 0.272705f, -0.853027f, -0.489258f, 0.513672f, 0.977051f, 0.310547f,
      -0.577148f, -0.479004f, 0.838867f, 0.872559f, -0.510254f, 0.101807f, -0.299805f, -1.179688f,
      -1.555664f, 0.668457f, 0.939453f, 0.118103f, -0.376709f, 0.735352f, -0.214233f, -1.987305f,
      -0.931152f, 1.268555f, 1.427734f, -0.757812f, -1.324219f, 0.375488f, 1.364258f, -1.708008f,
      0.976562f, -0.037659f, -1.779297f, -0.196655f, 1.636719f, 0.690430f, 0.941895f, -1.882812f,
      0.431641f, 0.203857f, 1.306641f, -0.126343f, 1.408203f, 1.188477f, 0.432861f, -2.296875f,
      -0.475342f, 1.517578f, -0.824219f, 1.288086f, -0.028244f, 1.918945f, 0.352295f, 0.693359f};

  std::vector<float> bias_data = {
      -0.537598f, 0.500488f, -0.252441f, -0.460693f, -1.640625f, -1.298828f, 0.331787f, -1.588867f,
      1.000977f, 1.458984f, 0.702637f, 0.147827f, 1.143555f, 0.533691f, -0.072510f, 0.511230f};

  std::vector<float> norm_data_nhwc = {
      -1.213867f, 0.856445f, -0.119141f, 0.386475f, 0.714355f, -0.804688f,
      1.048828f, -0.426270f, -1.091797f, 2.435547f, -1.641602f, 0.989746f,
      -0.200928f, 0.267334f, -0.800781f, 1.577148f, -1.357422f, 1.000977f,
      0.613281f, -0.963867f, 1.179688f, -1.169922f, 0.308350f, 0.304199f,
      -1.396484f, 2.513672f, -1.644531f, 1.206055f, -0.180664f, 1.896484f,
      -0.294678f, 2.046875f, -0.844238f, 0.448486f, -0.294189f, -0.291504f,
      2.480469f, -1.250977f, 0.833008f, 4.593750f, -1.238281f, 2.335938f,
      -1.651367f, 0.491943f, -0.204834f, 0.125610f, -0.682129f, 1.333984f,
      -1.384766f, -0.708008f, -0.630859f, -0.504883f, 1.924805f, -1.208008f,
      1.013672f, 1.809570f, -1.128906f, 2.546875f, -1.631836f, 0.610840f,
      -0.184326f, 0.110046f, -0.700195f, 1.471680f, -1.511719f, 0.492188f,
      -0.847168f, -1.373047f, 2.837891f, -0.998047f, 0.521484f, 0.262207f,
      -0.810547f, 2.400391f, -1.628906f, 0.049896f, -0.174927f, 1.076172f,
      -0.252197f, 1.784180f, -1.418945f, 0.090820f, -1.056641f, 0.002945f,
      0.627441f, -0.989746f, 0.679199f, 1.130859f, -1.371094f, 2.408203f,
      -1.645508f, -0.062988f, -0.192017f, -0.655762f, -0.718262f, 1.170898f,
      -1.550781f, 0.706055f, -1.492188f, -1.148438f, 2.921875f, -1.136719f,
      1.058594f, 2.781250f, -1.089844f, 2.201172f, -1.597656f, 0.785645f,
      -0.181396f, 0.868164f, -0.552246f, 1.097656f, -1.015625f, 0.565430f,
      -2.173828f, -0.955078f, -0.336426f, -1.503906f, 0.838867f, 3.136719f,
      -1.186523f, 2.580078f, -1.629883f, 0.094604f, -0.186523f, -3.884766f,
      -0.542480f, 1.990234f};

  std::vector<float> add_out_data_nhwc = {
      -0.414062f, 1.604492f, -1.374023f, 2.404297f, -1.011719f, -2.945312f, 0.556641f, -1.020508f,
      0.770508f, 2.382812f, 1.567383f, -2.003906f, 4.417969f, 1.105469f, -1.240234f, -0.394531f,
      -1.382812f, 2.027344f, -2.800781f, -1.487305f, -1.466797f, -1.229492f, -2.156250f, -1.568359f,
      -1.379883f, 3.917969f, 1.917969f, -2.808594f, 1.103516f, -0.219727f, 3.441406f, 2.113281f,
      2.076172f, 0.412598f, -1.033203f, 0.449951f, -2.738281f, -0.851562f, -0.233521f, -4.785156f,
      -0.265625f, 0.475586f, 2.595703f, -0.152222f, 5.046875f, 1.220703f, -0.144043f, -1.697266f,
      -1.566406f, -2.968750f, -0.377686f, -0.164551f, -2.195312f, -1.053711f, 0.427246f, -2.697266f,
      0.505859f, 4.562500f, 0.540527f, -0.594238f, 1.698242f, 1.233398f, -0.312500f, -0.958496f,
      -1.224609f, 0.751465f, 0.420898f, -1.384766f, -3.511719f, -2.046875f, -1.126953f, -1.351562f,
      2.494141f, 1.724609f, 0.608398f, 1.544922f, 0.200684f, 0.395020f, 2.732422f, 0.577148f,
      -0.807617f, -0.029785f, 0.692871f, 1.256836f, -0.502441f, -2.101562f, -0.321777f, -2.257812f,
      -0.479492f, 1.816406f, 1.916992f, 1.860352f, 2.134766f, 1.367188f, -0.243408f, -1.683594f,
      -1.400391f, 1.167969f, 1.257812f, -0.953613f, -3.625000f, -1.140625f, 1.609375f, -3.980469f,
      1.012695f, -1.170898f, -1.894531f, -0.510742f, 0.939453f, 0.511719f, 0.817383f, -1.955078f,
      1.007812f, 0.894531f, 2.142578f, -0.582031f, 0.809570f, 1.252930f, 0.490967f, -4.351562f,
      0.497803f, 4.320312f, 0.667969f, 1.419922f, 1.516602f, 3.179688f, 0.878906f, 1.337891f};

  int min_cuda_architecture = 530;
  bool enable_cuda = HasCudaEnvironment(min_cuda_architecture);
  bool enable_rocm = (nullptr != DefaultRocmExecutionProvider().get());

  std::array<int, 2> channels_last_values = {-1, 1};

  for (const int channels_last : channels_last_values) {
    if (enable_cuda || enable_rocm) {
      std::vector<std::unique_ptr<IExecutionProvider>> execution_providers;
      if (enable_cuda && channels_last != 0) {
        execution_providers.push_back(DefaultCudaExecutionProvider());
      }

      if (enable_rocm && channels_last != 0) {
        execution_providers.push_back(DefaultRocmExecutionProvider());
      }

      // Don't run the test if no providers are supported
      if (execution_providers.empty()) {
        continue;
      }

      OpTester test("SkipGroupNorm", 1, onnxruntime::kMSDomain);
      test.AddAttribute<float>("epsilon", 1e-05f);
      test.AddAttribute<int64_t>("groups", 4);
      test.AddAttribute<int64_t>("activation", 0);

      // We interpret channels_last==-1 as the attribute not being provided
      if (channels_last != -1) {
        test.AddAttribute<int64_t>("channels_last", channels_last);
      }

      test.AddInput<MLFloat16>("X", dims_nhwc, ToFloat16(input_data_nhwc));
      test.AddInput<float>("gamma", {C}, gamma_data);
      test.AddInput<float>("beta", {C}, beta_data);
      test.AddInput<MLFloat16>("skip", dims_nhwc, ToFloat16(skip_data_nhwc));
      test.AddInput<MLFloat16>("bias", {C}, ToFloat16(bias_data));

      constexpr float rel_error = 0.0f;
      constexpr float abs_error = 0.02f;
      test.AddOutput<MLFloat16>("Y", dims_nhwc, ToFloat16(norm_data_nhwc), false, rel_error, abs_error);
      test.AddOutput<MLFloat16>("S", dims_nhwc, ToFloat16(add_out_data_nhwc), false, rel_error, abs_error);

      test.Run(OpTester::ExpectResult::kExpectSuccess, "", {}, nullptr, &execution_providers);
    }
  }
}

TEST(SkipGroupNormTest, SkipGroupNorm_no_bias_broadcast_skip) {
  constexpr int64_t B = 1;
  constexpr int64_t C = 64;
  constexpr int64_t H = 1;
  constexpr int64_t W = 1;

  std::vector<int64_t> dims_nhwc{B, H, W, C};
  std::vector<float> input_data_nhwc = {
      0.588867f, 0.896484f, -0.213623f, 0.803223f, 0.659180f, -0.216187f, 1.197266f, -0.486084f,
      -0.718750f, 0.332031f, -0.364746f, -0.831543f, -0.031219f, -1.059570f, 0.161621f, 1.519531f,
      0.169312f, 1.048828f, 1.330078f, 0.450195f, -2.867188f, -1.456055f, 0.708496f, -1.120117f,
      -1.208984f, -1.199219f, -1.505859f, -0.549316f, 0.505371f, 0.723145f, -0.359131f, -0.250977f,
      -0.879883f, -0.305664f, 0.709473f, 0.815430f, 0.617676f, -0.638672f, 0.066772f, -2.330078f,
      -1.316406f, 1.744141f, 1.122070f, -0.633789f, -1.802734f, -0.825684f, 0.622559f, -0.481689f,
      -1.364258f, -0.536621f, -0.464111f, 0.247437f, -0.213989f, 0.384521f, 0.556641f, -0.303711f,
      -0.160034f, 0.882324f, -0.212036f, -0.796387f, 0.153076f, -1.311523f, 2.212891f, 0.685059f};

  std::vector<float> gamma_data = {
      0.789682f, 0.869051f, -0.010169f, -0.021685f, 0.506611f, 1.267444f, -0.312695f, 0.877844f,
      0.598637f, 0.598314f, -1.721544f, -0.593328f, 0.986705f, -0.419391f, -0.852584f, -0.572351f,
      0.912797f, -0.586863f, 0.477761f, -0.484418f, -0.193835f, 0.347757f, 0.327637f, -1.100304f,
      1.233108f, -0.272569f, -0.688656f, 0.687245f, 0.398386f, 0.888089f, -0.792587f, -0.769029f,
      -0.427778f, 0.100768f, -2.187060f, 1.279301f, 1.109054f, 0.375992f, 1.514775f, 1.271436f,
      0.822896f, -0.476750f, 0.475507f, -1.011297f, 1.177197f, 1.586540f, -1.059944f, -0.145351f,
      0.841555f, -2.014113f, -0.230498f, 0.302128f, -0.180508f, 0.980534f, -0.126871f, 0.203151f,
      -0.754841f, 0.420570f, -1.085798f, 1.335042f, -0.674930f, 2.453507f, 2.139259f, 1.087436f};

  std::vector<float> beta_data = {
      -0.064518f, -0.262683f, 0.827528f, -0.960938f, 1.062519f, 2.417941f, 0.212789f, -1.638430f,
      1.875453f, -0.883058f, -0.006704f, 0.424894f, -0.869972f, 0.727008f, 0.879303f, -3.024141f,
      -2.610873f, 1.269641f, 0.883006f, 0.804167f, -1.510324f, 2.258091f, -0.006750f, -1.553668f,
      -1.659453f, 0.579603f, 0.652358f, 0.007077f, 0.099180f, 0.418658f, -0.273778f, -1.036199f,
      -1.128691f, -0.296022f, -0.224056f, 1.476306f, 0.577624f, -0.372049f, -0.581659f, -1.841807f,
      -0.361721f, 0.051160f, -0.749332f, -2.634807f, 0.562719f, -0.738667f, 0.024864f, -1.135937f,
      -1.368144f, -1.458886f, -0.946683f, 1.953936f, -1.198661f, 0.166648f, 0.447206f, -0.458140f,
      -0.553395f, 0.112900f, 0.255989f, -0.184551f, 1.254163f, -0.260479f, -1.232429f, 1.902575f};

  std::vector<float> skip_data = {
      0.952148f, 1.342773f, -0.172974f, -0.395264f, 1.119141f, 0.330566f,
      0.281494f, 0.472900f, -0.692871f, -0.634766f, 0.013504f, -1.866211f,
      -0.428223f, 0.669922f, -0.323486f, 0.713867f, -0.350586f, 0.659180f,
      -0.288574f, 0.324219f, -0.300781f, -0.789551f, -0.216431f, -0.221436f,
      -0.086670f, 0.366211f, -0.643555f, -0.977051f, 0.001021f, 0.415527f,
      -0.271729f, 0.836426f, 0.035370f, -0.806152f, 0.936035f, -0.021332f,
      -1.095703f, 0.971680f, 1.648438f, 0.840820f, 0.837402f, 0.607910f,
      -1.894531f, 0.666016f, -0.171143f, 1.625977f, -0.620117f, -0.039581f,
      1.702148f, -2.410156f, 1.565430f, -0.756348f, 1.446289f, 0.583496f,
      -0.497559f, -0.271729f, -0.956055f, -1.642578f, 0.833496f, -1.136719f,
      1.248047f, -2.515625f, 0.080383f, 0.376221f};

  std::vector<float> norm_data_nhwc = {
      0.494873f, 1.017578f, 0.841797f, -0.949219f, 1.552734f, 1.333984f, 0.012703f, -2.511719f,
      1.424805f, -0.818359f, -0.128418f, 1.462891f, -0.882812f, 0.709961f, 0.693848f, -4.210938f,
      -2.505859f, 0.513184f, 1.300781f, 0.460938f, -1.172852f, 1.851562f, 0.167969f, -0.885254f,
      -2.535156f, 0.656738f, 1.683594f, -0.627441f, 0.478271f, 1.782227f, -0.196777f, -1.824219f,
      -0.791016f, -0.398682f, -3.197266f, 2.275391f, 0.052704f, -0.286865f, 1.567383f, -3.552734f,
      -0.646973f, -0.927734f, -1.032227f, -2.722656f, -1.337891f, 0.432129f, -0.040253f, -1.080078f,
      -1.118164f, 3.123047f, -1.153320f, 1.843750f, -1.378906f, 0.941406f, 0.437256f, -0.542969f,
      -0.218872f, 0.006115f, -0.265869f, -1.356445f, 0.649902f, -4.882812f, 1.696289f, 2.679688f};

  std::vector<float> add_out_data_nhwc = {
      1.541016f, 2.238281f, -0.386719f, 0.407959f, 1.778320f, 0.114380f,
      1.478516f, -0.013184f, -1.412109f, -0.302734f, -0.351318f, -2.697266f,
      -0.459473f, -0.389648f, -0.161865f, 2.234375f, -0.181274f, 1.708008f,
      1.041016f, 0.774414f, -3.167969f, -2.246094f, 0.492188f, -1.341797f,
      -1.295898f, -0.833008f, -2.148438f, -1.526367f, 0.506348f, 1.138672f,
      -0.630859f, 0.585449f, -0.844727f, -1.111328f, 1.645508f, 0.793945f,
      -0.478027f, 0.333008f, 1.714844f, -1.489258f, -0.479004f, 2.351562f,
      -0.772461f, 0.032227f, -1.973633f, 0.800293f, 0.002441f, -0.521484f,
      0.337891f, -2.947266f, 1.101562f, -0.508789f, 1.232422f, 0.967773f,
      0.059082f, -0.575195f, -1.116211f, -0.760254f, 0.621582f, -1.933594f,
      1.401367f, -3.828125f, 2.292969f, 1.061523f};

  int min_cuda_architecture = 530;
  bool enable_cuda = HasCudaEnvironment(min_cuda_architecture);
  bool enable_rocm = (nullptr != DefaultRocmExecutionProvider().get());

  std::array<bool, 2> has_add_out_values = {true, false};
  std::array<int, 2> skip_dims = {2, 4};

  constexpr int channels_last = 1;
  for (const int skip_dim : skip_dims) {
    for (const bool has_add_out : has_add_out_values) {
      if (enable_cuda || enable_rocm) {
        std::vector<std::unique_ptr<IExecutionProvider>> execution_providers;
        if (enable_cuda && channels_last != 0) {
          execution_providers.push_back(DefaultCudaExecutionProvider());
        }

        if (enable_rocm && channels_last != 0) {
          execution_providers.push_back(DefaultRocmExecutionProvider());
        }

        // Don't run the test if no providers are supported
        if (execution_providers.empty()) {
          continue;
        }

        OpTester test("SkipGroupNorm", 1, onnxruntime::kMSDomain);
        test.AddAttribute<float>("epsilon", 1e-05f);
        test.AddAttribute<int64_t>("groups", 8);
        test.AddAttribute<int64_t>("activation", 0);

        // We interpret channels_last==-1 as the attribute not being provided
        if (channels_last != -1) {
          test.AddAttribute<int64_t>("channels_last", channels_last);
        }

        test.AddInput<MLFloat16>("X", dims_nhwc, ToFloat16(input_data_nhwc));
        test.AddInput<float>("gamma", {C}, gamma_data);
        test.AddInput<float>("beta", {C}, beta_data);
        if (skip_dim == 2) {
          test.AddInput<MLFloat16>("skip", {B, C}, ToFloat16(skip_data));
        } else {
          test.AddInput<MLFloat16>("skip", {B, 1, 1, C}, ToFloat16(skip_data));
        }
        // no bias

        constexpr float rel_error = 0.0f;
        constexpr float abs_error = 0.02f;
        test.AddOutput<MLFloat16>("Y", dims_nhwc, ToFloat16(norm_data_nhwc), false, rel_error, abs_error);

        if (has_add_out) {
          test.AddOutput<MLFloat16>("S", dims_nhwc, ToFloat16(add_out_data_nhwc), false, rel_error, abs_error);
        }

        test.Run(OpTester::ExpectResult::kExpectSuccess, "", {}, nullptr, &execution_providers);
      }
    }
  }
}

}  // namespace test
}  // namespace onnxruntime
