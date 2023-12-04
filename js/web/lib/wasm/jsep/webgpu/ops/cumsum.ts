// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

import {DataType} from '../../../wasm-common';
import {TensorView} from '../../tensor-view';
import {ShapeUtil} from '../../util';
import {AttributeWithCacheKey, createAttributeWithCacheKey} from '../attribute-with-cache-key';
import {ComputeContext, ProgramInfo, ProgramUniform} from '../types';

import {createTensorShapeVariables, enableShapesUniforms, inputVariable, outputVariable, ShaderHelper} from './common';


export interface CumSumAttributes extends AttributeWithCacheKey {
  readonly exclusive: number;
  readonly reverse: number;
}
const createCumsumProgramInfo =
    (inputType: number, inputShape: readonly number[], axisInput: TensorView, attributes: CumSumAttributes):
        ProgramInfo => {
          const outputSize = ShapeUtil.size(inputShape);  // outputShape is same as inputShape.
          const enableShapeUniforms = enableShapesUniforms(inputShape.length);
          const shapeOrRank = enableShapeUniforms ? inputShape.length : inputShape;
          const input = inputVariable('input', inputType, shapeOrRank);
          const output = outputVariable('output', inputType, shapeOrRank);
          const axisValue = axisInput.dataType === DataType.int32 ? axisInput.getInt32Array()[0] :
                                                                    Number(axisInput.getBigInt64Array()[0]);
          const axis = axisValue >= 0 ? axisValue : axisValue + inputShape.length;
          const programUniforms: ProgramUniform[] = [{type: 'uint32', data: outputSize}, {type: 'int32', data: axis}];
          if (enableShapeUniforms) {
            const tmpProgramUniform = createTensorShapeVariables(inputShape);
            programUniforms.push(...tmpProgramUniform);  // For input
            programUniforms.push(...tmpProgramUniform);  // For output
          }
          const index = `i32(${input.indicesGet('inputIndices', 'uniforms.axis')})`
          const max = enableShapeUniforms ?
              (inputShape.length === 1 ? 'i32(uniforms.input_shape)' : 'i32(uniforms.input_shape[uniforms.axis])') :
              (inputShape.length === 1 ? 'i32(input_shape)' : 'i32(input_shape[uniforms.axis])');
          let lowerLimit = attributes.reverse === 1 ? index : '0';
          let upperLimit = attributes.reverse === 1 ? max : index + ' + 1';
          if (attributes.exclusive === 1) {
            if (attributes.reverse === 1) {
              lowerLimit = lowerLimit + ' + 1';
            } else {
              upperLimit = upperLimit + ' - 1';
            }
          }
          const getShaderSource = (shaderHelper: ShaderHelper) => `
${shaderHelper.registerUniform('outputSize', 'u32').registerUniform('axis', 'u32').declareVariables(input, output)}
  ${shaderHelper.mainStart()}
    ${shaderHelper.guardAgainstOutOfBoundsWorkgroupSizes('uniforms.outputSize')}
    var inputIndices = ${output.offsetToIndices('global_idx')};
    var sum = 0.0;
    let first : i32 = ${lowerLimit};
    let last : i32 = ${upperLimit};
    for (var i : i32 = first; i < last; i++) {
      ${input.indicesSet('inputIndices', 'uniforms.axis', 'u32(i)')};
      sum = sum + ${input.getByIndices('inputIndices')};
    }
    ${output.setByOffset('global_idx', 'sum')};
  }`;
          return {
            name: 'CumSum',
            shaderCache: {hint: attributes.cacheKey, inputDependencies: [enableShapeUniforms ? 'rank' : 'dims']},
            getRunData: () => ({
              outputs: [{dims: inputShape, dataType: inputType}],
              dispatchGroup: {x: Math.ceil(outputSize / 64 /* workgroup size */)},
              programUniforms
            }),
            getShaderSource
          };
        };


export const cumsum = (context: ComputeContext, attributes: CumSumAttributes): void => {
  const inputShape = context.inputs[0].dims;
  const inputType = context.inputs[0].dataType;
  const axis = context.inputs[1];
  context.compute(createCumsumProgramInfo(inputType, inputShape, axis, attributes), {inputs: [0]});
};

export const parseCumSumAttributes = (attributes: Record<string, unknown>): CumSumAttributes => {
  const exclusive = attributes.exclusive as number;
  const reverse = attributes.reverse as number;
  return createAttributeWithCacheKey({exclusive, reverse});
};
