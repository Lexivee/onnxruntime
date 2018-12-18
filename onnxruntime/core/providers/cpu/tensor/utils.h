// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "gsl/gsl_algorithm"
namespace onnxruntime {

struct TensorPitches : std::vector<int64_t> {
  TensorPitches(const Tensor& tensor, size_t rank = 0) : TensorPitches(tensor.Shape(), rank) {}
  TensorPitches(const TensorShape& shape, size_t rank = 0) : TensorPitches(shape.GetDims(), rank) {}
  TensorPitches(const std::vector<int64_t>& dims, size_t rank = 0)
      : std::vector<int64_t>(std::max(rank, dims.size()), 0) {
    Calculate(gsl::span<int64_t>(data(), size()), dims);
  }

  static bool Calculate(gsl::span<int64_t> p, const std::vector<int64_t>& dims) {
    // The pitches is the size of the next inner axis. Aka the amount to move by one of the next inner axis.
    // For a tensor with shape(2,3,4,5) the values would be: (3*4*5, 4*5, 5, 1)
    // Note that the outermost '2' is never used, as you never need to move by the entire size of the outermost axis

    auto tensor_rank = dims.size();
    auto pitch_rank = p.size();
    auto padded_rank = pitch_rank - tensor_rank;
    if (gsl::narrow_cast<ptrdiff_t>(padded_rank) < 0)
      return false;

    *(p.rbegin()) = 1;  // The innermost axis is 1 (single values)
    if (tensor_rank > 1) {
      for (size_t i = tensor_rank - 1; i-- > 0;) {
        p.operator[](i + padded_rank) = p.operator[](i + 1 + padded_rank) * dims[i + 1];
      }
    }

    if (padded_rank >= 1) {
      for (size_t i = 0; i < padded_rank; ++i) {
        if (i == 0)
          p.operator[](padded_rank - 1) = p.operator[](padded_rank) * dims[0];
        else
          p.operator[](padded_rank - 1 - i) = p.operator[](padded_rank - 1);
      }
    }
    return true;
  }
};

// This class is to iterate through the axes of an arbitrarily shaped tensor
// For example, a tensor with shape (2,3,4) will be iterated in this order:
// (0,0,x) (0,1,x) (0,2,x) (1,0,x) (1,1,x) (1,2,x)
// Note: The innermost axis is not iterated over since it's always special cased
struct TensorAxisCounters {
  TensorAxisCounters(const Tensor& tensor) : tensor_(tensor) {
    indices_.resize(tensor_.Shape().NumDimensions() - 1, 0);
    axis_ = indices_.size();

    // If a tensor has a shape, but one of the axes is 0 in size, there are no elements, so nothing to iterate
    if (tensor_.Shape().Size() == 0)
      running_ = false;
  }

  // Returns true if there was a carry to the next axis
  bool Increment() {
    if (axis_-- == 0) {
      running_ = false;
      return false;
    }

    if (++indices_[axis_] != tensor_.Shape()[axis_]) {
      axis_ = indices_.size();
      return false;
    }

    indices_[axis_] = 0;  // Reset the counter for this axis
    return true;          // There was a carry
  }

  size_t Axis() const { return axis_; }
  operator bool() const { return running_; }

 private:
  const Tensor& tensor_;
  bool running_{true};
  size_t axis_;
  std::vector<int64_t> indices_;  // There is no index for innermost axis since it's a special case
};

struct ExtentAxisCounters {
  ExtentAxisCounters(gsl::span<const int64_t> extents) : extents_(extents) {
    indices_.resize(extents_.size() - 1, 0);
    axis_ = indices_.size();

    // If a tensor has a shape, but one of the axes is 0 in size, there are no elements, so nothing to iterate
    if (std::find(extents.cbegin(), extents.cend(), 0) != extents.cend())
      running_ = false;
  }

  // Returns true if there was a carry to the next axis
  bool Increment() {
    if (axis_-- == 0) {
      running_ = false;
      return false;
    }

    if (++indices_[axis_] != extents_[axis_]) {
      axis_ = indices_.size();
      return false;
    }

    indices_[axis_] = 0;  // Reset the counter for this axis
    return true;          // There was a carry
  }

  size_t Axis() const { return axis_; }
  operator bool() const { return running_; }

 private:
  bool running_{true};
  size_t axis_;
  std::vector<int64_t> indices_;      // There is no index for innermost axis since it's a special case
  gsl::span<const int64_t> extents_;  // The extents of each axis
};

// A std::vector that holds the number of entries to skip to go to the next axis start given an extent in each axis
// This is used by the SliceIterator to iterate over a slice of a tensor
struct SliceSkips : std::vector<int64_t> {
  SliceSkips(const Tensor& tensor, gsl::span<const int64_t> extents)
      : std::vector<int64_t>(tensor.Shape().NumDimensions(), 0) {
    auto& dims = tensor.Shape().GetDims();
    ORT_ENFORCE(static_cast<ptrdiff_t>(dims.size()) == extents.size());
    size_t pitch = dims.back();
    back() = pitch - extents[size() - 1];
    for (size_t i = size() - 1; i-- > 0;) {
      auto prevPitch = pitch;
      pitch *= dims[i];
      operator[](i) = pitch - prevPitch * extents[i];
    }
  }
};

// This provides easy sequential iteration over a subset of a tensor given a span of starts & extents
template <typename T>
struct SliceIterator {
  SliceIterator(const Tensor& tensor, gsl::span<const int64_t> starts, gsl::span<const int64_t> extents)
      : tensor_(tensor), extents_(extents), skips_(tensor, extents), indices_(extents.size(), 0) {
    auto& dims = tensor_.Shape().GetDims();
    ORT_ENFORCE(static_cast<ptrdiff_t>(dims.size()) == starts.size() && static_cast<ptrdiff_t>(dims.size()) == extents.size());

    size_t pitch = 1;
    // Initial skip, so that input_ points to the first element to copy
    for (size_t i = dims.size(); i-- > 0;) {
      input_ += pitch * starts[i];
      pitch *= dims[i];
    }

    inner_extent_ = extents_[dims.size() - 1];
  }

  void AdvanceOverInnerExtent() {
    size_t axis = skips_.size() - 1;
    input_ += skips_[axis];
    while (axis-- && ++indices_[axis] == extents_[axis]) {
      indices_[axis] = 0;
      input_ += skips_[axis];
    }
  }

  const T* operator++(int) {
    const T* input = input_++;
    if (++inner_counter_ == inner_extent_) {
      inner_counter_ = 0;
      AdvanceOverInnerExtent();
    }
    return input;
  }

  T* CopyInnermostAxis(T* output) {
    std::copy(input_, input_ + inner_extent_, output);
    input_ += inner_extent_;
    output += inner_extent_;
    AdvanceOverInnerExtent();
    return output;
  }

 private:
  const Tensor& tensor_;
  const T* input_{tensor_.template Data<T>()};
  gsl::span<const int64_t> extents_;
  size_t inner_counter_{}, inner_extent_;
  SliceSkips skips_;
  std::vector<int64_t> indices_;  // There is no index for innermost axis since it's a special case
};

inline void CopyCpuTensor(const Tensor* src, Tensor* tgt) {
  void* target = tgt->MutableDataRaw();
  const void* source = src->DataRaw();

  if (target != source) {
    auto is_string_type = (src->DataType() == DataTypeImpl::GetType<std::string>());
    if (is_string_type) {
      for (int64_t i = 0; i < src->Shape().Size(); ++i)
        static_cast<std::string*>(target)[i] = static_cast<const std::string*>(source)[i];
    } else {
      memcpy(target, source, src->Shape().Size() * src->DataType()->Size());
    }
  }
}

}  // namespace onnxruntime
