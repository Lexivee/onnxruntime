// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <set>
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <dxgi1_6.h>
#ifdef _WIN32
#include "getopt.h"
#else
#include <getopt.h>
#include <thread>
#endif
#include "TestResultStat.h"
#include "TestCase.h"
#include "testenv.h"
#include "providers.h"

#include <google/protobuf/stubs/common.h>
#include "core/platform/path_lib.h"
#include "core/session/onnxruntime_cxx_api.h"
#include "core/optimizer/graph_transformer_level.h"
#include "core/framework/session_options.h"
#include "core/session/onnxruntime_session_options_config_keys.h"
#include "nlohmann/json.hpp"
#include <wrl.h>
#include <wrl/client.h>
#include <wincodec.h>
#include "d3dx12.h"
#include <algorithm>
#include <numeric>
#include <functional>
#include <wrl/client.h>
#include "core/providers/dml/dml_provider_factory.h"
#include <span>


using namespace onnxruntime;
using namespace Microsoft::WRL;
using namespace std;


#define THROW_IF_FAILED(hr)   \
  {                           \
    HRESULT localHr = (hr);   \
    if (FAILED(hr)) throw hr; \
  }
#define RETURN_IF_FAILED(hr)   \
  {                            \
    HRESULT localHr = (hr);    \
    if (FAILED(hr)) return hr; \
  }
#define THROW_IF_NOT_OK(status)    \
  {                                \
    auto localStatus = (status);   \
    if (localStatus) throw E_FAIL; \
  }
#define RETURN_HR_IF_NOT_OK(status) \
  {                                 \
    auto localStatus = (status);    \
    if (localStatus) return E_FAIL; \
  }

template <typename T>
using BaseType =
std::remove_cv_t<
    std::remove_reference_t<
    std::remove_pointer_t<
    std::remove_all_extents_t<T>
    >
    >
>;

template<typename T>
using deleting_unique_ptr = std::unique_ptr<T, std::function<void(T*)>>;

template <typename C, typename T = BaseType<decltype(*std::declval<C>().data())>>
T GetElementCount(C const& range)
{
    return std::accumulate(range.begin(), range.end(), static_cast<T>(1), std::multiplies<T>());
};


#ifdef USE_DML
    // DML device
IDXGIAdapter* dxgiAdapter = nullptr;
IDMLDevice* dmlDevice = nullptr;
ID3D12Device* d3D12Device = nullptr;
ID3D12GraphicsCommandList* commandList = nullptr;
#endif

D3D_FEATURE_LEVEL featureLevels[] = {
    D3D_FEATURE_LEVEL_1_0_CORE,
    D3D_FEATURE_LEVEL_9_1,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_12_1};
uint32_t featureLevelCount = sizeof(featureLevels) / sizeof(D3D_FEATURE_LEVEL);

UINT g_textureWidth = 0;
UINT g_textureHeight = 0;
int g_imageSize = 0;


using namespace Microsoft::WRL;

namespace {
void usage() {
  auto version_string = Ort::GetVersionString();
  printf(
      "onnx_test_runner [options...] <data_root>\n"
      "Options:\n"
      "\t-j [models]: Specifies the number of models to run simultaneously.\n"
      "\t-A : Disable memory arena\n"
      "\t-M : Disable memory pattern\n"
      "\t-c [runs]: Specifies the number of Session::Run() to invoke simultaneously for each model.\n"
      "\t-r [repeat]: Specifies the number of times to repeat\n"
      "\t-v: verbose\n"
      "\t-n [test_case_name]: Specifies a single test case to run.\n"
      "\t-e [EXECUTION_PROVIDER]: EXECUTION_PROVIDER could be 'cpu', 'cuda', 'dnnl', 'tensorrt', "
      "'openvino', 'rocm', 'migraphx', 'acl', 'armnn', 'xnnpack', 'nnapi', 'qnn', 'snpe' or 'coreml'. "
      "Default: 'cpu'.\n"
      "\t-p: Pause after launch, can attach debugger and continue\n"
      "\t-x: Use parallel executor, default (without -x): sequential executor.\n"
      "\t-d [device_id]: Specifies the device id for multi-device (e.g. GPU). The value should > 0\n"
      "\t-t: Specify custom relative tolerance values for output value comparison. default: 1e-5\n"
      "\t-a: Specify custom absolute tolerance values for output value comparison. default: 1e-5\n"
      "\t-i: Specify EP specific runtime options as key value pairs. Different runtime options available are: \n"
      "\t    [QNN only] [backend_path]: QNN backend path. e.g '/folderpath/libQnnHtp.so', '/folderpath/libQnnCpu.so'.\n"
      "\t    [QNN only] [qnn_context_cache_enable]: 1 to enable cache QNN context. Default to false.\n"
      "\t    [QNN only] [qnn_context_cache_path]: File path to the qnn context cache. Default to model_file.onnx.bin if not set.\n"
      "\t    [QNN only] [profiling_level]: QNN profiling level, options:  'basic', 'detailed', default 'off'.\n"
      "\t    [QNN only] [rpc_control_latency]: QNN rpc control latency. default to 10.\n"
      "\t    [QNN only] [htp_performance_mode]: QNN performance mode, options: 'burst', 'balanced', 'default', 'high_performance', \n"
      "\t    'high_power_saver', 'low_balanced', 'low_power_saver', 'power_saver', 'sustained_high_performance'. Default to 'default'. \n"
      "\t    [QNN only] [qnn_context_priority]: QNN context priority, options: 'low', 'normal', 'normal_high', 'high'. Default to 'normal'. \n"
      "\t    [QNN only] [qnn_context_embed_mode]: 1 means dump the QNN context binary into the Onnx skeleton model.\n"
      "\t    0 means dump the QNN context binary into separate bin file and set the path in the Onnx skeleton model.\n"
      "\t    [QNN only] [qnn_saver_path]: QNN Saver backend path. e.g '/folderpath/libQnnSaver.so'.\n"
      "\t    [QNN only] [htp_graph_finalization_optimization_mode]: QNN graph finalization optimization mode, options: \n"
      "\t    '0', '1', '2', '3', default is '0'.\n"
      "\t [Usage]: -e <provider_name> -i '<key1>|<value1> <key2>|<value2>' \n\n"
      "\t [Example] [For QNN EP] -e qnn -i \"profiling_level|detailed backend_path|/folderpath/libQnnCpu.so\" \n\n"
      "\t    [SNPE only] [runtime]: SNPE runtime, options: 'CPU', 'GPU', 'GPU_FLOAT16', 'DSP', 'AIP_FIXED_TF'. \n"
      "\t    [SNPE only] [priority]: execution priority, options: 'low', 'normal'. \n"
      "\t    [SNPE only] [buffer_type]: options: 'TF8', 'TF16', 'UINT8', 'FLOAT', 'ITENSOR'. default: ITENSOR'. \n"
      "\t    [SNPE only] [enable_init_cache]: enable SNPE init caching feature, set to 1 to enabled it. Disabled by default. \n"
      "\t [Usage]: -e <provider_name> -i '<key1>|<value1> <key2>|<value2>' \n\n"
      "\t [Example] [For SNPE EP] -e snpe -i \"runtime|CPU priority|low\" \n\n"
      "\t-o [optimization level]: Default is 99. Valid values are 0 (disable), 1 (basic), 2 (extended), 99 (all).\n"
      "\t\tPlease see onnxruntime_c_api.h (enum GraphOptimizationLevel) for the full list of all optimization levels. "
      "\n"
      "\t-h: help\n"
      "\n"
      "onnxruntime version: %s\n",
      version_string.c_str());
}

static TestTolerances LoadTestTolerances(bool enable_cuda, bool enable_openvino, bool useCustom, double atol, double rtol) {
  TestTolerances::Map absolute_overrides;
  TestTolerances::Map relative_overrides;
  if (useCustom) {
    return TestTolerances(atol, rtol, absolute_overrides, relative_overrides);
  }
  std::ifstream overrides_ifstream(ConcatPathComponent(
      ORT_TSTR("testdata"), ORT_TSTR("onnx_backend_test_series_overrides.jsonc")));
  if (!overrides_ifstream.good()) {
    constexpr double absolute = 1e-3;
    // when cuda is enabled, set it to a larger value for resolving random MNIST test failure
    // when openvino is enabled, set it to a larger value for resolving MNIST accuracy mismatch
    const double relative = enable_cuda ? 0.017 : enable_openvino ? 0.009
                                                                  : 1e-3;
    return TestTolerances(absolute, relative, absolute_overrides, relative_overrides);
  }
  const auto overrides_json = nlohmann::json::parse(
      overrides_ifstream,
      /*cb=*/nullptr, /*allow_exceptions=*/true
// Comment support is added in 3.9.0 with breaking change to default behavior.
#if NLOHMANN_JSON_VERSION_MAJOR * 1000 + NLOHMANN_JSON_VERSION_MINOR >= 3009
      ,
      /*ignore_comments=*/true
#endif
  );
  overrides_json["atol_overrides"].get_to(absolute_overrides);
  overrides_json["rtol_overrides"].get_to(relative_overrides);
  return TestTolerances(
      overrides_json["atol_default"], overrides_json["rtol_default"], absolute_overrides, relative_overrides);
}

#ifdef _WIN32
int GetNumCpuCores() {
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer[256];
  DWORD returnLength = sizeof(buffer);
  if (GetLogicalProcessorInformation(buffer, &returnLength) == FALSE) {
    // try GetSystemInfo
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    if (sysInfo.dwNumberOfProcessors <= 0) {
      ORT_THROW("Fatal error: 0 count processors from GetSystemInfo");
    }
    // This is the number of logical processors in the current group
    return sysInfo.dwNumberOfProcessors;
  }
  int processorCoreCount = 0;
  int count = (int)(returnLength / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
  for (int i = 0; i != count; ++i) {
    if (buffer[i].Relationship == RelationProcessorCore) {
      ++processorCoreCount;
    }
  }
  if (!processorCoreCount) ORT_THROW("Fatal error: 0 count processors from GetLogicalProcessorInformation");
  return processorCoreCount;
}
#else
int GetNumCpuCores() { return static_cast<int>(std::thread::hardware_concurrency()); }
#endif
}  // namespace

// get the dxgi format equivilent of a wic format
DXGI_FORMAT GetDXGIFormatFromWICFormat(WICPixelFormatGUID& wicFormatGUID) {
  if (wicFormatGUID == GUID_WICPixelFormat128bppRGBAFloat)
    return DXGI_FORMAT_R32G32B32A32_FLOAT;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBAHalf)
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBA)
    return DXGI_FORMAT_R16G16B16A16_UNORM;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA)
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppBGRA)
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppBGR)
    return DXGI_FORMAT_B8G8R8X8_UNORM;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA1010102XR)
    return DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM;

  else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA1010102)
    return DXGI_FORMAT_R10G10B10A2_UNORM;
  else if (wicFormatGUID == GUID_WICPixelFormat16bppBGRA5551)
    return DXGI_FORMAT_B5G5R5A1_UNORM;
  else if (wicFormatGUID == GUID_WICPixelFormat16bppBGR565)
    return DXGI_FORMAT_B5G6R5_UNORM;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppGrayFloat)
    return DXGI_FORMAT_R32_FLOAT;
  else if (wicFormatGUID == GUID_WICPixelFormat16bppGrayHalf)
    return DXGI_FORMAT_R16_FLOAT;
  else if (wicFormatGUID == GUID_WICPixelFormat16bppGray)
    return DXGI_FORMAT_R16_UNORM;
  else if (wicFormatGUID == GUID_WICPixelFormat8bppGray)
    return DXGI_FORMAT_R8_UNORM;
  else if (wicFormatGUID == GUID_WICPixelFormat8bppAlpha)
    return DXGI_FORMAT_A8_UNORM;

  else
    return DXGI_FORMAT_UNKNOWN;
}

// get a dxgi compatible wic format from another wic format
WICPixelFormatGUID GetConvertToWICFormat(WICPixelFormatGUID& wicFormatGUID) {
  if (wicFormatGUID == GUID_WICPixelFormatBlackWhite)
    return GUID_WICPixelFormat8bppGray;
  else if (wicFormatGUID == GUID_WICPixelFormat1bppIndexed)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat2bppIndexed)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat4bppIndexed)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat8bppIndexed)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat2bppGray)
    return GUID_WICPixelFormat8bppGray;
  else if (wicFormatGUID == GUID_WICPixelFormat4bppGray)
    return GUID_WICPixelFormat8bppGray;
  else if (wicFormatGUID == GUID_WICPixelFormat16bppGrayFixedPoint)
    return GUID_WICPixelFormat16bppGrayHalf;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppGrayFixedPoint)
    return GUID_WICPixelFormat32bppGrayFloat;
  else if (wicFormatGUID == GUID_WICPixelFormat16bppBGR555)
    return GUID_WICPixelFormat16bppBGRA5551;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppBGR101010)
    return GUID_WICPixelFormat32bppRGBA1010102;
  else if (wicFormatGUID == GUID_WICPixelFormat24bppBGR)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat24bppRGB)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppPBGRA)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppPRGBA)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat48bppRGB)
    return GUID_WICPixelFormat64bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat48bppBGR)
    return GUID_WICPixelFormat64bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppBGRA)
    return GUID_WICPixelFormat64bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppPRGBA)
    return GUID_WICPixelFormat64bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppPBGRA)
    return GUID_WICPixelFormat64bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat48bppRGBFixedPoint)
    return GUID_WICPixelFormat64bppRGBAHalf;
  else if (wicFormatGUID == GUID_WICPixelFormat48bppBGRFixedPoint)
    return GUID_WICPixelFormat64bppRGBAHalf;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBAFixedPoint)
    return GUID_WICPixelFormat64bppRGBAHalf;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppBGRAFixedPoint)
    return GUID_WICPixelFormat64bppRGBAHalf;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBFixedPoint)
    return GUID_WICPixelFormat64bppRGBAHalf;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBHalf)
    return GUID_WICPixelFormat64bppRGBAHalf;
  else if (wicFormatGUID == GUID_WICPixelFormat48bppRGBHalf)
    return GUID_WICPixelFormat64bppRGBAHalf;
  else if (wicFormatGUID == GUID_WICPixelFormat128bppPRGBAFloat)
    return GUID_WICPixelFormat128bppRGBAFloat;
  else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBFloat)
    return GUID_WICPixelFormat128bppRGBAFloat;
  else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBAFixedPoint)
    return GUID_WICPixelFormat128bppRGBAFloat;
  else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBFixedPoint)
    return GUID_WICPixelFormat128bppRGBAFloat;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBE)
    return GUID_WICPixelFormat128bppRGBAFloat;
  else if (wicFormatGUID == GUID_WICPixelFormat32bppCMYK)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppCMYK)
    return GUID_WICPixelFormat64bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat40bppCMYKAlpha)
    return GUID_WICPixelFormat64bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat80bppCMYKAlpha)
    return GUID_WICPixelFormat64bppRGBA;

#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8) || defined(_WIN7_PLATFORM_UPDATE)
  else if (wicFormatGUID == GUID_WICPixelFormat32bppRGB)
    return GUID_WICPixelFormat32bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppRGB)
    return GUID_WICPixelFormat64bppRGBA;
  else if (wicFormatGUID == GUID_WICPixelFormat64bppPRGBAHalf)
    return GUID_WICPixelFormat64bppRGBAHalf;
#endif

  else
    return GUID_WICPixelFormatDontCare;
}

// get the number of bits per pixel for a dxgi format
int GetDXGIFormatBitsPerPixel(DXGI_FORMAT& dxgiFormat) {
  if (dxgiFormat == DXGI_FORMAT_R32G32B32A32_FLOAT)
    return 128;
  else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
    return 64;
  else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_UNORM)
    return 64;
  else if (dxgiFormat == DXGI_FORMAT_R8G8B8A8_UNORM)
    return 32;
  else if (dxgiFormat == DXGI_FORMAT_B8G8R8A8_UNORM)
    return 32;
  else if (dxgiFormat == DXGI_FORMAT_B8G8R8X8_UNORM)
    return 32;
  else if (dxgiFormat == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM)
    return 32;

  else if (dxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM)
    return 32;
  else if (dxgiFormat == DXGI_FORMAT_B5G5R5A1_UNORM)
    return 16;
  else if (dxgiFormat == DXGI_FORMAT_B5G6R5_UNORM)
    return 16;
  else if (dxgiFormat == DXGI_FORMAT_R32_FLOAT)
    return 32;
  else if (dxgiFormat == DXGI_FORMAT_R16_FLOAT)
    return 16;
  else if (dxgiFormat == DXGI_FORMAT_R16_UNORM)
    return 16;
  else if (dxgiFormat == DXGI_FORMAT_R8_UNORM)
    return 8;
  else if (dxgiFormat == DXGI_FORMAT_A8_UNORM)
    return 8;
  else
    return 0;
}

int LoadImageDataFromFile(BYTE** imageData, D3D12_RESOURCE_DESC& resourceDescription, LPCWSTR filename, int& bytesPerRow) {
  HRESULT hr;

  // we only need one instance of the imaging factory to create decoders and frames
  static IWICImagingFactory* wicFactory;

  // reset decoder, frame, and converter, since these will be different for each image we load
  IWICBitmapDecoder* wicDecoder = NULL;
  IWICBitmapFrameDecode* wicFrame = NULL;
  IWICFormatConverter* wicConverter = NULL;

  bool imageConverted = false;

  if (wicFactory == NULL) {
    // Initialize the COM library
    CoInitialize(NULL);

    // create the WIC factory
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory));

    if (FAILED(hr)) {
      std::cout << "\nCoCreateInstance failed\n";
      return 0;
    }

    hr = wicFactory->CreateFormatConverter(&wicConverter);
    if (FAILED(hr)) {
      std::cout << "\nCreateFormatConverter failed\n";
      return 0;
    }
  }

  
  // load a decoder for the image
  hr = wicFactory->CreateDecoderFromFilename(
      filename,                      // Image we want to load in
      NULL,                          // This is a vendor ID, we do not prefer a specific one so set to null
      GENERIC_READ,                  // We want to read from this file
      WICDecodeMetadataCacheOnDemand,  // We will cache the metadata right away, rather than when needed, which might be unknown
      &wicDecoder                    // the wic decoder to be created
  );
  if (FAILED(hr)) {
    std::cout << "\nCreateDecoderFromFilename failed with error = " << std::hex <<  hr << std::endl;
    return 0;
  }
  
  // get image from decoder (this will decode the "frame")
  hr = wicDecoder->GetFrame(0, &wicFrame);
  if (FAILED(hr)) {
    std::cout << "\nGetFrame failed\n";
    return 0;
  }

  // get wic pixel format of image
  WICPixelFormatGUID pixelFormat;
  hr = wicFrame->GetPixelFormat(&pixelFormat);
  if (FAILED(hr)) {
    std::cout << "\nGetPixelFormat failed\n";
    return 0;
  }
  

  // get size of image
  //UINT textureWidth, textureHeight;
  hr = wicFrame->GetSize(&g_textureWidth, &g_textureHeight);
  if (FAILED(hr)) {
    std::cout << "\nGetSize failed\n";
    return 0;
  }
  std::cout << "\ng_textureWidth = " << g_textureWidth << std::endl;
  std::cout << "\ng_textureHeight = " << g_textureHeight << std::endl;

  // we are not handling sRGB types in this tutorial, so if you need that support, you'll have to figure
  // out how to implement the support yourself

  // convert wic pixel format to dxgi pixel format
  DXGI_FORMAT dxgiFormat = GetDXGIFormatFromWICFormat(pixelFormat);

  // if the format of the image is not a supported dxgi format, try to convert it
  if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
    // get a dxgi compatible wic format from the current image format
    std::cout << "\nCalling GetConvertToWICFormat" << std::endl;

    WICPixelFormatGUID convertToPixelFormat = GetConvertToWICFormat(pixelFormat);

    // return if no dxgi compatible format was found
    if (convertToPixelFormat == GUID_WICPixelFormatDontCare) {
      std::cout << "\nGUID_WICPixelFormatDontCare\n";
      return 0;
    }

    // set the dxgi format
    std::cout << "\nCalling GetDXGIFormatFromWICFormat" << std::endl;

    dxgiFormat = GetDXGIFormatFromWICFormat(convertToPixelFormat);
    
    //dxgiFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

    std::cout << "\ndxgiFormat = " << std::hex << dxgiFormat << std::endl;

    // make sure we can convert to the dxgi compatible format
    BOOL canConvert = FALSE;
    hr = wicConverter->CanConvert(pixelFormat, convertToPixelFormat, &canConvert);
    if (FAILED(hr) || !canConvert) {
      std::cout << "\nCanConvert failed\n";
      return 0;
    }

    // do the conversion (wicConverter will contain the converted image)
    hr = wicConverter->Initialize(wicFrame, convertToPixelFormat, WICBitmapDitherTypeErrorDiffusion, 0, 0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        std::cout << "\nInitialize failed\n";
        return 0;
    }

    // this is so we know to get the image data from the wicConverter (otherwise we will get from wicFrame)
    imageConverted = true;
  }


  int bitsPerPixel = GetDXGIFormatBitsPerPixel(dxgiFormat);  // number of bits per pixel

  bytesPerRow = (g_textureWidth * bitsPerPixel) / 8;           // number of bytes in each row of the image data
  //bytesPerRow = (g_textureWidth * bitsPerPixel) / 2;  // number of bytes in each row of the image data
  int imageSize = bytesPerRow * g_textureHeight;               // total image size in bytes
  g_imageSize = imageSize;

  // allocate enough memory for the raw image data, and set imageData to point to that memory
  *imageData = (BYTE*)malloc(imageSize);

  // copy (decoded) raw image data into the newly allocated memory (imageData)
  if (imageConverted) {
    // if image format needed to be converted, the wic converter will contain the converted image
    hr = wicConverter->CopyPixels(0, bytesPerRow, imageSize, *imageData);
    if (FAILED(hr)) {
      std::cout << "\nwicConverter->CopyPixels failed\n";
      return 0;
    }
    
  } else {
    // no need to convert, just copy data from the wic frame
    hr = wicFrame->CopyPixels(0, bytesPerRow, imageSize, *imageData);
    if (FAILED(hr)) {
      std::cout << "\nwicFrame->CopyPixels failed\n";
      return 0;
    }
  }

  // now describe the texture with the information we have obtained from the image
  resourceDescription = {};
  resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resourceDescription.Alignment = 0;                          // may be 0, 4KB, 64KB, or 4MB. 0 will let runtime decide between 64KB and 4MB (4MB for multi-sampled textures)
  resourceDescription.Width = g_textureWidth;                   // width of the texture
  resourceDescription.Height = g_textureHeight;                 // height of the texture
  resourceDescription.DepthOrArraySize = 1;                   // if 3d image, depth of 3d image. Otherwise an array of 1D or 2D textures (we only have one image, so we set 1)
  resourceDescription.MipLevels = 1;                          // Number of mipmaps. We are not generating mipmaps for this texture, so we have only one level
  resourceDescription.Format = dxgiFormat;                    // This is the dxgi format of the image (format of the pixels)
  resourceDescription.SampleDesc.Count = 1;                   // This is the number of samples per pixel, we just want 1 sample
  resourceDescription.SampleDesc.Quality = 0;                 // The quality level of the samples. Higher is better quality, but worse performance
  resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;  // The arrangement of the pixels. Setting to unknown lets the driver choose the most efficient one
  resourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;       // no flags

  // return the size of the image. remember to delete the image once your done with it (in this tutorial once its uploaded to the gpu)

  return imageSize;
}



void initDMLDevice(ID3D12CommandQueue*& commandQueue) 
{
  std::cout << "Entering initDMLDevice" << std::endl;

  HRESULT hr{};
  ComPtr<ID3D12Debug> _debugController = nullptr;
  UINT createFactoryFlags = 0;
  if (0) {
    if ((D3D12GetDebugInterface(IID_PPV_ARGS(&_debugController))) == S_OK) {
      _debugController->EnableDebugLayer();
      createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }
  }

  ComPtr<IDXGIFactory4> dxgiFactory;
  hr = CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory));

  //IDXGIFactory* dxgiFactory;
  // hr = CreateDXGIFactory(__uuidof(dxgiFactory), (void**)(&dxgiFactory));
  if (hr != S_OK) {
    std::cout << "Adapator creation failed" << std::endl;
    exit(-1);
    
  }

  //UINT adapterIndex{};
  std::vector<IDXGIAdapter*> validAdapters;

  do {
    for (UINT i = 0;; ++i) {
      dxgiAdapter = nullptr;
      if (dxgiFactory->EnumAdapters(i, &dxgiAdapter) != S_OK) {
        break;
      }
      DXGI_ADAPTER_DESC pDesc;
      dxgiAdapter->GetDesc(&pDesc);

      // is a software adapter
      if (pDesc.VendorId == 0x1414 && pDesc.DeviceId == 0x8c) {
        continue;
      }
      // valid GPU adapter
      else {
        validAdapters.push_back(dxgiAdapter);
      }
    }

    //valid adapters 0 will select the gpu
    if (validAdapters.size() == 0) {
      std::cout << "Valid devices not found" << std::endl;
      return;
    } else if (validAdapters.size() == 1) {
      dxgiAdapter = validAdapters.at(0);
    } else {
      //if (ortBenchCmdArgs.getDeviceName() == "iGPU")
        dxgiAdapter = validAdapters.at(0);
      //if (ortBenchCmdArgs.getDeviceName() == "GPU")
        //dxgiAdapter = validAdapters.at(1);
    }

    for (int featureLevel = featureLevelCount - 1; featureLevel >= 0; featureLevel--) {
      hr = ::D3D12CreateDevice(
          dxgiAdapter,
          featureLevels[featureLevel],
          __uuidof(ID3D12Device),
          (void**)(&d3D12Device));
      if (hr == DXGI_ERROR_UNSUPPORTED)
        continue;
      else if (hr == S_OK)
        break;
    }
  } while (hr != S_OK);

  D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};

  //if (ortBenchCmdArgs.getCmdQueueType() == "direct")
    commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  //else
    //commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

  commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

  hr = d3D12Device->CreateCommandQueue(
      &commandQueueDesc,
      __uuidof(ID3D12CommandQueue),
      (void**)(&commandQueue));

  if (hr != S_OK) {
    std::cout << "Queue creation failed" << std::endl;
    exit(-1);
    ;
  }
  DML_CREATE_DEVICE_FLAGS dmlCreateDeviceFlags = DML_CREATE_DEVICE_FLAG_NONE;

  hr = (DMLCreateDevice(
      d3D12Device,
      dmlCreateDeviceFlags,
      __uuidof(IDMLDevice),
      (void**)(&dmlDevice)));

  if (hr != S_OK) {
    std::cout << "DML device creation failed" << std::endl;
    exit(-1);
    ;
  }

  ID3D12CommandAllocator* directCmdListAlloc = nullptr;

  hr = d3D12Device->CreateCommandAllocator(
      commandQueueDesc.Type,
      IID_PPV_ARGS(&directCmdListAlloc));

  if (hr != S_OK) {
    std::cout << "Command Allocator creation failed" << std::endl;
    exit(-1);
    ;
  }

  hr = d3D12Device->CreateCommandList(
      0,
      commandQueueDesc.Type,
      directCmdListAlloc,
      nullptr,
      IID_PPV_ARGS(&commandList));

  if (hr != S_OK) {
    std::cout << "CreateCommandList failed" << std::endl;
    exit(-1);
    ;
  }

  std::cout << "initDMLDevice success" << std::endl;
}

// Create an ORT Session from a given model file path
Ort::Session CreateSession(const wchar_t* model_file_path) {
  OrtApi const& ortApi = Ort::GetApi();
  const OrtDmlApi* ortDmlApi;
  ortApi.GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&ortDmlApi));
  Ort::Env ortEnvironment(ORT_LOGGING_LEVEL_WARNING, "DirectML_Direct3D_TensorAllocation_Test");
  Ort::SessionOptions sessionOptions;
  sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
  sessionOptions.DisableMemPattern();
  sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  ortApi.AddFreeDimensionOverrideByName(sessionOptions, "batch_size", 1);
  OrtSessionOptionsAppendExecutionProvider_DML(sessionOptions, 0);

  return Ort::Session(ortEnvironment, model_file_path, sessionOptions);
}


// Create ORT Value from the D3D buffer currently being drawn to the screen
Ort::Value CreateTensorValueFromD3DResource(
    OrtDmlApi const& ortDmlApi,
    Ort::MemoryInfo const& memoryInformation,
    ID3D12Resource* d3dResource,
    std::span<const int64_t> tensorDimensions,  // std::span<const int64_t> tensorDimensions,
    ONNXTensorElementDataType elementDataType,
    /*out*/ void** dmlEpResourceWrapper  // Must stay alive with Ort::Value.
) 
{
    std::cout << "\nEntering CreateTensorValueFromD3DResource" << std::endl;
  *dmlEpResourceWrapper = nullptr;

  void* dmlAllocatorResource;
  THROW_IF_NOT_OK(ortDmlApi.CreateGPUAllocationFromD3DResource(d3dResource, &dmlAllocatorResource));
  auto deleter = [&](void*) { ortDmlApi.FreeGPUAllocation(dmlAllocatorResource); };
  deleting_unique_ptr<void> dmlAllocatorResourceCleanup(dmlAllocatorResource, deleter);

  // Calculate the tensor byte size
  size_t tensorByteSize = static_cast<size_t>(d3dResource->GetDesc().Width * d3dResource->GetDesc().Height * 3 * 4);

  std::cout << "\nCalling CreateTensor" << std::endl;

  // Create the ORT Value
  Ort::Value newValue(
      Ort::Value::CreateTensor(
          memoryInformation,
          dmlAllocatorResource,
          tensorByteSize * sizeof(float),
          tensorDimensions.data(),
          tensorDimensions.size(),
          elementDataType));

  std::cout << "\nCreateTensor succeeds" << std::endl;

  // Return values and the wrapped resource.
  *dmlEpResourceWrapper = dmlAllocatorResource;
  dmlAllocatorResourceCleanup.release();

  return newValue;
}


// Run the buffer through a preprocessing model that will shrink the
// image from 512 x 512 x 4 to 224 x 224 x 3
Ort::Value PreprocessAndEval(Ort::Session& session,
                      ComPtr<ID3D12Resource> currentBuffer) {
  
  std::cout << "\n\tEntering PreprocessAndEval\n" << std::endl;
    // Init OrtAPI
  OrtApi const& ortApi = Ort::GetApi();  // Uses ORT_API_VERSION
  const OrtDmlApi* ortDmlApi;
  THROW_IF_NOT_OK(ortApi.GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&ortDmlApi)));

  // Create ORT Value from buffer currently being drawn to screen
  const char* memoryInformationName = "DML";
  Ort::MemoryInfo memoryInformation(memoryInformationName, OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemType::OrtMemTypeDefault);
  ComPtr<IUnknown> inputTensorEpWrapper;

  // Calculate input shape
  //auto width = g_textureWidth; //800;
  //auto height = g_textureHeight;  // 600;

  //auto rowPitchInBytes = (width * 4 + 255) & ~255;
  //auto rowPitchInPixels = rowPitchInBytes / 4;
  //auto bufferInBytes = g_imageSize; // uint8 // rowPitchInBytes * height;
  auto bufferInBytes = g_imageSize;
  const std::array<int64_t, 2> inputShape = {1, bufferInBytes};
  
  std::cout << "\nCalling  CreateTensorValueFromD3DResource\n" << std::endl;
  Ort::Value inputTensor = CreateTensorValueFromD3DResource(
      *ortDmlApi,
      memoryInformation,
      currentBuffer.Get(),
      inputShape,
      ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, // ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8,
      /*out*/ IID_PPV_ARGS_Helper(inputTensorEpWrapper.GetAddressOf()));

  // Create input and output node names
  const char* inputTensorName = "data_0";         //"Input";
  const char* outputTensorName = "softmaxout_1";  //"Output";
  std::vector<const char*> input_node_names;
  input_node_names.push_back(inputTensorName);
  std::vector<const char*> output_node_names;
  output_node_names.push_back(outputTensorName);

  // Evaluate input (resize from 512 x 512 x 4 to 224 x 224 x 3)
  Ort::Value outputTensor(nullptr);
  std::cout << "\nCalling session.Run" << std::endl;

  session.Run(Ort::RunOptions{nullptr}, input_node_names.data(),
              &inputTensor, 1, output_node_names.data(), &outputTensor, 1);

  return outputTensor;
}

int RunDx12InputBenchmark() {

    ID3D12CommandQueue* commandQueue = nullptr;
    std::cout << "\n\tCalling initDMLDevice\n"<< std::endl;
    initDMLDevice(commandQueue);


    // Create DX12 input
    std::cout << "\n\tUsing dx12 input resource bind";
    // Bind input as DX12 resource in GPU
    // Load the image from file
    D3D12_RESOURCE_DESC textureDesc;
    ID3D12Resource* textureBuffer;  // the resource heap containing our texture
    ID3D12Resource* textureBufferUploadHeap;
    int imageBytesPerRow;
    BYTE* imageData;
    std::cout << "\n\tCalling LoadImageDataFromFile";
    int imageSize = LoadImageDataFromFile(&imageData, textureDesc, L"inputimage.jpg", imageBytesPerRow);

    if (imageSize == 0) {
      std::cout << "\n\t LoadImageDataFromFile failed:imageSize = " << imageSize << std::endl;
      return 1;
    } 
    else {
      std::cout << "\n\t LoadImageDataFromFile success= " << imageSize << std::endl;
    }

    HRESULT hr{};

    // create a default heap where the upload heap will copy its contents into (contents being the texture)
    auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    hr = d3D12Device->CreateCommittedResource(
        &heapProperties,                 // a default heap
        D3D12_HEAP_FLAG_NONE,                               // no flags
        &textureDesc,                                       // the description of our texture
        D3D12_RESOURCE_STATE_COPY_DEST,                     // We will copy the texture from the upload heap to here, so we start it out in a copy dest state
        nullptr,                                            // used for render targets and depth/stencil buffers
        IID_PPV_ARGS(&textureBuffer));
    if (FAILED(hr)) {
      std::cout << "\n\t CreateCommittedResource D3D12_HEAP_TYPE_DEFAULT failed = " << std::endl;
      return 1;
    }
    textureBuffer->SetName(L"Texture Buffer Resource Heap");

    UINT64 textureUploadBufferSize;
    // this function gets the size an upload buffer needs to be to upload a texture to the gpu.
    // each row must be 256 byte aligned except for the last row, which can just be the size in bytes of the row
    // eg. textureUploadBufferSize = ((((width * numBytesPerPixel) + 255) & ~255) * (height - 1)) + (width * numBytesPerPixel);
    //textureUploadBufferSize = (((imageBytesPerRow + 255) & ~255) * (textureDesc.Height - 1)) + imageBytesPerRow;
    d3D12Device->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &textureUploadBufferSize);

    // now we create an upload heap to upload our texture to the GPU
    heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize);

    hr = d3D12Device->CreateCommittedResource(
        &heapProperties,                                            // upload heap
        D3D12_HEAP_FLAG_NONE,                                     // no flags
        &(resourceDesc),                    // resource description for a buffer (storing the image data in this heap just to copy to the default heap)
        D3D12_RESOURCE_STATE_GENERIC_READ,                        // We will copy the contents from this heap to the default heap above
        nullptr,
        IID_PPV_ARGS(&textureBufferUploadHeap));

    if (FAILED(hr)) {
      std::cout << "\n\t CreateCommittedResource D3D12_HEAP_TYPE_UPLOAD failed = " << std::endl;
      return false;
    }
    textureBufferUploadHeap->SetName(L"Texture Buffer Upload Resource Heap");

    // store vertex buffer in upload heap
    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = &imageData[0];                               // pointer to our image data
    textureData.RowPitch = imageBytesPerRow;                         // size of all our triangle vertex data
    textureData.SlicePitch = imageBytesPerRow * textureDesc.Height;  // also the size of our triangle vertex data

    // Now we copy the upload buffer contents to the default heap
    UpdateSubresources(commandList, textureBuffer, textureBufferUploadHeap, 0, 0, 1, &textureData);
    std::cout << "\n\t UpdateSubresources done " << std::endl;

    // Now bind the DX12 resource to ORT and Evaluate

    // Now CreateSession
    const wchar_t model_file_path[] = L"Squeezenet.onnx";
    Ort::Session OrtSession = CreateSession(model_file_path);
    std::cout << "\n\t CreateSession done\n" << std::endl;

    // Preprocess the texture to convert to tensor and evaluate
    PreprocessAndEval(OrtSession, textureBuffer);

    std::cout << "\n\t PreprocessAndEval done\n" << std::endl;
    
    return 0;
};

    





#ifdef _WIN32
int real_main(int argc, wchar_t* argv[], Ort::Env& env) {
#else
int real_main(int argc, char* argv[], Ort::Env& env) {
#endif
  // if this var is not empty, only run the tests with name in this list
  std::vector<std::basic_string<PATH_CHAR_TYPE>> whitelisted_test_cases;
  int concurrent_session_runs = GetNumCpuCores();
  bool enable_cpu_mem_arena = true;
  ExecutionMode execution_mode = ExecutionMode::ORT_SEQUENTIAL;
  int repeat_count = 1;
  int p_models = GetNumCpuCores();
  bool enable_cuda = false;
  bool enable_dnnl = false;
  bool enable_openvino = false;
  bool enable_tensorrt = false;
  bool enable_mem_pattern = true;
  bool enable_qnn = false;
  bool enable_nnapi = false;
  bool enable_coreml = false;
  bool enable_snpe = false;
  bool enable_dml = false;
  bool enable_acl = false;
  bool enable_armnn = false;
  bool enable_rocm = false;
  bool enable_migraphx = false;
  bool enable_xnnpack = false;
  bool override_tolerance = false;
  double atol = 1e-5;
  double rtol = 1e-5;
  int device_id = 0;
  GraphOptimizationLevel graph_optimization_level = ORT_ENABLE_ALL;
  bool user_graph_optimization_level_set = false;
  bool set_denormal_as_zero = false;
  bool set_dml_dxinput = true;
  std::basic_string<ORTCHAR_T> ep_runtime_config_string;
  std::string provider_name = "cpu";

  OrtLoggingLevel logging_level = ORT_LOGGING_LEVEL_ERROR;
  bool verbose_logging_required = false;

  bool pause = false;
  {
    int ch;
    while ((ch = getopt(argc, argv, ORT_TSTR("Ac:hj:Mn:r:e:t:a:xvo:d:i:pz"))) != -1) {
      switch (ch) {
        case 'A':
          enable_cpu_mem_arena = false;
          break;
        case 'v':
          verbose_logging_required = true;
          break;
        case 'c':
          concurrent_session_runs = static_cast<int>(OrtStrtol<PATH_CHAR_TYPE>(optarg, nullptr));
          if (concurrent_session_runs <= 0) {
            usage();
            return -1;
          }
          break;
        case 'j':
          p_models = static_cast<int>(OrtStrtol<PATH_CHAR_TYPE>(optarg, nullptr));
          if (p_models <= 0) {
            usage();
            return -1;
          }
          break;
        case 'r':
          repeat_count = static_cast<int>(OrtStrtol<PATH_CHAR_TYPE>(optarg, nullptr));
          if (repeat_count <= 0) {
            usage();
            return -1;
          }
          break;
        case 'M':
          enable_mem_pattern = false;
          break;
        case 'n':
          // run only some whitelisted tests
          // TODO: parse name str to an array
          whitelisted_test_cases.emplace_back(optarg);
          break;
        case 'e':
          provider_name = ToUTF8String(optarg);
          if (!CompareCString(optarg, ORT_TSTR("cpu"))) {
            // do nothing
          } else if (!CompareCString(optarg, ORT_TSTR("cuda"))) {
            enable_cuda = true;
          } else if (!CompareCString(optarg, ORT_TSTR("dnnl"))) {
            enable_dnnl = true;
          } else if (!CompareCString(optarg, ORT_TSTR("openvino"))) {
            enable_openvino = true;
          } else if (!CompareCString(optarg, ORT_TSTR("tensorrt"))) {
            enable_tensorrt = true;
          } else if (!CompareCString(optarg, ORT_TSTR("qnn"))) {
            enable_qnn = true;
          } else if (!CompareCString(optarg, ORT_TSTR("nnapi"))) {
            enable_nnapi = true;
          } else if (!CompareCString(optarg, ORT_TSTR("coreml"))) {
            enable_coreml = true;
          } else if (!CompareCString(optarg, ORT_TSTR("snpe"))) {
            enable_snpe = true;
          } else if (!CompareCString(optarg, ORT_TSTR("dml"))) {
            enable_dml = true;
          } else if (!CompareCString(optarg, ORT_TSTR("acl"))) {
            enable_acl = true;
          } else if (!CompareCString(optarg, ORT_TSTR("armnn"))) {
            enable_armnn = true;
          } else if (!CompareCString(optarg, ORT_TSTR("rocm"))) {
            enable_rocm = true;
          } else if (!CompareCString(optarg, ORT_TSTR("migraphx"))) {
            enable_migraphx = true;
          } else if (!CompareCString(optarg, ORT_TSTR("xnnpack"))) {
            enable_xnnpack = true;
          } else {
            usage();
            return -1;
          }
          break;
        case 't':
          override_tolerance = true;
          rtol = OrtStrtod<PATH_CHAR_TYPE>(optarg, nullptr);
          break;
        case 'a':
          override_tolerance = true;
          atol = OrtStrtod<PATH_CHAR_TYPE>(optarg, nullptr);
          break;
        case 'x':
          execution_mode = ExecutionMode::ORT_PARALLEL;
          break;
        case 'p':
          pause = true;
          break;
        case 'o': {
          int tmp = static_cast<int>(OrtStrtol<PATH_CHAR_TYPE>(optarg, nullptr));
          switch (tmp) {
            case ORT_DISABLE_ALL:
              graph_optimization_level = ORT_DISABLE_ALL;
              break;
            case ORT_ENABLE_BASIC:
              graph_optimization_level = ORT_ENABLE_BASIC;
              break;
            case ORT_ENABLE_EXTENDED:
              graph_optimization_level = ORT_ENABLE_EXTENDED;
              break;
            case ORT_ENABLE_ALL:
              graph_optimization_level = ORT_ENABLE_ALL;
              break;
            default: {
              if (tmp > ORT_ENABLE_ALL) {  // relax constraint
                graph_optimization_level = ORT_ENABLE_ALL;
              } else {
                fprintf(stderr, "See usage for valid values of graph optimization level\n");
                usage();
                return -1;
              }
            }
          }
          user_graph_optimization_level_set = true;
          break;
        }
        case 'd':
          device_id = static_cast<int>(OrtStrtol<PATH_CHAR_TYPE>(optarg, nullptr));
          if (device_id < 0) {
            usage();
            return -1;
          }
          break;
        case 'i':
          ep_runtime_config_string = optarg;
          break;
        case 'z':
          set_denormal_as_zero = true;
          break;
        case 'D':
            set_dml_dxinput = true;
            break;
        case '?':
        case 'h':
        default:
          usage();
          return -1;
      }
    }
  }

  // TODO: Support specifying all valid levels of logging
  // Currently the logging level is ORT_LOGGING_LEVEL_ERROR by default and
  // if the user adds -v, the logging level is ORT_LOGGING_LEVEL_VERBOSE
  if (verbose_logging_required) {
    logging_level = ORT_LOGGING_LEVEL_VERBOSE;
  }

  if (concurrent_session_runs > 1 && repeat_count > 1) {
    fprintf(stderr, "when you use '-r [repeat]', please set '-c' to 1\n");
    usage();
    return -1;
  }
  argc -= optind;
  argv += optind;
  if (argc < 1) {
    fprintf(stderr, "please specify a test data dir\n");
    usage();
    return -1;
  }

  if (pause) {
    printf("Enter to continue...\n");
    fflush(stdout);
    (void)getchar();
  }

  {
    bool failed = false;
    ORT_TRY {
      env = Ort::Env{logging_level, "Default"};
    }
    ORT_CATCH(const std::exception& ex) {
      ORT_HANDLE_EXCEPTION([&]() {
        fprintf(stderr, "Error creating environment: %s \n", ex.what());
        failed = true;
      });
    }

    if (failed)
      return -1;
  }

  std::vector<std::basic_string<PATH_CHAR_TYPE>> data_dirs;
  TestResultStat stat;

  for (int i = 0; i != argc; ++i) {
    data_dirs.emplace_back(argv[i]);
  }

  std::vector<std::unique_ptr<ITestCase>> owned_tests;
  {
    Ort::SessionOptions sf;

    if (enable_cpu_mem_arena)
      sf.EnableCpuMemArena();
    else
      sf.DisableCpuMemArena();
    if (enable_mem_pattern)
      sf.EnableMemPattern();
    else
      sf.DisableMemPattern();
    sf.SetExecutionMode(execution_mode);
    if (set_denormal_as_zero)
      sf.AddConfigEntry(kOrtSessionOptionsConfigSetDenormalAsZero, "1");

    if (enable_tensorrt) {
#ifdef USE_TENSORRT
      OrtCUDAProviderOptions cuda_options;
      cuda_options.device_id = device_id;
      cuda_options.do_copy_in_default_stream = true;
      // TODO: Support arena configuration for users of test runner
      Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_Tensorrt(sf, device_id));
      sf.AppendExecutionProvider_CUDA(cuda_options);
#else
      fprintf(stderr, "TensorRT is not supported in this build");
      return -1;
#endif
    }
    if (enable_openvino) {
#ifdef USE_OPENVINO
      // Setting default optimization level for OpenVINO can be overridden with -o option
      sf.SetGraphOptimizationLevel(ORT_DISABLE_ALL);
      sf.AppendExecutionProvider_OpenVINO(OrtOpenVINOProviderOptions{});
#else
      fprintf(stderr, "OpenVINO is not supported in this build");
      return -1;
#endif
    }
    if (enable_cuda) {
#ifdef USE_CUDA
      OrtCUDAProviderOptions cuda_options;
      cuda_options.do_copy_in_default_stream = true;
      // TODO: Support arena configuration for users of test runner
      sf.AppendExecutionProvider_CUDA(cuda_options);
#else
      fprintf(stderr, "CUDA is not supported in this build");
      return -1;
#endif
    }
    if (enable_dnnl) {
#ifdef USE_DNNL
      // Generate dnnl_options to optimize dnnl performance
      OrtDnnlProviderOptions dnnl_options;
      dnnl_options.use_arena = enable_cpu_mem_arena ? 1 : 0;
      dnnl_options.threadpool_args = nullptr;
#if defined(DNNL_ORT_THREAD)
      dnnl_options.threadpool_args = static_cast<void*>(TestEnv::GetDefaultThreadPool(Env::Default()));
#endif  // defined(DNNL_ORT_THREAD)
      sf.AppendExecutionProvider_Dnnl(dnnl_options);
#else
      fprintf(stderr, "DNNL is not supported in this build");
      return -1;
#endif
    }
    if (enable_qnn) {
#ifdef USE_QNN
#ifdef _MSC_VER
      std::string option_string = ToUTF8String(ep_runtime_config_string);
#else
      std::string option_string = ep_runtime_config_string;
#endif
      std::istringstream ss(option_string);
      std::string token;
      std::unordered_map<std::string, std::string> qnn_options;

      while (ss >> token) {
        if (token == "") {
          continue;
        }
        auto pos = token.find("|");
        if (pos == std::string::npos || pos == 0 || pos == token.length()) {
          ORT_THROW("Use a '|' to separate the key and value for the run-time option you are trying to use.");
        }

        std::string key(token.substr(0, pos));
        std::string value(token.substr(pos + 1));

        if (key == "backend_path") {
          if (value.empty()) {
            ORT_THROW("Please provide the QNN backend path.");
          }
        } else if (key == "qnn_context_embed_mode") {
          if (value != "0") {
            ORT_THROW("Set to 0 to disable qnn_context_embed_mode.");
          }
        } else if (key == "qnn_context_cache_enable") {
          if (value != "1") {
            ORT_THROW("Set to 1 to enable qnn_context_cache_enable.");
          }
        } else if (key == "qnn_context_cache_path") {
          // no validation
        } else if (key == "profiling_level") {
          std::set<std::string> supported_profiling_level = {"off", "basic", "detailed"};
          if (supported_profiling_level.find(value) == supported_profiling_level.end()) {
            ORT_THROW("Supported profiling_level: off, basic, detailed");
          }
        } else if (key == "rpc_control_latency") {
          // no validation
        } else if (key == "htp_performance_mode") {
          std::set<std::string> supported_htp_perf_mode = {"burst", "balanced", "default", "high_performance",
                                                           "high_power_saver", "low_balanced", "low_power_saver",
                                                           "power_saver", "sustained_high_performance"};
          if (supported_htp_perf_mode.find(value) == supported_htp_perf_mode.end()) {
            std::ostringstream str_stream;
            std::copy(supported_htp_perf_mode.begin(), supported_htp_perf_mode.end(),
                      std::ostream_iterator<std::string>(str_stream, ","));
            std::string str = str_stream.str();
            ORT_THROW("Wrong value for htp_performance_mode. select from: " + str);
          }
        } else if (key == "qnn_context_priority") {
          std::set<std::string> supported_qnn_context_priority = {"low", "normal", "normal_high", "high"};
          if (supported_qnn_context_priority.find(value) == supported_qnn_context_priority.end()) {
            ORT_THROW("Supported qnn_context_priority: low, normal, normal_high, high");
          }
        } else if (key == "qnn_saver_path") {
          // no validation
        } else if (key == "htp_graph_finalization_optimization_mode") {
          std::unordered_set<std::string> supported_htp_graph_final_opt_modes = {"0", "1", "2", "3"};
          if (supported_htp_graph_final_opt_modes.find(value) == supported_htp_graph_final_opt_modes.end()) {
            std::ostringstream str_stream;
            std::copy(supported_htp_graph_final_opt_modes.begin(), supported_htp_graph_final_opt_modes.end(),
                      std::ostream_iterator<std::string>(str_stream, ","));
            std::string str = str_stream.str();
            ORT_THROW("Wrong value for htp_graph_finalization_optimization_mode. select from: " + str);
          }
        } else {
          ORT_THROW(R"(Wrong key type entered. Choose from options: ['backend_path', 'qnn_context_cache_enable',
'qnn_context_cache_path', 'profiling_level', 'rpc_control_latency', 'htp_performance_mode', 'qnn_saver_path',
'htp_graph_finalization_optimization_mode', 'qnn_context_priority'])");
        }

        qnn_options[key] = value;
      }
      sf.AppendExecutionProvider("QNN", qnn_options);
#else
      fprintf(stderr, "QNN is not supported in this build");
      return -1;
#endif
    }
    if (enable_nnapi) {
#ifdef USE_NNAPI
      Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_Nnapi(sf, 0));
#else
      fprintf(stderr, "NNAPI is not supported in this build");
      return -1;
#endif
    }
    if (enable_coreml) {
#ifdef USE_COREML
      Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(sf, 0));
#else
      fprintf(stderr, "CoreML is not supported in this build");
      return -1;
#endif
    }
    if (enable_snpe) {
#ifdef USE_SNPE
#ifdef _MSC_VER
      std::string option_string = ToUTF8String(ep_runtime_config_string);
#else
      std::string option_string = ep_runtime_config_string;
#endif
      std::istringstream ss(option_string);
      std::string token;
      std::unordered_map<std::string, std::string> snpe_options;

      while (ss >> token) {
        if (token == "") {
          continue;
        }
        auto pos = token.find("|");
        if (pos == std::string::npos || pos == 0 || pos == token.length()) {
          ORT_THROW(R"(Use a '|' to separate the key and value for
the run-time option you are trying to use.\n)");
        }

        std::string key(token.substr(0, pos));
        std::string value(token.substr(pos + 1));

        if (key == "runtime") {
          std::set<std::string> supported_runtime = {"CPU", "GPU_FP32", "GPU", "GPU_FLOAT16", "DSP", "AIP_FIXED_TF"};
          if (supported_runtime.find(value) == supported_runtime.end()) {
            ORT_THROW(R"(Wrong configuration value for the key 'runtime'.
select from 'CPU', 'GPU_FP32', 'GPU', 'GPU_FLOAT16', 'DSP', 'AIP_FIXED_TF'. \n)");
          }
        } else if (key == "priority") {
          // no validation
        } else if (key == "buffer_type") {
          std::set<std::string> supported_buffer_type = {"TF8", "TF16", "UINT8", "FLOAT", "ITENSOR"};
          if (supported_buffer_type.find(value) == supported_buffer_type.end()) {
            ORT_THROW(R"(Wrong configuration value for the key 'buffer_type'.
select from 'TF8', 'TF16', 'UINT8', 'FLOAT', 'ITENSOR'. \n)");
          }
        } else if (key == "enable_init_cache") {
          if (value != "1") {
            ORT_THROW("Set to 1 to enable_init_cache.");
          }
        } else {
          ORT_THROW("Wrong key type entered. Choose from options: ['runtime', 'priority', 'buffer_type', 'enable_init_cache'] \n");
        }

        snpe_options[key] = value;
      }

      sf.AppendExecutionProvider("SNPE", snpe_options);
#else
      fprintf(stderr, "SNPE is not supported in this build");
      return -1;
#endif
    }
    if (enable_dml) {
#ifdef USE_DML
      fprintf(stderr, "Disabling mem pattern and forcing single-threaded execution since DML is used");
      sf.DisableMemPattern();
      sf.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
      p_models = 1;
      concurrent_session_runs = 1;
      Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(sf, device_id));

      // Run inference with DX12 input
      fprintf(stderr, " calling RunDx12InputBenchmark: Executing DML inference with DX12 input using ORT");

      if (RunDx12InputBenchmark() == 0) {
        fprintf(stderr, "Successfully Executed DML inference with DX12 input using ORT");
      } else {
        fprintf(stderr, "Failed Executing DML inference with DX12 input using ORT");
      }

      
      
#else
      fprintf(stderr, "DML is not supported in this build");
      return -1;
#endif
    }
    if (enable_acl) {
#ifdef USE_ACL
      Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_ACL(sf, enable_cpu_mem_arena ? 1 : 0));
#else
      fprintf(stderr, "ACL is not supported in this build");
      return -1;
#endif
    }
    if (enable_armnn) {
#ifdef USE_ARMNN
      Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_ArmNN(sf, enable_cpu_mem_arena ? 1 : 0));
#else
      fprintf(stderr, "ArmNN is not supported in this build\n");
      return -1;
#endif
    }
    if (enable_rocm) {
#ifdef USE_ROCM
      OrtROCMProviderOptions rocm_options;
      rocm_options.do_copy_in_default_stream = true;
      // TODO: Support arena configuration for users of test runner
      sf.AppendExecutionProvider_ROCM(rocm_options);
#else
      fprintf(stderr, "ROCM is not supported in this build");
      return -1;
#endif
    }
    if (enable_migraphx) {
#ifdef USE_MIGRAPHX
      Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_MIGraphX(sf, device_id));
#else
      fprintf(stderr, "MIGRAPHX is not supported in this build");
      return -1;
#endif
    }

    if (enable_xnnpack) {
#ifdef USE_XNNPACK
      sf.AppendExecutionProvider("XNNPACK", {});
#else
      fprintf(stderr, "XNNPACK is not supported in this build");
      return -1;
#endif
    }

    if (user_graph_optimization_level_set) {
      sf.SetGraphOptimizationLevel(graph_optimization_level);
    }

    // TODO: Get these from onnx_backend_test_series_filters.jsonc.
    // Permanently exclude following tests because ORT support only opset staring from 7,
    // Please make no more changes to the list
    static const ORTCHAR_T* immutable_broken_tests[] =
        {
            ORT_TSTR("AvgPool1d"),
            ORT_TSTR("AvgPool1d_stride"),
            ORT_TSTR("AvgPool2d"),
            ORT_TSTR("AvgPool2d_stride"),
            ORT_TSTR("AvgPool3d"),
            ORT_TSTR("AvgPool3d_stride"),
            ORT_TSTR("AvgPool3d_stride1_pad0_gpu_input"),
            ORT_TSTR("BatchNorm1d_3d_input_eval"),
            ORT_TSTR("BatchNorm2d_eval"),
            ORT_TSTR("BatchNorm2d_momentum_eval"),
            ORT_TSTR("BatchNorm3d_eval"),
            ORT_TSTR("BatchNorm3d_momentum_eval"),
            ORT_TSTR("GLU"),
            ORT_TSTR("GLU_dim"),
            ORT_TSTR("Linear"),
            ORT_TSTR("PReLU_1d"),
            ORT_TSTR("PReLU_1d_multiparam"),
            ORT_TSTR("PReLU_2d"),
            ORT_TSTR("PReLU_2d_multiparam"),
            ORT_TSTR("PReLU_3d"),
            ORT_TSTR("PReLU_3d_multiparam"),
            ORT_TSTR("PoissonNLLLLoss_no_reduce"),
            ORT_TSTR("Softsign"),
            ORT_TSTR("operator_add_broadcast"),
            ORT_TSTR("operator_add_size1_broadcast"),
            ORT_TSTR("operator_add_size1_right_broadcast"),
            ORT_TSTR("operator_add_size1_singleton_broadcast"),
            ORT_TSTR("operator_addconstant"),
            ORT_TSTR("operator_addmm"),
            ORT_TSTR("operator_basic"),
            ORT_TSTR("operator_mm"),
            ORT_TSTR("operator_non_float_params"),
            ORT_TSTR("operator_params"),
            ORT_TSTR("operator_pow"),
            ORT_TSTR("bernoulli"),
            ORT_TSTR("bernoulli_double"),
            ORT_TSTR("bernoulli_seed")};

    // float 8 types are not supported by any language.
    static const ORTCHAR_T* float8_tests[] = {
        ORT_TSTR("cast_FLOAT16_to_FLOAT8E4M3FN"),
        ORT_TSTR("cast_FLOAT16_to_FLOAT8E4M3FNUZ"),
        ORT_TSTR("cast_FLOAT16_to_FLOAT8E5M2"),
        ORT_TSTR("cast_FLOAT16_to_FLOAT8E5M2FNUZ"),
        ORT_TSTR("cast_FLOAT8E4M3FNUZ_to_FLOAT"),
        ORT_TSTR("cast_FLOAT8E4M3FNUZ_to_FLOAT16"),
        ORT_TSTR("cast_FLOAT8E4M3FN_to_FLOAT"),
        ORT_TSTR("cast_FLOAT8E4M3FN_to_FLOAT16"),
        ORT_TSTR("cast_FLOAT8E5M2FNUZ_to_FLOAT"),
        ORT_TSTR("cast_FLOAT8E5M2FNUZ_to_FLOAT16"),
        ORT_TSTR("cast_FLOAT8E5M2_to_FLOAT"),
        ORT_TSTR("cast_FLOAT8E5M2_to_FLOAT16"),
        ORT_TSTR("cast_FLOAT_to_FLOAT8E4M3FN"),
        ORT_TSTR("cast_FLOAT_to_FLOAT8E4M3FNUZ"),
        ORT_TSTR("cast_FLOAT_to_FLOAT8E5M2"),
        ORT_TSTR("cast_FLOAT_to_FLOAT8E5M2FNUZ"),
        ORT_TSTR("cast_no_saturate_FLOAT16_to_FLOAT8E4M3FN"),
        ORT_TSTR("cast_no_saturate_FLOAT16_to_FLOAT8E4M3FNUZ"),
        ORT_TSTR("cast_no_saturate_FLOAT16_to_FLOAT8E5M2"),
        ORT_TSTR("cast_no_saturate_FLOAT16_to_FLOAT8E5M2FNUZ"),
        ORT_TSTR("cast_no_saturate_FLOAT_to_FLOAT8E4M3FN"),
        ORT_TSTR("cast_no_saturate_FLOAT_to_FLOAT8E4M3FNUZ"),
        ORT_TSTR("cast_no_saturate_FLOAT_to_FLOAT8E5M2"),
        ORT_TSTR("cast_no_saturate_FLOAT_to_FLOAT8E5M2FNUZ"),
        ORT_TSTR("castlike_FLOAT8E4M3FNUZ_to_FLOAT"),
        ORT_TSTR("castlike_FLOAT8E4M3FNUZ_to_FLOAT_expanded"),
        ORT_TSTR("castlike_FLOAT8E4M3FN_to_FLOAT"),
        ORT_TSTR("castlike_FLOAT8E4M3FN_to_FLOAT_expanded"),
        ORT_TSTR("castlike_FLOAT8E5M2FNUZ_to_FLOAT"),
        ORT_TSTR("castlike_FLOAT8E5M2FNUZ_to_FLOAT_expanded"),
        ORT_TSTR("castlike_FLOAT8E5M2_to_FLOAT"),
        ORT_TSTR("castlike_FLOAT8E5M2_to_FLOAT_expanded"),
        ORT_TSTR("castlike_FLOAT_to_BFLOAT16"),
        ORT_TSTR("castlike_FLOAT_to_BFLOAT16_expanded"),
        ORT_TSTR("castlike_FLOAT_to_FLOAT8E4M3FN"),
        ORT_TSTR("castlike_FLOAT_to_FLOAT8E4M3FNUZ"),
        ORT_TSTR("castlike_FLOAT_to_FLOAT8E4M3FNUZ_expanded"),
        ORT_TSTR("castlike_FLOAT_to_FLOAT8E4M3FN_expanded"),
        ORT_TSTR("castlike_FLOAT_to_FLOAT8E5M2"),
        ORT_TSTR("castlike_FLOAT_to_FLOAT8E5M2FNUZ"),
        ORT_TSTR("castlike_FLOAT_to_FLOAT8E5M2FNUZ_expanded"),
        ORT_TSTR("castlike_FLOAT_to_FLOAT8E5M2_expanded"),
        ORT_TSTR("dequantizelinear_e4m3fn"),
        ORT_TSTR("dequantizelinear_e5m2"),
        ORT_TSTR("quantizelinear_e4m3fn"),
        ORT_TSTR("quantizelinear_e5m2")};

    static const ORTCHAR_T* cuda_flaky_tests[] = {
        ORT_TSTR("fp16_inception_v1"),
        ORT_TSTR("fp16_shufflenet"), ORT_TSTR("fp16_tiny_yolov2")};
    static const ORTCHAR_T* dml_disabled_tests[] = {ORT_TSTR("mlperf_ssd_resnet34_1200"), ORT_TSTR("mlperf_ssd_mobilenet_300"), ORT_TSTR("mask_rcnn"), ORT_TSTR("faster_rcnn"), ORT_TSTR("tf_pnasnet_large"), ORT_TSTR("zfnet512"), ORT_TSTR("keras2coreml_Dense_ImageNet")};
    static const ORTCHAR_T* dnnl_disabled_tests[] = {ORT_TSTR("test_densenet121"), ORT_TSTR("test_resnet18v2"), ORT_TSTR("test_resnet34v2"), ORT_TSTR("test_resnet50v2"), ORT_TSTR("test_resnet101v2"),
                                                     ORT_TSTR("test_resnet101v2"), ORT_TSTR("test_vgg19"), ORT_TSTR("tf_inception_resnet_v2"), ORT_TSTR("tf_inception_v1"), ORT_TSTR("tf_inception_v3"), ORT_TSTR("tf_inception_v4"), ORT_TSTR("tf_mobilenet_v1_1.0_224"),
                                                     ORT_TSTR("tf_mobilenet_v2_1.0_224"), ORT_TSTR("tf_mobilenet_v2_1.4_224"), ORT_TSTR("tf_nasnet_large"), ORT_TSTR("tf_pnasnet_large"), ORT_TSTR("tf_resnet_v1_50"), ORT_TSTR("tf_resnet_v1_101"), ORT_TSTR("tf_resnet_v1_101"),
                                                     ORT_TSTR("tf_resnet_v2_101"), ORT_TSTR("tf_resnet_v2_152"), ORT_TSTR("batchnorm_example_training_mode"), ORT_TSTR("batchnorm_epsilon_training_mode")};
    static const ORTCHAR_T* qnn_disabled_tests[] = {
        ORT_TSTR("nllloss_NCd1d2d3_none_no_weight_negative_ii"),
        ORT_TSTR("nllloss_NCd1d2d3_none_no_weight_negative_ii_expanded"),
        ORT_TSTR("sce_NCd1d2d3_none_no_weight_negative_ii"),
        ORT_TSTR("sce_NCd1d2d3_none_no_weight_negative_ii_expanded"),
        ORT_TSTR("sce_NCd1d2d3_none_no_weight_negative_ii_log_prob"),
        ORT_TSTR("sce_NCd1d2d3_none_no_weight_negative_ii_log_prob_expanded"),
        ORT_TSTR("gather_negative_indices"),
        ORT_TSTR("nllloss_NCd1d2_with_weight_reduction_sum"),
        ORT_TSTR("nllloss_NCd1d2_with_weight_reduction_sum_ii_expanded"),
        ORT_TSTR("nllloss_NCd1d2_with_weight"),
        ORT_TSTR("nllloss_NCd1d2_with_weight_expanded"),
        ORT_TSTR("nllloss_NCd1d2_with_weight_reduction_sum_expanded"),
        ORT_TSTR("nllloss_NCd1d2_with_weight_reduction_sum_ii"),
        ORT_TSTR("nllloss_NCd1_weight_ii_expanded"),
        ORT_TSTR("nllloss_NCd1_ii_expanded"),
        ORT_TSTR("nllloss_NCd1d2_no_weight_reduction_mean_ii_expanded"),
        ORT_TSTR("sce_none_weights"),
        ORT_TSTR("sce_none_weights_log_prob"),
        ORT_TSTR("sce_NCd1d2d3_sum_weight_high_ii_log_prob"),
        ORT_TSTR("sce_NCd1d2d3_sum_weight_high_ii_log_prob_expanded"),
        ORT_TSTR("sce_NCd1d2d3_sum_weight_high_ii"),
        ORT_TSTR("sce_NCd1d2d3_sum_weight_high_ii_expanded"),
        ORT_TSTR("sce_none_weights_log_prob_expanded"),
        ORT_TSTR("sce_none_weights_expanded")};

    std::unordered_set<std::basic_string<ORTCHAR_T>> all_disabled_tests(std::begin(immutable_broken_tests), std::end(immutable_broken_tests));

    if (enable_cuda) {
      all_disabled_tests.insert(std::begin(cuda_flaky_tests), std::end(cuda_flaky_tests));
    }
    if (enable_dml) {
      all_disabled_tests.insert(std::begin(dml_disabled_tests), std::end(dml_disabled_tests));
    }
    if (enable_dnnl) {
      // these models run but disabled tests to keep memory utilization low
      // This will be removed after LRU implementation
      all_disabled_tests.insert(std::begin(dnnl_disabled_tests), std::end(dnnl_disabled_tests));
      all_disabled_tests.insert(std::begin(float8_tests), std::end(float8_tests));
    }
    if (enable_qnn) {
      all_disabled_tests.insert(std::begin(qnn_disabled_tests), std::end(qnn_disabled_tests));
      all_disabled_tests.insert(std::begin(float8_tests), std::end(float8_tests));
    }
#if !defined(__amd64__) && !defined(_M_AMD64)
    // out of memory
    static const ORTCHAR_T* x86_disabled_tests[] = {ORT_TSTR("mlperf_ssd_resnet34_1200"), ORT_TSTR("mask_rcnn_keras"), ORT_TSTR("mask_rcnn"), ORT_TSTR("faster_rcnn"), ORT_TSTR("vgg19"), ORT_TSTR("coreml_VGG16_ImageNet")};
    all_disabled_tests.insert(std::begin(x86_disabled_tests), std::end(x86_disabled_tests));
#endif

    auto broken_tests = GetBrokenTests(provider_name);
    auto broken_tests_keyword_set = GetBrokenTestsKeyWordSet(provider_name);
    std::vector<ITestCase*> tests;
    LoadTests(data_dirs, whitelisted_test_cases,
              LoadTestTolerances(enable_cuda, enable_openvino, override_tolerance, atol, rtol),
              all_disabled_tests,
              std::move(broken_tests),
              std::move(broken_tests_keyword_set),
              [&owned_tests, &tests](std::unique_ptr<ITestCase> l) {
                tests.push_back(l.get());
                owned_tests.push_back(std::move(l));
              });

    auto tp = TestEnv::CreateThreadPool(Env::Default());
    TestEnv test_env(env, sf, tp.get(), std::move(tests), stat);
    Status st = test_env.Run(p_models, concurrent_session_runs, repeat_count);
    if (!st.IsOK()) {
      fprintf(stderr, "%s\n", st.ErrorMessage().c_str());
      return -1;
    }
    std::string res = stat.ToString();
    fwrite(res.c_str(), 1, res.size(), stdout);
  }

  int result = 0;
  for (const auto& p : stat.GetFailedTest()) {
    fprintf(stderr, "test %s failed, please fix it\n", p.first.c_str());
    result = -1;
  }
  return result;
}
#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
  Ort::Env env{nullptr};
  int retval = -1;
  ORT_TRY {
    retval = real_main(argc, argv, env);
  }
  ORT_CATCH(const std::exception& ex) {
    ORT_HANDLE_EXCEPTION([&]() {
      fprintf(stderr, "%s\n", ex.what());
      retval = -1;
    });
  }

  ::google::protobuf::ShutdownProtobufLibrary();
  return retval;
}
