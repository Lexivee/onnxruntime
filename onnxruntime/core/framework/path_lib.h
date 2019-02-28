// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include <string.h>
#include <string>
#include <sstream>
#include <assert.h>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#include <pathcch.h>
#else
#include <libgen.h>
#include <sys/types.h>
#include <dirent.h>
#include <stddef.h>  //ptrdiff_t
#endif
#include "core/session/onnxruntime_c_api.h"

using PATH_CHAR_TYPE = ORTCHAR_T;

template <typename T>
long OrtStrtol(const T* nptr, T** endptr, int base);

/**
 * Convert a C string to ssize_t(or ptrdiff_t)
 * @return the converted integer value.
 */
template <typename T>
ptrdiff_t OrtStrtoPtrDiff(const T* nptr, T** endptr, int base);

template <>
inline ptrdiff_t OrtStrtoPtrDiff<wchar_t>(const wchar_t* nptr, wchar_t** endptr, int base) {
#ifdef _WIN32
  // TODO: on x86_32, it should be wcstol
  return _wcstoi64(nptr, endptr, base);
#else
  return wcstol(nptr, endptr, base);
#endif
}

template <typename T>
size_t OrtStrftime(T* strDest, size_t maxsize, const T* format, const struct tm* timeptr);

template <>
inline size_t OrtStrftime<char>(char* strDest, size_t maxsize, const char* format, const struct tm* timeptr) {
  return strftime(strDest, maxsize, format, timeptr);
}

template <>
inline size_t OrtStrftime<wchar_t>(wchar_t* strDest, size_t maxsize, const wchar_t* format, const struct tm* timeptr) {
  return wcsftime(strDest, maxsize, format, timeptr);
}

template <>
inline ptrdiff_t OrtStrtoPtrDiff<char>(const char* nptr, char** endptr, int base) {
#ifdef _WIN32
  // TODO: on x86_32, it should be strtol
  return _strtoi64(nptr, endptr, base);
#else
  return strtol(nptr, endptr, base);
#endif
}

template <>
inline long OrtStrtol<char>(const char* nptr, char** endptr, int base) {
  return strtol(nptr, endptr, base);
}

template <>
inline long OrtStrtol<wchar_t>(const wchar_t* nptr, wchar_t** endptr, int base) {
  return wcstol(nptr, endptr, base);
}

template <typename T>
inline int CompareCString(const T* s1, const T* s2);

template <>
inline int CompareCString<char>(const char* s1, const char* s2) {
  return strcmp(s1, s2);
}

template <>
inline int CompareCString<wchar_t>(const wchar_t* s1, const wchar_t* s2) {
  return wcscmp(s1, s2);
}

enum class OrtFileType { TYPE_BLK, TYPE_CHR, TYPE_DIR, TYPE_FIFO, TYPE_LNK, TYPE_REG, TYPE_SOCK, TYPE_UNKNOWN };

template <typename PATH_CHAR_TYPE>
PATH_CHAR_TYPE GetPathSep();

template <typename PATH_CHAR_TYPE>
PATH_CHAR_TYPE GetDot();

template <typename PATH_CHAR_TYPE>
bool HasExtensionOf(const std::basic_string<PATH_CHAR_TYPE>& s1, _In_ const PATH_CHAR_TYPE* s2) {
  typename std::basic_string<PATH_CHAR_TYPE>::size_type pos = s1.rfind(GetDot<PATH_CHAR_TYPE>());
  if (pos == std::basic_string<PATH_CHAR_TYPE>::npos || pos == s1.size() - 1) {
    return false;
  }
  ++pos;
  size_t extension_length = s1.size() - pos;
  return s1.compare(pos, extension_length, s2) == 0;
}

template <>
inline char GetDot<char>() {
  return '.';
}

template <>
inline wchar_t GetDot<wchar_t>() {
  return L'.';
}

#ifdef _WIN32
template <>
inline char GetPathSep<char>() {
  return '\\';
}

template <>
inline wchar_t GetPathSep<wchar_t>() {
  return L'\\';
}
#else
template <>
inline char GetPathSep<char>() {
  return '/';
}

template <>
inline wchar_t GetPathSep<wchar_t>() {
  return L'/';
}
#endif

template <typename PATH_CHAR_TYPE>
std::basic_string<PATH_CHAR_TYPE> ConcatPathComponent(const std::basic_string<PATH_CHAR_TYPE>& left,
                                                      const std::basic_string<PATH_CHAR_TYPE>& right) {
  std::basic_string<PATH_CHAR_TYPE> ret(left);
  ret.append(1, GetPathSep<PATH_CHAR_TYPE>()).append(right);
  return ret;
}

#ifdef _WIN32
inline OrtFileType DTToFileType(DWORD dwFileAttributes) {
  if (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    return OrtFileType::TYPE_DIR;
  }
  // TODO: test if it is reg
  return OrtFileType::TYPE_REG;
}
inline std::string FormatErrorCode(DWORD dw) {
  char* lpMsgBuf;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw,
                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&lpMsgBuf, 0, NULL);
  std::string s(lpMsgBuf);
  LocalFree(lpMsgBuf);
  return s;
}
template <typename T>
void LoopDir(const std::wstring& dir_name, T func) {
  std::wstring pattern = dir_name + L"\\*";
  WIN32_FIND_DATAW ffd;
  std::unique_ptr<void, decltype(&FindClose)> hFind(FindFirstFileW(pattern.c_str(), &ffd), FindClose);
  if (hFind.get() == INVALID_HANDLE_VALUE) {
    DWORD dw = GetLastError();
    std::string s = FormatErrorCode(dw);
    throw std::runtime_error(s);
  }
  do {
    if (!func(ffd.cFileName, DTToFileType(ffd.dwFileAttributes))) return;
  } while (FindNextFileW(hFind.get(), &ffd) != 0);
  DWORD dwError = GetLastError();
  if (dwError != ERROR_NO_MORE_FILES) {
    DWORD dw = GetLastError();
    std::string s = FormatErrorCode(dw);
    throw std::runtime_error(s);
  }
}

inline std::wstring GetDirNameFromFilePath(const std::wstring& s) {
  std::wstring input = s;
  if (input.empty()) throw std::runtime_error("illegal input path");
  if (input.back() == L'\\') input.resize(input.size() - 1);
  std::wstring ret(input);
  if (PathCchRemoveFileSpec(const_cast<wchar_t*>(ret.data()), ret.length() + 1) != S_OK) {
    throw std::runtime_error("illegal input path");
  }
  ret.resize(wcslen(ret.c_str()));
  return ret;
}

template <typename PATH_CHAR_TYPE>
inline std::basic_string<PATH_CHAR_TYPE> GetLastComponent(const std::basic_string<PATH_CHAR_TYPE>& s) {
  if (s.empty()) return std::basic_string<PATH_CHAR_TYPE>(1, GetDot<PATH_CHAR_TYPE>());
  std::basic_string<PATH_CHAR_TYPE> input = s;
  typename std::basic_string<PATH_CHAR_TYPE>::size_type pos = input.length();
  PATH_CHAR_TYPE sep = GetPathSep<PATH_CHAR_TYPE>();
  // remove trailing backslash
  for (; pos > 1 && input[pos - 1] == sep; --pos)
    ;
  input.resize(pos);
  for (; pos != 0 && input[pos - 1] != sep; --pos)
    ;
  return input.substr(pos);
}

#else
inline std::string GetDirNameFromFilePath(const std::string& input) {
  char* s = strdup(input.c_str());
  std::string ret = dirname(s);
  free(s);
  return ret;
}

inline std::string GetLastComponent(const std::string& input) {
  char* s = strdup(input.c_str());
  std::string ret = basename(s);
  free(s);
  return ret;
}

inline OrtFileType DTToFileType(unsigned char t) {
  switch (t) {
    case DT_BLK:
      return OrtFileType::TYPE_BLK;
    case DT_CHR:
      return OrtFileType::TYPE_CHR;
    case DT_DIR:
      return OrtFileType::TYPE_DIR;
    case DT_FIFO:
      return OrtFileType::TYPE_FIFO;
    case DT_LNK:
      return OrtFileType::TYPE_LNK;
    case DT_REG:
      return OrtFileType::TYPE_REG;
    case DT_SOCK:
      return OrtFileType::TYPE_SOCK;
    default:
      return OrtFileType::TYPE_UNKNOWN;
  }
}

template <typename T>
void LoopDir(const std::string& dir_name, T func) {
  DIR* dir = opendir(dir_name.c_str());
  if (dir == nullptr) {
    auto e = errno;
    char buf[1024];
    char* msg;
#ifdef _GNU_SOURCE
    msg = strerror_r(e, buf, sizeof(buf));
#else
    // for Mac OS X
    if (strerror_r(e, buf, sizeof(buf)) != 0) {
      buf[0] = '\0';
    }
    msg = buf;
#endif
    std::ostringstream oss;
    oss << "couldn't open '" << dir_name << "':" << msg;
    std::string s = oss.str();
    throw std::runtime_error(s);
  }
  try {
    struct dirent* dp;
    while ((dp = readdir(dir)) != nullptr) {
      if (!func(dp->d_name, DTToFileType(dp->d_type))) {
        break;
      }
    }
  } catch (std::exception& ex) {
    closedir(dir);
    throw;
  }
  closedir(dir);
}
#endif
template <typename T>
inline T ReplaceFilename(const T& input, const T& new_value) {
  T ret = GetDirNameFromFilePath(input);
  return ConcatPathComponent(ret, new_value);
}
