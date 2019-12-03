#include "testPch.h"

#include <d3dx12.h>
#include <gtest/gtest.h>

#include "winrt/Windows.Devices.Enumeration.Pnp.h"
#include "winrt/Windows.Graphics.DirectX.Direct3D11.h"
#include "winrt/Windows.Media.Capture.h"
#include "winrt/Windows.Media.h"
#include "winrt/Windows.Security.Cryptography.Core.h"
#include "winrt/Windows.Security.Cryptography.h"
#include "winrt/Windows.Storage.h"
#include "winrt/Windows.Storage.Streams.h"

// lame, but WinBase.h redefines this, which breaks winrt headers later
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include "CustomOperatorProvider.h"
#include "DeviceHelpers.h"
#include "filehelpers.h"
#include "robuffer.h"
#include "runtimeParameters.h"
#include "Windows.AI.MachineLearning.Native.h"
#include "Windows.Graphics.DirectX.Direct3D11.interop.h"
#include "windows.ui.xaml.media.dxinterop.h"
#include "winrt/Windows.UI.Xaml.Controls.h"
#include "winrt/Windows.UI.Xaml.Media.Imaging.h"
#include <d2d1.h>
#include <d3d11.h>
#include <initguid.h>
#include <MemoryBuffer.h>

#if __has_include("dxcore.h")
#define ENABLE_DXCORE 1
#endif
#ifdef ENABLE_DXCORE
#include <dxcore.h>
#endif

using namespace winrt;
using namespace winrt::Windows::AI::MachineLearning;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Graphics::DirectX;
using namespace ::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::UI::Xaml::Media::Imaging;

class ScenarioCppWinrtTest : public ::testing::Test
{
protected:
    ScenarioCppWinrtTest()
    {
        init_apartment();
    }
};

class ScenarioCppWinrtGpuTest : public ScenarioCppWinrtTest
{
protected:
    void SetUp() override
    {
        GPUTEST
    }
};

class ScenarioCppWinrtGpuSkipEdgeCoreTest : public ScenarioCppWinrtTest
{
protected:
    void SetUp() override
    {
        ScenarioCppWinrtTest::SetUp();
        SKIP_EDGECORE
    }
};

TEST_F(ScenarioCppWinrtTest, Sample1)
{
    LearningModel model = nullptr;
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    EXPECT_NO_THROW(model = LearningModel::LoadFromFilePath(filePath));
}

ILearningModelFeatureValue MakeTensor(const ITensorFeatureDescriptor& descriptor)
{
    auto dataType = descriptor.TensorKind();
    std::vector<int64_t> shape;
    int64_t size = 1;
    for (auto&& dim : descriptor.Shape())
    {
        if (dim == -1) dim = 1;
        shape.push_back(dim);
        size *= dim;
    }

    switch (dataType)
    {
    case TensorKind::Float:
    {
        std::vector<float> buffer;
        buffer.resize(size);
        auto ftv = TensorFloat::CreateFromIterable(shape, winrt::single_threaded_vector<float>(std::move(buffer)));
        return ftv;
    }
    default:
        throw_hresult(E_NOTIMPL);
        break;
    }
}

ILearningModelFeatureValue MakeImage(const IImageFeatureDescriptor& /*descriptor*/, winrt::Windows::Foundation::IInspectable data)
{
    VideoFrame videoFrame = nullptr;
    if (data != nullptr)
    {
        SoftwareBitmap sb = nullptr;
        data.as(sb);
        videoFrame = VideoFrame::CreateWithSoftwareBitmap(sb);
    }
    else
    {
        SoftwareBitmap sb = SoftwareBitmap(BitmapPixelFormat::Bgra8, 28, 28);
        videoFrame = VideoFrame::CreateWithSoftwareBitmap(sb);
    }
    auto imageValue = ImageFeatureValue::CreateFromVideoFrame(videoFrame);
    return imageValue;
}

ILearningModelFeatureValue FeatureValueFromFeatureValueDescriptor(ILearningModelFeatureDescriptor descriptor, winrt::Windows::Foundation::IInspectable data = nullptr)
{
    auto kind = descriptor.Kind();
    switch (kind)
    {
    case LearningModelFeatureKind::Image:
    {
        ImageFeatureDescriptor imageDescriptor = nullptr;
        descriptor.as(imageDescriptor);
        return MakeImage(imageDescriptor, data);
    }
    case LearningModelFeatureKind::Map:
        throw_hresult(E_NOTIMPL);
        break;
    case LearningModelFeatureKind::Sequence:
        throw_hresult(E_NOTIMPL);
        break;
    case LearningModelFeatureKind::Tensor:
    {
        TensorFeatureDescriptor tensorDescriptor = nullptr;
        descriptor.as(tensorDescriptor);
        return MakeTensor(tensorDescriptor);
    }
    default:
        throw_hresult(E_INVALIDARG);
        break;
    }
}

// helper method that populates a binding object with default data
static void BindFeatures(LearningModelBinding binding, IVectorView<ILearningModelFeatureDescriptor> features)
{
    for (auto&& feature : features)
    {
        auto featureValue = FeatureValueFromFeatureValueDescriptor(feature);
        // set an actual buffer here. we're using uninitialized data for simplicity.
        binding.Bind(feature.Name(), featureValue);
    }
}

//! Scenario1 : Load , bind, eval a model using all the system defaults (easy path)
TEST_F(ScenarioCppWinrtTest, Scenario1_LoadBindEvalDefault)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a session on the default device
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::Default));
    // create a binding set
    LearningModelBinding binding(session);
    // bind the input and the output buffers by name
    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        auto featureValue = FeatureValueFromFeatureValueDescriptor(input);
        // set an actual buffer here. we're using uninitialized data for simplicity.
        binding.Bind(input.Name(), featureValue);
    }
    // run eval
    EXPECT_NO_THROW(session.Evaluate(binding, L""));
}

//! Scenario2: Load a model from stream
//          - winRT, and win32
TEST_F(ScenarioCppWinrtTest, Scenario2_LoadModelFromStream)
{
    // get a stream
    std::wstring path = FileHelpers::GetModulePath() + L"model.onnx";
    auto storageFile = StorageFile::GetFileFromPathAsync(path).get();

    // load the stream
    Streams::IRandomAccessStreamReference streamref;
    storageFile.as(streamref);

    // load a model
    LearningModel model = nullptr;
    EXPECT_NO_THROW(model = LearningModel::LoadFromStreamAsync(streamref).get());
    EXPECT_TRUE(model != nullptr);
}

//! Scenario3: pass a SoftwareBitmap into a model
TEST_F(ScenarioCppWinrtGpuTest, Scenario3_SoftwareBitmapInputBinding)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a session on the default device
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::Default));
    // create a binding set
    LearningModelBinding binding(session);
    // bind the input and the output buffers by name
    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        // load the SoftwareBitmap
        SoftwareBitmap sb = FileHelpers::GetSoftwareBitmapFromFile(FileHelpers::GetModulePath() + L"fish.png");
        auto videoFrame = VideoFrame::CreateWithSoftwareBitmap(sb);
        auto imageValue = ImageFeatureValue::CreateFromVideoFrame(videoFrame);

        EXPECT_NO_THROW(binding.Bind(input.Name(), imageValue));
    }
    // run eval
    EXPECT_NO_THROW(session.Evaluate(binding, L""));
}

//! Scenario5: run an async eval
winrt::Windows::Foundation::IAsyncOperation<LearningModelEvaluationResult> DoEvalAsync()
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a session on the default device
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::Default));
    // create a binding set
    LearningModelBinding binding(session);
    // bind the input and the output buffers by name
    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        auto featureValue = FeatureValueFromFeatureValueDescriptor(input);
        // set an actual buffer here. we're using uninitialized data for simplicity.
        binding.Bind(input.Name(), featureValue);
    }
    // run eval async
    return session.EvaluateAsync(binding, L"");
}

TEST_F(ScenarioCppWinrtTest, Scenario5_AsyncEval)
{
    auto task = DoEvalAsync();

    while (task.Status() == winrt::Windows::Foundation::AsyncStatus::Started)
    {
        std::cout << "Waiting...\n";
        Sleep(30);
    }
    std::cout << "Done\n";
    EXPECT_NO_THROW(task.get());
}

//! Scenario6: use BindInputWithProperties - BitmapBounds, BitmapPixelFormat
// apparently this scenario is cut for rs5. - not cut, just rewprked. move props
// to the image value when that is checked in.
TEST_F(ScenarioCppWinrtGpuTest, Scenario6_BindWithProperties)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a session on the default device
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::Default));
    // create a binding set
    LearningModelBinding binding(session);
    // bind the input and the output buffers by name
    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        SoftwareBitmap sb = SoftwareBitmap(BitmapPixelFormat::Bgra8, 224, 224);
        auto videoFrame = VideoFrame::CreateWithSoftwareBitmap(sb);
        auto imageValue = ImageFeatureValue::CreateFromVideoFrame(videoFrame);

        PropertySet propertySet;

        // make a BitmapBounds
        BitmapBounds bounds;
        bounds.X = 0;
        bounds.Y = 0;
        bounds.Height = 100;
        bounds.Width = 100;

        auto bitmapsBoundsProperty = winrt::Windows::Foundation::PropertyValue::CreateUInt32Array({ bounds.X, bounds.Y, bounds.Width, bounds.Height });
        // insert it in the property set
        propertySet.Insert(L"BitmapBounds", bitmapsBoundsProperty);

        // make a BitmapPixelFormat
        BitmapPixelFormat bitmapPixelFormat = BitmapPixelFormat::Bgra8;
        // translate it to an int so it can be used as a PropertyValue;
        int intFromBitmapPixelFormat = static_cast<int>(bitmapPixelFormat);
        auto bitmapPixelFormatProperty = winrt::Windows::Foundation::PropertyValue::CreateInt32(intFromBitmapPixelFormat);
        // insert it in the property set
        propertySet.Insert(L"BitmapPixelFormat", bitmapPixelFormatProperty);

        // bind with properties
        EXPECT_NO_THROW(binding.Bind(input.Name(), imageValue, propertySet));
    }
    // run eval
    EXPECT_NO_THROW(session.Evaluate(binding, L""));
}

//! Scenario7: run eval without creating a binding object
TEST_F(ScenarioCppWinrtTest, Scenario7_EvalWithNoBind)
{
    auto map = winrt::single_threaded_map<hstring, winrt::Windows::Foundation::IInspectable>();

    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a session on the default device
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::Default));
    // enumerate feature descriptors and create features (but don't bind them)
    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        auto featureValue = FeatureValueFromFeatureValueDescriptor(input);
        map.Insert(input.Name(), featureValue);
    }
    // run eval
    EXPECT_NO_THROW(session.EvaluateFeaturesAsync(map, L"").get());
}

//! Scenario8: choose which device to run the model on - PreferredDeviceType, PreferredDevicePerformance, SetDeviceFromSurface, SetDevice
// create a session on the default device
TEST_F(ScenarioCppWinrtTest, Scenario8_SetDeviceSample_Default)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);

    LearningModelDevice anyDevice(LearningModelDeviceKind::Default);
    LearningModelSession anySession(model, anyDevice);
}

// create a session on the CPU device
TEST_F(ScenarioCppWinrtTest, Scenario8_SetDeviceSample_CPU)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);

    LearningModelDevice cpuDevice(LearningModelDeviceKind::Cpu);
    LearningModelSession cpuSession(model, cpuDevice);
}

// create a session on the default DML device
TEST_F(ScenarioCppWinrtGpuTest, Scenario8_SetDeviceSample_DefaultDirectX)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);

    LearningModelDevice dmlDeviceDefault(LearningModelDeviceKind::DirectX);
    LearningModelSession dmlSessionDefault(model, dmlDeviceDefault);
}

// create a session on the DML device that provides best power
TEST_F(ScenarioCppWinrtGpuTest, Scenario8_SetDeviceSample_MinPower)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);

    LearningModelDevice dmlDeviceMinPower(LearningModelDeviceKind::DirectXMinPower);
    LearningModelSession dmlSessionMinPower(model, dmlDeviceMinPower);
}

// create a session on the DML device that provides best perf
TEST_F(ScenarioCppWinrtGpuTest, Scenario8_SetDeviceSample_MaxPerf)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);

    LearningModelDevice dmlDeviceMaxPerf(LearningModelDeviceKind::DirectXHighPerformance);
    LearningModelSession dmlSessionMaxPerf(model, dmlDeviceMaxPerf);
}

// create a session on the same device my camera is on
TEST_F(ScenarioCppWinrtGpuTest, Scenario8_SetDeviceSample_MyCameraDevice)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);

    auto devices = winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(winrt::Windows::Devices::Enumeration::DeviceClass::VideoCapture).get();
    hstring deviceId;
    if (devices.Size() > 0)
    {
        auto device = devices.GetAt(0);
        deviceId = device.Id();
        auto deviceName = device.Name();
        auto enabled = device.IsEnabled();
        std::cout << "Found device " << deviceName.c_str() << ", enabled = " << enabled << "\n";
        winrt::Windows::Media::Capture::MediaCapture captureManager;
        winrt::Windows::Media::Capture::MediaCaptureInitializationSettings settings;
        settings.VideoDeviceId(deviceId);
        captureManager.InitializeAsync(settings).get();
        auto mediaCaptureSettings = captureManager.MediaCaptureSettings();
        auto direct3D11Device = mediaCaptureSettings.Direct3D11Device();
        LearningModelDevice dmlDeviceCamera = LearningModelDevice::CreateFromDirect3D11Device(direct3D11Device);
        LearningModelSession dmlSessionCamera(model, dmlDeviceCamera);
    }
    else
    {
        GTEST_SKIP() << "Test skipped because video capture device is missing";
    }
}

// create a device from D3D11 Device
TEST_F(ScenarioCppWinrtGpuSkipEdgeCoreTest, Scenario8_SetDeviceSample_D3D11Device)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);

    com_ptr<ID3D11Device> pD3D11Device = nullptr;
    com_ptr<ID3D11DeviceContext> pContext = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT result = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE::D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, pD3D11Device.put(), &fl, pContext.put());
    if (FAILED(result))
    {
        GTEST_SKIP() << "Test skipped because d3d11 device is missing";
    }

    // get dxgiDevice from d3ddevice
    com_ptr<IDXGIDevice> pDxgiDevice;
    pD3D11Device.get()->QueryInterface<IDXGIDevice>(pDxgiDevice.put());

    com_ptr<::IInspectable> pInspectable;
    CreateDirect3D11DeviceFromDXGIDevice(pDxgiDevice.get(), pInspectable.put());

    LearningModelDevice device = LearningModelDevice::CreateFromDirect3D11Device(
        pInspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>());
    LearningModelSession session(model, device);
}

// create a session on the a specific dx device that I chose some other way , note we have to use native interop here and pass a cmd queue
TEST_F(ScenarioCppWinrtGpuTest, Scenario8_SetDeviceSample_CustomCommandQueue)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);

    com_ptr<ID3D12Device> pD3D12Device = nullptr;
    DeviceHelpers::AdapterEnumerationSupport support;
    if (FAILED(DeviceHelpers::GetAdapterEnumerationSupport(&support)))
    {
        FAIL() << "Unable to load DXGI or DXCore";
        return;
    }
    HRESULT result = S_OK;
    if (support.has_dxgi)
    {
        EXPECT_NO_THROW(result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(pD3D12Device.put())));
    }
#ifdef ENABLE_DXCORE
    if (support.has_dxgi == false)
    {
        com_ptr<IDXCoreAdapterFactory> spFactory;
        DXCoreCreateAdapterFactory(IID_PPV_ARGS(spFactory.put()));
        const GUID gpuFilter[] = { DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS };
        com_ptr<IDXCoreAdapterList> spAdapterList;
        spFactory->CreateAdapterList(1, gpuFilter, IID_PPV_ARGS(spAdapterList.put()));
        com_ptr<IDXCoreAdapter> spAdapter;
        EXPECT_NO_THROW(spAdapterList->GetAdapter(0, IID_PPV_ARGS(spAdapter.put())));
        ::IUnknown* pAdapter = spAdapter.get();
        EXPECT_NO_THROW(result = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(pD3D12Device.put())));
    }
#endif

    if (FAILED(result))
    {
        GTEST_SKIP() << "Test skipped because d3d12 device is missing";
        return;
    }
    com_ptr<ID3D12CommandQueue> dxQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
    commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    pD3D12Device->CreateCommandQueue(&commandQueueDesc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&dxQueue));
    auto factory = get_activation_factory<LearningModelDevice, ILearningModelDeviceFactoryNative>();

    com_ptr<::IUnknown> spUnk;
    factory->CreateFromD3D12CommandQueue(dxQueue.get(), spUnk.put());

    auto dmlDeviceCustom = spUnk.as<LearningModelDevice>();
    LearningModelSession dmlSessionCustom(model, dmlDeviceCustom);
}


//pass a Tensor in as an input GPU
TEST_F(ScenarioCppWinrtGpuTest, DISABLED_Scenario9_LoadBindEval_InputTensorGPU)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"fns-candy.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);

    com_ptr<ID3D12Device> pD3D12Device;
    EXPECT_NO_THROW(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), pD3D12Device.put_void()));
    com_ptr<ID3D12CommandQueue> dxQueue;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
    commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    pD3D12Device->CreateCommandQueue(&commandQueueDesc, __uuidof(ID3D12CommandQueue), dxQueue.put_void());
    auto devicefactory = get_activation_factory<LearningModelDevice, ILearningModelDeviceFactoryNative>();
    auto tensorfactory = get_activation_factory<TensorFloat, ITensorStaticsNative>();


    com_ptr<::IUnknown> spUnk;
    EXPECT_NO_THROW(devicefactory->CreateFromD3D12CommandQueue(dxQueue.get(), spUnk.put()));

    LearningModelDevice dmlDeviceCustom = nullptr;
    EXPECT_NO_THROW(spUnk.as(dmlDeviceCustom));
    LearningModelSession dmlSessionCustom = nullptr;
    EXPECT_NO_THROW(dmlSessionCustom = LearningModelSession(model, dmlDeviceCustom));

    LearningModelBinding modelBinding(dmlSessionCustom);

    UINT64 bufferbytesize = 720 * 720 * 3 * sizeof(float);
    D3D12_HEAP_PROPERTIES heapProperties = {
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        D3D12_MEMORY_POOL_UNKNOWN,
        0,
        0
    };
    D3D12_RESOURCE_DESC resourceDesc = {
        D3D12_RESOURCE_DIMENSION_BUFFER,
        0,
        bufferbytesize,
        1,
        1,
        1,
        DXGI_FORMAT_UNKNOWN,
    { 1, 0 },
    D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    };

    com_ptr<ID3D12Resource> pGPUResource = nullptr;
    pD3D12Device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        __uuidof(ID3D12Resource),
        pGPUResource.put_void()
    );
    com_ptr<::IUnknown> spUnkTensor;
    TensorFloat input1imagetensor(nullptr);
    __int64 shape[4] = { 1,3, 720, 720 };
    tensorfactory->CreateFromD3D12Resource(pGPUResource.get(), shape, 4, spUnkTensor.put());
    spUnkTensor.try_as(input1imagetensor);

    auto feature = model.InputFeatures().First();
    EXPECT_NO_THROW(modelBinding.Bind(feature.Current().Name(), input1imagetensor));

    auto outputtensordescriptor = model.OutputFeatures().First().Current().as<ITensorFeatureDescriptor>();
    auto outputtensorshape = outputtensordescriptor.Shape();
    VideoFrame outputimage(
		BitmapPixelFormat::Rgba8,
		static_cast<int32_t>(outputtensorshape.GetAt(3)),
		static_cast<int32_t>(outputtensorshape.GetAt(2)));
    ImageFeatureValue outputTensor = ImageFeatureValue::CreateFromVideoFrame(outputimage);

    EXPECT_NO_THROW(modelBinding.Bind(model.OutputFeatures().First().Current().Name(), outputTensor));

    // Testing GetAsD3D12Resource
    com_ptr<ID3D12Resource> pReturnedResource;
    input1imagetensor.as<ITensorNative>()->GetD3D12Resource(pReturnedResource.put());
    EXPECT_EQ(pReturnedResource.get(), pGPUResource.get());

    // Evaluate the model
    winrt::hstring correlationId;
    dmlSessionCustom.EvaluateAsync(modelBinding, correlationId).get();

}

TEST_F(ScenarioCppWinrtGpuTest, Scenario13_SingleModelOnCPUandGPU)
{
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    LearningModelSession cpuSession(model, LearningModelDevice(LearningModelDeviceKind::Cpu));
    LearningModelSession gpuSession(model, LearningModelDevice(LearningModelDeviceKind::DirectX));

    LearningModelBinding cpuBinding(cpuSession);
    LearningModelBinding gpuBinding(gpuSession);
    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        auto cpuFeatureValue = FeatureValueFromFeatureValueDescriptor(input);
        cpuBinding.Bind(input.Name(), cpuFeatureValue);

        auto gpuFeatureValue = FeatureValueFromFeatureValueDescriptor(input);
        gpuBinding.Bind(input.Name(), gpuFeatureValue);
    }

    auto cpuTask = cpuSession.EvaluateAsync(cpuBinding, L"cpu");
    auto gpuTask = gpuSession.EvaluateAsync(gpuBinding, L"gpu");

    EXPECT_NO_THROW(cpuTask.get());
    EXPECT_NO_THROW(gpuTask.get());
}

// Validates when binding input image with free dimensions, the binding step is executed correctly.
TEST_F(ScenarioCppWinrtGpuTest, Scenario11_FreeDimenions_tensor)
{
    std::wstring filePath = FileHelpers::GetModulePath() + L"free_dimensional_image_input.onnx";
    // load a model with expected input size: -1 x -1
    auto model = LearningModel::LoadFromFilePath(filePath);
    auto session = LearningModelSession(model);
    auto binding = LearningModelBinding(session);

    VideoFrame inputImage(BitmapPixelFormat::Rgba8, 1000, 1000);
    ImageFeatureValue inputimagetensor = ImageFeatureValue::CreateFromVideoFrame(inputImage);

    auto feature = model.InputFeatures().First();
    binding.Bind(feature.Current().Name(), inputimagetensor);
    feature.MoveNext();
    binding.Bind(feature.Current().Name(), inputimagetensor);

    session.Evaluate(binding, L"");
}

TEST_F(ScenarioCppWinrtGpuTest, Scenario11_FreeDimenions_image)
{
    std::wstring filePath = FileHelpers::GetModulePath() + L"free_dimensional_imageDes.onnx";
    // load a model with expected input size: -1 x -1
    auto model = LearningModel::LoadFromFilePath(filePath);
    auto session = LearningModelSession(model);
    auto binding = LearningModelBinding(session);

    VideoFrame inputImage(BitmapPixelFormat::Bgra8, 1000, 1000);
    ImageFeatureValue inputimagetensor = ImageFeatureValue::CreateFromVideoFrame(inputImage);

    auto feature = model.InputFeatures().First();
    ImageFeatureDescriptor imageDescriptor = nullptr;
    feature.Current().as(imageDescriptor);
    binding.Bind(feature.Current().Name(), inputimagetensor);

    feature.MoveNext();
    feature.Current().as(imageDescriptor);
    binding.Bind(feature.Current().Name(), inputimagetensor);

    session.Evaluate(binding, L"");
}

struct SwapChainEntry
{
    LearningModelSession session;
    LearningModelBinding binding;
    winrt::Windows::Foundation::IAsyncOperation<LearningModelEvaluationResult> activetask;
    SwapChainEntry() :session(nullptr), binding(nullptr), activetask(nullptr)
    {}
};
void SubmitEval(LearningModel model, SwapChainEntry *sessionBindings, int swapchaindex)
{
    if (sessionBindings[swapchaindex].activetask != nullptr)
    {
        //make sure the previously submitted work for this swapchain index is complete before reusing resources
        sessionBindings[swapchaindex].activetask.get();
    }
    // bind the input and the output buffers by name
    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        auto featureValue = FeatureValueFromFeatureValueDescriptor(input);
        // set an actual buffer here. we're using uninitialized data for simplicity.
        sessionBindings[swapchaindex].binding.Bind(input.Name(), featureValue);
    }
    // submit an eval and wait for it to finish submitting work
    sessionBindings[swapchaindex].activetask = sessionBindings[swapchaindex].session.EvaluateAsync(sessionBindings[swapchaindex].binding, L"0");
    // return without waiting for the submit to finish, setup the completion handler
}

//Scenario14:Load single model, run it mutliple times on a single gpu device using a fast swapchain pattern
TEST_F(ScenarioCppWinrtGpuTest, Scenario14_RunModelSwapchain)
{
    const int swapchainentrycount = 3;
    SwapChainEntry sessionBindings[swapchainentrycount];

    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a session on gpu1
    LearningModelDevice dmlDevice = LearningModelDevice(LearningModelDeviceKind::DirectX);
    // create the swapchain style bindings to cycle through
    for (int i = 0; i < swapchainentrycount; i++)
    {
        sessionBindings[i].session = LearningModelSession(model, dmlDevice);
        sessionBindings[i].binding = LearningModelBinding(sessionBindings[i].session);
    }

    //submit 10 evaluations to 3 swapchain entries
    int swapchaindex = 0;
    for (int i = 0; i < 10; i++)
    {
        swapchaindex = swapchaindex % swapchainentrycount;
        SubmitEval(model, sessionBindings, (swapchaindex)++);
    }

    //wait for all work to be completed
    for (int i = 0; i < swapchainentrycount; i++)
    {
        if (sessionBindings[i].activetask != nullptr)
        {
            //make sure the previously submitted work for this swapchain index is compolete before resuing resources
            sessionBindings[i].activetask.get();
        }
    }
}
static void LoadBindEval_CustomOperator_CPU(const wchar_t* fileName)
{
    auto customOperatorProvider = winrt::make<CustomOperatorProvider>();
    auto provider = customOperatorProvider.as<ILearningModelOperatorProvider>();

    LearningModel model = LearningModel::LoadFromFilePath(fileName, provider);
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::Default));
    LearningModelBinding bindings(session);

    auto inputShape = std::vector<int64_t>{ 5 };
    auto inputData = std::vector<float>{ -50.f, -25.f, 0.f, 25.f, 50.f };
    auto inputValue =
        TensorFloat::CreateFromIterable(
            inputShape,
            single_threaded_vector<float>(std::move(inputData)).GetView());
    EXPECT_NO_THROW(bindings.Bind(L"X", inputValue));

    auto outputValue = TensorFloat::Create();
    EXPECT_NO_THROW(bindings.Bind(L"Y", outputValue));

    hstring correlationId;
    EXPECT_NO_THROW(session.Evaluate(bindings, correlationId));

    auto buffer = outputValue.GetAsVectorView();
    EXPECT_TRUE(buffer != nullptr);
}

//! Scenario17 : Control the dev diagnostics features of WinML Tracing
TEST_F(ScenarioCppWinrtTest, Scenario17_DevDiagnostics)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a session on the default device
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::Default));
    // create a binding set
    LearningModelBinding binding(session);
    // bind the input and the output buffers by name
    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        auto featureValue = FeatureValueFromFeatureValueDescriptor(input);
        // set an actual buffer here. we're using uninitialized data for simplicity.
        binding.Bind(input.Name(), featureValue);
    }
    session.EvaluationProperties().Insert(L"EnableDebugOutput", nullptr);
    // run eval
    EXPECT_NO_THROW(session.Evaluate(binding, L""));
}

// create a session that loads a model with a branch new operator, register the custom operator, and load/bind/eval
TEST_F(ScenarioCppWinrtTest, Scenario20a_LoadBindEval_CustomOperator_CPU)
{
    std::wstring filePath = FileHelpers::GetModulePath() + L"noisy_relu.onnx";
    LoadBindEval_CustomOperator_CPU(filePath.c_str());
}

// create a session that loads a model with an overridden operator, register the replacement custom operator, and load/bind/eval
TEST_F(ScenarioCppWinrtTest, Scenario20b_LoadBindEval_ReplacementCustomOperator_CPU)
{
    std::wstring filePath = FileHelpers::GetModulePath() + L"relu.onnx";
    LoadBindEval_CustomOperator_CPU(filePath.c_str());
}

//! Scenario21: Load two models, set them up to run chained after one another on the same gpu hardware device
TEST_F(ScenarioCppWinrtGpuTest, DISABLED_Scenario21_RunModel2ChainZ)
{
    // load a model, TODO: get a model that has an image descriptor
    std::wstring filePath = FileHelpers::GetModulePath() + L"fns-candy.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create both session on the default gpu
    LearningModelSession session1(model, LearningModelDevice(LearningModelDeviceKind::DirectX));
    LearningModelSession session2(model, LearningModelDevice(LearningModelDeviceKind::DirectX));
    // create both binding sets
    LearningModelBinding binding1(session1);
    LearningModelBinding binding2(session2);
    // get the input descriptor
    auto input = model.InputFeatures().GetAt(0);
    // load a SoftwareBitmap
    auto sb = FileHelpers::GetSoftwareBitmapFromFile(FileHelpers::GetModulePath() + L"fish_720.png");
    auto videoFrame = VideoFrame::CreateWithSoftwareBitmap(sb);
    // bind it
    binding1.Bind(input.Name(), videoFrame);
    // get the output descriptor
    auto output = model.OutputFeatures().GetAt(0);
    // create an empty output tensor since we don't want the first model to detensorize into an image.

    std::vector<int64_t> shape = { 1, 3, 720, 720 };
    auto outputValue = TensorFloat::Create(shape);  //   FeatureValueFromFeatureValueDescriptor(input, nullptr);
                                                    // now bind the(empty) output so we have a marker to chain with
    binding1.Bind(output.Name(), outputValue);
    // and leave the output unbound on the second model, we will fetch it later
    // run both models async
    EXPECT_NO_THROW(session1.EvaluateAsync(binding1, L""));

    // now bind that output to the next models input
    binding2.Bind(input.Name(), outputValue);

    //eval the second model
    auto session2AsyncOp = session2.EvaluateAsync(binding2, L"");

    // now get the output don't wait, queue up the next model
    auto finalOutput = session2AsyncOp.get().Outputs().First().Current().Value();
}

bool VerifyHelper(ImageFeatureValue actual, ImageFeatureValue expected)
{
    auto softwareBitmapActual = actual.VideoFrame().SoftwareBitmap();
    auto softwareBitmapExpected = expected.VideoFrame().SoftwareBitmap();
    EXPECT_EQ(softwareBitmapActual.PixelHeight(), softwareBitmapExpected.PixelHeight());
    EXPECT_EQ(softwareBitmapActual.PixelWidth(), softwareBitmapExpected.PixelWidth());
    EXPECT_EQ(softwareBitmapActual.BitmapPixelFormat(), softwareBitmapExpected.BitmapPixelFormat());

    // 4 means 4 channels
    uint32_t size = 4 * softwareBitmapActual.PixelHeight() * softwareBitmapActual.PixelWidth();

    winrt::Windows::Storage::Streams::Buffer actualOutputBuffer(size);
    winrt::Windows::Storage::Streams::Buffer expectedOutputBuffer(size);

    softwareBitmapActual.CopyToBuffer(actualOutputBuffer);
    softwareBitmapExpected.CopyToBuffer(expectedOutputBuffer);

    byte* actualBytes;
    actualOutputBuffer.try_as<::Windows::Storage::Streams::IBufferByteAccess>()->Buffer(&actualBytes);
    byte* expectedBytes;
    expectedOutputBuffer.try_as<::Windows::Storage::Streams::IBufferByteAccess>()->Buffer(&expectedBytes);

    byte* pActualByte = actualBytes;
    byte* pExpectedByte = expectedBytes;

    // hard code, might need to be modified later.
    const float cMaxErrorRate = 0.06f;
    byte epsilon = 20;

    UINT errors = 0;
    for (uint32_t i = 0; i < size; i++, pActualByte++, pExpectedByte++)
    {
        auto diff = (*pActualByte - *pExpectedByte);
        if (diff > epsilon)
        {
            errors++;
        }
    }
    std::cout << "total errors is " << errors << "/" << size << ", errors rate is " << (float)errors / size << "\n";

    return ((float)errors / size < cMaxErrorRate);
}

TEST_F(ScenarioCppWinrtTest, DISABLED_Scenario22_ImageBindingAsCPUTensor)
{
    std::wstring modulePath = FileHelpers::GetModulePath();
    std::wstring inputImagePath = modulePath + L"fish_720.png";
    std::wstring bmImagePath = modulePath + L"bm_fish_720.jpg";
    std::wstring modelPath = modulePath + L"fns-candy.onnx";

    auto device = LearningModelDevice(LearningModelDeviceKind::Default);
    auto model = LearningModel::LoadFromFilePath(modelPath);
    auto session = LearningModelSession(model, device);
    auto binding = LearningModelBinding(session);

    SoftwareBitmap softwareBitmap = FileHelpers::GetSoftwareBitmapFromFile(inputImagePath);
    softwareBitmap = SoftwareBitmap::Convert(softwareBitmap, BitmapPixelFormat::Bgra8);

    // Put softwareBitmap into buffer
    BYTE* pData = nullptr;
    UINT32 size = 0;
    winrt::Windows::Graphics::Imaging::BitmapBuffer spBitmapBuffer(softwareBitmap.LockBuffer(winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode::Read));
    winrt::Windows::Foundation::IMemoryBufferReference reference = spBitmapBuffer.CreateReference();
    auto spByteAccess = reference.as<::Windows::Foundation::IMemoryBufferByteAccess>();
    spByteAccess->GetBuffer(&pData, &size);

    std::vector<int64_t> shape = { 1, 3, softwareBitmap.PixelHeight() , softwareBitmap.PixelWidth() };
    float* pCPUTensor;
    uint32_t uCapacity;
    TensorFloat tf = TensorFloat::Create(shape);
    com_ptr<ITensorNative> itn = tf.as<ITensorNative>();
    itn->GetBuffer(reinterpret_cast<BYTE**>(&pCPUTensor), &uCapacity);

    uint32_t height = softwareBitmap.PixelHeight();
    uint32_t width = softwareBitmap.PixelWidth();
    for (UINT32 i = 0; i < size; i += 4)
    {
        UINT32 pixelInd = i / 4;
        pCPUTensor[pixelInd] = (float)pData[i];
        pCPUTensor[(height * width) + pixelInd] = (float)pData[i + 1];
        pCPUTensor[(height * width * 2) + pixelInd] = (float)pData[i + 2];
    }

    // Bind input
    binding.Bind(model.InputFeatures().First().Current().Name(), tf);

    // Bind output
    auto outputtensordescriptor = model.OutputFeatures().First().Current().as<ITensorFeatureDescriptor>();
    auto outputtensorshape = outputtensordescriptor.Shape();
    VideoFrame outputimage(
        BitmapPixelFormat::Bgra8,
        static_cast<int32_t>(outputtensorshape.GetAt(3)),
        static_cast<int32_t>(outputtensorshape.GetAt(2)));
    ImageFeatureValue outputTensor = ImageFeatureValue::CreateFromVideoFrame(outputimage);
    EXPECT_NO_THROW(binding.Bind(model.OutputFeatures().First().Current().Name(), outputTensor));

    // Evaluate the model
    winrt::hstring correlationId;
    EXPECT_NO_THROW(session.EvaluateAsync(binding, correlationId).get());

    // Verify the output by comparing with the benchmark image
    SoftwareBitmap bm_softwareBitmap = FileHelpers::GetSoftwareBitmapFromFile(bmImagePath);
    bm_softwareBitmap = SoftwareBitmap::Convert(bm_softwareBitmap, BitmapPixelFormat::Bgra8);
    VideoFrame bm_videoFrame = VideoFrame::CreateWithSoftwareBitmap(bm_softwareBitmap);
    ImageFeatureValue bm_imagevalue = ImageFeatureValue::CreateFromVideoFrame(bm_videoFrame);
    EXPECT_TRUE(VerifyHelper(bm_imagevalue, outputTensor));

    // check the output video frame object by saving output image to disk
    std::wstring outputDataImageFileName = L"out_cpu_tensor_fish_720.jpg";
    StorageFolder currentfolder = StorageFolder::GetFolderFromPathAsync(modulePath).get();
    StorageFile outimagefile = currentfolder.CreateFileAsync(outputDataImageFileName, CreationCollisionOption::ReplaceExisting).get();
    IRandomAccessStream writestream = outimagefile.OpenAsync(FileAccessMode::ReadWrite).get();
    BitmapEncoder encoder = BitmapEncoder::CreateAsync(BitmapEncoder::JpegEncoderId(), writestream).get();
    // Set the software bitmap
    encoder.SetSoftwareBitmap(outputimage.SoftwareBitmap());
    encoder.FlushAsync().get();
}

TEST_F(ScenarioCppWinrtGpuTest, DISABLED_Scenario22_ImageBindingAsGPUTensor)
{
    std::wstring modulePath = FileHelpers::GetModulePath();
    std::wstring inputImagePath = modulePath + L"fish_720.png";
    std::wstring bmImagePath = modulePath + L"bm_fish_720.jpg";
    std::wstring modelPath = modulePath + L"fns-candy.onnx";
    std::wstring outputDataImageFileName = L"out_gpu_tensor_fish_720.jpg";

    SoftwareBitmap softwareBitmap = FileHelpers::GetSoftwareBitmapFromFile(inputImagePath);
    softwareBitmap = SoftwareBitmap::Convert(softwareBitmap, BitmapPixelFormat::Bgra8);

    // Put softwareBitmap into cpu buffer
    BYTE* pData = nullptr;
    UINT32 size = 0;
    winrt::Windows::Graphics::Imaging::BitmapBuffer spBitmapBuffer(softwareBitmap.LockBuffer(winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode::Read));
    winrt::Windows::Foundation::IMemoryBufferReference reference = spBitmapBuffer.CreateReference();
    auto spByteAccess = reference.as<::Windows::Foundation::IMemoryBufferByteAccess>();
    spByteAccess->GetBuffer(&pData, &size);

    std::vector<int64_t> shape = { 1, 3, softwareBitmap.PixelHeight() , softwareBitmap.PixelWidth() };
    FLOAT* pCPUTensor;
    uint32_t uCapacity;

    // CPU tensorization
    TensorFloat tf = TensorFloat::Create(shape);
    com_ptr<ITensorNative> itn = tf.as<ITensorNative>();
    itn->GetBuffer(reinterpret_cast<BYTE**>(&pCPUTensor), &uCapacity);

    uint32_t height = softwareBitmap.PixelHeight();
    uint32_t width = softwareBitmap.PixelWidth();
    for (UINT32 i = 0; i < size; i += 4)
    {
        UINT32 pixelInd = i / 4;
        pCPUTensor[pixelInd] = (FLOAT)pData[i];
        pCPUTensor[(height * width) + pixelInd] = (FLOAT)pData[i + 1];
        pCPUTensor[(height * width * 2) + pixelInd] = (FLOAT)pData[i + 2];
    }

    // create the d3d device.
    com_ptr<ID3D12Device> pD3D12Device = nullptr;
    EXPECT_NO_THROW(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&pD3D12Device)));

    // create the command queue.
    com_ptr<ID3D12CommandQueue> dxQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
    commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    pD3D12Device->CreateCommandQueue(&commandQueueDesc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&dxQueue));
    auto devicefactory = get_activation_factory<LearningModelDevice, ILearningModelDeviceFactoryNative>();
    auto tensorfactory = get_activation_factory<TensorFloat, ITensorStaticsNative>();
    com_ptr<::IUnknown> spUnk;
    devicefactory->CreateFromD3D12CommandQueue(dxQueue.get(), spUnk.put());

    LearningModel model(nullptr);
    EXPECT_NO_THROW(model = LearningModel::LoadFromFilePath(modelPath));
    LearningModelDevice dmlDeviceCustom = nullptr;
    EXPECT_NO_THROW(spUnk.as(dmlDeviceCustom));
    LearningModelSession dmlSessionCustom = nullptr;
    EXPECT_NO_THROW(dmlSessionCustom = LearningModelSession(model, dmlDeviceCustom));
    LearningModelBinding modelBinding = nullptr;
    EXPECT_NO_THROW(modelBinding = LearningModelBinding(dmlSessionCustom));

    // Create ID3D12GraphicsCommandList and Allocator
    D3D12_COMMAND_LIST_TYPE queuetype = dxQueue->GetDesc().Type;
    com_ptr<ID3D12CommandAllocator> alloctor;
    com_ptr<ID3D12GraphicsCommandList> cmdList;

    pD3D12Device->CreateCommandAllocator(
        queuetype,
        winrt::guid_of<ID3D12CommandAllocator>(),
        alloctor.put_void());

    pD3D12Device->CreateCommandList(
        0,
        queuetype,
        alloctor.get(),
        nullptr,
        winrt::guid_of<ID3D12CommandList>(),
        cmdList.put_void());

    // Create Committed Resource
    // 3 is number of channels we use. R G B without alpha.
    UINT64 bufferbytesize = 3 * sizeof(float) * softwareBitmap.PixelWidth()*softwareBitmap.PixelHeight();
    D3D12_HEAP_PROPERTIES heapProperties = {
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        D3D12_MEMORY_POOL_UNKNOWN,
        0,
        0
    };
    D3D12_RESOURCE_DESC resourceDesc = {
        D3D12_RESOURCE_DIMENSION_BUFFER,
        0,
        bufferbytesize,
        1,
        1,
        1,
        DXGI_FORMAT_UNKNOWN,
    { 1, 0 },
    D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    };

    com_ptr<ID3D12Resource> pGPUResource = nullptr;
    com_ptr<ID3D12Resource> imageUploadHeap;
    pD3D12Device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        __uuidof(ID3D12Resource),
        pGPUResource.put_void()
    );

    // Create the GPU upload buffer.
    CD3DX12_HEAP_PROPERTIES props(D3D12_HEAP_TYPE_UPLOAD);
    auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bufferbytesize);
    EXPECT_NO_THROW(pD3D12Device->CreateCommittedResource(
        &props,
        D3D12_HEAP_FLAG_NONE,
        &buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource),
        imageUploadHeap.put_void()));

    // Copy from Cpu to GPU
    D3D12_SUBRESOURCE_DATA CPUData = {};
    CPUData.pData = reinterpret_cast<BYTE*>(pCPUTensor);
    CPUData.RowPitch = bufferbytesize;
    CPUData.SlicePitch = bufferbytesize;
    UpdateSubresources(cmdList.get(), pGPUResource.get(), imageUploadHeap.get(), 0, 0, 1, &CPUData);

    // Close the command list and execute it to begin the initial GPU setup.
    EXPECT_NO_THROW(cmdList->Close());
    ID3D12CommandList* ppCommandLists[] = { cmdList.get() };
    dxQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // GPU tensorize
    com_ptr<::IUnknown> spUnkTensor;
    TensorFloat input1imagetensor(nullptr);
    __int64 shapes[4] = { 1,3, softwareBitmap.PixelWidth(), softwareBitmap.PixelHeight() };
    tensorfactory->CreateFromD3D12Resource(pGPUResource.get(), shapes, 4, spUnkTensor.put());
    spUnkTensor.try_as(input1imagetensor);

    auto feature = model.InputFeatures().First();
    EXPECT_NO_THROW(modelBinding.Bind(feature.Current().Name(), input1imagetensor));

    auto outputtensordescriptor = model.OutputFeatures().First().Current().as<ITensorFeatureDescriptor>();
    auto outputtensorshape = outputtensordescriptor.Shape();
    VideoFrame outputimage(
        BitmapPixelFormat::Rgba8,
        static_cast<int32_t>(outputtensorshape.GetAt(3)),
        static_cast<int32_t>(outputtensorshape.GetAt(2)));
    ImageFeatureValue outputTensor = ImageFeatureValue::CreateFromVideoFrame(outputimage);

    EXPECT_NO_THROW(modelBinding.Bind(model.OutputFeatures().First().Current().Name(), outputTensor));

    // Evaluate the model
    winrt::hstring correlationId;
    dmlSessionCustom.EvaluateAsync(modelBinding, correlationId).get();

    // Verify the output by comparing with the benchmark image
    SoftwareBitmap bm_softwareBitmap = FileHelpers::GetSoftwareBitmapFromFile(bmImagePath);
    bm_softwareBitmap = SoftwareBitmap::Convert(bm_softwareBitmap, BitmapPixelFormat::Rgba8);
    VideoFrame bm_videoFrame = VideoFrame::CreateWithSoftwareBitmap(bm_softwareBitmap);
    ImageFeatureValue bm_imagevalue = ImageFeatureValue::CreateFromVideoFrame(bm_videoFrame);
    EXPECT_TRUE(VerifyHelper(bm_imagevalue, outputTensor));


    //check the output video frame object
    StorageFolder currentfolder = StorageFolder::GetFolderFromPathAsync(modulePath).get();
    StorageFile outimagefile = currentfolder.CreateFileAsync(outputDataImageFileName, CreationCollisionOption::ReplaceExisting).get();
    IRandomAccessStream writestream = outimagefile.OpenAsync(FileAccessMode::ReadWrite).get();
    BitmapEncoder encoder = BitmapEncoder::CreateAsync(BitmapEncoder::JpegEncoderId(), writestream).get();
    // Set the software bitmap
    encoder.SetSoftwareBitmap(outputimage.SoftwareBitmap());
    encoder.FlushAsync().get();
}

TEST_F(ScenarioCppWinrtTest, QuantizedModels)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"onnxzoo_lotus_inception_v1-dq.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a session on the default device
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::Default));
    // create a binding set
    LearningModelBinding binding(session);
    // bind the input and the output buffers by name
    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        auto featureValue = FeatureValueFromFeatureValueDescriptor(input);
        // set an actual buffer here. we're using uninitialized data for simplicity.
        binding.Bind(input.Name(), featureValue);
    }
    // run eval
    EXPECT_NO_THROW(session.Evaluate(binding, filePath));
}

TEST_F(ScenarioCppWinrtGpuTest, MsftQuantizedModels)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"coreml_Resnet50_ImageNet-dq.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::DirectX));
    // create a binding set
    LearningModelBinding binding(session);
    // bind the input and the output buffers by name

    std::wstring fullImagePath = FileHelpers::GetModulePath() + L"kitten_224.png";
    StorageFile imagefile = StorageFile::GetFileFromPathAsync(fullImagePath).get();
    IRandomAccessStream stream = imagefile.OpenAsync(FileAccessMode::Read).get();
    SoftwareBitmap softwareBitmap = (BitmapDecoder::CreateAsync(stream).get()).GetSoftwareBitmapAsync().get();

    auto inputs = model.InputFeatures();
    for (auto&& input : inputs)
    {
        auto featureValue = FeatureValueFromFeatureValueDescriptor(input, softwareBitmap);
        // set an actual buffer here. we're using uninitialized data for simplicity.
        binding.Bind(input.Name(), featureValue);
    }
    // run eval
    EXPECT_NO_THROW(session.Evaluate(binding, filePath));
}

TEST_F(ScenarioCppWinrtGpuTest, DISABLED_SyncVsAsync)
{
    // create model, device and session
    LearningModel model = nullptr;
    EXPECT_NO_THROW(model = LearningModel::LoadFromFilePath(FileHelpers::GetModulePath() + L"fns-candy.onnx"));

    LearningModelSession session = nullptr;
    EXPECT_NO_THROW(session = LearningModelSession(model, LearningModelDevice(LearningModelDeviceKind::DirectX)));

    // create the binding
    LearningModelBinding modelBinding(session);

    // bind the input
    std::wstring fullImagePath = FileHelpers::GetModulePath() + L"fish_720.png";
    StorageFile imagefile = StorageFile::GetFileFromPathAsync(fullImagePath).get();
    IRandomAccessStream stream = imagefile.OpenAsync(FileAccessMode::Read).get();
    SoftwareBitmap softwareBitmap = (BitmapDecoder::CreateAsync(stream).get()).GetSoftwareBitmapAsync().get();
    VideoFrame frame = VideoFrame::CreateWithSoftwareBitmap(softwareBitmap);

    auto imagetensor = ImageFeatureValue::CreateFromVideoFrame(frame);
    auto inputFeatureDescriptor = model.InputFeatures().First();
    EXPECT_NO_THROW(modelBinding.Bind(inputFeatureDescriptor.Current().Name(), imagetensor));

    UINT N = 20;

    auto outputtensordescriptor = model.OutputFeatures().First().Current().as<ITensorFeatureDescriptor>();
    auto outputtensorshape = outputtensordescriptor.Shape();
    VideoFrame outputimage(
        BitmapPixelFormat::Rgba8,
        static_cast<int32_t>(outputtensorshape.GetAt(3)),
        static_cast<int32_t>(outputtensorshape.GetAt(2)));
    ImageFeatureValue outputTensor = ImageFeatureValue::CreateFromVideoFrame(outputimage);
    EXPECT_NO_THROW(modelBinding.Bind(model.OutputFeatures().First().Current().Name(), outputTensor));

    // evaluate N times synchronously and time it
    auto startSync = std::chrono::high_resolution_clock::now();
    for (UINT i = 0; i < N; i++)
    {
        session.Evaluate(modelBinding, L"");
    }
    auto syncTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startSync);
    std::cout << "Synchronous time for " << N << " evaluations: " << syncTime.count() << " milliseconds\n";

    // evaluate N times Asynchronously and time it
    std::vector<winrt::Windows::Foundation::IAsyncOperation<LearningModelEvaluationResult>> tasks;
    std::vector<LearningModelBinding> bindings(N, nullptr);

    for (size_t i = 0; i < bindings.size(); i++)
    {
        bindings[i] = LearningModelBinding(session);
        bindings[i].Bind(inputFeatureDescriptor.Current().Name(), imagetensor);
        bindings[i].Bind(
            model.OutputFeatures().First().Current().Name(),
			VideoFrame(BitmapPixelFormat::Rgba8,
				static_cast<int32_t>(outputtensorshape.GetAt(3)),
				static_cast<int32_t>(outputtensorshape.GetAt(2))));
    }

    auto startAsync = std::chrono::high_resolution_clock::now();
    for (UINT i = 0; i < N; i++)
    {
        tasks.emplace_back(session.EvaluateAsync(bindings[i], L""));
    }
    // wait for them all to complete
    for (auto&& task : tasks)
    {
        task.get();
    }
    auto asyncTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startAsync);
    std::cout << "Asynchronous time for " << N << " evaluations: " << asyncTime.count() << " milliseconds\n";
}


TEST_F(ScenarioCppWinrtGpuTest, DISABLED_CustomCommandQueueWithFence)
{
    static const wchar_t* const modelFileName = L"fns-candy.onnx";
    static const wchar_t* const inputDataImageFileName = L"fish_720.png";

    com_ptr<ID3D12Device> d3d12Device;
    EXPECT_HRESULT_SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), d3d12Device.put_void()));

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    com_ptr<ID3D12CommandQueue> queue;
    EXPECT_HRESULT_SUCCEEDED(d3d12Device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), queue.put_void()));

    com_ptr<ID3D12Fence> fence;
    EXPECT_HRESULT_SUCCEEDED(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), fence.put_void()));

    auto devicefactory = get_activation_factory<LearningModelDevice, ILearningModelDeviceFactoryNative>();

    com_ptr<::IUnknown> learningModelDeviceUnknown;
    EXPECT_HRESULT_SUCCEEDED(devicefactory->CreateFromD3D12CommandQueue(queue.get(), learningModelDeviceUnknown.put()));

    LearningModelDevice device = nullptr;
    EXPECT_NO_THROW(learningModelDeviceUnknown.as(device));

    std::wstring modulePath = FileHelpers::GetModulePath();

    // WinML model creation
    std::wstring fullModelPath = modulePath + modelFileName;
    LearningModel model(nullptr);
    EXPECT_NO_THROW(model = LearningModel::LoadFromFilePath(fullModelPath));
    LearningModelSession modelSession = nullptr;
    EXPECT_NO_THROW(modelSession = LearningModelSession(model, device));
    LearningModelBinding modelBinding = nullptr;
    EXPECT_NO_THROW(modelBinding = LearningModelBinding(modelSession));

    std::wstring fullImagePath = modulePath + inputDataImageFileName;

    StorageFile imagefile = StorageFile::GetFileFromPathAsync(fullImagePath).get();
    IRandomAccessStream stream = imagefile.OpenAsync(FileAccessMode::Read).get();
    SoftwareBitmap softwareBitmap = (BitmapDecoder::CreateAsync(stream).get()).GetSoftwareBitmapAsync().get();
    VideoFrame frame = VideoFrame::CreateWithSoftwareBitmap(softwareBitmap);
    ImageFeatureValue input1imagetensor = ImageFeatureValue::CreateFromVideoFrame(frame);

    auto feature = model.InputFeatures().First();
    EXPECT_NO_THROW(modelBinding.Bind(feature.Current().Name(), input1imagetensor));

    auto outputtensordescriptor = model.OutputFeatures().First().Current().as<ITensorFeatureDescriptor>();
    auto outputtensorshape = outputtensordescriptor.Shape();
    VideoFrame outputimage(
        BitmapPixelFormat::Rgba8,
        static_cast<int32_t>(outputtensorshape.GetAt(3)),
        static_cast<int32_t>(outputtensorshape.GetAt(2)));
    ImageFeatureValue outputTensor = ImageFeatureValue::CreateFromVideoFrame(outputimage);

    EXPECT_NO_THROW(modelBinding.Bind(model.OutputFeatures().First().Current().Name(), outputTensor));

    // Block the queue on the fence, evaluate the model, then queue a signal. The model evaluation should not complete
    // until after the wait is unblocked, and the signal should not complete until model evaluation does. This can
    // only be true if WinML executes the workload on the supplied queue (instead of using its own).

    EXPECT_HRESULT_SUCCEEDED(queue->Wait(fence.get(), 1));

    EXPECT_HRESULT_SUCCEEDED(queue->Signal(fence.get(), 2));

    winrt::hstring correlationId;
    winrt::Windows::Foundation::IAsyncOperation<LearningModelEvaluationResult> asyncOp;
    EXPECT_NO_THROW(asyncOp = modelSession.EvaluateAsync(modelBinding, correlationId));

    Sleep(1000); // Give the model a chance to run (which it shouldn't if everything is working correctly)

    // Because we haven't unblocked the wait yet, model evaluation must not have completed (nor the fence signal)
    EXPECT_NE(asyncOp.Status(), winrt::Windows::Foundation::AsyncStatus::Completed);
    EXPECT_EQ(fence->GetCompletedValue(), 0);

    // Unblock the queue
    EXPECT_HRESULT_SUCCEEDED(fence->Signal(1));

    // Wait for model evaluation to complete
    asyncOp.get();

    // The fence must be signaled by now (because model evaluation has completed)
    EXPECT_EQ(fence->GetCompletedValue(), 2);
}

TEST_F(ScenarioCppWinrtGpuTest, DISABLED_ReuseVideoFrame)
{
    std::wstring modulePath = FileHelpers::GetModulePath();
    std::wstring inputImagePath = modulePath + L"fish_720.png";
    std::wstring bmImagePath = modulePath + L"bm_fish_720.jpg";
    std::wstring modelPath = modulePath + L"fns-candy.onnx";

    std::vector<LearningModelDeviceKind> deviceKinds = { LearningModelDeviceKind::Cpu, LearningModelDeviceKind::DirectX };
    std::vector<std::string> videoFrameSources;
    DeviceHelpers::AdapterEnumerationSupport support;
    DeviceHelpers::GetAdapterEnumerationSupport(&support);
    if (support.has_dxgi)
    {
        videoFrameSources = { "SoftwareBitmap", "Direct3DSurface" };
    }
    else {
        videoFrameSources = { "SoftwareBitmap" };

    }

    for (auto deviceKind : deviceKinds)
    {
        auto device = LearningModelDevice(deviceKind);
        auto model = LearningModel::LoadFromFilePath(modelPath);
        auto session = LearningModelSession(model, device);
        auto binding = LearningModelBinding(session);
        for (auto videoFrameSource : videoFrameSources)
        {
            VideoFrame reuseVideoFrame = nullptr;
            if (videoFrameSource == "SoftwareBitmap")
            {
                reuseVideoFrame = VideoFrame::CreateWithSoftwareBitmap(SoftwareBitmap(BitmapPixelFormat::Bgra8, 720, 720));
            }
            else
            {
                reuseVideoFrame = VideoFrame::CreateAsDirect3D11SurfaceBacked(DirectXPixelFormat::B8G8R8X8UIntNormalized, 720, 720);
            }
            for (uint32_t i = 0; i < 3; ++i)
            {
                SoftwareBitmap softwareBitmap = FileHelpers::GetSoftwareBitmapFromFile(inputImagePath);
                VideoFrame videoFrame = VideoFrame::CreateWithSoftwareBitmap(softwareBitmap);
                // reuse video frame
                videoFrame.CopyToAsync(reuseVideoFrame).get();

                // bind input
                binding.Bind(model.InputFeatures().First().Current().Name(), reuseVideoFrame);

                // bind output
                VideoFrame outputimage(BitmapPixelFormat::Bgra8, 720, 720);
                ImageFeatureValue outputTensor = ImageFeatureValue::CreateFromVideoFrame(outputimage);
                EXPECT_NO_THROW(binding.Bind(model.OutputFeatures().First().Current().Name(), outputTensor));

                // evaluate
                winrt::hstring correlationId;
                EXPECT_NO_THROW(session.EvaluateAsync(binding, correlationId).get());

                // verify result
                SoftwareBitmap bm_softwareBitmap = FileHelpers::GetSoftwareBitmapFromFile(bmImagePath);
                bm_softwareBitmap = SoftwareBitmap::Convert(bm_softwareBitmap, BitmapPixelFormat::Bgra8);
                VideoFrame bm_videoFrame = VideoFrame::CreateWithSoftwareBitmap(bm_softwareBitmap);
                ImageFeatureValue bm_imagevalue = ImageFeatureValue::CreateFromVideoFrame(bm_videoFrame);
                EXPECT_TRUE(VerifyHelper(bm_imagevalue, outputTensor));
            }
        }
    }
}

TEST_F(ScenarioCppWinrtTest, EncryptedStream)
{
    // get a stream
    std::wstring path = FileHelpers::GetModulePath() + L"model.onnx";
    auto storageFile = StorageFile::GetFileFromPathAsync(path).get();
    auto fileBuffer = winrt::Windows::Storage::FileIO::ReadBufferAsync(storageFile).get();

    // encrypt
    auto algorithmName = winrt::Windows::Security::Cryptography::Core::SymmetricAlgorithmNames::AesCbcPkcs7();
    auto algorithm = winrt::Windows::Security::Cryptography::Core::SymmetricKeyAlgorithmProvider::OpenAlgorithm(algorithmName);
    uint32_t keyLength = 32;
    auto keyBuffer = winrt::Windows::Security::Cryptography::CryptographicBuffer::GenerateRandom(keyLength);
    auto key = algorithm.CreateSymmetricKey(keyBuffer);
    auto iv = winrt::Windows::Security::Cryptography::CryptographicBuffer::GenerateRandom(algorithm.BlockLength());
    auto encryptedBuffer = winrt::Windows::Security::Cryptography::Core::CryptographicEngine::Encrypt(key, fileBuffer, iv);

    // verify loading the encrypted stream fails appropriately.
    auto encryptedStream = InMemoryRandomAccessStream();
    encryptedStream.WriteAsync(encryptedBuffer).get();
    EXPECT_THROW_SPECIFIC(LearningModel::LoadFromStream(RandomAccessStreamReference::CreateFromStream(encryptedStream)),
        winrt::hresult_error,
        [](const winrt::hresult_error& e) -> bool
        {
            return e.code() == E_INVALIDARG;
        });

    // now decrypt
    auto decryptedBuffer = winrt::Windows::Security::Cryptography::Core::CryptographicEngine::Decrypt(key, encryptedBuffer, iv);
    auto decryptedStream = InMemoryRandomAccessStream();
    decryptedStream.WriteAsync(decryptedBuffer).get();

    // load!
    LearningModel model = nullptr;
    EXPECT_NO_THROW(model = LearningModel::LoadFromStream(RandomAccessStreamReference::CreateFromStream(decryptedStream)));
    LearningModelSession session = nullptr;
    EXPECT_NO_THROW(session = LearningModelSession(model));
}

TEST_F(ScenarioCppWinrtGpuTest, DeviceLostRecovery)
{
    // load a model
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a session on the DirectX device
    LearningModelSession session(model, LearningModelDevice(LearningModelDeviceKind::DirectX));
    // create a binding set
    LearningModelBinding binding(session);
    // bind the inputs
    BindFeatures(binding, model.InputFeatures());

    // force device lost here
    {
        winrt::com_ptr<ID3D12Device5> d3d12Device;
        D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device5), d3d12Device.put_void());
        d3d12Device->RemoveDevice();
    }

    // evaluate should fail
    try
    {
        session.Evaluate(binding, L"");
        FAIL() << "Evaluate should fail after removing the device";
    }
    catch(...)
    {
    }

    // remove all references to the device by reseting the session and binding.
    session = nullptr;
    binding = nullptr;

    // create new session and binding and try again!
    EXPECT_NO_THROW(session = LearningModelSession(model, LearningModelDevice(LearningModelDeviceKind::DirectX)));
    EXPECT_NO_THROW(binding = LearningModelBinding(session));
    BindFeatures(binding, model.InputFeatures());
    EXPECT_NO_THROW(session.Evaluate(binding, L""));
}

TEST_F(ScenarioCppWinrtGpuSkipEdgeCoreTest, D2DInterop)
{
    // load a model (model.onnx == squeezenet[1,3,224,224])
    std::wstring filePath = FileHelpers::GetModulePath() + L"model.onnx";
    LearningModel model = LearningModel::LoadFromFilePath(filePath);
    // create a dx12 device
    com_ptr<ID3D12Device1> device = nullptr;
    EXPECT_HRESULT_SUCCEEDED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device1), device.put_void()));
    // now create a command queue from it
    com_ptr<ID3D12CommandQueue> commandQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    EXPECT_HRESULT_SUCCEEDED(device->CreateCommandQueue(&queueDesc, winrt::guid_of<ID3D12CommandQueue>(), commandQueue.put_void()));
    // create a winml learning device based on that dx12 queue
    auto factory = get_activation_factory<LearningModelDevice, ILearningModelDeviceFactoryNative>();
    com_ptr<::IUnknown> spUnk;
    EXPECT_HRESULT_SUCCEEDED(factory->CreateFromD3D12CommandQueue(commandQueue.get(), spUnk.put()));
    auto learningDevice = spUnk.as<LearningModelDevice>();
    // create a winml session from that dx device
    LearningModelSession session(model, learningDevice);
    // now lets try and do some XAML/d2d on that same device, first prealloc a VideoFrame
    VideoFrame frame = VideoFrame::CreateAsDirect3D11SurfaceBacked(
        DirectXPixelFormat::B8G8R8A8UIntNormalized,
        224,
        224,
        session.Device().Direct3D11Device()
        );
    // create a D2D factory
    D2D1_FACTORY_OPTIONS options = {};
    com_ptr<ID2D1Factory> d2dFactory;
    EXPECT_HRESULT_SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options, d2dFactory.put_void()));
    // grab the dxgi surface back from our video frame
    com_ptr<IDXGISurface> dxgiSurface;
    com_ptr<IDirect3DDxgiInterfaceAccess> dxgiInterfaceAccess = frame.Direct3DSurface().as<IDirect3DDxgiInterfaceAccess>();
    EXPECT_HRESULT_SUCCEEDED(dxgiInterfaceAccess->GetInterface(__uuidof(IDXGISurface), dxgiSurface.put_void()));
    // and try and use our surface to create a render targer
    com_ptr<ID2D1RenderTarget> renderTarget;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.pixelFormat = D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM,
        D2D1_ALPHA_MODE_IGNORE
    );
    EXPECT_HRESULT_SUCCEEDED(d2dFactory->CreateDxgiSurfaceRenderTarget(
        dxgiSurface.get(),
        props,
        renderTarget.put()));
}
