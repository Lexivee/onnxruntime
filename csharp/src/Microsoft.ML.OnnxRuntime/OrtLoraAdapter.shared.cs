﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
using System;
using System.Runtime.InteropServices;

namespace Microsoft.ML.OnnxRuntime
{
    /// <summary>
    /// Represents Lora Adapter in memory
    /// </summary>
    public class OrtLoraAdapter : SafeHandle
    {
        /// <summary>
        /// Creates an instance of OrtLoraAdapter.
        /// The adapter file is memory mapped. If allocator parameter
        /// is provided, then lora parameters are copied to the memory
        /// allocated by the specified allocator.
        /// </summary>
        /// <param name="adapterPath">path to the adapter file</param>
        /// <param name="ortAllocator">optional allocator, can be null, must be a device allocator</param>
        /// <returns></returns>
        public static OrtLoraAdapter Create(string adapterPath, OrtAllocator ortAllocator)
        {
            var platformPath = NativeOnnxValueHelper.GetPlatformSerializedString(adapterPath);
            var allocatorHandle = (ortAllocator != null) ? ortAllocator.Pointer : IntPtr.Zero;
            NativeApiStatus.VerifySuccess(NativeMethods.CreateLoraAdapter(platformPath, allocatorHandle,
                out IntPtr adapterHandle));
            return new OrtLoraAdapter(adapterHandle);
        }

        internal OrtLoraAdapter(IntPtr adapter)
            : base(adapter, true)
        {
        }

        internal IntPtr Handle
        {
            get
            {
                return handle;
            }
        }

        #region SafeHandle

        /// <summary>
        /// Overrides SafeHandle.IsInvalid
        /// </summary>
        /// <value>returns true if handle is equal to Zero</value>
        public override bool IsInvalid { get { return handle == IntPtr.Zero; } }

        /// <summary>
        /// Overrides SafeHandle.ReleaseHandle() to properly dispose of
        /// the native instance of OrtLoraAdapter
        /// </summary>
        /// <returns>always returns true</returns>
        protected override bool ReleaseHandle()
        {
            NativeMethods.OrtReleaseAllocator(handle);
            handle = IntPtr.Zero;
            return true;
        }

        #endregion
    }
}
