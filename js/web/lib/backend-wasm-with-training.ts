// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

import {InferenceSession, TrainingSessionHandler} from 'onnxruntime-common';

import {OnnxruntimeWebAssemblyBackend} from './backend-wasm'

class OnnxruntimeTrainingWebAssemblyBackend extends OnnxruntimeWebAssemblyBackend {
  async createTrainingSessionHandler(
      checkpointStateUriOrBuffer: string|Uint8Array, trainModelUriOrBuffer: string|Uint8Array,
      evalModelUriOrBuffer: string|Uint8Array, optimizerModelUriOrBuffer: string|Uint8Array,
      options: InferenceSession.SessionOptions): Promise<TrainingSessionHandler> {
    throw new Error('Method not implemented yet.');
  }
}

export const wasmBackend = new OnnxruntimeTrainingWebAssemblyBackend();
