// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Collections.Generic;


namespace Microsoft.ML.OnnxRuntime
{
    internal struct GlobalOptions  //Options are currently not accessible to user
    {
        public string LogId { get; set; }
        public LogLevel LogLevel { get; set; }
    }

    /// <summary>
    /// Logging level used to specify amount of logging when
    /// creating environment. The lower the value is the more logging
    /// will be output. A specific value output includes everything
    /// that higher values output.
    /// </summary>
    public enum LogLevel
    {
        Verbose = 0, // Everything
        Info = 1,    // Informational
        Warning = 2, // Warnings
        Error = 3,   // Errors
        Fatal = 4    // Results in the termination of the application.
    }

    /// <summary>
    /// Language projection property for telemetry event for tracking the source usage of ONNXRUNTIME
    /// </summary>
    public enum OrtLanguageProjection
    {
        ORT_PROJECTION_C = 0,
        ORT_PROJECTION_CPLUSPLUS = 1,
        ORT_PROJECTION_CSHARP = 2,
        ORT_PROJECTION_PYTHON = 3,
        ORT_PROJECTION_JAVA = 4,
        ORT_PROJECTION_WINML = 5,
    }

    /// <summary>
    /// Delegate for logging function callback.
    /// </summary>
    /// <param name="param">Pointer to data passed into Constructor `log_param` parameter.</param>
    /// <param name="severity">Log severity level.</param>
    /// <param name="category">Log category</param>
    /// <param name="logid">Log Id.</param>
    /// <param name="code_location">Code location detail.</param>
    /// <param name="message">Log message.</param>
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    public delegate void OrtLoggingFunction(IntPtr param, OrtLoggingLevel severity, string category, string logid, string code_location, string message);

    /// <summary>
    /// This class initializes the process-global ONNX Runtime environment instance (OrtEnv).
    /// The singleton class OrtEnv contains the process-global ONNX Runtime environment.
    /// It sets up logging, creates system wide thread-pools (if Thread Pool options are provided)
    /// and other necessary things for OnnxRuntime to function. Create or access OrtEnv by calling
    /// the Instance() method. Call this method before doing anything else in your application.
    /// </summary>
    public sealed class OrtEnv : SafeHandle
    {
        private static readonly Lazy<OrtEnv> _instance = new Lazy<OrtEnv>(CreateDefaultOrtEnv);
        private static LogLevel envLogLevel = LogLevel.Warning;
        private readonly bool _owned;
        private OrtLoggingFunction _loggingFunction;

        #region private methods
        private static OrtEnv CreateDefaultOrtEnv()
        {
            IntPtr envHandle = IntPtr.Zero;
            NativeApiStatus.VerifySuccess(NativeMethods.OrtCreateEnv(envLogLevel, @"CSharpOnnxRuntime", out envHandle));
            try
            {
                NativeApiStatus.VerifySuccess(NativeMethods.OrtSetLanguageProjection(envHandle, OrtLanguageProjection.ORT_PROJECTION_CSHARP));
                return new OrtEnv(envHandle, true);
            }
            catch (OnnxRuntimeException)
            {
                NativeMethods.OrtReleaseEnv(envHandle);
                throw;
            }
        }
        #endregion

        #region internal methods

        internal IntPtr Handle => handle;

        internal OrtEnv(IntPtr allocInfo, bool owned)
            : base(allocInfo, true)
        {
            _owned = owned;
        }

        #endregion

        #region public methods

        /// <summary>
        /// Returns an instance of OrtEnv
        /// It returns the same instance on every call - `OrtEnv` is singleton
        /// Exception caching: May throw an exception on every call, if the `OrtEnv` constructor threw an exception
        /// </summary>
        /// <returns>Returns a singleton instance of OrtEnv that represents native OrtEnv object</returns>
        public static OrtEnv Instance() { return _instance.Value; }

        /// <summary>
        /// Constructor to create a non-default OrtEnv instance (for reuse between InferenceSessions) and receive internal logging callbacks.
        /// </summary>
        /// <param name="logging_function">The logging callback function.</param>
        /// <param name="log_level">The log severity level.</param>
        /// <param name="log_param">Pointer to arbitrary data passed as the OrtLoggingFunction `param` parameter to <paramref name="logging_function"/>.</param>
        public OrtEnv(OrtLoggingFunction logging_function, LogLevel log_level, IntPtr log_param)
            : base (IntPtr.Zero, true)
        {
            _loggingFunction = logging_function; // prevent GC
            var logFunctionPtr = Marshal.GetFunctionPointerForDelegate(_loggingFunction);

            IntPtr envHandle = IntPtr.Zero;
            NativeApiStatus.VerifySuccess(NativeMethods.OrtCreateEnvWithCustomLogger(logFunctionPtr, log_param, log_level, @"CSharpOnnxRuntime", out handle));
            try
            {
                NativeApiStatus.VerifySuccess(NativeMethods.OrtSetLanguageProjection(handle, OrtLanguageProjection.ORT_PROJECTION_CSHARP));
            }
            catch (OnnxRuntimeException)
            {
                NativeMethods.OrtReleaseEnv(handle);
                throw;
            }
            _owned = true;
        }

        /// <summary>
        /// Enable platform telemetry collection where applicable
        /// (currently only official Windows ORT builds have telemetry collection capabilities)
        /// </summary>
        public void EnableTelemetryEvents()
        {
            NativeApiStatus.VerifySuccess(NativeMethods.OrtEnableTelemetryEvents(Handle));
        }

        /// <summary>
        /// Disable platform telemetry collection
        /// </summary>
        public void DisableTelemetryEvents()
        {
            NativeApiStatus.VerifySuccess(NativeMethods.OrtDisableTelemetryEvents(Handle));
        }

        /// <summary>
        /// Create and register an allocator to the OrtEnv instance
        /// so as to enable sharing across all sessions using the OrtEnv instance
        /// <param name="memInfo">OrtMemoryInfo instance to be used for allocator creation</param>
        /// <param name="arenaCfg">OrtArenaCfg instance that will be used to define the behavior of the arena based allocator</param>
        /// </summary>
        public void CreateAndRegisterAllocator(OrtMemoryInfo memInfo, OrtArenaCfg arenaCfg)
        {
            NativeApiStatus.VerifySuccess(NativeMethods.OrtCreateAndRegisterAllocator(Handle, memInfo.Pointer, arenaCfg.Pointer));
        }

        /// <summary>
        /// This function returns the onnxruntime version string
        /// </summary>
        /// <returns>version string</returns>
        public string GetVersionString()
        {
            IntPtr versionString = NativeMethods.OrtGetVersionString();
            return NativeOnnxValueHelper.StringFromNativeUtf8(versionString);
        }

        /// <summary>
        /// Queries all the execution providers supported in the native onnxruntime shared library
        /// </summary>
        /// <returns>an array of strings that represent execution provider names</returns>
        public string[] GetAvailableProviders()
        {
            IntPtr availableProvidersHandle = IntPtr.Zero;
            int numProviders;

            NativeApiStatus.VerifySuccess(NativeMethods.OrtGetAvailableProviders(out availableProvidersHandle, out numProviders));
            try
            {
                var availableProviders = new string[numProviders];
                for (int i = 0; i < numProviders; ++i)
                {
                    availableProviders[i] = NativeOnnxValueHelper.StringFromNativeUtf8(Marshal.ReadIntPtr(availableProvidersHandle, IntPtr.Size * i));
                }
                return availableProviders;
            }
            finally
            {
                // This should never throw. The original C API should have never returned status in the first place.
                // If it does, it is BUG and we would like to propagate that to the user in the form of an exception
                NativeApiStatus.VerifySuccess(NativeMethods.OrtReleaseAvailableProviders(availableProvidersHandle, numProviders));
            }
        }


        /// <summary>
        /// Get/Set log level property of OrtEnv instance
        /// </summary>
        /// <returns>env log level</returns>
        public LogLevel EnvLogLevel
        {
            get { return envLogLevel; }
            set
            {
                NativeApiStatus.VerifySuccess(NativeMethods.OrtUpdateEnvWithCustomLogLevel(Handle, value));
                envLogLevel = value;
            }
        }
        #endregion

        #region SafeHandle
        /// <summary>
        /// Overrides SafeHandle.IsInvalid
        /// </summary>
        /// <value>returns true if handle is equal to Zero</value>
        public override bool IsInvalid => handle == IntPtr.Zero;

        /// <summary>
        /// Overrides SafeHandle.ReleaseHandle() to properly dispose of
        /// the native instance of OrtEnv
        /// </summary>
        /// <returns>always returns true</returns>
        protected override bool ReleaseHandle()
        {
            if (_owned)
            {
                NativeMethods.OrtReleaseEnv(handle);
            }
            handle = IntPtr.Zero;
            _loggingFunction = null;
            return true;
        }
        #endregion
    }
}
