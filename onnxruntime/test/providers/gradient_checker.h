/* Copyright 2016 The TensorFlow Authors. All Rights Reserved.

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

#pragma once
#include "core/training/training_session.h"

namespace onnxruntime {
namespace test {

// TODO: This class currently assumes the inputs share types and the outputs share a type.
// However there are cases like MaxPool and Gather where this is not true.
template <typename X_T, typename Y_T, typename JAC_T>
class GradientChecker {
 public:
  GradientChecker() = default;

  /// Returns in 'max_error' the maximum element-wise error for dy/dx between the
  /// computed and numeric Jacobian matrices where 'xs' and 'ys' are tensors.
  /// X_T and Y_T are the c++ types for the x and y tensors, and JAC_T is a
  /// real-valued type to store the Jacobian derivatives dy/dx.
  /// This function adds operations to the graph associated with 'scope'.
  ///
  /// Examples:
  /// if y = Square(x), where x (and so y) are DT_FLOAT,
  /// <X_T, Y_T, JAC_T> should be <float, float, float>
  ///
  /// if y = Square(x), where x (and so y) are DT_DOUBLE,
  /// <X_T, Y_T, JAC_T> should be <double, double, double>
  Status ComputeGradientError(
      std::string& op_name,
      const std::vector<onnxruntime::TensorShape>& x_shapes,
      const std::vector<onnxruntime::TensorShape>& y_shapes,
      JAC_T* max_error,
      std::vector<std::string> attributes = {});

 private:
  Status InitJacobians(const std::vector<TensorShape>& x_shapes,
                       const std::vector<TensorShape>& y_shapes,
                       std::vector<std::vector<JAC_T>>* jacobians);

  std::vector<onnxruntime::MLValue> EvaluateFunctionAtInput(const std::string& op_name,
                                                            const std::vector<TensorShape>& x_shapes,
                                                            const std::vector<TensorShape>& y_shapes,
                                                            std::vector<std::vector<X_T>>* x_datas,
                                                            std::vector<std::vector<Y_T>>* y_datas,
                                                            std::vector<std::string> attributes);

  Status ComputeTheoreticalJacobianTranspose(std::string& op_name,
                                             const std::vector<TensorShape>& x_shapes,
                                             const std::vector<TensorShape>& y_shapes,
                                             std::vector<std::vector<X_T>>* x_datas,
                                             std::vector<std::vector<Y_T>>* y_datas,
                                             std::vector<std::vector<JAC_T>>* jacobian_ts,
                                             std::vector<std::string> attributes);

  Status ComputeNumericJacobianTranspose(const std::string& op_name,
                                         const std::vector<TensorShape>& x_shapes,
                                         const std::vector<TensorShape>& y_shapes,
                                         const JAC_T delta,
                                         std::vector<std::vector<X_T>>* x_datas,
                                         std::vector<std::vector<Y_T>>* y_datas,
                                         std::vector<std::vector<JAC_T>>* jacobian_ts,
                                         std::vector<std::string> attributes);

  Status ComputeGradientErrorInternal(std::string& op_name,
                                      const std::vector<TensorShape>& x_shapes,
                                      const std::vector<TensorShape>& y_shapes,
                                      std::vector<std::vector<X_T>>* x_datas,
                                      std::vector<std::vector<Y_T>>* y_datas,
                                      JAC_T* max_error,
                                      std::vector<std::string> attributes);
};
}  // namespace test
}  // namespace onnxruntime
