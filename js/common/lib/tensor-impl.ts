// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

import {Tensor as TensorInterface} from './tensor';

type TensorType = TensorInterface.Type;
type TensorDataType = TensorInterface.DataType;

type SupportedTypedArrayConstructors = Float32ArrayConstructor|Uint8ArrayConstructor|Int8ArrayConstructor|
    Uint16ArrayConstructor|Int16ArrayConstructor|Int32ArrayConstructor|BigInt64ArrayConstructor|Uint8ArrayConstructor|
    Float64ArrayConstructor|Uint32ArrayConstructor|BigUint64ArrayConstructor;
type SupportedTypedArray = InstanceType<SupportedTypedArrayConstructors>;

const isBigInt64ArrayAvailable = typeof BigInt64Array !== 'undefined' && typeof BigInt64Array.from === 'function';
const isBigUint64ArrayAvailable = typeof BigUint64Array !== 'undefined' && typeof BigUint64Array.from === 'function';

// a runtime map that maps type string to TypedArray constructor. Should match Tensor.DataTypeMap.
const NUMERIC_TENSOR_TYPE_TO_TYPEDARRAY_MAP = new Map<string, SupportedTypedArrayConstructors>([
  ['float32', Float32Array],
  ['uint8', Uint8Array],
  ['int8', Int8Array],
  ['uint16', Uint16Array],
  ['int16', Int16Array],
  ['int32', Int32Array],
  ['bool', Uint8Array],
  ['float64', Float64Array],
  ['uint32', Uint32Array],
]);

// a runtime map that maps type string to TypedArray constructor. Should match Tensor.DataTypeMap.
const NUMERIC_TENSOR_TYPEDARRAY_TO_TYPE_MAP = new Map<SupportedTypedArrayConstructors, TensorType>([
  [Float32Array, 'float32'],
  [Uint8Array, 'uint8'],
  [Int8Array, 'int8'],
  [Uint16Array, 'uint16'],
  [Int16Array, 'int16'],
  [Int32Array, 'int32'],
  [Float64Array, 'float64'],
  [Uint32Array, 'uint32'],
]);

if (isBigInt64ArrayAvailable) {
  NUMERIC_TENSOR_TYPE_TO_TYPEDARRAY_MAP.set('int64', BigInt64Array);
  NUMERIC_TENSOR_TYPEDARRAY_TO_TYPE_MAP.set(BigInt64Array, 'int64');
}
if (isBigUint64ArrayAvailable) {
  NUMERIC_TENSOR_TYPE_TO_TYPEDARRAY_MAP.set('uint64', BigUint64Array);
  NUMERIC_TENSOR_TYPEDARRAY_TO_TYPE_MAP.set(BigUint64Array, 'uint64');
}

/**
 * calculate size from dims.
 *
 * @param dims the dims array. May be an illegal input.
 */
const calculateSize = (dims: readonly unknown[]): number => {
  let size = 1;
  for (let i = 0; i < dims.length; i++) {
    const dim = dims[i];
    if (typeof dim !== 'number' || !Number.isSafeInteger(dim)) {
      throw new TypeError(`dims[${i}] must be an integer, got: ${dim}`);
    }
    if (dim < 0) {
      throw new RangeError(`dims[${i}] must be a non-negative integer, got: ${dim}`);
    }
    size *= dim;
  }
  return size;
};

export class Tensor implements TensorInterface {
  // #region constructors
  constructor(type: TensorType, data: TensorDataType|readonly number[]|readonly boolean[], dims?: readonly number[]);
  constructor(data: TensorDataType|readonly boolean[], dims?: readonly number[]);
  constructor(
      arg0: TensorType|TensorDataType|readonly boolean[], arg1?: TensorDataType|readonly number[]|readonly boolean[],
      arg2?: readonly number[]) {
    let type: TensorType;
    let data: TensorDataType;
    let dims: typeof arg1|typeof arg2;
    // check whether arg0 is type or data
    if (typeof arg0 === 'string') {
      //
      // Override: constructor(type, data, ...)
      //
      type = arg0;
      dims = arg2;
      if (arg0 === 'string') {
        // string tensor
        if (!Array.isArray(arg1)) {
          throw new TypeError('A string tensor\'s data must be a string array.');
        }
        // we don't check whether every element in the array is string; this is too slow. we assume it's correct and
        // error will be populated at inference
        data = arg1;
      } else {
        // numeric tensor
        const typedArrayConstructor = NUMERIC_TENSOR_TYPE_TO_TYPEDARRAY_MAP.get(arg0);
        if (typedArrayConstructor === undefined) {
          throw new TypeError(`Unsupported tensor type: ${arg0}.`);
        }
        if (Array.isArray(arg1)) {
          // use 'as any' here because TypeScript's check on type of 'SupportedTypedArrayConstructors.from()' produces
          // incorrect results.
          // 'typedArrayConstructor' should be one of the typed array prototype objects.
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          data = (typedArrayConstructor as any).from(arg1);
        } else if (arg1 instanceof typedArrayConstructor) {
          data = arg1;
        } else {
          throw new TypeError(`A ${type} tensor's data must be type of ${typedArrayConstructor}`);
        }
      }
    } else {
      //
      // Override: constructor(data, ...)
      //
      dims = arg1;
      if (Array.isArray(arg0)) {
        // only boolean[] and string[] is supported
        if (arg0.length === 0) {
          throw new TypeError('Tensor type cannot be inferred from an empty array.');
        }
        const firstElementType = typeof arg0[0];
        if (firstElementType === 'string') {
          type = 'string';
          data = arg0;
        } else if (firstElementType === 'boolean') {
          type = 'bool';
          // 'arg0' is of type 'boolean[]'. Uint8Array.from(boolean[]) actually works, but typescript thinks this is
          // wrong type. We use 'as any' to make it happy.
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          data = Uint8Array.from(arg0 as any[]);
        } else {
          throw new TypeError(`Invalid element type of data array: ${firstElementType}.`);
        }
      } else {
        // get tensor type from TypedArray
        const mappedType =
            NUMERIC_TENSOR_TYPEDARRAY_TO_TYPE_MAP.get(arg0.constructor as SupportedTypedArrayConstructors);
        if (mappedType === undefined) {
          throw new TypeError(`Unsupported type for tensor data: ${arg0.constructor}.`);
        }
        type = mappedType;
        data = arg0 as SupportedTypedArray;
      }
    }

    // type and data is processed, now processing dims
    if (dims === undefined) {
      // assume 1-D tensor if dims omitted
      dims = [data.length];
    } else if (!Array.isArray(dims)) {
      throw new TypeError('A tensor\'s dims must be a number array');
    }

    // perform check
    const size = calculateSize(dims);
    if (size !== data.length) {
      throw new Error(`Tensor's size(${size}) does not match data length(${data.length}).`);
    }

    this.dims = dims as readonly number[];
    this.type = type;
    this.data = data;
    this.size = size;
  }
  // #endregion
  static bufferToTensor(
      buffer: Uint8ClampedArray, height: number, width: number, format: string = 'rgba',
      norm: boolean = false): Tensor {
    var offset = height * width;
    const float32Data = new Float32Array(offset * 3);
    var step: number = 4;
    var R_ptr: number = 0;
    var G_ptr: number = 1;
    var B_ptr: number = 2;
    var normValue: number = 255.;

    if (format == 'RGBA') {
      step = 4;
      R_ptr = 0;
      G_ptr = 1;
      B_ptr = 2;
    } else if (format == 'RGB') {
      step = 3;
      R_ptr = 0;
      G_ptr = 1;
      B_ptr = 2;
    } else if (format == 'RBG') {
      step = 3;
      R_ptr = 0;
      B_ptr = 1;
      G_ptr = 2;
    }

    if (norm) {
      var maxValue: number = -300;
      for (let i = 0; i < offset * step; i++) {
        if (buffer[i] > maxValue) maxValue = buffer[i];
      }
      normValue = maxValue;
    }

    for (let i = 0, RIndex = 0, GIndex = offset, BIndex = offset * 2; i < offset;
         i++, R_ptr += step, B_ptr += step, G_ptr += step) {
      float32Data[RIndex++] = buffer[R_ptr] / normValue;
      float32Data[GIndex++] = buffer[G_ptr] / normValue;
      float32Data[BIndex++] = buffer[B_ptr] / normValue;
    }

    // Float32Array -> ort.Tensor
    const inputTensor = new Tensor('float32', float32Data, [1, 3, height, width]);
    return inputTensor;
  }

  // #region factory
  static fromImage(image: ImageData): Tensor;
  static fromImage(image: HTMLImageElement): Tensor;
  static fromImage(image: ImageBitmap, format?: string): Tensor;
  static fromImage(image: ImageData|HTMLImageElement|ImageBitmap, format?: 'rgb'|'rbg'|'rgba'): Tensor {
    const isHTMLImageEle = typeof (HTMLImageElement) !== 'undefined' && image instanceof HTMLImageElement;
    const isImageDataEle = typeof (ImageData) !== 'undefined' && image instanceof ImageData;
    const isImageBitmap = typeof (ImageBitmap) !== 'undefined' && image instanceof ImageBitmap;

    var data: Uint8ClampedArray;
    var image_height: number = image.height;
    var image_width: number = image.width;

    if (isHTMLImageEle) {
      let Pixels2DContext: CanvasRenderingContext2D|null;

      Pixels2DContext = document.createElement('canvas').getContext('2d');

      if (Pixels2DContext != null) {
        Pixels2DContext.drawImage(image as HTMLImageElement, 0, 0, image_width, image_height);
        data = Pixels2DContext.getImageData(0, 0, image.width, image.height).data;
      } else {
        throw new Error('Can not access image data');
      }
    } else if (isImageDataEle) {
      data = (image as ImageData).data;
    } else if (isImageBitmap) {
      if (format == null) {
        throw new Error('Please provide image format with Imagebitmap');
      }
      let Pixels2DContext: CanvasRenderingContext2D|null;

      Pixels2DContext = document.createElement('canvas').getContext('2d');

      if (Pixels2DContext != null) {
        Pixels2DContext.drawImage(image as ImageBitmap, 0, 0, image_width, image_height);
        data = Pixels2DContext.getImageData(0, 0, image.width, image.height).data;
        return Tensor.bufferToTensor(data, image_width, image_width, format);
      } else {
        throw new Error('Can not access image data');
      }
    } else {
      throw new Error('Input data provided is not supported - aborted tensor creation');
    }

    return Tensor.bufferToTensor(data, image_width, image_width);
  }

  static toImage(tensor: Tensor): ImageData;
  static toImage(tensor: Tensor): ImageData {
    let Pixels2DContext: CanvasRenderingContext2D|null;

    Pixels2DContext = document.createElement('canvas').getContext('2d');
    if (Pixels2DContext != null) {
      var image_height = tensor.dims[3];
      var image_width = tensor.dims[2];
      var image = Pixels2DContext.createImageData(image_width, image_height);
      var R_ptr = 0;
      var G_ptr = image_width * image_height;
      var B_ptr = image_width * image_height * 2;

      for (let i = 0; i < image_height * image_width * 3; i += 4) {
        image.data[i + 0] = (tensor.data[R_ptr++] as number) * 255.;  // R value
        image.data[i + 1] = (tensor.data[G_ptr++] as number) * 255.;  // G value
        image.data[i + 2] = (tensor.data[B_ptr++] as number) * 255.;  // B value
        image.data[i + 3] = 255;                                      // A value
      }
    } else {
      throw new Error('Can not access image data');
    }

    return image;
  }

  // #region fields
  readonly dims: readonly number[];
  readonly type: TensorType;
  readonly data: TensorDataType;
  readonly size: number;
  // #endregion

  // #region tensor utilities
  reshape(dims: readonly number[]): Tensor {
    return new Tensor(this.type, this.data, dims);
  }
  // #endregion
}
