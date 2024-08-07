// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

import {DataType, tensorDataTypeEnumToString} from '../../../wasm-common';
import {TensorView} from '../../tensor-view';
import {convertFloat16ToFloat32, MAX_CLIP, MIN_CLIP, ShapeUtil} from '../../util';
import {AttributeWithCacheKey, createAttributeWithCacheKey} from '../attribute-with-cache-key';
import {ComputeContext, ProgramInfo} from '../types';

import {inputVariable, outputVariable, ShaderHelper, tensorTypeToWsglValueType} from './common';

type BuiltinFunctionName = string;
type ElementwiseCustomExpression = (expression: string) => string;
type ElementwiseFunctionCall = BuiltinFunctionName|ElementwiseCustomExpression;

const createElementwiseProgramShader =
    (shaderHelper: ShaderHelper, datasize: number, inputDataType: number, outputDataType: number,
     funcCall: ElementwiseFunctionCall, additionalImplementation?: string): string => {
      const vecSize = Math.ceil(datasize / 4);

      let expression = '';
      if (typeof funcCall === 'string') {
        expression = `${funcCall}(a)`;
      } else {
        expression = funcCall('a');
      }

      const input = inputVariable('inputData', inputDataType, [vecSize], 4);
      const output = outputVariable('outputData', outputDataType, [vecSize], 4);

      return `
      ${shaderHelper.registerUniform('vec_size', 'u32').declareVariables(input, output)}

  ${additionalImplementation ?? ''}

  ${shaderHelper.mainStart()}
    ${shaderHelper.guardAgainstOutOfBoundsWorkgroupSizes('uniforms.vec_size')}

    let a = ${input.getByOffset('global_idx')};
    ${output.setByOffset('global_idx', expression)}
  }`;
    };

const createElementwiseProgramInfo =
    (input: TensorView, name: string, funcCall: ElementwiseFunctionCall, additionalImplementation?: string,
     cacheKey?: string, outputDataType: number = input.dataType): ProgramInfo => ({
      name,
      shaderCache: {hint: cacheKey, inputDependencies: ['type']},
      getShaderSource: shaderHelper => createElementwiseProgramShader(
          shaderHelper, ShapeUtil.size(input.dims), input.dataType, outputDataType, funcCall, additionalImplementation),
      getRunData: (inputTensors) => ({
        outputs: [{dims: input.dims, dataType: outputDataType}],
        dispatchGroup:
            {x: Math.ceil(ShapeUtil.size(inputTensors[0].dims) / 64 /* workgroup size */ / 4 /* vec size */)},
        programUniforms: [
          {type: DataType.uint32, data: Math.ceil(ShapeUtil.size(input.dims) / 4)},
        ],
      })
    });

export const abs = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Abs', 'abs'));
};

export const acos = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Acos', 'acos'));
};

export const acosh = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Acosh', 'acosh'));
};

export const asin = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Asin', 'asin'));
};

export const asinh = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Asinh', 'asinh'));
};

export const atan = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Atan', 'atan'));
};
export const atanh = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Atanh', 'atanh'));
};

export interface CastAttributes extends AttributeWithCacheKey {
  readonly to: number;
  readonly saturate?: boolean;
}

export const parseCastAttributes = (attributes: Record<string, unknown>): CastAttributes =>
    createAttributeWithCacheKey(attributes as {to: number});


export const cast = (context: ComputeContext, attributes: CastAttributes): void => {
  let func: ElementwiseFunctionCall;
  switch (attributes.to) {
    case DataType.float16:
      func = 'vec4<f16>';
      break;
    case DataType.float:
      func = 'vec4<f32>';
      break;
    case DataType.uint32:
      func = 'vec4<u32>';
      break;
    case DataType.int32:
      func = 'vec4<i32>';
      break;
    case DataType.bool:
      func = 'vec4<bool>';
      break;
    default:
      throw new RangeError(`not supported type (specified in attribute 'to' from 'Cast' operator): ${attributes.to}`);
  }
  context.compute(
      createElementwiseProgramInfo(context.inputs[0], 'Cast', func, undefined, attributes.cacheKey, attributes.to));
};

export interface ClipAttributes extends AttributeWithCacheKey {
  readonly min: number|undefined;
  readonly max: number|undefined;
}

const generateClipAttributesFromInputs = (inputs: readonly TensorView[]): ClipAttributes => {
  let min = MIN_CLIP.get(tensorDataTypeEnumToString(inputs[0].dataType));
  let max = MAX_CLIP.get(tensorDataTypeEnumToString(inputs[0].dataType));
  if (inputs.length >= 2 && inputs[1].data !== 0) {
    switch (inputs[1].dataType) {
      case DataType.float:
        min = inputs[1].getFloat32Array()[0];
        break;
      case DataType.int32:
        min = inputs[1].getInt32Array()[0];
        break;
      case DataType.float16:
        min = convertFloat16ToFloat32(inputs[1].getUint16Array())[0];
        break;
      default:
        throw new Error('Unsupport data type');
    }
  }

  if (inputs.length >= 3 && inputs[2].data !== 0) {
    switch (inputs[2].dataType) {
      case DataType.float:
        max = inputs[2].getFloat32Array()[0];
        break;
      case DataType.int32:
        max = inputs[2].getInt32Array()[0];
        break;
      case DataType.float16:
        max = convertFloat16ToFloat32(inputs[2].getUint16Array())[0];
        break;
      default:
        throw new Error('Unsupport data type');
    }
  }

  return createAttributeWithCacheKey({min, max});
};

export const clip = (context: ComputeContext, clipAttributes: ClipAttributes): void => {
  const attributes = context.inputs.length === 1 ? clipAttributes : generateClipAttributesFromInputs(context.inputs);
  const dataType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(
      createElementwiseProgramInfo(
          context.inputs[0], 'Clip', a => `clamp(${a}, clip_min_, clip_max_)`, `
    const clip_min_: vec4<${dataType}> = vec4(${dataType}(${attributes.min}));
    const clip_max_: vec4<${dataType}> = vec4(${dataType}(${attributes.max}));
`,
          attributes.cacheKey),
      {inputs: [0]});
};

export const ceil = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Ceil', 'ceil'));
};

export const cos = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Cos', 'cos'));
};

export const cosh = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Cosh', 'cosh'));
};

export interface AlphaAttributes extends AttributeWithCacheKey {
  readonly alpha: number;
}

export const parseAlphaAttributes = (attributes: Record<string, unknown>): AlphaAttributes =>
    createAttributeWithCacheKey(attributes as {alpha: number});

export const elu = (context: ComputeContext, attributes: AlphaAttributes): void => {
  const dataType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(createElementwiseProgramInfo(
      context.inputs[0], 'Elu', a => `elu_vf32(${a})`, `
  const elu_alpha_ = ${dataType}(${attributes.alpha});

  fn elu_f32(a: ${dataType}) -> ${dataType} {
  return select((exp(a) - 1.0) * elu_alpha_, a, a >= 0.0);
  }

  fn elu_vf32(v: vec4<${dataType}>) -> vec4<${dataType}> {
  return vec4(elu_f32(v.x), elu_f32(v.y), elu_f32(v.z), elu_f32(v.w));
  }`,
      attributes.cacheKey));
};

export const erfImpl = (varType = 'f32') => `
const r0: ${varType} = 0.3275911;
const r1: ${varType} = 0.254829592;
const r2: ${varType} = -0.284496736;
const r3: ${varType} = 1.421413741;
const r4: ${varType} = -1.453152027;
const r5: ${varType} = 1.061405429;

fn erf_vf32(v: vec4<${varType}>) -> vec4<${varType}> {
  let absv = abs(v);
  let x = 1.0 / (1.0 + r0 * absv);
  return sign(v) * (1.0 - ((((r5 * x + r4) * x + r3) * x + r2) * x + r1) * x * exp(-absv * absv));
}`;

export const erf = (context: ComputeContext): void => {
  const dataType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Erf', a => `erf_vf32(${a})`, erfImpl(dataType)));
};

export const exp = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Exp', 'exp'));
};

export const floor = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Floor', 'floor'));
};

export const gelu = (context: ComputeContext): void => {
  const dataType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(createElementwiseProgramInfo(
      context.inputs[0], 'Gelu', a => `0.5 * ${a} * (1.0 + erf_vf32(${a} * 0.7071067811865475))`, erfImpl(dataType)));
};

export const leakyRelu = (context: ComputeContext, attributes: AlphaAttributes): void => {
  const dataType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(createElementwiseProgramInfo(
      context.inputs[0], 'LeakyRelu', a => `select(leaky_relu_alpha_ * ${a}, ${a}, ${a} >= vec4<${dataType}>(0.0))`,
      `const leaky_relu_alpha_ = ${dataType}(${attributes.alpha});`, attributes.cacheKey));
};

export const not = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Not', a => `!${a}`));
};

export const neg = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Neg', a => `-${a}`));
};

export const reciprocal = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Reciprocal', a => `1.0/${a}`));
};

export const relu = (context: ComputeContext): void => {
  const dataType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(createElementwiseProgramInfo(
      context.inputs[0], 'Relu', a => `select(vec4<${dataType}>(0.0), ${a}, ${a} > vec4<${dataType}>(0.0))`));
};

export const sigmoid = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Sigmoid', a => `(1.0 / (1.0 + exp(-${a})))`));
};

export interface HardSigmoidAttributes extends AttributeWithCacheKey {
  readonly alpha: number;
  readonly beta: number;
}

export const parseHardSigmoidAttributes = (attributes: Record<string, unknown>): HardSigmoidAttributes =>
    createAttributeWithCacheKey(attributes as {
      alpha: number;
      beta: number;
    });

export const hardSigmoid = (context: ComputeContext, attributes: HardSigmoidAttributes): void => {
  const dataType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(createElementwiseProgramInfo(
      context.inputs[0], 'HardSigmoid',
      a => `max(vec4<${dataType}>(0.0), min(vec4<${dataType}>(1.0), ${attributes.alpha} * ${a} + vec4<${dataType}>(${
          attributes.beta})))`,
      undefined, attributes.cacheKey));
};

export const sin = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Sin', 'sin'));
};

export const sinh = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Sinh', 'sinh'));
};

export const sqrt = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Sqrt', 'sqrt'));
};

export const tan = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Tan', 'tan'));
};

export const tanhExpression = (a: string) => `sign(${a}) * (1 - exp(-2 * abs(${a}))) / (1 + exp(-2 * abs(${a})))`;

export const tanh = (context: ComputeContext): void => {
  // TODO: revisit after https://github.com/gpuweb/gpuweb/issues/4458 is resolved
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Tanh', tanhExpression));
};

export const fastGeluImpl = (varType = 'f32') => `
const fast_gelu_a: ${varType} = 0.5;
const fast_gelu_b: ${varType} = 0.7978845608028654;
const fast_gelu_c: ${varType} = 0.035677408136300125;

fn tanh_v(v: vec4<${varType}>) -> vec4<${varType}> {
  return ${tanhExpression('v')};
}
`;

export const fastGeluExpression = (x: string) =>
    `(fast_gelu_a + fast_gelu_a * tanh_v(${x} * (fast_gelu_c * ${x} * ${x} + fast_gelu_b))) * ${x}`;

export const fastGelu = (context: ComputeContext): void => {
  const dataType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(createElementwiseProgramInfo(
      context.inputs[0], 'FastGelu', fastGeluExpression, fastGeluImpl(dataType), undefined,
      context.inputs[0].dataType));
};

export const thresholdedRelu = (context: ComputeContext, attributes: AlphaAttributes): number => {
  const dataType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(createElementwiseProgramInfo(
      context.inputs[0], 'ThresholdedRelu', a => `select(vec4<${dataType}>(0.0), ${a}, ${a} > thresholded_relu_alpha_)`,
      `const thresholded_relu_alpha_ = vec4<${dataType}>(${attributes.alpha});`, attributes.cacheKey));
  return 0;
};

export const log = (context: ComputeContext): void => {
  context.compute(createElementwiseProgramInfo(context.inputs[0], 'Log', 'log'));
};

export const quickGeluImpl = (varType: string, alpha: number) => `
const alpha = vec4<${varType}>(${alpha});
const one = ${varType}(1.0);
const zero = ${varType}(0.0);

fn quick_gelu_impl(x: vec4<${varType}>) -> vec4<${varType}> {
  let v = x *alpha;
  var x1 : vec4<${varType}>;
  for (var i = 0; i < 4; i = i + 1) {
    if (v[i] >= zero) {
      x1[i] = one / (one + exp(-v[i]));
    } else {
      x1[i] = one - one / (one + exp(v[i]));
    }
  }
  return x * x1;
}
`;

export const quickGeluExpression = (x: string) => `quick_gelu_impl(${x})`;

export const quickgelu = (context: ComputeContext, attributes: AlphaAttributes): void => {
  const dType = tensorTypeToWsglValueType(context.inputs[0].dataType);
  context.compute(createElementwiseProgramInfo(
      context.inputs[0], 'QuickGelu', quickGeluExpression, quickGeluImpl(dType, attributes.alpha), attributes.cacheKey,
      context.inputs[0].dataType));
};
