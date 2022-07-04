// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifndef ONNXRUNTIME_CORE_PROVIDERS_TVM_HASH_ALG_HASHER_IMPL_H_
#define ONNXRUNTIME_CORE_PROVIDERS_TVM_HASH_ALG_HASHER_IMPL_H_

#ifdef USE_TVM_HASH
#include <ippcp.h>
#endif
#include <string>
#include <iomanip>
#include <sstream>
#include <memory>

#include "core/common/common.h"


namespace onnxruntime {
namespace tvm {

class HasherImpl {
 public:
  HasherImpl() = default;
  virtual ~HasherImpl() = default;

  virtual std::string hash(const char* src, size_t size) const = 0;
};

class HasherSHA256Impl : public HasherImpl {
 public:
  HasherSHA256Impl() = default;
  virtual ~HasherSHA256Impl() = default;

  std::string hash(const char* src, size_t size) const final;

 private:
 #ifdef USE_TVM_HASH
  static void digest(const Ipp8u* src, int size, Ipp8u* dst);
#endif
  static std::string digest(const char* src, size_t size);
  static std::string hexdigest(const char* src, size_t size);
};

}   // namespace tvm
}   // namespace onnxruntime

#endif  // ONNXRUNTIME_CORE_PROVIDERS_TVM_HASH_ALG_HASHER_IMPL_H_
