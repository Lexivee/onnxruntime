// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

import type {Env, InferenceSession, Tensor} from 'onnxruntime-common';

/**
 * Among all the tensor locations, only 'cpu' is serializable.
 */
export type SerializableTensorMetadata =
    [dataType: Tensor.Type, dims: readonly number[], data: Tensor.DataType, location: 'cpu'];

export type GpuBufferMetadata = {
  gpuBuffer: Tensor.GpuBufferType;
  download?: () => Promise<Tensor.DataTypeMap[Tensor.GpuBufferDataTypes]>;
  dispose?: () => void;
};

/**
 * Tensors on location 'cpu-pinned' and 'gpu-buffer' are not serializable.
 */
export type UnserializableTensorMetadata =
    [dataType: Tensor.Type, dims: readonly number[], data: GpuBufferMetadata, location: 'gpu-buffer']|
    [dataType: Tensor.Type, dims: readonly number[], data: Tensor.DataType, location: 'cpu-pinned'];

/**
 * Tensor metadata is a tuple of [dataType, dims, data, location], where
 * - dataType: tensor data type
 * - dims: tensor dimensions
 * - data: tensor data, which can be one of the following depending on the location:
 *   - cpu: Uint8Array
 *   - cpu-pinned: Uint8Array
 *   - gpu-buffer: GpuBufferMetadata
 * - location: tensor data location
 */
export type TensorMetadata = SerializableTensorMetadata|UnserializableTensorMetadata;

export type SerializableSessionMetadata = [sessionHandle: number, inputNames: string[], outputNames: string[]];

export type SerializableInternalBuffer = [bufferOffset: number, bufferLength: number];

interface MessageError {
  err?: string;
}

interface MessageInitWasm extends MessageError {
  type: 'init-wasm';
  in ?: Env.WebAssemblyFlags;
}

interface MessageInitOrt extends MessageError {
  type: 'init-ort';
  in ?: Env;
}

interface MessageCopyFromExternalBuffer extends MessageError {
  type: 'copy_from';
  in ?: {buffer: Uint8Array};
  out?: SerializableInternalBuffer;
}

interface MessageCreateSession extends MessageError {
  type: 'create';
  in ?: {model: SerializableInternalBuffer|Uint8Array; options?: InferenceSession.SessionOptions};
  out?: SerializableSessionMetadata;
}

interface MessageReleaseSession extends MessageError {
  type: 'release';
  in ?: number;
  out?: void;
}

interface MessageRun extends MessageError {
  type: 'run';
  in ?: {
    sessionId: number; inputIndices: number[]; inputs: SerializableTensorMetadata[]; outputIndices: number[];
    options: InferenceSession.RunOptions;
  };
  out?: SerializableTensorMetadata[];
}

interface MesssageEndProfiling extends MessageError {
  type: 'end-profiling';
  in ?: number;
  out?: void;
}

export type OrtWasmMessage = MessageInitWasm|MessageInitOrt|MessageCopyFromExternalBuffer|MessageCreateSession|
    MessageReleaseSession|MessageRun|MesssageEndProfiling;
