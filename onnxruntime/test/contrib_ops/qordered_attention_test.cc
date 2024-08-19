// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "test/contrib_ops/qordered_test_utils.h"

// The "Attention_WithData_ROW_ORDER", "MatMul_COL_16x64x32", "MatMul_COL_16x64x32_perchannel", "MatMul_addC_COL_16x64x32", "MatMul_addC_COL_16x64x32_perchannel", "MatMul_COL_16x64x32_b3_1", "MatMul_addC_COL_16x64x32_b2_1", "MatMul_addC_COL_16x64x32_b2_1_perchannel", "MatMul_addC_broadcastC_COL_16x64x32_b2_1" tests fails in Windows Orttraining build with errors like:
//"qkv_bias_const_cout_ == 3 && scale_qkv_weight_const_count_ == 3 && qkv_weight_const_count_ == 3 was false. qkv gemm weight and their scales, qkv gemm bias must all be constant!"

#if defined(USE_CUDA) && !defined(ENABLE_TRAINING_CORE)

#include <cuda.h>

#if defined(USE_CUDA)

namespace onnxruntime {
namespace test {

static constexpr size_t batch_size = 1;
static constexpr size_t sequence_len = 16;
static constexpr size_t input_hidden_size = 32;
static constexpr size_t num_heads = 2;
static constexpr size_t head_size = 16;
static constexpr size_t hidden_size = num_heads * head_size;

static std::array<int32_t, 16> input_mask = {  // [1, 16]
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};

static constexpr float input_scale = 0.025f;

static std::array<int8_t, 512> inputq = {  // [1, 16, 32]
    -33, 7, -54, 29, 14, 6, 14, 16, 1, 16, 22, 0, 16, 49, -14, -15, 68, 11, -18, -9, -42, 6, 6, 58, 22, 31, 0, -13, 42, 40, 4, 0,
    -47, -50, -14, -30, 12, -11, -33, 79, 13, -33, 65, 26, -16, 23, 3, -4, -7, -13, 32, 25, 4, -3, -13, -35, 67, 18, -43, 65, -18, 6, -10, 36,
    -10, 2, -51, 14, 98, 17, 71, 114, 75, -70, -17, 3, -22, -28, 34, 39, 78, -13, 5, -7, 21, -15, 12, -64, 8, -11, -9, -49, -6, 45, -52, 24,
    56, 13, -55, 4, -81, 49, -21, 72, 2, 33, -1, 26, -16, -15, -37, -27, 56, -57, 12, -51, 37, 11, 63, -33, -39, -5, -5, 8, -20, -45, -46, -10,
    41, -26, -20, 18, 0, 21, -2, -5, 57, 95, -27, -3, 12, -52, -62, -9, 3, -14, -34, -41, -24, 21, 50, -22, -56, -28, -46, 44, 0, 18, 24, 1,
    9, -50, -22, 23, -4, 45, 54, 12, 5, 50, -38, 12, -9, 27, -57, 32, -42, -30, -19, 47, 7, -21, -21, -8, 22, 32, 50, -126, 32, -6, -6, -13,
    26, -12, 12, 25, 33, -94, 20, 15, 41, 50, -4, -67, -16, -9, 84, -39, -35, 1, -57, 37, -25, -48, 34, 66, -15, 24, 27, -39, 12, 19, -29, 9,
    11, 18, -50, 0, 29, -10, -19, -3, 38, 9, 15, -5, 58, -20, -22, -27, 5, -39, -47, -31, 18, 55, 65, 22, -22, 35, 32, 2, 20, -74, 37, 19,
    -34, 22, 32, 41, -6, 36, 19, 14, 2, 39, -20, -3, 14, 45, 71, 23, -41, 61, 41, -28, 88, 13, -14, -18, 7, 54, 28, 54, 17, -24, 39, 58,
    0, -28, -39, -19, -67, 33, 21, -80, -38, 8, 16, -39, 62, -25, -15, -12, 49, 8, 5, -4, 43, 28, 2, -54, -5, -46, -25, 59, 64, -9, 74, -6,
    -20, 7, -11, 21, -49, 54, 10, -42, 33, -1, 6, 27, 30, 20, -5, 17, -9, 22, -24, 24, -16, -25, 32, -3, 46, -22, 10, -45, 10, 62, -38, -34,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static std::array<float, 32> qw_scale = {  // [32]
    0.0250f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0250f, 0.0125f,
    0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0250f, 0.0250f, 0.0125f, 0.0250f,
    0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0125f,
    0.0125f, 0.0125f, 0.0125f, 0.0250f, 0.0250f, 0.0250f, 0.0250f, 0.0250f};

static std::array<int8_t, 1024> weightq = {  // 32 x 32
    102, -19, -58, 36, 16, 45, -51, -96, 18, 39, 82, -5, 26, -70, -26, 83, 77, -45, -52, -21, 30, -34, 42, 12, 68, 22, 38, -60, 19, 83, 56, -22,
    -38, 6, 1, -29, -12, -52, 46, 77, 22, 19, -110, -1, 30, -101, -27, -38, -70, -3, 71, 13, 25, 82, 84, 48, -5, 84, 57, -12, -2, 58, 24, -5,
    13, 2, 126, 30, 10, 25, 20, -1, -41, -99, 99, 41, 0, -6, -46, -90, 88, -4, 0, -40, -64, -38, 7, -29, 47, -34, -38, -2, -34, -27, -29, -18,
    -34, -91, 55, -5, -13, -126, 36, 40, -63, 17, -106, -94, -52, -68, -102, 4, 32, -103, -32, 64, 55, 45, -70, -10, -89, 74, -2, -126, 28, 21, 13, -31,
    26, 67, 52, 37, 12, -75, -25, -126, -80, -34, -18, 88, -90, -28, -42, -63, -77, 4, 60, 118, 38, 39, 32, -97, -47, 38, 87, -2, -39, 27, 126, -102,
    126, 116, 23, 9, 91, -89, -58, -25, -20, -41, -37, 2, -47, 29, -89, 13, 31, -23, 15, -27, 78, -34, 56, -3, -38, 36, 4, -7, -30, 86, 6, 19,
    13, 27, -1, -19, 56, -82, -16, 1, 4, 27, 8, 104, -37, -45, 29, 14, 11, 58, -14, 82, 126, 59, -12, -78, -22, 61, -61, -55, -126, 17, -103, -20,
    8, 121, -4, -76, 25, -117, 11, 25, -88, 126, -15, 57, -50, 28, -79, -40, -38, -40, -62, -40, -126, -23, 62, -36, -45, 11, 72, -76, -112, 73, 20, 28,
    -19, 71, 27, -35, 52, -91, -76, 26, 24, -85, 23, 7, 16, -93, 64, -29, 24, -71, -24, 69, 42, 67, -35, -26, -36, -37, -38, 73, -17, 106, 45, 70,
    70, -90, -70, -33, 93, -57, 20, -10, 34, 1, 9, -111, -61, -12, -26, 85, -33, 58, -69, -63, 22, 41, -89, -30, 5, 13, 87, 33, -5, -47, 30, -48,
    2, -47, 14, -84, -23, 56, -50, 31, 12, 26, 44, 126, -12, 11, 41, 29, -54, 63, -8, 89, 126, 10, -76, -33, 39, 67, 124, -4, -39, -38, 94, 9,
    26, -55, -83, 17, 99, -65, -11, 85, 106, -41, 32, 11, -31, 53, 9, 71, -27, 118, 8, -51, -34, -15, -56, 59, 20, -9, -12, -11, 10, -26, 4, -102,
    43, -103, -112, -5, 32, -18, 23, 68, 7, -16, 4, 64, 37, 15, 0, -14, -37, 64, 14, -126, 10, 0, 81, 28, -33, 39, 45, -5, 52, -10, -25, 42,
    -61, 62, 59, 42, 18, 2, 29, -48, 14, -38, 8, -14, -86, 4, 6, 3, 21, -49, -64, 14, 64, 14, 32, 32, 20, 41, -18, 4, -20, -37, 13, 10,
    -53, -70, 87, -84, -126, 88, -13, -29, 126, 5, -112, -76, -6, -38, 1, 100, -44, 49, -116, 32, -18, -50, -29, 26, -126, -80, 68, 29, -36, -34, -76, -48,
    19, 17, -13, -53, 32, 17, 81, 32, 33, -49, -25, -18, -15, 4, 24, 66, -12, -39, -55, 69, 3, 75, 43, -101, -20, -5, 75, -15, -72, -16, 15, 33,
    -51, -25, -63, 14, -62, -48, -126, 19, -8, 9, 8, -52, 63, 32, 85, 10, -49, 0, -40, 75, -16, -6, -24, 59, 96, 70, -31, 10, -25, 70, -2, 61,
    71, -112, 47, 74, -86, 18, 77, -121, 54, -113, 105, -117, 60, 24, -11, 32, -17, -1, 89, -24, -75, 84, 4, -54, 10, -2, 24, -30, 27, -126, 52, 14,
    -8, -1, 5, 20, -24, -120, 76, 8, -62, -86, -22, -27, -45, 21, -9, -60, -84, 126, 65, -53, -82, -18, -19, 1, 5, 40, -30, 32, 68, -36, 6, -15,
    38, -44, 0, 13, -18, -4, 46, -88, 36, -100, 31, -75, -47, 2, -51, -3, 1, -33, -20, 72, -118, 126, -91, -10, 9, 69, 18, -109, 71, 34, -21, 10,
    15, 41, 7, 43, 79, 116, 14, 6, -4, -28, -67, 31, -18, 54, 50, -44, 48, 15, -22, 28, -72, 64, 31, 116, 61, -29, 37, -56, -61, 7, 2, -78,
    24, 69, -7, 82, 97, -54, 46, 30, 69, 14, -74, -13, 35, -75, -81, 73, -63, 23, -65, -2, -3, 16, 46, 64, -103, 35, 90, -28, 30, -74, 69, 29,
    -35, -106, 40, 39, 64, 14, 31, -10, -9, -25, -52, -20, 34, 13, 61, 63, 91, -30, -1, 57, 29, -77, -90, 109, -65, -9, -120, -2, 38, -57, 40, 104,
    1, -38, 14, -25, -25, 85, 10, -45, -9, -3, -24, 105, 48, -53, 4, 5, -29, 10, -29, 109, -105, 8, -86, -25, -99, 81, 73, -65, 30, 46, -10, -37,
    -23, -115, 96, -42, 23, 33, 36, 25, 7, 20, 26, -49, -8, -32, -3, 54, 115, 47, -74, -48, -76, -74, 2, 78, -35, 126, -48, -52, -73, -68, 34, 58,
    46, -44, -56, 118, -111, 120, -20, 4, 95, -23, 38, 71, -126, 13, 80, -52, 1, 50, 73, 50, -1, 66, -126, -126, 37, 114, -33, -34, 21, -19, 6, 58,
    8, 58, -60, 4, -20, 38, -3, 21, -2, -32, -76, 87, -50, -29, -126, 6, -81, -18, 48, -9, 79, 14, -30, -69, 22, -33, 77, -12, -3, 68, 2, 26,
    -5, -51, -74, 62, -106, -76, 25, -122, 18, 109, 3, 8, -12, 61, 37, 17, -46, 30, -48, -3, 46, -32, 54, 1, -35, 3, 3, -8, -25, 29, 113, 27,
    -34, -36, 120, 1, 56, -80, -65, -93, 77, 90, -26, -31, 16, -32, -42, 126, -75, 23, 23, 21, -64, 13, 29, -90, 46, 88, -11, -75, -26, -73, -4, -57,
    16, -24, 28, -126, 19, -2, 66, 62, 71, 44, 0, 87, -37, -109, -80, -54, 92, 49, 99, -96, 25, 45, 111, 19, -66, -30, 49, -34, -63, -26, 95, -44,
    -38, -126, 19, 1, -35, -20, -21, -33, 16, 67, 11, -9, -1, 17, 42, 41, -126, -17, 126, 27, -60, 21, -29, 37, 69, 13, -98, -74, 34, -41, 15, -32,
    86, 79, 41, 107, -4, -27, -34, -57, -6, -37, 126, 34, 54, 126, 55, 46, -34, -22, -56, -20, -36, -20, -23, -10, 77, -24, 126, -76, 81, -45, -65, -126};

static std::array<float, 32> kw_scale = {
    0.0125f, 0.0250f, 0.0125f, 0.0250f, 0.0125f, 0.0125f, 0.0125f, 0.0250f,
    0.0125f, 0.0250f, 0.0250f, 0.0250f, 0.0125f, 0.0125f, 0.0250f, 0.0250f,
    0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0250f,
    0.0125f, 0.0250f, 0.0250f, 0.0250f, 0.0125f, 0.0125f, 0.0125f, 0.0125f};

static std::array<int8_t, 1024> weightk = {
    -28, -126, -112, 21, 58, 98, 51, -31, -17, 11, -18, 126, -63, 2, -51, 53, -15, 59, 63, -19, 3, -87, 60, 2, 2, 37, -41, 32, -10, 59, 57, 118,
    -59, -93, -32, 1, -70, -5, 35, 34, -80, -43, 29, 25, -34, 79, -116, -2, 54, -57, -26, -12, -28, -75, 27, -31, 3, 61, -37, -49, -99, -31, 18, 78,
    -76, 46, 38, -47, -40, -46, -27, 24, -7, -23, -35, -17, 79, 100, -26, -9, 17, -126, -59, -16, 70, 59, -81, 61, 1, 20, -57, -2, 86, 55, 69, 91,
    -68, 43, -7, -25, -5, -16, -11, 52, 89, -118, -100, 29, -24, 46, -13, 54, 28, 51, 126, -8, -30, 4, -68, 84, -73, 53, 14, -10, -37, 32, -39, 48,
    17, -80, 67, -26, 32, 46, -41, -40, 7, 10, 54, 17, 43, -19, 40, -101, -29, 5, -98, 29, 1, -59, 17, -126, 9, 25, -18, -126, 48, 74, -18, 0,
    91, -4, 84, 6, 5, -68, 48, 54, -126, 43, -17, 38, 1, -30, 21, -47, 87, 108, 58, -37, 47, -17, 4, -106, 14, -12, -13, -9, -12, -69, 4, 11,
    81, 74, -9, 18, 25, 45, -94, -101, -11, 62, 78, -53, -19, 7, -7, 11, 85, 17, 1, -38, -33, -58, 38, -19, -14, -13, 17, 33, -39, 126, -121, -54,
    91, 124, -25, 7, 75, 15, 18, -10, 42, 12, -9, 43, -44, 61, 57, 35, 27, -38, 26, 31, 31, -41, -48, 3, -9, -75, -11, 72, -59, -3, -24, -126,
    53, 8, 117, 125, 65, 44, -54, 6, -56, 90, 112, -24, 27, -55, 28, 30, -32, 23, -25, -5, -126, -33, -126, 17, -50, 22, 75, 3, 65, -113, 2, -11,
    -64, 5, -108, -53, 20, 54, -19, 44, -33, -12, 99, -61, -30, -40, -35, -70, -54, 34, 15, 7, -15, -45, -17, -3, 82, 26, 4, -47, 13, 27, 12, 59,
    38, 84, 5, -35, -39, 38, -2, 4, -67, 6, -36, -38, -42, 18, 24, 24, -50, -85, -46, 4, -93, -45, -3, 61, -9, -40, -15, -62, 47, 14, -64, 5,
    17, 34, -24, 48, 16, -126, 30, 8, 72, 14, -25, 10, 126, -16, 39, -32, 90, -6, -62, -39, 45, -5, 47, -39, 34, 23, 4, 11, 15, 88, 3, -44,
    29, -12, -21, -18, 47, -50, 40, -102, -71, 23, -4, 36, -52, -34, 12, 40, 19, -29, 32, 16, 96, 46, -11, 6, 18, 53, -15, -82, -44, -22, -126, 10,
    -126, -30, 0, 40, 27, -6, 52, -3, -59, 89, 5, -32, 86, 42, 95, -7, -47, 46, -16, -3, 68, 15, -90, 5, -45, -39, -27, 118, -38, 23, 12, -22,
    -39, 58, -18, 29, -45, -108, 17, -35, 126, 40, 13, -17, 59, -27, 22, -28, 84, -55, -81, -29, -78, -45, -5, -11, 39, -2, 11, -74, -126, -70, 21, 8,
    41, -60, -46, -35, 1, -109, 35, 49, 54, 24, 119, -2, -68, -79, 39, -14, -31, -42, 80, -59, -80, -5, 53, 64, -13, 4, 22, 22, -74, -24, -50, -45,
    -74, -51, 51, -20, 23, -81, 40, 47, 23, 62, 19, -104, -30, -78, 36, 14, -1, -83, 94, 1, -73, -65, 19, 73, 16, 17, -56, -31, 121, -38, 28, 105,
    -33, -37, 41, 66, -5, 41, -126, -83, 12, -14, 83, -64, -109, -45, 10, -14, 69, 102, 15, -29, 43, 10, 114, -11, -15, 47, 0, -80, 65, 121, 14, -57,
    3, -108, -5, -20, 20, 14, 19, -35, 87, 106, 42, -16, -4, -26, -15, -126, -5, -12, 108, 12, -5, 27, 74, -46, -21, -23, -33, -43, -23, 26, -66, 22,
    95, 3, -126, 28, 49, 65, 49, -82, -22, -100, 43, 54, -83, -11, -58, 30, 126, 29, 3, -66, -5, 49, 46, -96, 75, -10, -70, -55, 9, -12, -118, 20,
    0, 20, 46, 14, 65, -78, -49, 11, 11, 25, 7, -25, 40, -71, -13, 6, -9, 26, -64, 4, 18, -29, -52, -81, -27, 40, -5, -54, -38, -7, 61, 9,
    114, -55, 34, 70, -51, 120, 34, -21, -27, -54, -32, -77, -112, -72, 0, 37, 7, 27, 6, 58, 51, -7, -29, 0, 56, -6, -108, 10, -23, 53, 118, 38,
    65, 81, 48, 53, -27, -26, 0, 53, 70, -38, -9, 70, 9, 68, 5, 19, 4, -40, 10, -83, -52, 10, -51, 30, -52, 63, 2, 14, -60, 15, 35, 60,
    58, 51, 37, 52, -11, -79, -58, 39, 46, 34, -46, -59, 16, -6, 45, -7, 45, 55, 64, -20, 15, 82, 17, -11, 94, -126, -66, 23, -37, -79, -98, -47,
    25, 24, -54, 25, 21, -1, 55, -19, 30, -84, 80, 36, -44, 82, 16, -2, -16, -35, -17, -78, 53, -81, 33, -12, 126, 33, -50, -60, -33, 47, -68, 106,
    38, -117, 43, 28, -37, -27, -4, 69, 33, 8, 2, 30, -24, -4, -23, -44, -57, 30, 77, 13, -29, -124, -59, 34, -12, 10, -45, 9, 28, 27, 10, 16,
    -65, 17, 12, -42, 20, -120, -30, -57, -32, -53, 126, -3, 33, 31, -126, -24, 97, -32, -38, -25, -107, -73, 6, -8, 44, -19, -103, -43, -6, -53, -16, -111,
    -81, 10, -38, 91, -48, 64, -39, 14, 88, 126, -1, -33, -51, -126, -65, 56, 27, 11, -68, -76, 29, -75, 118, 39, 24, -47, 13, -20, -31, -99, 101, -8,
    -30, -1, 14, -33, -81, -49, 27, 9, 1, 2, 29, -34, -24, -18, 17, 0, 65, 39, 73, -14, 33, 38, -70, 5, -80, 36, -5, 53, -56, 2, -30, 112,
    -85, -13, -13, 126, -17, -78, -74, 126, 0, -16, 25, -42, -32, -55, 6, 7, 62, -18, -62, -126, 29, -126, -33, 9, 17, 49, -15, -4, 8, -32, 75, -1,
    -8, -39, -88, -49, 73, 82, -1, -61, -63, -19, 53, 51, -49, -37, 67, -39, -3, -71, 56, 22, -13, -19, -15, -7, 42, 38, -126, -32, -54, -5, 44, -34,
    -34, 49, 10, -32, 126, -85, -35, -14, 29, -4, 45, 8, -63, -83, 9, -5, -33, -32, 9, 3, -89, -43, 38, -86, -45, 16, 60, 125, -56, 17, -41, 5};

static std::array<float, 32> vw_scale = {
    0.0250f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0125f, 0.0250f, 0.0125f,
    0.0125f, 0.0125f, 0.0125f, 0.0250f, 0.0250f, 0.0125f, 0.0250f, 0.0250f,
    0.0250f, 0.0125f, 0.0250f, 0.0250f, 0.0125f, 0.0125f, 0.0250f, 0.0125f,
    0.0125f, 0.0250f, 0.0250f, 0.0125f, 0.0250f, 0.0125f, 0.0125f, 0.0250f};

static std::array<int8_t, 1024> weightv = {
    31, 86, -21, 34, 44, -27, 55, -44, 51, 2, 88, 8, -15, 67, 49, 83, -37, -64, 45, 2, -22, 69, -40, -10, -58, -22, -64, -59, -91, -56, -83, -9,
    11, 22, 20, -76, -74, 15, -59, 101, -4, 12, 22, -18, -1, 28, 55, -10, -7, 25, 8, 34, -113, 38, 72, -53, -36, -9, -3, 43, 44, 60, -30, 122,
    70, -123, 54, -21, -35, 23, -93, -52, -54, -29, 41, 126, 71, -33, 92, 27, -55, -118, 6, 5, -57, 67, -126, 77, -16, 83, -51, -11, 31, 0, -14, 15,
    5, -15, 11, 37, 84, -106, 4, 0, -5, -2, 28, -58, -11, -44, -72, -92, 83, 71, -55, -46, -30, -53, 27, 41, -51, -36, 26, -23, -11, -3, 67, 28,
    -16, 7, -91, 110, 26, -83, 58, 82, -52, 63, 19, 62, 71, -85, 60, -22, -126, 100, -57, 115, -110, -42, -70, -74, -7, 3, -16, 17, -126, 7, 124, 29,
    9, -55, 37, 49, -6, -29, -29, 44, 11, 63, -83, -60, 20, 51, 84, 36, 67, 23, 2, -37, -46, -14, -28, -65, 27, -20, -24, -71, -19, -1, -4, 32,
    -56, 13, -15, 8, -44, 40, -10, -79, 63, 9, -62, -25, 58, -80, 3, 18, 36, -79, 24, -55, 41, -33, 68, 38, 33, -83, -64, 56, 66, 85, 55, -25,
    -21, 126, -91, -75, -55, -56, 98, -47, -78, 105, 70, -3, -6, 111, 66, -52, 15, -113, -21, 53, -3, -5, 69, 55, -14, 72, 9, -87, 49, -113, -7, 16,
    -13, -19, -29, 51, 3, 38, -71, -61, -23, 123, 57, 27, -70, 60, 19, -10, 90, -8, 23, 1, 38, 19, -1, -126, -29, -14, 47, 86, 3, 126, -74, -126,
    -40, -20, 44, 42, -72, 49, -5, 15, 33, -15, -16, -58, 32, 42, 33, 55, -79, 31, 19, 52, -98, 45, 3, -101, -51, -72, 91, -6, -72, -93, -105, 23,
    -87, 44, -22, 63, 53, -43, 53, -56, -32, -97, -34, -22, 57, 32, -85, 3, 30, -58, 12, 2, -29, -91, -1, 45, -5, 12, 45, -55, 55, -88, 33, -11,
    -87, -79, 42, -31, 72, 28, -44, 126, -26, -68, -79, -55, 56, -22, -45, 31, -35, 56, -27, -40, 96, -15, -39, 9, -77, -50, 4, -33, -5, -74, -36, -50,
    121, -6, -78, 13, -63, 5, 87, 36, 14, 50, 5, -39, -7, -14, 5, -6, -54, 15, -40, 46, -20, 82, 0, -34, 116, 28, -15, 93, -60, 34, -13, -16,
    -10, -50, -44, 5, 60, -6, -126, -19, -61, 60, -24, -14, 49, -40, -22, -43, -67, -56, 30, -78, -44, -8, -13, -31, -11, 15, -23, -39, 11, 51, 126, -16,
    1, 5, -23, 46, -72, -9, 1, -103, -55, 28, 126, 68, 25, -95, -6, -126, -78, 60, -9, -16, 46, 26, 21, 49, 14, 36, -3, 34, 57, 9, 54, -22,
    37, -26, -17, -3, -100, 75, -29, -14, -95, 72, -65, -14, 44, 126, -126, -35, -42, 30, -33, -89, 4, -98, -96, 99, 4, -22, 18, -32, 24, -10, -9, 45,
    -17, -44, -1, -83, -49, -126, -39, -53, -7, -55, -8, 7, 21, -61, -22, 22, 5, 7, -9, -34, 34, -101, -52, -7, 49, -2, 27, -48, 30, -109, 8, 1,
    -81, 38, 51, -34, -63, -50, 69, -85, 20, -54, -21, 7, -18, 33, -10, -16, 42, -51, -13, -26, -51, 13, -38, -116, 60, -26, 22, 21, 64, -28, -58, -95,
    38, -30, 47, -38, -31, 40, -50, 19, -126, -41, 37, 20, -1, -84, 63, 15, 96, 79, -33, 35, -53, 32, 12, -45, 116, 1, 51, -41, -26, -46, -42, 23,
    21, -13, 126, -56, 97, 0, 43, -5, 36, 4, 7, 3, -64, -84, -54, -49, 31, 22, 0, -21, -126, 126, 2, -29, -47, -16, 52, -126, -20, 34, -16, -27,
    61, -15, -23, 29, 13, 14, -65, 34, -79, 79, -44, -35, -83, 68, -65, 39, 30, 35, 17, -37, -74, 73, -50, 23, 126, 3, -126, 23, 34, -35, -39, -48,
    -126, 49, -78, 35, -27, -7, -43, -56, 68, 126, 4, 56, -2, 38, -21, 114, 51, 84, 10, -78, -65, -58, -51, -2, -34, 126, 20, -45, 33, -76, 66, 23,
    -6, -108, -16, 3, -30, -36, 112, 65, 27, 50, -38, 46, -33, -26, -44, -42, -26, -38, -54, -8, 37, -79, -29, -25, -8, 9, 104, 36, -76, 17, 112, 19,
    -3, -23, 95, 92, 25, -15, -3, -57, -7, -82, -113, -54, -34, -19, 6, 42, -112, -126, -2, 38, -35, -8, 13, -27, 38, -11, 32, -53, 36, 59, -33, -38,
    -13, -59, 18, 126, -39, -66, -4, -45, -54, -114, 95, -53, -9, 92, 14, 21, -64, 46, -11, -65, -86, 42, -51, -8, -47, 67, 10, 102, -39, 9, -85, -34,
    62, -21, 22, -92, 60, 26, 4, -12, 13, 38, 0, -2, -44, 81, 67, 49, 22, -25, 3, -56, -81, 22, -6, -16, -58, 68, -7, -34, -49, 30, -12, 7,
    29, -76, 95, 69, -14, 27, 12, 51, -10, 33, 53, -4, 19, 81, -33, -15, 53, 2, 45, -126, 12, -58, 29, -61, -1, -16, -126, -1, -49, -93, 32, -21,
    63, 34, -25, -50, 38, -39, -42, 17, -75, -14, 40, 25, 28, -70, 37, 22, -87, -5, -14, -14, -91, 90, 44, -60, 52, 8, -39, 18, 9, 45, -28, 26,
    -89, -105, 7, 49, -12, -21, -30, -8, -3, -97, 22, 23, -13, -12, 94, 9, -38, -83, -16, 76, 1, 1, -72, 43, -117, -4, 26, 62, -41, 24, -8, -45,
    42, -20, 98, 51, -126, 60, -79, 24, -33, 68, 23, -53, 73, 17, -47, -17, 28, 20, -26, 19, 21, 50, -52, -30, -3, -2, 59, 42, 76, -105, -9, -73,
    -5, 11, -33, 48, -14, -77, -16, 6, 15, 1, -44, -52, -126, 43, 4, -20, -23, -56, -126, 17, -28, 41, -2, -40, 87, -6, 45, 6, -56, 5, 10, -11,
    -58, -45, 2, 6, -113, 71, -15, -60, 72, -34, -36, 71, 33, 52, 24, -66, -85, 76, 25, 41, -54, 82, 34, -80, -21, 36, 42, -50, 11, -82, 49, -20};

static std::array<float, 32> q_bias = {
    -1.277186436757234f, 1.6187121758878464f, -1.734613728415165f, -0.17514886640301613f, -0.5098317940714134f, -0.5539087959931915f, -0.608257688189045f, -0.07264181257774535f,
    -0.4217122365862292f, 2.247702032959441f, -1.2776239444695194f, -0.1706083121592084f, -1.211101883627284f, 0.680304624880067f, -1.8905538383994187f, 1.355968463751147f,
    0.4723109770247926f, 2.825123513221927f, -2.5563638184482795f, -0.5366137135206605f, -1.6873320382776866f, -0.24543444087348498f, 1.5508321072902214f, -1.1761097529605153f,
    0.48049124885039013f, -0.16735560938589972f, 0.19936906780563687f, -0.3431399962180654f, 0.5722586120066665f, 0.14891485501103213f, 1.3247935952147036f, 0.22843396512506456f};

static std::array<float, 32> k_bias = {
    0.24619460819200945f, -0.08398404567883526f, -0.1500027822803949f, 0.2827861114080157f, 0.15482546570701788f, 0.26877192933770017f, -0.46768427617609665f, 1.3671302954869269f,
    0.32288310641565066f, 1.1726558522475279f, 0.4783289872819158f, 0.9634353447318214f, 0.4671970457407523f, -0.1052461385389901f, -1.6387214532075853f, -0.4108569800917438f,
    -0.8989560657617106f, -0.4238957219438502f, 0.6239658221332831f, 0.16537652854760315f, -0.42372289575386823f, -1.922602169949666f, 0.8162008423709411f, -0.04897485014090968f,
    0.11893978754757699f, -0.6391347512055069f, 0.9036214933796791f, -0.975085221369177f, 2.0252801791768866f, 0.4230645123992678f, -0.6865073761010612f, -0.4003525956673408f};

static std::array<float, 32> v_bias = {
    -1.7609127564951939f, 0.0104078206287181f, -0.7146410924581169f, 1.437348002065195f, -1.5303125385401033f, -0.918459755104044f, -0.867712954494343f, -2.5262209707832306f,
    0.531867939183994f, -1.970780061886488f, 1.5568776067288108f, -0.023019048055965895f, -0.49155965466351387f, 0.8327196070301217f, -0.763922384702817f, -1.644325441573084f,
    -1.5637541858090884f, 0.053171526292804416f, -1.5821961194911058f, -1.2062417346542489f, 0.23029741928149683f, -0.8920457050782132f, -0.06220760650838387f, 0.2942590084687021f,
    -0.4362228349183151f, -0.2344379226413643f, -0.586149329261036f, -1.5243876669794532f, 0.22378084867382358f, -1.715499198175354f, -1.3795418183607775f, -1.2237706022285266f};

static constexpr float qlayer_scale = 0.250f;
static constexpr float klayer_scale = 0.250f;
static constexpr float vlayer_scale = 0.125f;

static constexpr float qk_scale = 0.5f;
static constexpr float probs_scale = 0.0078125f;
static constexpr float attn_out_scale = 0.05f;

static std::array<int8_t, 512> attn_out_q8 = {
    -39, 8, -75, 2, -69, -31, -42, -29, 44, 6, 0, -61, -102, 61, 28, 76,
    -48, 23, -96, -7, -38, 11, 7, -71, 51, -26, 8, 24, -119, -30, -34, -19,
    -55, -36, 60, 127, -6, 38, 127, -107, 88, -1, 56, -52, -63, 56, -31, -128,
    -54, 29, -86, -3, -67, 45, 6, -95, 27, -47, 25, 4, -70, -50, -60, -25,
    -46, 22, -57, 13, -49, -27, 25, -93, -36, 13, 86, 38, -27, 52, 19, -127,
    -97, 57, -89, -128, -128, 114, 5, -79, 102, 89, -128, 2, 67, -40, -17, -55,
    -87, 4, -21, 38, -33, -36, 51, -59, 30, -22, 35, -63, -13, 38, -32, -46,
    -66, -40, -86, -45, 5, -116, 5, -57, -58, -48, 127, -19, 34, -46, 21, -120,
    -64, 23, -96, -8, -72, -49, -27, -64, -43, 22, 52, -4, -25, 54, -5, -68,
    127, -10, 98, -85, 122, -114, 127, 31, -23, -51, -30, -128, -34, -128, -90, 127,
    -70, 8, -56, 52, -24, -26, 99, -37, 96, 9, 11, -59, -118, 62, -8, 35,
    109, 40, -29, -128, 11, -44, -40, -4, -31, -64, -48, -9, 16, -40, -8, -124,
    -43, -11, -81, 49, -75, -35, -38, -39, 55, 4, -5, -34, -126, 46, 29, 51,
    30, -10, 9, -2, 51, -67, 28, 24, -98, -76, -5, -23, -40, -29, 23, -128,
    -68, 50, -62, 12, -21, -31, 104, -65, 31, 2, 64, 12, -42, 46, 16, -29,
    -2, 7, -94, 127, 43, -9, -9, -91, -3, -128, 127, 19, -128, -54, -95, -10,
    -60, 69, -102, -112, 4, -44, 86, 32, 48, 41, 9, -116, -96, 127, 14, 127,
    -57, 11, -33, 21, -3, -22, 57, -8, -30, 9, -21, 8, 51, -29, 38, -128,
    -61, 72, -104, -116, 5, -46, 87, 32, 47, 41, 10, -116, -96, 127, 17, 127,
    -12, -11, -107, -50, 6, -48, -22, -34, 46, -67, 99, 19, 3, -44, -19, -86,
    -62, 35, -89, -12, -46, -47, 32, -5, 97, -8, -17, -95, -119, 52, 0, 127,
    106, 19, -66, -128, 46, -6, -82, -12, 51, -120, 40, 60, 25, -10, -70, -125,
    -60, 72, -105, -116, 4, -45, 86, 32, 48, 41, 10, -115, -98, 127, 17, 127,
    57, 75, -76, -45, 1, -53, 13, -4, 16, 35, -58, -4, 95, -72, 45, -98,
    -60, 72, -105, -116, 4, -45, 86, 32, 48, 41, 10, -115, -98, 127, 17, 127,
    57, 75, -76, -45, 1, -53, 13, -4, 16, 35, -58, -4, 95, -72, 45, -98,
    -60, 72, -105, -116, 4, -45, 86, 32, 48, 41, 10, -115, -98, 127, 17, 127,
    57, 75, -76, -45, 1, -53, 13, -4, 16, 35, -58, -4, 95, -72, 45, -98,
    -60, 72, -105, -116, 4, -45, 86, 32, 48, 41, 10, -115, -98, 127, 17, 127,
    57, 75, -76, -45, 1, -53, 13, -4, 16, 35, -58, -4, 95, -72, 45, -98,
    -60, 72, -105, -116, 4, -45, 86, 32, 48, 41, 10, -115, -98, 127, 17, 127,
    57, 75, -76, -45, 1, -53, 13, -4, 16, 35, -58, -4, 95, -72, 45, -98};

template <typename T>
static std::vector<int8_t> transpose(const T& src, size_t h, size_t w) {
  std::vector<int8_t> transposed(src.size());
  const int8_t* s = src.data();
  for (size_t y = 0; y < h; y++) {
    int8_t* t = transposed.data() + y;
    for (size_t x = 0; x < w; x++) {
      *t = *s++;
      t += h;
    }
  }
  return transposed;
}

TEST(QOrderedTest, Attention_WithData_ROW_ORDER) {
  // Needs Turing architecture
  if (NeedSkipIfCudaArchLowerThan(750) || NeedSkipIfCudaArchGreaterEqualThan(800)) {
    return;
  }

  OpTester test_qorder("QOrderedAttention", 1, onnxruntime::kMSDomain);
  test_qorder.AddAttribute("order_input", (int64_t)ORDER_ROW);
  test_qorder.AddAttribute("order_output", (int64_t)ORDER_ROW);
  test_qorder.AddAttribute("order_weight", (int64_t)ORDER_COL);
  test_qorder.AddAttribute("num_heads", (int64_t)num_heads);
  std::vector<int64_t> qkv_hidden_size(3, (int64_t)num_heads * head_size);
  test_qorder.AddAttribute("qkv_hidden_sizes", qkv_hidden_size);

  test_qorder.AddInput<int8_t>("input", {batch_size, sequence_len, input_hidden_size}, inputq.data(), inputq.size());
  test_qorder.AddInput<float>("scale_input", {}, {input_scale}, true);
  test_qorder.AddInput<float>("scale_Q_gemm", {}, {qlayer_scale}, true);
  test_qorder.AddInput<float>("scale_K_gemm", {}, {klayer_scale}, true);
  test_qorder.AddInput<float>("scale_V_gemm", {}, {vlayer_scale}, true);
  test_qorder.AddInput<int8_t>("Q_weight", {input_hidden_size, hidden_size}, transpose(weightq, input_hidden_size, hidden_size), true);
  test_qorder.AddInput<int8_t>("K_weight", {input_hidden_size, hidden_size}, transpose(weightk, input_hidden_size, hidden_size), true);
  test_qorder.AddInput<int8_t>("V_weight", {input_hidden_size, hidden_size}, transpose(weightv, input_hidden_size, hidden_size), true);
  test_qorder.AddInput<float>("scale_Q_weight", {hidden_size}, qw_scale.data(), qw_scale.size(), true);
  test_qorder.AddInput<float>("scale_K_weight", {hidden_size}, kw_scale.data(), kw_scale.size(), true);
  test_qorder.AddInput<float>("scale_V_weight", {hidden_size}, vw_scale.data(), vw_scale.size(), true);
  test_qorder.AddInput<float>("Q_bias", {hidden_size}, q_bias.data(), q_bias.size(), true);
  test_qorder.AddInput<float>("K_bias", {hidden_size}, k_bias.data(), k_bias.size(), true);
  test_qorder.AddInput<float>("V_bias", {hidden_size}, v_bias.data(), v_bias.size(), true);
  test_qorder.AddInput<float>("scale_QKT_gemm", {}, {qk_scale}, true);
  test_qorder.AddInput<float>("scale_QKT_softmax", {}, {probs_scale}, true);
  test_qorder.AddInput<float>("scale_values_gemm", {}, {attn_out_scale}, true);
  test_qorder.AddInput<int32_t>("mask_index", {batch_size, sequence_len}, input_mask.data(), input_mask.size());
  test_qorder.AddOptionalInputEdge<int8_t>();  // past
  test_qorder.AddOptionalInputEdge<float>();   // attention_bias

  test_qorder.AddOutput<int8_t>("output", {batch_size, sequence_len, hidden_size}, attn_out_q8.data(), attn_out_q8.size());

  std::vector<std::unique_ptr<IExecutionProvider>> execution_providers;
  execution_providers.push_back(DefaultCudaExecutionProvider());
  test_qorder.Run(OpTester::ExpectResult::kExpectSuccess, "", {}, nullptr, &execution_providers);
}

}  // namespace test
}  // namespace onnxruntime

#endif  // CUDA_VERSION

#endif  // USE_CUDA
