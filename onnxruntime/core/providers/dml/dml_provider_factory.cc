// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <dxcore.h>
#include <vector>

#include <DirectML.h>
#ifndef _GAMING_XBOX
#include <dxgi1_4.h>
#endif

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <wil/wrl.h>
#include <wil/result.h>

#include "core/providers/dml/dml_provider_factory.h"
#include "core/providers/dml/dml_provider_factory_creator.h"
#include "core/session/abi_session_options_impl.h"
#include "core/session/allocator_adapters.h"
#include "core/session/ort_apis.h"
#include "core/framework/error_code_helper.h"
#include "DmlExecutionProvider/src/ErrorHandling.h"
#include "DmlExecutionProvider/src/GraphicsUnknownHelper.h"
#include "DmlExecutionProvider/inc/DmlExecutionProvider.h"
#include "core/platform/env.h"

namespace onnxruntime {

struct DMLProviderFactory : IExecutionProviderFactory {
  DMLProviderFactory(IDMLDevice* dml_device,
                     ID3D12CommandQueue* cmd_queue) : dml_device_(dml_device),
                                                      cmd_queue_(cmd_queue) {}
  ~DMLProviderFactory() override {}

  std::unique_ptr<IExecutionProvider> CreateProvider() override;
  void SetDefaultRoundingMode(AllocatorRoundingMode rounding_mode);

  void SetMetacommandsEnabled(bool metacommands_enabled);

 private:
  ComPtr<IDMLDevice> dml_device_{};
  ComPtr<ID3D12CommandQueue> cmd_queue_{};
  AllocatorRoundingMode rounding_mode_ = AllocatorRoundingMode::Enabled;
  bool metacommands_enabled_ = true;
};

std::unique_ptr<IExecutionProvider> DMLProviderFactory::CreateProvider() {
  auto provider = Dml::CreateExecutionProvider(dml_device_.Get(), cmd_queue_.Get(), metacommands_enabled_);
  Dml::SetDefaultRoundingMode(provider.get(), rounding_mode_);
  return provider;
}

void DMLProviderFactory::SetDefaultRoundingMode(AllocatorRoundingMode rounding_mode) {
  rounding_mode_ = rounding_mode;
}

void DMLProviderFactory::SetMetacommandsEnabled(bool metacommands_enabled) {
  metacommands_enabled_ = metacommands_enabled;
}

std::shared_ptr<IExecutionProviderFactory> CreateExecutionProviderFactory_DML(IDMLDevice* dml_device,
                                                                              ID3D12CommandQueue* cmd_queue) {
#ifndef _GAMING_XBOX
  // Validate that the D3D12 devices match between DML and the command queue. This specifically asks for IUnknown in
  // order to be able to compare the pointers for COM object identity.
  ComPtr<IUnknown> d3d12_device_0;
  ComPtr<IUnknown> d3d12_device_1;
  ORT_THROW_IF_FAILED(dml_device->GetParentDevice(IID_PPV_ARGS(&d3d12_device_0)));
  ORT_THROW_IF_FAILED(cmd_queue->GetDevice(IID_PPV_ARGS(&d3d12_device_1)));

  if (d3d12_device_0 != d3d12_device_1) {
    ORT_THROW_HR(E_INVALIDARG);
  }
#endif

  ComPtr<ID3D12Device> d3d12_device;
  ORT_THROW_IF_FAILED(dml_device->GetParentDevice(IID_GRAPHICS_PPV_ARGS(d3d12_device.ReleaseAndGetAddressOf())));
  const Env& env = Env::Default();
  auto luid = d3d12_device->GetAdapterLuid();
  env.GetTelemetryProvider().LogExecutionProviderEvent(&luid);
  return std::make_shared<onnxruntime::DMLProviderFactory>(dml_device, cmd_queue);
}

void DmlConfigureProviderFactoryDefaultRoundingMode(IExecutionProviderFactory* factory, AllocatorRoundingMode rounding_mode) {
  auto dml_provider_factory = static_cast<DMLProviderFactory*>(factory);
  dml_provider_factory->SetDefaultRoundingMode(rounding_mode);
}

void DmlConfigureProviderFactoryMetacommandsEnabled(IExecutionProviderFactory* factory, bool metacommandsEnabled) {
  auto dml_provider_factory = static_cast<DMLProviderFactory*>(factory);
  dml_provider_factory->SetMetacommandsEnabled(metacommandsEnabled);
}


bool IsSoftwareAdapter(IDXGIAdapter1* adapter) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);

    // see here for documentation on filtering WARP adapter:
    // https://docs.microsoft.com/en-us/windows/desktop/direct3ddxgi/d3d10-graphics-programming-guide-dxgi#new-info-about-enumerating-adapters-for-windows-8
    auto isBasicRenderDriverVendorId = desc.VendorId == 0x1414;
    auto isBasicRenderDriverDeviceId = desc.DeviceId == 0x8c;
    auto isSoftwareAdapter = desc.Flags == DXGI_ADAPTER_FLAG_SOFTWARE;

    return isSoftwareAdapter || (isBasicRenderDriverVendorId && isBasicRenderDriverDeviceId);
}

std::shared_ptr<IExecutionProviderFactory> DMLProviderFactoryCreator::Create(int device_id) {
  return Create(device_id, /*skip_software_device_check*/ false);
}

Microsoft::WRL::ComPtr<ID3D12Device> DMLProviderFactoryCreator::CreateD3D12Device(int device_id, bool skip_software_device_check)
{
#ifdef _GAMING_XBOX
    ComPtr<ID3D12Device> d3d12_device;
    D3D12XBOX_CREATE_DEVICE_PARAMETERS params = {};
    params.Version = D3D12_SDK_VERSION;
    params.GraphicsCommandQueueRingSizeBytes = static_cast<UINT>(D3D12XBOX_DEFAULT_SIZE_BYTES);
    params.GraphicsScratchMemorySizeBytes = static_cast<UINT>(D3D12XBOX_DEFAULT_SIZE_BYTES);
    params.ComputeScratchMemorySizeBytes = static_cast<UINT>(D3D12XBOX_DEFAULT_SIZE_BYTES);
    ORT_THROW_IF_FAILED(D3D12XboxCreateDevice(nullptr, &params, IID_GRAPHICS_PPV_ARGS(d3d12_device.ReleaseAndGetAddressOf())));
#else
    ComPtr<IDXGIFactory4> dxgi_factory;
    ORT_THROW_IF_FAILED(CreateDXGIFactory2(0, IID_GRAPHICS_PPV_ARGS(dxgi_factory.ReleaseAndGetAddressOf())));

    ComPtr<IDXGIAdapter1> adapter;
    ORT_THROW_IF_FAILED(dxgi_factory->EnumAdapters1(device_id, &adapter));

    // Disallow using DML with the software adapter (Microsoft Basic Display Adapter) because CPU evaluations are much
    // faster. Some scenarios though call for EP initialization without this check (as execution will not actually occur
    // anyway) such as operation kernel registry enumeration for documentation purposes.
    if (!skip_software_device_check)
    {
      ORT_THROW_HR_IF(ERROR_GRAPHICS_INVALID_DISPLAY_ADAPTER, IsSoftwareAdapter(adapter.Get()));
    }

    ComPtr<ID3D12Device> d3d12_device;
    ORT_THROW_IF_FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_GRAPHICS_PPV_ARGS(d3d12_device.ReleaseAndGetAddressOf())));
#endif

  return d3d12_device;
}

std::shared_ptr<IExecutionProviderFactory> DMLProviderFactoryCreator::Create(int device_id, bool skip_software_device_check) {
  ComPtr<ID3D12Device> d3d12_device = CreateD3D12Device(device_id, skip_software_device_check);

  D3D12_COMMAND_QUEUE_DESC cmd_queue_desc = {};
  cmd_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  cmd_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;

  ComPtr<ID3D12CommandQueue> cmd_queue;
  ORT_THROW_IF_FAILED(d3d12_device->CreateCommandQueue(&cmd_queue_desc, IID_GRAPHICS_PPV_ARGS(cmd_queue.ReleaseAndGetAddressOf())));

  DML_CREATE_DEVICE_FLAGS flags = DML_CREATE_DEVICE_FLAG_NONE;

  // In debug builds, enable the DML debug layer if the D3D12 debug layer is also enabled
#if _DEBUG && !_GAMING_XBOX
  ComPtr<ID3D12DebugDevice> debug_device;
  (void)d3d12_device->QueryInterface(IID_PPV_ARGS(&debug_device));  // ignore failure
  const bool is_d3d12_debug_layer_enabled = (debug_device != nullptr);

  if (is_d3d12_debug_layer_enabled) {
    flags |= DML_CREATE_DEVICE_FLAG_DEBUG;
  }
#endif

  ComPtr<IDMLDevice> dml_device;
  ORT_THROW_IF_FAILED(DMLCreateDevice1(d3d12_device.Get(),
                                   flags,
                                   DML_FEATURE_LEVEL_5_0,
                                   IID_PPV_ARGS(&dml_device)));

  return CreateExecutionProviderFactory_DML(dml_device.Get(), cmd_queue.Get());
}

std::shared_ptr<IExecutionProviderFactory> DMLProviderFactoryCreator::CreateDXCore(
    std::vector<ComPtr<IDXCoreAdapter>> dxcore_devices) {
  // Choose the first device from the list since it's the highest priority
  auto dxcore_device = dxcore_devices[0];

  // Create D3D12 Device from DXCore Adapter
  ComPtr<ID3D12Device> d3d12_device;
  ORT_THROW_IF_FAILED(D3D12CreateDevice(dxcore_device.Get(), D3D_FEATURE_LEVEL_11_0, IID_GRAPHICS_PPV_ARGS(d3d12_device.ReleaseAndGetAddressOf())));

  // Create DML Device from D3D12 device
  D3D12_COMMAND_QUEUE_DESC cmd_queue_desc = {};
  cmd_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  cmd_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;

  ComPtr<ID3D12CommandQueue> cmd_queue;
  ORT_THROW_IF_FAILED(d3d12_device->CreateCommandQueue(&cmd_queue_desc, IID_GRAPHICS_PPV_ARGS(cmd_queue.ReleaseAndGetAddressOf())));

  DML_CREATE_DEVICE_FLAGS flags = DML_CREATE_DEVICE_FLAG_NONE;

  // In debug builds, enable the DML debug layer if the D3D12 debug layer is also enabled
#if _DEBUG && !_GAMING_XBOX
  ComPtr<ID3D12DebugDevice> debug_device;
  (void)d3d12_device->QueryInterface(IID_PPV_ARGS(&debug_device));  // ignore failure
  const bool is_d3d12_debug_layer_enabled = (debug_device != nullptr);

  if (is_d3d12_debug_layer_enabled) {
    flags |= DML_CREATE_DEVICE_FLAG_DEBUG;
  }
#endif

  ComPtr<IDMLDevice> dml_device;
  ORT_THROW_IF_FAILED(DMLCreateDevice1(d3d12_device.Get(),
                                   flags,
                                   DML_FEATURE_LEVEL_5_0,
                                   IID_PPV_ARGS(&dml_device)));

  return CreateExecutionProviderFactory_DML(dml_device.Get(), cmd_queue.Get());
}

}  // namespace onnxruntime

// [[deprecated]]
// This export should be deprecated.
// The OrtSessionOptionsAppendExecutionProvider_DML export on the OrtDmlApi should be used instead.
ORT_API_STATUS_IMPL(OrtSessionOptionsAppendExecutionProvider_DML, _In_ OrtSessionOptions* options, int device_id) {
API_IMPL_BEGIN
  options->provider_factories.push_back(onnxruntime::DMLProviderFactoryCreator::Create(device_id));
API_IMPL_END
  return nullptr;
}

// [[deprecated]]
// This export should be deprecated.
// The OrtSessionOptionsAppendExecutionProvider_DML1 export on the OrtDmlApi should be used instead.
ORT_API_STATUS_IMPL(OrtSessionOptionsAppendExecutionProviderEx_DML, _In_ OrtSessionOptions* options,
                    _In_ IDMLDevice* dml_device, _In_ ID3D12CommandQueue* cmd_queue) {
API_IMPL_BEGIN
  options->provider_factories.push_back(onnxruntime::CreateExecutionProviderFactory_DML(dml_device,
                                                                                        cmd_queue));
API_IMPL_END
  return nullptr;
}

ORT_API_STATUS_IMPL(CreateGPUAllocationFromD3DResource, _In_ ID3D12Resource* d3d_resource, _Out_ void** dml_resource) {
  API_IMPL_BEGIN
#ifdef USE_DML
  *dml_resource = Dml::CreateGPUAllocationFromD3DResource(d3d_resource);
#else
  *dml_resource = nullptr;
#endif  // USE_DML
  return nullptr;
  API_IMPL_END
}

ORT_API_STATUS_IMPL(FreeGPUAllocation, _In_ void* ptr) {
  API_IMPL_BEGIN
#ifdef USE_DML
  Dml::FreeGPUAllocation(ptr);
#endif  // USE_DML
  return nullptr;
  API_IMPL_END
}

static bool IsHardwareAdapter(IDXCoreAdapter* adapter) {
    bool is_hardware{ false };
    THROW_IF_FAILED(adapter->GetProperty(
        DXCoreAdapterProperty::IsHardware,
        &is_hardware));
    return is_hardware;
}

static bool IsGPU(IDXCoreAdapter* compute_adapter) {
    // Only considering hardware adapters
    if (!IsHardwareAdapter(compute_adapter))
        return false;
    return compute_adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS);
}

static bool IsNPU(IDXCoreAdapter* compute_adapter) {
    // Only considering hardware adapters
    if (!IsHardwareAdapter(compute_adapter))
        return false;
    return !(compute_adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS));
}

ORT_API_STATUS_IMPL(SessionOptionsAppendExecutionProvider_DML2, _In_ OrtSessionOptions* options, OrtDmlDeviceOptions* device_opts) {
API_IMPL_BEGIN
    OrtDmlPerformancePreference perf_pref = device_opts->perf_pref;
    OrtDmlDeviceFilter dev_filter = device_opts->dev_filter;

    // Create DXCore Adapter Factory
    ComPtr<IDXCoreAdapterFactory> adapterFactory;
    ORT_THROW_IF_FAILED(::DXCoreCreateAdapterFactory(adapterFactory.GetAddressOf()));

    // Get a list of all the adapters that support compute
    ComPtr<IDXCoreAdapterList> d3D12CoreComputeAdapters;
    GUID attributes[]{ DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE };
    ORT_THROW_IF_FAILED(
        adapterFactory->CreateAdapterList(_countof(attributes),
            attributes,
            d3D12CoreComputeAdapters.GetAddressOf()));

    const uint32_t count{ d3D12CoreComputeAdapters->GetAdapterCount() };
    int compute_only_device_id = -1;

    // Struct for holding each adapter
    enum class DeviceType { GPU, NPU, BadDevice};
    struct AdapterInfo {
        ComPtr<IDXCoreAdapter> Adapter;
        DeviceType Type; // GPU or NPU
    };

    // Returns an adapter type based on the user supplied device filter
    std::function<DeviceType (IDXCoreAdapter*)> filterAdapterTypeQuery[4] = {
        nullptr, // enum cannot take 0 value
        // Only GPUs are considered so all other devices aren't needed
        [](IDXCoreAdapter* adapter){ return IsGPU(adapter) ? DeviceType::GPU : DeviceType::BadDevice; },
        // Only NPUs are considered so all other devices aren't needed
        [](IDXCoreAdapter* adapter){ return IsNPU(adapter) ? DeviceType::NPU : DeviceType::BadDevice; },
        // Both GPUs and NPUs are considered
        [](IDXCoreAdapter* adapter){ return IsGPU(adapter) ? DeviceType::GPU :
        IsNPU(adapter) ? DeviceType::NPU : DeviceType::BadDevice; }
    };

    auto selected_adapters = std::vector<AdapterInfo>();
    // Iterate through all compute capable adapters
    for (uint32_t i = 0; i < count; ++i)
    {
        ComPtr<IDXCoreAdapter> candidateAdapter;
        ORT_THROW_IF_FAILED(
            d3D12CoreComputeAdapters->GetAdapter(i, candidateAdapter.GetAddressOf()));

        // Add the adapters that are valid based on the device filter (GPU, NPU, or Both)
        auto adapterType = filterAdapterTypeQuery[static_cast<int>(dev_filter)](candidateAdapter.Get());
        if (adapterType != DeviceType::BadDevice) {
            selected_adapters.push_back(AdapterInfo{candidateAdapter, adapterType});
        }
    }

    // When considering both GPUs and NPUs sort them by performance preference
    // of Default (Gpus first), HighPerformance (GPUs first), or LowPower (NPUs first)
    if (dev_filter == OrtDmlDeviceFilter::Both) // considering both NPUs and GPUs
    {
        struct SortingPolicy
        {
            // default is false because GPUs are considered higher priority in
            // a mixed adapter environment
            bool npus_first_ = false;
            SortingPolicy(bool npus_first = false) : npus_first_(npus_first){}
            bool operator()(const AdapterInfo& a, const AdapterInfo& b) {
                return npus_first_ ? a.Type > b.Type : a.Type < b.Type;
            }
        };
        auto sortingPolicy = SortingPolicy(perf_pref != OrtDmlPerformancePreference::LowPower);
        std::sort(selected_adapters.begin(), selected_adapters.end(), sortingPolicy);
    }

    // Extract just the adapters
    std::vector<ComPtr<IDXCoreAdapter>> sorted_dxcore_adapters;
    std::for_each(selected_adapters.begin(), selected_adapters.end(), [&](AdapterInfo a){
        sorted_dxcore_adapters.push_back(a.Adapter); });

    // check if num adapters is 0 (no adapters?)
    // return the create function for a dxcore device
    options->provider_factories.push_back(onnxruntime::DMLProviderFactoryCreator::CreateDXCore(
        sorted_dxcore_adapters));
  return nullptr;
API_IMPL_END
}

ORT_API_STATUS_IMPL(GetD3D12ResourceFromAllocation, _In_ OrtAllocator* ort_allocator, _In_ void* allocation, _Out_ ID3D12Resource** d3d_resource) {
  API_IMPL_BEGIN
#ifdef USE_DML
  auto wrapping_allocator = static_cast<onnxruntime::OrtAllocatorImplWrappingIAllocator*>(ort_allocator);
  auto allocator = wrapping_allocator->GetWrappedIAllocator();
  if (!allocator) {
    return OrtApis::CreateStatus(ORT_INVALID_ARGUMENT, "No requested allocator available");
  }
  *d3d_resource = Dml::GetD3D12ResourceFromAllocation(allocator.get(), allocation);
  (*d3d_resource)->AddRef();
#else
  *d3d_resource = nullptr;
#endif  // USE_DML
  return nullptr;
  API_IMPL_END
}

static constexpr OrtDmlApi ort_dml_api_10_to_x = {
  &OrtSessionOptionsAppendExecutionProvider_DML,
  &OrtSessionOptionsAppendExecutionProviderEx_DML,
  &CreateGPUAllocationFromD3DResource,
  &FreeGPUAllocation,
  &GetD3D12ResourceFromAllocation
};

const OrtDmlApi* GetOrtDmlApi(_In_ uint32_t /*version*/) NO_EXCEPTION {
#ifdef USE_DML
  return &ort_dml_api_10_to_x;
#else
    return nullptr;
#endif
}
