// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <boost/beast/core.hpp>
#include <boost/beast/http/status.hpp>
#include <google/protobuf/stubs/status.h>

#include "context.h"
#include "util.h"

namespace protobufutil = google::protobuf::util;
namespace onnxruntime {
namespace hosting {

// Report a failure
void ErrorHandling(beast::error_code ec, char const* what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

boost::beast::http::status GetHttpStatusCode(const protobufutil::Status& status) {
  switch (status.error_code()) {
    case protobufutil::error::Code::OK:
      return boost::beast::http::status::ok;

    case protobufutil::error::Code::UNKNOWN:
    case protobufutil::error::Code::DEADLINE_EXCEEDED:
    case protobufutil::error::Code::RESOURCE_EXHAUSTED:
    case protobufutil::error::Code::ABORTED:
    case protobufutil::error::Code::UNIMPLEMENTED:
    case protobufutil::error::Code::INTERNAL:
    case protobufutil::error::Code::UNAVAILABLE:
    case protobufutil::error::Code::DATA_LOSS:
      return boost::beast::http::status::internal_server_error;

    case protobufutil::error::Code::CANCELLED:
    case protobufutil::error::Code::INVALID_ARGUMENT:
    case protobufutil::error::Code::ALREADY_EXISTS:
    case protobufutil::error::Code::FAILED_PRECONDITION:
    case protobufutil::error::Code::OUT_OF_RANGE:
      return boost::beast::http::status::bad_request;

    case protobufutil::error::Code::NOT_FOUND:
      return boost::beast::http::status::not_found;

    case protobufutil::error::Code::PERMISSION_DENIED:
      return boost::beast::http::status::forbidden;

    case protobufutil::error::Code::UNAUTHENTICATED:
      return boost::beast::http::status::unauthorized;

    default:
      return boost::beast::http::status::internal_server_error;
  }
}

SupportedContentType GetRequestContentType(const HttpContext& context) {
  if (context.request.find("Content-Type") != context.request.end()) {
    if (context.request["Content-Type"] == "application/json") {
      return SupportedContentType::Json;
    } else if (context.request["Content-Type"] == "application/octet-stream") {
      return SupportedContentType::PbByteArray;
    }
  }

  return SupportedContentType::Unknown;
}

SupportedContentType GetResponseContentType(const HttpContext& context) {
  if (context.request.find("Accept") != context.request.end()) {
    if (context.request["Accept"] == "application/json") {
      return SupportedContentType::Json;
    } else if (context.request["Accept"] == "*/*" || context.request["Accept"] == "application/octet-stream") {
      return SupportedContentType::PbByteArray;
    }
  } else {
    return SupportedContentType::PbByteArray;
  }

  return SupportedContentType::Unknown;
}

}  // namespace hosting
}  // namespace onnxruntime