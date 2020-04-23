/* Copyright 2015 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
// Portions Copyright (c) Microsoft Corporation

#include "core/platform/env.h"

#include <Shlwapi.h>
#include <Windows.h>

#include <fstream>
#include <string>
#include <thread>
#include <process.h>
#include <fcntl.h>
#include <io.h>

#include "core/common/logging/logging.h"
#include "core/platform/env.h"
#include "core/platform/scoped_resource.h"
#include "core/platform/windows/telemetry.h"
#include "unsupported/Eigen/CXX11/src/ThreadPool/ThreadPoolInterface.h"
#include <wil/Resource.h>

namespace onnxruntime {

namespace {
class WindowsThread : public EnvThread {
 private:
  struct Param {
    const ORTCHAR_T* name_prefix;
    int index;
    unsigned (*start_address)(int id, Eigen::ThreadPoolInterface* param);
    Eigen::ThreadPoolInterface* param;
    const ThreadOptions& thread_options;
  };

 public:
  WindowsThread(const ORTCHAR_T* name_prefix, int index,
                unsigned (*start_address)(int id, Eigen::ThreadPoolInterface* param), Eigen::ThreadPoolInterface* param,
                const ThreadOptions& thread_options)
      : hThread((HANDLE)_beginthreadex(nullptr, thread_options.stack_size, ThreadMain,
                                       new Param{name_prefix, index, start_address, param, thread_options}, 0,
                                       &threadID)) {
  }

  ~WindowsThread() {
    DWORD waitStatus = WaitForSingleObject(hThread.get(), INFINITE);
    FAIL_FAST_LAST_ERROR_IF(waitStatus == WAIT_FAILED);
  }

  // This function is called when the threadpool is cancelled.
  // TODO: Find a way to avoid calling TerminateThread
  void OnCancel() {
    TerminateThread(hThread.get(), 1);
  }

 private:
  typedef HRESULT(WINAPI* SetThreadDescriptionFunc)(HANDLE hThread, PCWSTR lpThreadDescription);
  static unsigned __stdcall ThreadMain(void* param) {
    std::unique_ptr<Param> p((Param*)param);
    // TODO: should I try to use SetThreadSelectedCpuSets?
    if (!p->thread_options.affinity.empty())
      SetThreadAffinityMask(GetCurrentThread(), p->thread_options.affinity[p->index]);
    // kernel32.dll is always loaded
    SetThreadDescriptionFunc pSetThrDesc =
        (SetThreadDescriptionFunc)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "SetThreadDescription");
    if (pSetThrDesc != nullptr) {
      const ORTCHAR_T* name_prefix =
          (p->name_prefix == nullptr || wcslen(p->name_prefix) == 0) ? L"onnxruntime" : p->name_prefix;
      std::wostringstream oss;
      oss << name_prefix << "-" << p->index;
      // Ignore the error
      (void)pSetThrDesc(GetCurrentThread(), oss.str().c_str());
    }

    unsigned ret = 0;
    try {
      ret = p->start_address(p->index, p->param);
    } catch (std::exception&) {
      p->param->Cancel();
      ret = 1;
    }
    return ret;
  }
  unsigned threadID = 0;
  wil::unique_handle hThread;
};

class WindowsEnv : public Env {
 public:
  EnvThread* CreateThread(_In_opt_z_ const ORTCHAR_T* name_prefix, int index,
                          unsigned (*start_address)(int id, Eigen::ThreadPoolInterface* param),
                          Eigen::ThreadPoolInterface* param, const ThreadOptions& thread_options) {
    return new WindowsThread(name_prefix, index, start_address, param, thread_options);
  }
  Task CreateTask(std::function<void()> f) {
    return Task{std::move(f)};
  }
  void ExecuteTask(const Task& t) {
    t.f();
  }

  void SleepForMicroseconds(int64_t micros) const override {
    Sleep(static_cast<DWORD>(micros) / 1000);
  }

  int GetNumCpuCores() const override {
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
    if (!processorCoreCount)
      ORT_THROW("Fatal error: 0 count processors from GetLogicalProcessorInformation");
    return processorCoreCount;
  }

  std::vector<size_t> GetThreadAffinityMasks() const override {
    auto generate_vector_of_n = [](int n) {
      std::vector<size_t> ret(n);
      std::iota(ret.begin(), ret.end(), 0);
      return ret;
    };
    // Indeed 64 should be enough. However, it's harmless to have a little more.
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer[256];
    DWORD returnLength = sizeof(buffer);
    if (GetLogicalProcessorInformation(buffer, &returnLength) == FALSE) {
      return generate_vector_of_n(std::thread::hardware_concurrency());
    }
    std::vector<size_t> ret;
    int count = (int)(returnLength / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    for (int i = 0; i != count; ++i) {
      if (buffer[i].Relationship == RelationProcessorCore) {
        ret.push_back(buffer[i].ProcessorMask);
      }
    }
    if (ret.empty())
      return generate_vector_of_n(std::thread::hardware_concurrency());
    return ret;
  }

  static WindowsEnv& Instance() {
    static WindowsEnv default_env;
    return default_env;
  }

  PIDType GetSelfPid() const override {
    return GetCurrentProcessId();
  }

  Status GetFileLength(_In_z_ const ORTCHAR_T* file_path, size_t& length) const override {
    wil::unique_hfile file_handle{
        CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)};
    LARGE_INTEGER filesize;
    if (!GetFileSizeEx(file_handle.get(), &filesize)) {
      const int err = GetLastError();
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "GetFileSizeEx ", ToMBString(file_path), " fail, errcode = ", err);
    }
    if (static_cast<ULONGLONG>(filesize.QuadPart) > std::numeric_limits<size_t>::max()) {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "GetFileLength: File is too large");
    }
    length = static_cast<size_t>(filesize.QuadPart);
    return Status::OK();
  }

  Status ReadFileIntoBuffer(_In_z_ const ORTCHAR_T* const file_path, const FileOffsetType offset, const size_t length,
                            const gsl::span<char> buffer) const override {
    ORT_RETURN_IF_NOT(file_path);
    ORT_RETURN_IF_NOT(offset >= 0);
    ORT_RETURN_IF_NOT(length <= buffer.size());

    wil::unique_hfile file_handle{
        CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)};
    if (file_handle.get() == INVALID_HANDLE_VALUE) {
      const int err = GetLastError();
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "open file ", ToMBString(file_path), " fail, errcode = ", err);
    }

    if (length == 0)
      return Status::OK();

    if (offset > 0) {
      LARGE_INTEGER current_position;
      current_position.QuadPart = offset;
      if (!SetFilePointerEx(file_handle.get(), current_position, &current_position, FILE_BEGIN)) {
        const int err = GetLastError();
        return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "SetFilePointerEx ", ToMBString(file_path), " fail, errcode = ", err);
      }
    }

    size_t total_bytes_read = 0;
    while (total_bytes_read < length) {
      constexpr DWORD k_max_bytes_to_read = 1 << 30;  // read at most 1GB each time
      const size_t bytes_remaining = length - total_bytes_read;
      const DWORD bytes_to_read = static_cast<DWORD>(std::min<size_t>(bytes_remaining, k_max_bytes_to_read));
      DWORD bytes_read;

      if (!ReadFile(file_handle.get(), buffer.data() + total_bytes_read, bytes_to_read, &bytes_read, nullptr)) {
        const int err = GetLastError();
        return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "ReadFile ", ToMBString(file_path), " fail, errcode = ", err);
      }

      if (bytes_read != bytes_to_read) {
        return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "ReadFile ", ToMBString(file_path), " fail: unexpected end");
      }

      total_bytes_read += bytes_read;
    }

    return Status::OK();
  }

  Status MapFileIntoMemory(_In_z_ const ORTCHAR_T*, FileOffsetType, size_t, MappedMemoryPtr&) const override {
    return ORT_MAKE_STATUS(ONNXRUNTIME, NOT_IMPLEMENTED, "MapFileIntoMemory is not implemented on Windows.");
  }

  common::Status FileOpenRd(const std::wstring& path, /*out*/ int& fd) const override {
    _wsopen_s(&fd, path.c_str(), _O_RDONLY | _O_SEQUENTIAL | _O_BINARY, _SH_DENYWR, _S_IREAD | _S_IWRITE);
    if (0 > fd) {
      return common::Status(common::SYSTEM, errno);
    }
    return Status::OK();
  }

  common::Status FileOpenWr(const std::wstring& path, /*out*/ int& fd) const override {
    _wsopen_s(&fd, path.c_str(), _O_CREAT | _O_TRUNC | _O_SEQUENTIAL | _O_BINARY | _O_WRONLY, _SH_DENYWR,
              _S_IREAD | _S_IWRITE);
    if (0 > fd) {
      return common::Status(common::SYSTEM, errno);
    }
    return Status::OK();
  }

  common::Status FileOpenRd(const std::string& path, /*out*/ int& fd) const override {
    _sopen_s(&fd, path.c_str(), _O_RDONLY | _O_SEQUENTIAL | _O_BINARY, _SH_DENYWR, _S_IREAD | _S_IWRITE);
    if (0 > fd) {
      return common::Status(common::SYSTEM, errno);
    }
    return Status::OK();
  }

  common::Status FileOpenWr(const std::string& path, /*out*/ int& fd) const override {
    _sopen_s(&fd, path.c_str(), _O_CREAT | _O_TRUNC | _O_SEQUENTIAL | _O_BINARY | _O_WRONLY, _SH_DENYWR,
             _S_IREAD | _S_IWRITE);
    if (0 > fd) {
      return common::Status(common::SYSTEM, errno);
    }
    return Status::OK();
  }

  common::Status FileClose(int fd) const override {
    int ret = _close(fd);
    if (0 != ret) {
      return common::Status(common::SYSTEM, errno);
    }
    return Status::OK();
  }

  virtual Status LoadDynamicLibrary(const std::string& library_filename, void** handle) const override {
    *handle = ::LoadLibraryA(library_filename.c_str());
    if (!handle)
      return common::Status(common::ONNXRUNTIME, common::FAIL, "Failed to load library");
    return common::Status::OK();
  }

  virtual common::Status UnloadDynamicLibrary(void* handle) const override {
    if (::FreeLibrary(reinterpret_cast<HMODULE>(handle)) == 0)
      return common::Status(common::ONNXRUNTIME, common::FAIL, "Failed to unload library");
    return common::Status::OK();
  }

  virtual Status GetSymbolFromLibrary(void* handle, const std::string& symbol_name, void** symbol) const override {
    *symbol = ::GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name.c_str());
    if (!*symbol)
      return common::Status(common::ONNXRUNTIME, common::FAIL, "Failed to find symbol in library");
    return common::Status::OK();
  }

  virtual std::string FormatLibraryFileName(const std::string& name, const std::string& version) const override {
    ORT_UNUSED_PARAMETER(name);
    ORT_UNUSED_PARAMETER(version);
    ORT_NOT_IMPLEMENTED(__FUNCTION__, " is not implemented");
  }

  // \brief returns a provider that will handle telemetry on the current platform
  const Telemetry& GetTelemetryProvider() const override {
    return telemetry_provider_;
  }

  // \brief returns a value for the queried variable name (var_name)
  std::string GetEnvironmentVar(const std::string& var_name) const override {
    // Why getenv() should be avoided on Windows:
    // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/getenv-wgetenv
    // Instead use the Win32 API: GetEnvironmentVariableA()

    // Max limit of an environment variable on Windows including the null-terminating character
    constexpr DWORD kBufferSize = 32767;

    // Create buffer to hold the result
    char buffer[kBufferSize];

    auto char_count = GetEnvironmentVariableA(var_name.c_str(), buffer, kBufferSize);

    // Will be > 0 if the API call was successful
    if (char_count) {
      return std::string(buffer, buffer + char_count);
    }

    // TODO: Understand the reason for failure by calling GetLastError().
    // If it is due to the specified environment variable being found in the environment block,
    // GetLastError() returns ERROR_ENVVAR_NOT_FOUND.
    // For now, we assume that the environment variable is not found.

    return std::string();
  }

 private:
  WindowsEnv() : GetSystemTimePreciseAsFileTime_(nullptr) {
    // GetSystemTimePreciseAsFileTime function is only available in the latest
    // versions of Windows. For that reason, we try to look it up in
    // kernel32.dll at runtime and use an alternative option if the function
    // is not available.
    HMODULE module = GetModuleHandleW(L"kernel32.dll");
    if (module != nullptr) {
      auto func = (FnGetSystemTimePreciseAsFileTime)GetProcAddress(module, "GetSystemTimePreciseAsFileTime");
      GetSystemTimePreciseAsFileTime_ = func;
    }
  }

  typedef VOID(WINAPI* FnGetSystemTimePreciseAsFileTime)(LPFILETIME);
  FnGetSystemTimePreciseAsFileTime GetSystemTimePreciseAsFileTime_;
  WindowsTelemetry telemetry_provider_;
};
}  // namespace

Env& Env::Default() {
  return WindowsEnv::Instance();
}

}  // namespace onnxruntime
