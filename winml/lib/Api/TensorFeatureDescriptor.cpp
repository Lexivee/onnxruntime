﻿// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"

#include "LearningModel.h"

#include "TensorFeatureDescriptor.h"

namespace winrt::Windows::AI::MachineLearning::implementation {
TensorFeatureDescriptor::TensorFeatureDescriptor(
    const char* name,
    const char* description,
    bool is_required,
    winml::TensorKind tensor_kind,
    const std::vector<int64_t>& shape,
    bool has_unsupported_image_metadata) : name_(WinML::Strings::HStringFromUTF8(name)),
                                           description_(WinML::Strings::HStringFromUTF8(description)),
                                           tensor_kind_(tensor_kind),
                                           shape_(shape),
                                           is_required_(is_required),
                                           has_unsupported_image_metadata_(has_unsupported_image_metadata) {
}

TensorFeatureDescriptor::TensorFeatureDescriptor(
    hstring const& Name,
    hstring const& Description,
    bool IsRequired,
    winml::TensorKind const& TensorKind,
    array_view<int64_t const> Shape,
    bool HasUnsupportedImageMetadata) : name_(Name),
                                        description_(Description),
                                        tensor_kind_(TensorKind),
                                        shape_(Shape.begin(), Shape.end()),
                                        is_required_(IsRequired),
                                        has_unsupported_image_metadata_(HasUnsupportedImageMetadata) {
}

winml::TensorKind
TensorFeatureDescriptor::TensorKind() try {
  return tensor_kind_;
}
WINML_CATCH_ALL

wfc::IVectorView<int64_t>
TensorFeatureDescriptor::Shape() try {
  return winrt::single_threaded_vector<int64_t>(
             std::vector<int64_t>(
                 std::begin(shape_),
                 std::end(shape_)))
      .GetView();
}
WINML_CATCH_ALL

winrt::hstring
TensorFeatureDescriptor::Name() try {
  return name_;
}
WINML_CATCH_ALL

winrt::hstring
TensorFeatureDescriptor::Description() try {
  return description_;
}
WINML_CATCH_ALL

winml::LearningModelFeatureKind
TensorFeatureDescriptor::Kind() try {
  return LearningModelFeatureKind::Tensor;
}
WINML_CATCH_ALL

bool TensorFeatureDescriptor::IsRequired() try {
  return is_required_;
}
WINML_CATCH_ALL

bool TensorFeatureDescriptor::HasUnsupportedImageMetadata() try {
    return has_unsupported_image_metadata_;
}
WINML_CATCH_ALL

bool TensorFeatureDescriptor::IsUnsupportedMetaData() try {
  return has_unsupported_image_metadata_;
}
WINML_CATCH_ALL

HRESULT
TensorFeatureDescriptor::GetName(
    const wchar_t** name,
    uint32_t* cchName) {
  *name = name_.data();
  *cchName = static_cast<uint32_t>(name_.size());
  return S_OK;
}

HRESULT
TensorFeatureDescriptor::GetDescription(
    const wchar_t** description,
    uint32_t* cchDescription) {
  *description = description_.data();
  *cchDescription = static_cast<uint32_t>(description_.size());
  return S_OK;
}
}  // namespace winrt::Windows::AI::MachineLearning::implementation
