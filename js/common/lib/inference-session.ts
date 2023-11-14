// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

import {InferenceSession as InferenceSessionImpl} from './inference-session-impl.js';
import {OnnxValue, OnnxValueDataLocation} from './onnx-value.js';

export type MaybePromise<T> = T|Promise<T>;

/* eslint-disable @typescript-eslint/no-redeclare */

export declare namespace InferenceSession {
  // #region input/output types

  type OnnxValueMapType = {readonly [name: string]: OnnxValue};
  type NullableOnnxValueMapType = {readonly [name: string]: OnnxValue | null};

  /**
   * A feeds (model inputs) is an object that uses input names as keys and OnnxValue as corresponding values.
   */
  type FeedsType = OnnxValueMapType;

  /**
   * A fetches (model outputs) could be one of the following:
   *
   * - Omitted. Use model's output names definition.
   * - An array of string indicating the output names.
   * - An object that use output names as keys and OnnxValue or null as corresponding values.
   *
   * @remark
   * different from input argument, in output, OnnxValue is optional. If an OnnxValue is present it will be
   * used as a pre-allocated value by the inference engine; if omitted, inference engine will allocate buffer
   * internally.
   */
  type FetchesType = readonly string[]|NullableOnnxValueMapType;

  /**
   * A inferencing return type is an object that uses output names as keys and OnnxValue as corresponding values.
   */
  type ReturnType = OnnxValueMapType;

  // #endregion

  // #region session options

  /**
   * A set of configurations for session behavior.
   */
  export interface SessionOptions {
    /**
     * An array of execution provider options.
     *
     * An execution provider option can be a string indicating the name of the execution provider,
     * or an object of corresponding type.
     */
    executionProviders?: readonly ExecutionProviderConfig[];

    /**
     * The intra OP threads number.
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native).
     */
    intraOpNumThreads?: number;

    /**
     * The inter OP threads number.
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native).
     */
    interOpNumThreads?: number;

    /**
     * The free dimension override.
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native) or WebAssembly backend
     */
    freeDimensionOverrides?: {readonly [dimensionName: string]: number};

    /**
     * The optimization level.
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native) or WebAssembly backend
     */
    graphOptimizationLevel?: 'disabled'|'basic'|'extended'|'all';

    /**
     * Whether enable CPU memory arena.
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native) or WebAssembly backend
     */
    enableCpuMemArena?: boolean;

    /**
     * Whether enable memory pattern.
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native) or WebAssembly backend
     */
    enableMemPattern?: boolean;

    /**
     * Execution mode.
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native) or WebAssembly backend
     */
    executionMode?: 'sequential'|'parallel';

    /**
     * Optimized model file path.
     *
     * If this setting is specified, the optimized model will be dumped. In browser, a blob will be created
     * with a pop-up window.
     */
    optimizedModelFilePath?: string;

    /**
     * Wether enable profiling.
     *
     * This setting is a placeholder for a future use.
     */
    enableProfiling?: boolean;

    /**
     * File prefix for profiling.
     *
     * This setting is a placeholder for a future use.
     */
    profileFilePrefix?: string;

    /**
     * Log ID.
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native) or WebAssembly backend
     */
    logId?: string;

    /**
     * Log severity level. See
     * https://github.com/microsoft/onnxruntime/blob/main/include/onnxruntime/core/common/logging/severity.h
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native) or WebAssembly backend
     */
    logSeverityLevel?: 0|1|2|3|4;

    /**
     * Log verbosity level.
     *
     * This setting is available only in WebAssembly backend. Will support Node.js binding and react-native later
     */
    logVerbosityLevel?: number;

    /**
     * Specify string as a preferred data location for all outputs, or an object that use output names as keys and a
     * preferred data location as corresponding values.
     *
     * This setting is available only in ONNXRuntime Web for WebGL and WebGPU EP.
     */
    preferredOutputLocation?: OnnxValueDataLocation|{readonly [outputName: string]: OnnxValueDataLocation};

    /**
     * Store configurations for a session. See
     * https://github.com/microsoft/onnxruntime/blob/main/include/onnxruntime/core/session/
     * onnxruntime_session_options_config_keys.h
     *
     * This setting is available only in WebAssembly backend. Will support Node.js binding and react-native later
     *
     * @example
     * ```js
     * extra: {
     *   session: {
     *     set_denormal_as_zero: "1",
     *     disable_prepacking: "1"
     *   },
     *   optimization: {
     *     enable_gelu_approximation: "1"
     *   }
     * }
     * ```
     */
    extra?: Record<string, unknown>;
  }

  // #region execution providers

  // Currently, we have the following backends to support execution providers:
  // Backend Node.js binding: supports 'cpu' and 'cuda'.
  // Backend WebAssembly: supports 'cpu', 'wasm', 'xnnpack' and 'webnn'.
  // Backend ONNX.js: supports 'webgl'.
  // Backend React Native: supports 'cpu', 'xnnpack', 'coreml' (iOS), 'nnapi' (Android).
  interface ExecutionProviderOptionMap {
    cpu: CpuExecutionProviderOption;
    coreml: CoreMlExecutionProviderOption;
    cuda: CudaExecutionProviderOption;
    dml: DmlExecutionProviderOption;
    tensorrt: TensorRtExecutionProviderOption;
    wasm: WebAssemblyExecutionProviderOption;
    webgl: WebGLExecutionProviderOption;
    xnnpack: XnnpackExecutionProviderOption;
    webgpu: WebGpuExecutionProviderOption;
    webnn: WebNNExecutionProviderOption;
    nnapi: NnapiExecutionProviderOption;
  }

  type ExecutionProviderName = keyof ExecutionProviderOptionMap;
  type ExecutionProviderConfig =
      ExecutionProviderOptionMap[ExecutionProviderName]|ExecutionProviderOption|ExecutionProviderName|string;

  export interface ExecutionProviderOption {
    readonly name: string;
  }
  export interface CpuExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'cpu';
    useArena?: boolean;
  }
  export interface CudaExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'cuda';
    deviceId?: number;
  }
  export interface CoreMlExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'coreml';
    coreMlFlags?: number;
  }
  export interface DmlExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'dml';
    deviceId?: number;
  }
  export interface TensorRtExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'tensorrt';
    deviceId?: number;
  }
  export interface WebAssemblyExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'wasm';
  }
  export interface WebGLExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'webgl';
    // TODO: add flags
  }
  export interface XnnpackExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'xnnpack';
  }
  export interface WebGpuExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'webgpu';
    preferredLayout?: 'NCHW'|'NHWC';
  }
  export interface WebNNExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'webnn';
    deviceType?: 'cpu'|'gpu';
    numThreads?: number;
    powerPreference?: 'default'|'low-power'|'high-performance';
  }
  export interface CoreMLExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'coreml';
    useCPUOnly?: boolean;
    enableOnSubgraph?: boolean;
    onlyEnableDeviceWithANE?: boolean;
  }
  export interface NnapiExecutionProviderOption extends ExecutionProviderOption {
    readonly name: 'nnapi';
    useFP16?: boolean;
    useNCHW?: boolean;
    cpuDisabled?: boolean;
    cpuOnly?: boolean;
  }
  // #endregion

  // #endregion

  // #region run options

  /**
   * A set of configurations for inference run behavior
   */
  export interface RunOptions {
    /**
     * Log severity level. See
     * https://github.com/microsoft/onnxruntime/blob/main/include/onnxruntime/core/common/logging/severity.h
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native) or WebAssembly backend
     */
    logSeverityLevel?: 0|1|2|3|4;

    /**
     * Log verbosity level.
     *
     * This setting is available only in WebAssembly backend. Will support Node.js binding and react-native later
     */
    logVerbosityLevel?: number;

    /**
     * Terminate all incomplete OrtRun calls as soon as possible if true
     *
     * This setting is available only in WebAssembly backend. Will support Node.js binding and react-native later
     */
    terminate?: boolean;

    /**
     * A tag for the Run() calls using this
     *
     * This setting is available only in ONNXRuntime (Node.js binding and react-native) or WebAssembly backend
     */
    tag?: string;

    /**
     * Set a single run configuration entry. See
     * https://github.com/microsoft/onnxruntime/blob/main/include/onnxruntime/core/session/
     * onnxruntime_run_options_config_keys.h
     *
     * This setting is available only in WebAssembly backend. Will support Node.js binding and react-native later
     *
     * @example
     *
     * ```js
     * extra: {
     *   memory: {
     *     enable_memory_arena_shrinkage: "1",
     *   }
     * }
     * ```
     */
    extra?: Record<string, unknown>;
  }

  // #endregion

  // #region value metadata

  // eslint-disable-next-line @typescript-eslint/no-empty-interface
  interface ValueMetadata {
    // TBD
  }

  // #endregion
}

/**
 * Represent a runtime instance of an ONNX model.
 */
export interface InferenceSession {
  // #region run()

  /**
   * Execute the model asynchronously with the given feeds and options.
   *
   * @param feeds - Representation of the model input. See type description of `InferenceSession.InputType` for detail.
   * @param options - Optional. A set of options that controls the behavior of model inference.
   * @returns A promise that resolves to a map, which uses output names as keys and OnnxValue as corresponding values.
   */
  run(feeds: InferenceSession.FeedsType, options?: InferenceSession.RunOptions): Promise<InferenceSession.ReturnType>;

  /**
   * Execute the model asynchronously with the given feeds, fetches and options.
   *
   * @param feeds - Representation of the model input. See type description of `InferenceSession.InputType` for detail.
   * @param fetches - Representation of the model output. See type description of `InferenceSession.OutputType` for
   * detail.
   * @param options - Optional. A set of options that controls the behavior of model inference.
   * @returns A promise that resolves to a map, which uses output names as keys and OnnxValue as corresponding values.
   */
  run(feeds: InferenceSession.FeedsType, fetches: InferenceSession.FetchesType,
      options?: InferenceSession.RunOptions): Promise<InferenceSession.ReturnType>;

  // #endregion

  // #region release()

  /**
   * Release the inference session and the underlying resources.
   */
  release(): Promise<void>;

  // #endregion

  // #region profiling

  /**
   * Start profiling.
   */
  startProfiling(): void;

  /**
   * End profiling.
   */
  endProfiling(): void;

  // #endregion

  // #region metadata

  /**
   * Get input names of the loaded model.
   */
  readonly inputNames: readonly string[];

  /**
   * Get output names of the loaded model.
   */
  readonly outputNames: readonly string[];

  // /**
  //  * Get input metadata of the loaded model.
  //  */
  // readonly inputMetadata: ReadonlyArray<Readonly<InferenceSession.ValueMetadata>>;

  // /**
  //  * Get output metadata of the loaded model.
  //  */
  // readonly outputMetadata: ReadonlyArray<Readonly<InferenceSession.ValueMetadata>>;

  // #endregion
}

export interface InferenceSessionFactory {
  // #region create()

  /**
   * Create a new inference session and load model asynchronously from an ONNX model file.
   *
   * @param uri - The URI or file path of the model to load.
   * @param options - specify configuration for creating a new inference session.
   * @returns A promise that resolves to an InferenceSession object.
   */
  create(uri: string, options?: InferenceSession.SessionOptions): Promise<InferenceSession>;

  /**
   * Create a new inference session and load model asynchronously from an array bufer.
   *
   * @param buffer - An ArrayBuffer representation of an ONNX model.
   * @param options - specify configuration for creating a new inference session.
   * @returns A promise that resolves to an InferenceSession object.
   */
  create(buffer: MaybePromise<ArrayBufferLike>, options?: InferenceSession.SessionOptions): Promise<InferenceSession>;

  /**
   * Create a new inference session and load model asynchronously from segment of an array bufer.
   *
   * @param buffer - An ArrayBuffer representation of an ONNX model.
   * @param byteOffset - The beginning of the specified portion of the array buffer.
   * @param byteLength - The length in bytes of the array buffer.
   * @param options - specify configuration for creating a new inference session.
   * @returns A promise that resolves to an InferenceSession object.
   */
  create(
      buffer: MaybePromise<ArrayBufferLike>, byteOffset: number, byteLength?: number,
      options?: InferenceSession.SessionOptions): Promise<InferenceSession>;

  /**
   * Create a new inference session and load model asynchronously from a Uint8Array.
   *
   * @param buffer - A Uint8Array representation of an ONNX model.
   * @param options - specify configuration for creating a new inference session.
   * @returns A promise that resolves to an InferenceSession object.
   */
  create(buffer: MaybePromise<Uint8Array>, options?: InferenceSession.SessionOptions): Promise<InferenceSession>;

  // #endregion
}

// eslint-disable-next-line @typescript-eslint/naming-convention
export const InferenceSession: InferenceSessionFactory = InferenceSessionImpl;
