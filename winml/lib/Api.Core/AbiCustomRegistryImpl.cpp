// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"

#ifdef USE_DML

#include "inc/AbiCustomRegistryImpl.h"

namespace Windows::AI::MachineLearning::Adapter {

HRESULT STDMETHODCALLTYPE AbiCustomRegistryImpl::RegisterOperatorSetSchema(
    const MLOperatorSetId* opSetId,
    int baseline_version,
    const MLOperatorSchemaDescription* const* schema,
    uint32_t schemaCount,
    _In_opt_ IMLOperatorTypeInferrer* typeInferrer,
    _In_opt_ IMLOperatorShapeInferrer* shapeInferrer) const noexcept try {
#ifdef LAYERING_DONE
  for (uint32_t i = 0; i < schemaCount; ++i) {
    telemetry_helper.RegisterOperatorSetSchema(
        schema[i]->name,
        schema[i]->inputCount,
        schema[i]->outputCount,
        schema[i]->typeConstraintCount,
        schema[i]->attributeCount,
        schema[i]->defaultAttributeCount);
  }
#endif

  // Delegate to base class
  return AbiCustomRegistry::RegisterOperatorSetSchema(
      opSetId,
      baseline_version,
      schema,
      schemaCount,
      typeInferrer,
      shapeInferrer);
}
CATCH_RETURN();

HRESULT STDMETHODCALLTYPE AbiCustomRegistryImpl::RegisterOperatorKernel(
    const MLOperatorKernelDescription* opKernel,
    IMLOperatorKernelFactory* operatorKernelFactory,
    _In_opt_ IMLOperatorShapeInferrer* shapeInferrer) const noexcept {
  return RegisterOperatorKernel(opKernel, operatorKernelFactory, shapeInferrer, false, false, false);
}

HRESULT STDMETHODCALLTYPE AbiCustomRegistryImpl::RegisterOperatorKernel(
    const MLOperatorKernelDescription* opKernel,
    IMLOperatorKernelFactory* operatorKernelFactory,
    _In_opt_ IMLOperatorShapeInferrer* shapeInferrer,
    bool isInternalOperator,
    bool canAliasFirstInput,
    bool supportsGraph,
    const uint32_t* requiredInputCountForGraph,
    bool requiresFloatFormatsForGraph,
    _In_reads_(constantCpuInputCount) const uint32_t* requiredConstantCpuInputs,
    uint32_t constantCpuInputCount) const noexcept try {
#ifdef LAYERING_DONE
  // Log a custom op telemetry if the operator is not a built-in DML operator
  if (!isInternalOperator) {
    telemetry_helper.LogRegisterOperatorKernel(
        opKernel->name,
        opKernel->domain,
        static_cast<int>(opKernel->executionType));
  }
#endif

  // Delegate to base class
  return AbiCustomRegistry::RegisterOperatorKernel(
      opKernel,
      operatorKernelFactory,
      shapeInferrer,
      isInternalOperator,
      canAliasFirstInput,
      supportsGraph,
      requiredInputCountForGraph,
      requiresFloatFormatsForGraph,
      requiredConstantCpuInputs,
      constantCpuInputCount);
}
CATCH_RETURN();

}  // namespace Windows::AI::MachineLearning::Adapter

#endif USE_DML
