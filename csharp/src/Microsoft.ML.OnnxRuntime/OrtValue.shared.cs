﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

using Microsoft.ML.OnnxRuntime.Tensors;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace Microsoft.ML.OnnxRuntime
{
    /// <summary>
    /// A type of data that OrtValue encapsulates.
    /// </summary>
    public enum OnnxValueType
    {
        ONNX_TYPE_UNKNOWN = 0, // Not set
        ONNX_TYPE_TENSOR = 1, // It's a Tensor
        ONNX_TYPE_SEQUENCE = 2, // It's an Onnx sequence which may be a sequence of Tensors/Maps/Sequences
        ONNX_TYPE_MAP = 3,  // It's a map
        ONNX_TYPE_OPAQUE = 4, // It's an experimental Opaque object
        ONNX_TYPE_SPARSETENSOR = 5, // It's a Sparse Tensor
        ONNX_TYPE_OPTIONAL = 6, // It's an optional type that designates anything above (except UNKOWN)
    }

    /// <summary>
    /// Represents a disposable OrtValue.
    /// This class exposes a native instance of OrtValue.
    /// The class implements IDisposable and must
    /// be disposed.
    /// </summary>
    public class OrtValue : IDisposable
    {
        private IntPtr _handle;
        private MemoryHandle? _memHandle;
        private bool _disposed;
        /// <summary>
        /// __Ctor
        /// </summary>
        /// <param name="handle">Pointer to a native instance of OrtValue</param>
        internal OrtValue(IntPtr handle)
        {
            _handle = handle;
        }

        /// <summary>
        /// __Ctor to construct OrtValue over managed memory.
        /// We pin the memory and unpin it at the disposal time.
        /// </summary>
        /// <param name="handle"></param>
        /// <param name="memHandle"></param>
        private OrtValue(IntPtr handle, MemoryHandle memHandle)
        {
            _handle = handle;
            _memHandle = memHandle;
        }

        /// <summary>
        /// Native handle to OrtValue for internal use.
        /// </summary>
        internal IntPtr Handle { get { return _handle; } }

        /// <summary>
        /// Fetches OrtValue type if it has one.
        /// </summary>
        /// <value>OnnxValueType</value>
        public OnnxValueType OnnxType
        {
            get
            {
                NativeApiStatus.VerifySuccess(NativeMethods.OrtGetValueType(Handle, out IntPtr onnxType));
                return (OnnxValueType)onnxType;
            }
        }

        /// <summary>
        /// Returns true if OrtValue contains a tensor
        /// </summary>
        /// <returns>true if tensor</returns>
        public bool IsTensor
        {
            get
            {
                return OnnxType == OnnxValueType.ONNX_TYPE_TENSOR;
            }
        }

        /// <summary>
        /// Returns true if OrtValue contains a sparse tensor
        /// </summary>
        /// <returns>true if sparse tensor</returns>
        public bool IsSparseTensor
        {
            get
            {
                return OnnxType == OnnxValueType.ONNX_TYPE_SPARSETENSOR;
            }
        }

        /// <summary>
        /// Valid for composite ML types like map, sequence.
        /// Returns 2 for map (keys, values) and N for sequence, where N is the number of elements
        /// int he sequence.
        /// </summary>
        /// <returns>Element count</returns>
        public int GetValueCount()
        {
            NativeApiStatus.VerifySuccess(NativeMethods.OrtGetValueCount(Handle, out IntPtr count));
            return (int)count;
        }

        /// <summary>
        /// For non tensors return OrtValue element at the specified index.
        /// For maps only indices 0 and 1 are valid. For sequences, [0..N) are valid.
        /// See GetValueCount() to determine the valid range.
        /// </summary>
        /// <param name="index"></param>
        /// <param name="allocator">allocator to use</param>
        /// <returns>OrtValue disposable instance that points to the corresponding element of the composite type</returns>
        public OrtValue GetValue(int index, OrtAllocator allocator)
        {
            NativeApiStatus.VerifySuccess(NativeMethods.OrtGetValue(Handle, index,
                allocator.Pointer, out IntPtr ortValueHandle));
            return new OrtValue(ortValueHandle);
        }

        /// <summary>
        /// Returns a ReadOnlySpan<typeparamref name="T"/> over tensor native buffer that
        /// provides a read-only view.
        /// 
        /// Note, that the memory may be device allocated and, therefore, not accessible from the CPU.
        /// To get memory descriptor use GetTensorMemoryInfo().
        /// 
        /// OrtValue must contain a non-string tensor.
        /// The span is valid as long as the OrtValue instance is alive (not disposed).
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <returns>ReadOnlySpan<typeparamref name="T"/></returns>
        /// <exception cref="OnnxRuntimeException"></exception>
        public ReadOnlySpan<T> GetTensorDataAsSpan<T>() where T : struct
        {
            var byteSpan = GetTensorBufferRawData(typeof(T));
            return MemoryMarshal.Cast<byte, T>(byteSpan);
        }

        /// <summary>
        /// Returns a Span<typeparamref name="T"/> over tensor native buffer.
        /// This enables you to safely and efficiently modify the underlying
        /// native buffer in a type-safe manner. This is useful for example in IOBinding scenarios
        /// where you want to modify results of the inference and feed it back as input.
        /// 
        /// Note, that the memory may be device allocated.
        /// To get memory descriptor use GetTensorMemoryInfo().
        /// 
        /// OrtValue must contain a non-string tensor.
        /// The span is valid as long as the OrtValue instance is alive (not disposed).
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <returns></returns>
        public Span<T> GetTensorMutableDataAsSpan<T>() where T : struct
        {
            var byteSpan = GetTensorBufferRawData(typeof(T));
            return MemoryMarshal.Cast<byte, T>(byteSpan);
        }

        /// <summary>
        /// 
        /// </summary>
        /// <returns></returns>
        public Span<byte> GetTensorMutableRawData()
        {
            return GetTensorBufferRawData(typeof(byte));
        }


        /// <summary>
        /// Fetch string tensor element buffer pointer at the specified index,
        /// convert/copy to UTF-16 char[] and return a ReadOnlyMemory<char> instance.
        /// 
        /// Obtain TensorTypeAndShape to get shape and element count.
        /// </summary>
        /// <param name="index">flat string tensor element index</param>
        /// <returns>ReadOnlyMemory<char> backed by a managed char[]. Its lifespan is not
        /// tied to the native buffer of OrtValue.</returns>
        public ReadOnlyMemory<char> GetStringElementAsMemory(int index)
        {
            var chars = GetStringTensorElementChars(index);
            if (chars.Length == 0)
            {
                return ReadOnlyMemory<char>.Empty;
            }
            return new ReadOnlyMemory<char>(chars);
        }

        /// <summary>
        /// Fetch string tensor element buffer pointer at the specified index,
        /// copy/convert UTF-8 into a UTF-16 string and return it.
        /// 
        /// Obtain TensorTypeAndShape to get shape and element count.
        /// </summary>
        /// <param name="index">flat string tensor element index</param>
        /// <returns>UTF-16 string instance</returns>
        public string GetStringElement(int index)
        {
            var chars = GetStringTensorElementChars(index);
            if (chars.Length == 0)
            {
                return string.Empty;
            }
            return new string(chars);
        }


        /// <summary>
        /// Get a span over the native memory of the string tensor element.
        /// The span is valid as long as the OrtValue is valid.
        /// 
        /// This is useful if you want to perform your own UTF-8 decoding or
        /// you do not care about decoding.
        /// Obtain TensorTypeAndShape to get shape and element count.
        /// </summary>
        /// <param name="index">flat element index</param>
        /// <returns>ReadOnlySpan over UTF-8 bytes of the string tensor element</returns>
        public ReadOnlySpan<byte> GetStringElementAsSpan(int index)
        {
            GetStringTensorElementBuffer((UIntPtr)index, out UIntPtr len, out IntPtr buffer);
            if ((uint)len == 0)
            {
                return ReadOnlySpan<byte>.Empty;
            }
            unsafe
            {
                return new ReadOnlySpan<byte>((buffer).ToPointer(), (int)len);
            }
        }


        /// <summary>
        /// Convenience method to obtain all string tensor elements as a string array.
        /// </summary>
        /// <returns>string[]</returns>
        /// <exception cref="OnnxRuntimeException"></exception>
        public string[] GetStringTensorAsArray()
        {
            GetTensorElementTypeAndCount(out long count, out TensorElementType elementType);
            if (elementType != TensorElementType.String)
            {
                throw new OnnxRuntimeException(
                            ErrorCode.Fail,
                            $"GetStringTensorAsArray() is only supported for string tensors. This OrtValue contains a {elementType} tensor.");
            }

            var strings = new string[count];
            for (int i = 0; i < count; i++)
            {
                strings[i] = GetStringElement(i);
            }
            return strings;
        }

        /// <summary>
        /// Creates and fetches Type information about the contained OnnxValue.
        /// </summary>
        /// <returns>a disposable instance of OrtTypeInfo</returns>
        public OrtTypeInfo GetTypeInfo()
        {
            NativeApiStatus.VerifySuccess(NativeMethods.OrtGetTypeInfo(Handle, out IntPtr typeInfo));
            return new OrtTypeInfo(typeInfo);
        }

        /// <summary>
        /// Obtains Tensor And Type Information from the OrtValue iff it contains a tensor.
        /// Valid only for OrtValues that contain a tensor.
        /// </summary>
        /// <returns>A disposable instance of OrtTensorTypeAndShapeInfo</returns>
        public OrtTensorTypeAndShapeInfo GetTensorTypeAndShape()
        {
            var onnxType = OnnxType;
            if (onnxType != OnnxValueType.ONNX_TYPE_TENSOR &&
                onnxType != OnnxValueType.ONNX_TYPE_SPARSETENSOR)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument,
                    $"This OrtValue type contains: {onnxType}, not a tensor or sparse tensor");
            }

            NativeMethods.OrtGetTensorTypeAndShape(Handle, out IntPtr typeAndShapeInfo);
            return new OrtTensorTypeAndShapeInfo(typeAndShapeInfo, true);
        }

        /// <summary>
        /// Returns OrtMemoryInfo iff this OrtValue contains a tensor or a sparse tensor.
        /// </summary>
        /// <returns>OrtMemoryInfo that describes the underlying memory allocation</returns>
        /// <exception cref="OnnxRuntimeException"></exception>
        public OrtMemoryInfo GetTensorMemoryInfo()
        {
            var onnxType = OnnxType;
            if (onnxType != OnnxValueType.ONNX_TYPE_TENSOR &&
                               onnxType != OnnxValueType.ONNX_TYPE_SPARSETENSOR)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument,
                                       $"This OrtValue type contains: {onnxType}, not a tensor or sparse tensor");
            }
            NativeMethods.OrtGetTensorMemoryInfo(Handle, out IntPtr memoryInfo);
            return new OrtMemoryInfo(memoryInfo, false);
        }

        private void GetTensorElementTypeAndCount(out long count, out TensorElementType elementType)
        {
            NativeApiStatus.VerifySuccess(NativeMethods.OrtGetTensorTypeAndShape(Handle, out IntPtr typeAndShapeInfo));
            try
            {
                NativeApiStatus.VerifySuccess(NativeMethods.OrtGetTensorElementType(typeAndShapeInfo, out IntPtr elType));
                NativeApiStatus.VerifySuccess(NativeMethods.OrtGetTensorShapeElementCount(typeAndShapeInfo, out UIntPtr cnt));
                elementType = (TensorElementType)elType;
                count = (long)cnt;
            }
            finally
            {
                NativeMethods.OrtReleaseTensorTypeAndShapeInfo(typeAndShapeInfo);
            }
        }

        private char[] GetStringTensorElementChars(int index)
        {
            GetStringTensorElementBuffer((UIntPtr)index, out UIntPtr len, out IntPtr buffer);
            if ((uint)len == 0)
            {
                return Array.Empty<char>();
            }

            unsafe
            {
                int charCount = Encoding.UTF8.GetCharCount((byte*)(buffer).ToPointer(), (int)len);
                var chars = new char[charCount];
                fixed (char* ch = chars)
                {
                    Encoding.UTF8.GetChars((byte*)(buffer).ToPointer(), (int)len, (char*)ch, charCount);
                }
                return chars;
            }
        }

        private void GetStringTensorElementBuffer(UIntPtr index, out UIntPtr bufferLen, out IntPtr buffer)
        {
            // Length is in UTF-8 bytes. Strings are not zero terminated, so length is required.
            NativeApiStatus.VerifySuccess(NativeMethods.OrtGetStringTensorElementLength(Handle, index, out bufferLen));

            if ((uint)bufferLen == 0)
            {
                buffer = IntPtr.Zero;
                return;
            }

            // XXX: We lack the API (at the moment) that simply gives access to string element buffer. So we get the resized one
            // to the same length which leaves it unchanged.
            NativeApiStatus.VerifySuccess(NativeMethods.OrtGetResizedStringTensorElementBuffer(Handle,
                   (UIntPtr)index, bufferLen, out buffer));
        }

        private Span<byte> GetTensorBufferRawData(Type requestedType)
        {
            var onnxType = OnnxType;
            if (onnxType != OnnxValueType.ONNX_TYPE_TENSOR)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument,
                    $"This OrtValue type contains: {onnxType}, not a tensor");
            }

            GetTensorElementTypeAndCount(out long count, out TensorElementType elementType);

            if (elementType == TensorElementType.String)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument, "Strings are not supported by this API");
            }

            var typeInfo = TensorBase.GetElementTypeInfo(elementType) ??
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument, $"Element type: {elementType} is not registered type.");

            // We are always Ok with byte
            if (requestedType != typeof(byte) && requestedType != typeInfo.TensorType)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument,
                    $"Requested type: {requestedType} does not match the actual type: {typeInfo.TensorType}");
            }

            if (count == 0)
            {
                return Span<byte>.Empty;
            }

            NativeApiStatus.VerifySuccess(NativeMethods.OrtGetTensorMutableData(Handle, out IntPtr tensorData));

            var bufferLenInBytes = count * typeInfo.TypeSize;

            unsafe
            {
                return new Span<byte>(tensorData.ToPointer(), (int)bufferLenInBytes);
            }
        }

        /// <summary>
        /// Factory method to construct an OrtValue of Tensor type on top of pre-allocated memory.
        /// This can be a piece of arbitrary memory that may be allocated by OrtAllocator (possibly on a device),
        /// a chunk of managed memory (must be pinned for the duration of OrtValue lifetime) or a memory that is allocated
        /// natively allocated using Marshal.AllocHGlobal(), stackalloc or other means (may be on a device).
        /// 
        /// The resulting OrtValue does not own the underlying memory buffer and will not attempt to
        /// deallocate it.
        /// </summary>
        /// <param name="memInfo">Memory Info. For managed memory its default is cpu.
        ///                       For other kinds of memory, one must construct as appropriate.</param>
        /// <param name="elementType">DataType for the Tensor</param>
        /// <param name="shape">shape of the tensor to create. The size required by the shape
        /// must be less of equal of the memory.Length</param>
        /// <param name="dataBufferPtr">Pointer to a raw memory buffer which may reside on a device</param>
        /// <param name="bufferLengthInBytes">Buffer length in bytes</param>
        /// <returns>A disposable instance of OrtValue</returns>
        public static OrtValue CreateTensorValueWithData(OrtMemoryInfo memInfo, TensorElementType elementType,
                                                         long[] shape,
                                                         IntPtr dataBufferPtr,
                                                         long bufferLengthInBytes)
        {
            var typeInfo = TensorBase.GetElementTypeInfo(elementType) ?? throw new OnnxRuntimeException(ErrorCode.InvalidArgument,
                                       $"Tensor element type: {elementType} is not supported");
            if (typeInfo.IsString)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument,
                    "Cannot map managed strings buffer to native OrtValue. Use string specific interfaces");
            }

            var shapeSize = ArrayUtilities.GetSizeForShape(shape);
            var requiredBufferSizeInBytes = shapeSize * typeInfo.TypeSize;

            // We allow creating a tensor over part of the buffer
            if (requiredBufferSizeInBytes > bufferLengthInBytes)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument,
                    $"Shape: {shape} has: {shapeSize} elements requires a buffer of at least {requiredBufferSizeInBytes} bytes. Provided: {bufferLengthInBytes} bytes");
            }

            NativeApiStatus.VerifySuccess(NativeMethods.OrtCreateTensorWithDataAsOrtValue(
                                    memInfo.Pointer,
                                    dataBufferPtr,
                                    (UIntPtr)bufferLengthInBytes,
                                    shape,
                                    (UIntPtr)shape.Length,
                                    elementType,
                                    out IntPtr ortValueHandle
                                ));
            return new OrtValue(ortValueHandle);
        }

        /// <summary>
        /// This is a factory method that creates an OrtValue of Tensor type on top of Memory<typeparamref name="T"/> memory.
        /// The API pins the memory for the duration of the OrtValue lifetime.
        /// It is unpinned at disposal time.
        /// </summary>
        /// <typeparam name="T">T must be one of the supported types</typeparam>
        /// <param name="memoryInfo">Memory information that describes memory location</param>
        /// <param name="memory">contiguous region of memory</param>
        /// <param name="shape">shape of the tensor to create. The size required by the shape
        /// must be less of equal of the memory.Length</param>
        /// <returns>A disposable OrtValue instance</returns>
        /// <exception cref="OnnxRuntimeException"></exception>
        public static OrtValue CreateTensorValueFromMemory<T>(OrtMemoryInfo memoryInfo, Memory<T> memory, long[] shape)
        {
            var typeInfo = TensorBase.GetTypeInfo(typeof(T)) ?? 
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument, $"Tensor of type: {typeof(T)} is not supported");

            if (typeInfo.IsString)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument,
                    "Cannot map managed strings buffer to native OrtValue. Use string specific interfaces.");
            }

            var shapeSize = ArrayUtilities.GetSizeForShape(shape);
            // We allow creating a tensor over part of the buffer
            if (shapeSize > memory.Length)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument,
                    $"Managed memory size: {memory.Length} elements is less than shape size: {shapeSize} elements");
            }

            var bufferLengthInBytes = memory.Length * typeInfo.TypeSize;
            var memoryHandle = memory.Pin();
            try
            {
                IntPtr bufferPtr;
                unsafe
                {
                    bufferPtr = new IntPtr(memoryHandle.Pointer);
                }

                NativeApiStatus.VerifySuccess(NativeMethods.OrtCreateTensorWithDataAsOrtValue(
                                        memoryInfo.Pointer,
                                        bufferPtr,
                                        (UIntPtr)bufferLengthInBytes,
                                        shape,
                                        (UIntPtr)shape.Length,
                                        typeInfo.ElementType,
                                        out IntPtr ortValueHandle
                                    ));
                return new OrtValue(ortValueHandle, memoryHandle);
            }
            catch (Exception)
            {
                memoryHandle.Dispose();
                throw;
            }

        }

        /// <summary>
        /// This is a factory method that creates an OrtValue of Tensor type on top managed data array.
        /// The API pins the memory for the duration of the OrtValue lifetime.
        /// It is unpinned at disposal time.
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <param name="data">managed data buffer</param>
        /// <param name="shape">shape that describes the buffer</param>
        /// <returns>A disposable OrtValue instance</returns>
        public static OrtValue CreateTensorValueFromMemory<T>(T[] data, long[] shape)
        {
            return OrtValue.CreateTensorValueFromMemory(OrtMemoryInfo.DefaultInstance, new Memory<T>(data), shape);
        }

        /// <summary>
        /// The factory API creates an OrtValue with memory allocated using the given allocator
        /// according to the specified shape and element type. The memory will be released when OrtValue
        /// is disposed. Use GetTensorMutableDataAsSpan&lt;T&gt;() API to fill in the data.
        /// </summary>
        /// <param name="allocator"></param>
        /// <param name="elementType"></param>
        /// <param name="shape"></param>
        /// <returns>A disposable OrtValue</returns>
        public static OrtValue CreateAllocatedTensorValue(OrtAllocator allocator, TensorElementType elementType,
                                                         long[] shape)
        {
            NativeApiStatus.VerifySuccess(NativeMethods.OrtCreateTensorAsOrtValue(allocator.Pointer, shape,
                (UIntPtr)shape.Length, elementType, out IntPtr ortValueHandle));
            return new OrtValue(ortValueHandle);
        }

        /// <summary>
        /// This is a factory method creates a native Onnxruntime OrtValue containing a tensor.
        /// The method will attempt to pin managed memory so no copying occurs when data is passed down
        /// to native code.
        /// </summary>
        /// <param name="value">Tensor object</param>
        /// <param name="memoryHandle">For all tensor types but string tensors we endeavor to use managed memory
        ///  to avoid additional allocation and copy. This out parameter represents a chunk of pinned memory which will need
        ///  to be disposed when no longer needed. The lifespan of memoryHandle should eclipse the lifespan of the corresponding
        ///  OrtValue.
        /// </param>
        /// <param name="elementType">discovered tensor element type</param>
        /// <returns>And instance of OrtValue constructed on top of the object</returns>
        internal static OrtValue CreateFromTensorObject(TensorBase value, out TensorElementType elementType)
        {
            var typeInfo = value.GetTypeInfo();
            MemoryHandle? memHandle;
            OrtValue ortValue = null;
            int dataBufferLength;
            long[] shape;
            int rank;

            TensorElementType elType = typeInfo.ElementType;
            var typeSize = typeInfo.TypeSize;
            if (typeInfo.IsString)
            {
                ortValue = CreateStringTensor(value as Tensor<string>);
            }
            else
            {
                switch (elType)
                {
                    case TensorElementType.Float:
                        PinAsTensor(value as Tensor<float>, typeSize, out memHandle, out dataBufferLength,
                            out shape, out rank);
                        break;
                    case TensorElementType.Double:
                        PinAsTensor(value as Tensor<double>, typeSize, out memHandle, out dataBufferLength,
                                            out shape, out rank);
                        break;
                    case TensorElementType.Int32:
                        PinAsTensor(value as Tensor<int>, typeSize, out memHandle, out dataBufferLength,
                            out shape, out rank);
                        break;
                    case TensorElementType.UInt32:
                        PinAsTensor(value as Tensor<uint>, typeSize, out memHandle, out dataBufferLength,
                            out shape, out rank);
                        break;
                    case TensorElementType.Int64:
                        PinAsTensor(value as Tensor<long>, typeSize, out memHandle, out dataBufferLength,
                            out shape, out rank);
                        break;
                    case TensorElementType.UInt64:
                        PinAsTensor(value as Tensor<ulong>, typeSize, out memHandle, out dataBufferLength,
                                    out shape, out rank);
                        break;
                    case TensorElementType.Int16:
                        PinAsTensor(value as Tensor<short>, typeSize, out memHandle, out dataBufferLength,
                            out shape, out rank);
                        break;

                    case TensorElementType.UInt16:
                        PinAsTensor(value as Tensor<ushort>, typeSize,
                                    out memHandle, out dataBufferLength,
                                    out shape, out rank);

                        break;
                    case TensorElementType.UInt8:
                        PinAsTensor(value as Tensor<byte>, typeSize,
                                    out memHandle, out dataBufferLength,
                                    out shape, out rank);
                        break;
                    case TensorElementType.Int8:
                        PinAsTensor(value as Tensor<sbyte>, typeSize,
                            out memHandle, out dataBufferLength,
                            out shape, out rank);
                        break;
                    case TensorElementType.Bool:
                        PinAsTensor(value as Tensor<bool>, typeSize,
                                    out memHandle, out dataBufferLength,
                                    out shape, out rank);
                        break;
                    case TensorElementType.Float16:
                        PinAsTensor(value as Tensor<Float16>, typeSize,
                                    out memHandle, out dataBufferLength,
                                    out shape, out rank);
                        break;
                    case TensorElementType.BFloat16:
                        PinAsTensor(value as Tensor<BFloat16>, typeSize,
                                    out memHandle, out dataBufferLength,
                                    out shape, out rank);
                        break;
                    default:
                        throw new NotSupportedException("Element type: " + elType + " is not of a supported type");
                }

                try
                {
                    Debug.Assert(memHandle.HasValue);
                    IntPtr dataBufferPointer = IntPtr.Zero;
                    unsafe
                    {
                        dataBufferPointer = (IntPtr)(memHandle.Value).Pointer;
                    }

                    NativeApiStatus.VerifySuccess(NativeMethods.OrtCreateTensorWithDataAsOrtValue(
                        OrtMemoryInfo.DefaultInstance.Pointer,
                        dataBufferPointer,
                        (UIntPtr)(dataBufferLength),
                        shape,
                        (UIntPtr)rank,
                        elType,
                        out IntPtr nativeValue));

                    ortValue = new OrtValue(nativeValue, memHandle.Value);
                }
                catch (Exception)
                {
                    memHandle?.Dispose();
                    throw;
                }
            }

            elementType = elType;
            return ortValue;
        }

        /// <summary>
        /// Creates an OrtValue that contains a string tensor of specified shape, and
        /// containing empty strings. String tensors are always on CPU.
        /// Use FillStringTensorElement to assign individual elements values.
        /// </summary>
        /// <param name="allocator"></param>
        /// <returns>disposable OrtValue</returns>
        /// <param name="shape">tensor shape</param>
        public static OrtValue CreateTensorWithEmptyStrings(OrtAllocator allocator, long[] shape)
        {
            // allocate the native tensor
            NativeApiStatus.VerifySuccess(NativeMethods.OrtCreateTensorAsOrtValue(
                                allocator.Pointer,
                                shape,
                                (UIntPtr)(shape.Length),
                                TensorElementType.String,
                                out IntPtr valueHandle
                                ));
            return new OrtValue(valueHandle);
        }

        /// <summary>
        /// Converts the string argument represented by ReadOnlySpan to UTF-8,
        /// allocates space in the native tensor and copies it into the native tensor memory.
        /// Typically, this is used to populate a new empty string tensor element.
        /// 
        /// The number of elements is according to the shape supplied to CreateTensorWithEmptyStrings().
        /// However, this API can also be used to overwrite any existing element within the string tensor.
        /// 
        /// In general, to obtain the number of elements for any tensor, use GetTensorTypeAndShape() which
        /// would return a disposable instance of TensorTypeAndShapeInfo. 
        /// Then call GetElementCount() or GetShape().
        /// </summary>
        /// <param name="str">ReadOnlySpan over chars</param>
        /// <param name="index">index of the string element within the tensor
        /// must be within bounds of [0, N)</param>
        public void FillStringTensorElement(ReadOnlySpan<char> str, int index)
        {
            unsafe
            {
                fixed (char* strPtr = str)
                {
                    FillStringTensorElement(strPtr, str.Length, index);
                }
            }
        }

        /// <summary>
        /// Converts the string argument represented by ReadOnlyMemory to UTF-8,
        /// allocates space in the native tensor and copies it into the native tensor memory.
        /// Typically, this is used to populate a new empty string tensor element.
        /// 
        /// The number of elements is according to the shape supplied to CreateTensorWithEmptyStrings().
        /// However, this API can also be used to overwrite any existing element within the string tensor.
        /// 
        /// In general, to obtain the number of elements for any tensor, use GetTensorTypeAndShape() which
        /// would return a disposable instance of TensorTypeAndShapeInfo. 
        /// Then call GetElementCount() or GetShape().
        ///
        /// </summary>
        /// <param name="rom">ReadOnlyMemory instance over an array of chars</param>
        /// <param name="index">index of the string element within the tensor
        /// must be within bounds of [0, N)</param>
        public void FillStringTensorElement(ReadOnlyMemory<char> rom, int index)
        {
            FillStringTensorElement(rom.Span, index);
        }

        /// <summary>
        /// Creates an OrtValue that contains a string tensor.
        /// String tensors are always allocated on CPU.
        /// String data will be converted to UTF-8 and copied to native memory.
        /// 
        /// Note, this is different from creating an OrtValue from other primitive data types
        /// where memory is pinned (if necessary) and the OrtValue points to that chunk of memory.
        /// </summary>
        /// <param name="tensor">Tensor<string></param>
        /// <returns>A disposable OrtValue instance</returns>
        /// <exception cref="OnnxRuntimeException"></exception>
        public static OrtValue CreateStringTensor(Tensor<string> tensor)
        {
            if (tensor == null)
            {
                throw new OnnxRuntimeException(ErrorCode.InvalidArgument, "Expecting a valid string tensor");
            }

            long[] shape = Array.ConvertAll<int, long>(tensor.Dimensions.ToArray(), (x) => (long)x);

            var ortValue = CreateTensorWithEmptyStrings(OrtAllocator.DefaultInstance, shape);
            try
            {
                var len = tensor.Length;
                for (int i = 0; i < len; ++i)
                {
                    var str = tensor.GetValue(i) ?? throw new ArgumentNullException($"Tensor<string> contains null reference at index:{i}");
                    unsafe
                    {
                        fixed (char* strPtr = str)
                        {
                            ortValue.FillStringTensorElement(strPtr, str.Length, i);
                        }
                    }
                }
            }
            catch (Exception)
            {
                ortValue.Dispose();
                throw;
            }
            return ortValue;
        }

        /// <summary>
        /// Creates a sequence of OrtValues from a collection of OrtValues.
        /// All OrtValues in the collection must be of the same Onnx type.
        /// I.e. (Tensor, SparseTensor, Map, Sequence, etc.)
        /// 
        /// All OrtValues are internally ref-counted and stored within the sequence OrtValue
        /// so the input OrtValues can be disposed of after this call.
        /// </summary>
        /// <param name="ortValues">a collection of OrtValues</param>
        /// <returns>A disposable instance of OrtValues</returns>
        /// <exception cref="ArgumentNullException"></exception>
        public static OrtValue CreateSequence(IReadOnlyCollection<OrtValue> ortValues)
        {
            if (ortValues is null)
            {
                throw new ArgumentNullException(nameof(ortValues));
            }

            var handles = new IntPtr[ortValues.Count];
            for (int i = 0; i < ortValues.Count; i++)
            {
                handles[i] = ortValues.ElementAt(i).Handle;
            }

            NativeApiStatus.VerifySuccess(NativeMethods.OrtCreateValue(handles,
                (UIntPtr)ortValues.Count, (IntPtr)OnnxValueType.ONNX_TYPE_SEQUENCE,
                out IntPtr sequenceHandle));
            return new OrtValue(sequenceHandle);
        }


        /// <summary>
        /// Creates a map OrtValue with keys and values.
        /// ORT supports only a subset of types for keys and values.
        /// We are not restricting them here.
        /// 
        /// All OrtValues are internally ref-counted and stored within the map OrtValue
        /// so the input OrtValues can be disposed of after this call.
        /// </summary>
        /// <param name="keys">Contains keys</param>
        /// <param name="values">Contains values</param>
        /// <returns>A disposable OrtValue</returns>
        /// <exception cref="ArgumentNullException"></exception>
        public static OrtValue CreateMap(OrtValue keys, OrtValue values)
        {
            if (keys is null || values is null)
            {
                throw new ArgumentNullException($"keys or/and values are null");
            }

            IntPtr[] handles = { keys.Handle, values.Handle };
            NativeApiStatus.VerifySuccess(
                NativeMethods.OrtCreateValue(handles, (UIntPtr)2, (IntPtr)OnnxValueType.ONNX_TYPE_MAP,
                               out IntPtr mapHandle));
            return new OrtValue(mapHandle);
        }

        private unsafe void FillStringTensorElement(char* strPtr, int strLength, int index)
        {
            IntPtr buffer;
            if (strLength == 0)
            {
                NativeApiStatus.VerifySuccess(NativeMethods.OrtGetResizedStringTensorElementBuffer(Handle,
                                                  (UIntPtr)index, UIntPtr.Zero, out buffer));
                return;
            }

            var bytesCount = Encoding.UTF8.GetByteCount(strPtr, strLength);
            NativeApiStatus.VerifySuccess(NativeMethods.OrtGetResizedStringTensorElementBuffer(Handle,
                                              (UIntPtr)index, (UIntPtr)bytesCount, out buffer));
            NativeOnnxValueHelper.StringToUtf8NativeMemory(strPtr, strLength, buffer, bytesCount);
        }

        private static void PinAsTensor<T>(
                                        Tensor<T> tensor,
                                        int elementSize,
                                        out MemoryHandle? pinnedHandle,
                                        out int dataBufferLength,
                                        out long[] shape,
                                        out int rank)
        {
            if (tensor == null)
            {
                throw new OnnxRuntimeException(ErrorCode.Fail, "Cast to Tensor<T> failed. BUG check!");
            }

            if (tensor.IsReversedStride)
            {
                //TODO: not sure how to support reverse stride. may be able to calculate the shape differently
                throw new NotSupportedException(nameof(Tensor<T>) + " of reverseStride is not supported");
            }

            DenseTensor<T> dt = tensor as DenseTensor<T> ?? tensor.ToDenseTensor();
            shape = Array.ConvertAll<int, long>(dt.Dimensions.ToArray(), (x) => (long)x);
            rank = dt.Rank;

            dataBufferLength = dt.Buffer.Length * elementSize;
            pinnedHandle = dt.Buffer.Pin();
        }

        #region IDisposable Support

        ~OrtValue()
        {
            Dispose(false);
        }

        /// <summary>
        /// IDisposable implementation
        /// </summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        /// <summary>
        /// IDisposable implementation
        /// </summary>
        /// <param name="disposing">true if invoked from Dispose() method</param>
        protected virtual void Dispose(bool disposing)
        {
            if (_disposed)
            {
                return;
            }

            if (disposing)
            {
                _memHandle?.Dispose();
                _memHandle = null;
            }

            Debug.Assert(_handle != IntPtr.Zero);
            if (_handle != IntPtr.Zero)
            {
                NativeMethods.OrtReleaseValue(_handle);
                _handle = IntPtr.Zero;
            }
            _disposed = true;
        }

        #endregion
    }
}
