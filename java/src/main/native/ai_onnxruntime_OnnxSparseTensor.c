/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the MIT License.
 */
#include <jni.h>
#include <math.h>
#include "onnxruntime/core/session/onnxruntime_c_api.h"
#include "OrtJniUtil.h"
#include "ai_onnxruntime_OnnxSparseTensor.h"

/*
 * Class:     ai_onnxruntime_OnnxSparseTensor
 * Method:    getIndexBuffer
 * Signature: (JJ)Ljava/nio/ByteBuffer;
 */
JNIEXPORT jobject JNICALL Java_ai_onnxruntime_OnnxSparseTensor_getIndexBuffer
  (JNIEnv * jniEnv, jobject jobj, jlong apiHandle, jlong handle) {
    (void) jobj; // Required JNI parameter not needed by functions which don't need to access their host object.
    const OrtApi* api = (const OrtApi*) apiHandle;
    OrtSparseFormat format;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorFormat((OrtValue*) handle, &format));
    enum OrtSparseIndicesFormat indicesFormat;
    switch (format) {
        case ORT_SPARSE_COO:
            indicesFormat = ORT_SPARSE_COO_INDICES;
            break;
        case ORT_SPARSE_CSRC:
            indicesFormat = ORT_SPARSE_CSR_OUTER_INDICES;
            break;
        case ORT_SPARSE_BLOCK_SPARSE:
            indicesFormat = ORT_SPARSE_BLOCK_SPARSE_INDICES;
            break;
        case ORT_SPARSE_UNDEFINED: {
            throwOrtException(jniEnv,convertErrorCode(ORT_NOT_IMPLEMENTED),"Sparse format is ORT_SPARSE_UNDEFINED, cannot get indices");
            return NULL;
        }
    }

    OrtTensorTypeAndShapeInfo* info;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorIndicesTypeShape((OrtValue*) handle, indicesFormat, &info));
    size_t arrSize;
    checkOrtStatus(jniEnv,api,api->GetTensorShapeElementCount(info,&arrSize));
    ONNXTensorElementDataType onnxTypeEnum;
    checkOrtStatus(jniEnv,api,api->GetTensorElementType(info,&onnxTypeEnum));
    api->ReleaseTensorTypeAndShapeInfo(info);

    size_t typeSize = onnxTypeSize(onnxTypeEnum);
    size_t sizeBytes = arrSize*typeSize;

    uint8_t* arr;
    size_t indices_size;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorIndices((OrtValue*)handle,indicesFormat,&indices_size,(const void**)&arr));

    if (indices_size != arrSize) {
        throwOrtException(jniEnv,convertErrorCode(ORT_RUNTIME_EXCEPTION),"Unexpected size");
    }

    return (*jniEnv)->NewDirectByteBuffer(jniEnv, arr, sizeBytes);
}

/*
 * Class:     ai_onnxruntime_OnnxSparseTensor
 * Method:    getInnerIndexBuffer
 * Signature: (JJ)Ljava/nio/ByteBuffer;
 */
JNIEXPORT jobject JNICALL Java_ai_onnxruntime_OnnxSparseTensor_getInnerIndexBuffer
  (JNIEnv * jniEnv, jobject jobj, jlong apiHandle, jlong handle) {
    (void) jobj; // Required JNI parameter not needed by functions which don't need to access their host object.
    const OrtApi* api = (const OrtApi*) apiHandle;
    OrtSparseFormat format;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorFormat((OrtValue*) handle, &format));
    enum OrtSparseIndicesFormat indicesFormat;
    switch (format) {
        case ORT_SPARSE_CSRC:
            indicesFormat = ORT_SPARSE_CSR_INNER_INDICES;
            break;
        case ORT_SPARSE_COO:
        case ORT_SPARSE_BLOCK_SPARSE:
        case ORT_SPARSE_UNDEFINED: {
            throwOrtException(jniEnv,convertErrorCode(ORT_NOT_IMPLEMENTED),"Sparse format is ORT_SPARSE_COO, ORT_SPARSE_BLOCK_SPARSE, or ORT_SPARSE_UNDEFINED, inner indices are not defined.");
            return NULL;
        }
    }

    OrtTensorTypeAndShapeInfo* info;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorIndicesTypeShape((OrtValue*) handle, indicesFormat, &info));
    size_t arrSize;
    checkOrtStatus(jniEnv,api,api->GetTensorShapeElementCount(info,&arrSize));
    ONNXTensorElementDataType onnxTypeEnum;
    checkOrtStatus(jniEnv,api,api->GetTensorElementType(info,&onnxTypeEnum));
    api->ReleaseTensorTypeAndShapeInfo(info);

    size_t typeSize = onnxTypeSize(onnxTypeEnum);
    size_t sizeBytes = arrSize*typeSize;

    uint8_t* arr;
    size_t indices_size;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorIndices((OrtValue*)handle,indicesFormat,&indices_size,(const void**)&arr));

    if (indices_size != arrSize) {
        throwOrtException(jniEnv,convertErrorCode(ORT_RUNTIME_EXCEPTION),"Unexpected size");
    }

    return (*jniEnv)->NewDirectByteBuffer(jniEnv, arr, sizeBytes);
}

/*
 * Class:     ai_onnxruntime_OnnxSparseTensor
 * Method:    getDataBuffer
 * Signature: (JJ)Ljava/nio/ByteBuffer;
 */
JNIEXPORT jobject JNICALL Java_ai_onnxruntime_OnnxSparseTensor_getDataBuffer
  (JNIEnv * jniEnv, jobject jobj, jlong apiHandle, jlong handle) {
    (void) jobj; // Required JNI parameter not needed by functions which don't need to access their host object.
    const OrtApi* api = (const OrtApi*) apiHandle;
    OrtSparseFormat format;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorFormat((OrtValue*) handle, &format));
    switch (format) {
        case ORT_SPARSE_COO:
        case ORT_SPARSE_CSRC:
        case ORT_SPARSE_BLOCK_SPARSE: {
            OrtTensorTypeAndShapeInfo* info;
            checkOrtStatus(jniEnv,api,api->GetSparseTensorValuesTypeAndShape((OrtValue*) handle, &info));
            size_t arrSize;
            checkOrtStatus(jniEnv,api,api->GetTensorShapeElementCount(info,&arrSize));
            ONNXTensorElementDataType onnxTypeEnum;
            checkOrtStatus(jniEnv,api,api->GetTensorElementType(info,&onnxTypeEnum));
            api->ReleaseTensorTypeAndShapeInfo(info);

            size_t typeSize = onnxTypeSize(onnxTypeEnum);
            size_t sizeBytes = arrSize*typeSize;

            uint8_t* arr;
            checkOrtStatus(jniEnv,api,api->GetSparseTensorValues((OrtValue*)handle,(const void**)&arr));

            return (*jniEnv)->NewDirectByteBuffer(jniEnv, arr, sizeBytes);
        }
        case ORT_SPARSE_UNDEFINED: {
            throwOrtException(jniEnv,convertErrorCode(ORT_NOT_IMPLEMENTED),"Sparse format is ORT_SPARSE_UNDEFINED, cannot get data");
            break;
        }
    }

    return NULL;
}

/*
 * Class:     ai_onnxruntime_OnnxSparseTensor
 * Method:    getInnerIndicesShape
 * Signature: (JJJ)[J;
 */
JNIEXPORT jobject JNICALL Java_ai_onnxruntime_OnnxSparseTensor_getInnerIndicesShape
  (JNIEnv * jniEnv, jobject jobj, jlong apiHandle, jlong allocatorHandle, jlong handle) {
    (void) jobj; // Required JNI parameter not needed by functions which don't need to access their host object.
    const OrtApi* api = (const OrtApi*) apiHandle;
    const OrtValue* value = (const OrtValue*) handle;
    OrtAllocator* allocator = (OrtAllocator*) allocatorHandle;

    // Extract the info
    OrtTensorTypeAndShapeInfo* info;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorIndicesTypeShape(value,ORT_SPARSE_CSR_INNER_INDICES,&info));

    // Extract the shape
    size_t numDim;
    checkOrtStatus(jniEnv,api,api->GetDimensionsCount(info,&numDim));
    int64_t* dimensions;
    checkOrtStatus(jniEnv,api,api->AllocatorAlloc(allocator,sizeof(int64_t)*numDim,(void**)&dimensions));
    checkOrtStatus(jniEnv,api,api->GetDimensions(info, dimensions, numDim));

    // Free the info
    api->ReleaseTensorTypeAndShapeInfo(info);

    // Create the long array for the shape.
    jlongArray shape = (*jniEnv)->NewLongArray(jniEnv, safecast_size_t_to_jsize(numDim));
    (*jniEnv)->SetLongArrayRegion(jniEnv, shape, 0, safecast_size_t_to_jsize(numDim), (jlong*)dimensions);
    // Free the dimensions array
    checkOrtStatus(jniEnv, api, api->AllocatorFree(allocator, (void*)dimensions));
    dimensions = NULL;

    return shape;
}

/*
 * Class:     ai_onnxruntime_OnnxSparseTensor
 * Method:    getIndicesShape
 * Signature: (JJJ)[J;
 */
JNIEXPORT jobject JNICALL Java_ai_onnxruntime_OnnxSparseTensor_getIndicesShape
  (JNIEnv * jniEnv, jobject jobj, jlong apiHandle, jlong allocatorHandle, jlong handle) {
    (void) jobj; // Required JNI parameter not needed by functions which don't need to access their host object.
    const OrtApi* api = (const OrtApi*) apiHandle;
    const OrtValue* value = (const OrtValue*) handle;
    OrtAllocator* allocator = (OrtAllocator*) allocatorHandle;

    // Get the indices format
    OrtSparseFormat format;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorFormat(value, &format));
    enum OrtSparseIndicesFormat indicesFormat;
    switch (format) {
        case ORT_SPARSE_CSRC:
            indicesFormat = ORT_SPARSE_CSR_OUTER_INDICES;
            break;
        case ORT_SPARSE_COO:
            indicesFormat = ORT_SPARSE_COO_INDICES;
            break;
        case ORT_SPARSE_BLOCK_SPARSE:
            indicesFormat = ORT_SPARSE_BLOCK_SPARSE_INDICES;
            break;
        case ORT_SPARSE_UNDEFINED: {
            throwOrtException(jniEnv,convertErrorCode(ORT_NOT_IMPLEMENTED),"Sparse format is ORT_SPARSE_UNDEFINED, indices are not defined.");
            return NULL;
        }
    }

    // Extract the info
    OrtTensorTypeAndShapeInfo* info;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorIndicesTypeShape(value,indicesFormat,&info));

    // Extract the shape
    size_t numDim;
    checkOrtStatus(jniEnv,api,api->GetDimensionsCount(info,&numDim));
    int64_t* dimensions;
    checkOrtStatus(jniEnv,api,api->AllocatorAlloc(allocator,sizeof(int64_t)*numDim,(void**)&dimensions));
    checkOrtStatus(jniEnv,api,api->GetDimensions(info, dimensions, numDim));

    // Free the info
    api->ReleaseTensorTypeAndShapeInfo(info);

    // Create the long array for the shape.
    jlongArray shape = (*jniEnv)->NewLongArray(jniEnv, safecast_size_t_to_jsize(numDim));
    (*jniEnv)->SetLongArrayRegion(jniEnv, shape, 0, safecast_size_t_to_jsize(numDim), (jlong*)dimensions);
    // Free the dimensions array
    checkOrtStatus(jniEnv, api, api->AllocatorFree(allocator, (void*)dimensions));
    dimensions = NULL;

    return shape;
}

/*
 * Class:     ai_onnxruntime_OnnxSparseTensor
 * Method:    getValuesShape
 * Signature: (JJJ)[J;
 */
JNIEXPORT jobject JNICALL Java_ai_onnxruntime_OnnxSparseTensor_getValuesShape
  (JNIEnv * jniEnv, jobject jobj, jlong apiHandle, jlong allocatorHandle, jlong handle) {
    (void) jobj; // Required JNI parameter not needed by functions which don't need to access their host object.
    const OrtApi* api = (const OrtApi*) apiHandle;
    const OrtValue* value = (const OrtValue*) handle;
    OrtAllocator* allocator = (OrtAllocator*) allocatorHandle;

    // Extract the info
    OrtTensorTypeAndShapeInfo* info;
    checkOrtStatus(jniEnv,api,api->GetSparseTensorValuesTypeAndShape(value,&info));

    // Extract the shape
    size_t numDim;
    checkOrtStatus(jniEnv,api,api->GetDimensionsCount(info,&numDim));
    int64_t* dimensions;
    checkOrtStatus(jniEnv,api,api->AllocatorAlloc(allocator,sizeof(int64_t)*numDim,(void**)&dimensions));
    checkOrtStatus(jniEnv,api,api->GetDimensions(info, dimensions, numDim));

    // Free the info
    api->ReleaseTensorTypeAndShapeInfo(info);

    // Create the long array for the shape.
    jlongArray shape = (*jniEnv)->NewLongArray(jniEnv, safecast_size_t_to_jsize(numDim));
    (*jniEnv)->SetLongArrayRegion(jniEnv, shape, 0, safecast_size_t_to_jsize(numDim), (jlong*)dimensions);

    // Free the dimensions array
    checkOrtStatus(jniEnv, api, api->AllocatorFree(allocator, (void*)dimensions));
    dimensions = NULL;

    return shape;
}

/*
 * Class:     ai_onnxruntime_OnnxSparseTensor
 * Method:    close
 * Signature: (JJ)V
 */
JNIEXPORT void JNICALL Java_ai_onnxruntime_OnnxSparseTensor_close(JNIEnv * jniEnv, jobject jobj, jlong apiHandle, jlong handle) {
    (void) jniEnv; (void) jobj; // Required JNI parameters not needed by functions which don't need to access their host object.
    const OrtApi* api = (const OrtApi*) apiHandle;
    api->ReleaseValue((OrtValue*)handle);
}

/*
 * Class:     ai_onnxruntime_OnnxSparseTensor
 * Method:    createCSRCSparseTensorFromBuffer
 * Signature: (JJLjava/nio/Buffer;IJLjava/nio/Buffer;IJLjava/nio/Buffer;IJ[J[JI)J
 */
JNIEXPORT jlong JNICALL Java_ai_onnxruntime_OnnxSparseTensor_createCSRCSparseTensorFromBuffer
  (JNIEnv * jniEnv, jclass cls, jlong apiHandle, jlong allocatorHandle,
  jobject indicesBuffer, jint indicesBufferPos, jlong indicesBufferSize,
  jobject innerIndicesBuffer, jint innerIndicesBufferPos, jlong innerIndicesBufferSize,
  jobject dataBuffer, jint dataBufferPos,
  jlongArray denseShape, jlongArray valuesShape,
  jint onnxTypeJava) {
    (void) cls; // Required JNI parameters not needed by functions which don't need to access their host object.
    const OrtApi* api = (const OrtApi*) apiHandle;
    OrtAllocator* allocator = (OrtAllocator*) allocatorHandle;
    const OrtMemoryInfo* allocatorInfo;
    checkOrtStatus(jniEnv, api, api->AllocatorGetInfo(allocator,&allocatorInfo));

    // Convert types to ONNX C enums
    ONNXTensorElementDataType onnxType = convertToONNXDataFormat(onnxTypeJava);

    // Extract the buffers
    char* indicesBufferArr = (char*)(*jniEnv)->GetDirectBufferAddress(jniEnv,indicesBuffer);
    char* innerIndicesBufferArr = (char*)(*jniEnv)->GetDirectBufferAddress(jniEnv,innerIndicesBuffer);
    char* dataBufferArr = (char*)(*jniEnv)->GetDirectBufferAddress(jniEnv,dataBuffer);
    // Increment by bufferPos bytes
    indicesBufferArr = indicesBufferArr + indicesBufferPos;
    innerIndicesBufferArr = innerIndicesBufferArr + innerIndicesBufferPos;
    dataBufferArr = dataBufferArr + dataBufferPos;

    // Extract the dense shape information
    jboolean mkCopy;
    jlong* shapeArr = (*jniEnv)->GetLongArrayElements(jniEnv,denseShape,&mkCopy);
    jsize shapeLen = (*jniEnv)->GetArrayLength(jniEnv,denseShape);

    // Extract the value shape
    jlong* valuesShapeArr = (*jniEnv)->GetLongArrayElements(jniEnv,valuesShape,&mkCopy);
    jsize valuesShapeLen = (*jniEnv)->GetArrayLength(jniEnv,valuesShape);

    // Create the OrtValue
    OrtValue* ortValue;
    checkOrtStatus(jniEnv, api, api->CreateSparseTensorWithValuesAsOrtValue(allocatorInfo, dataBufferArr,
     (int64_t*) shapeArr, shapeLen, (int64_t*) valuesShapeArr, valuesShapeLen, onnxType, &ortValue));

    // Release shapes
    (*jniEnv)->ReleaseLongArrayElements(jniEnv,denseShape,shapeArr,JNI_ABORT);
    (*jniEnv)->ReleaseLongArrayElements(jniEnv,valuesShape,valuesShapeArr,JNI_ABORT);

    // Fill it with indices
    checkOrtStatus(jniEnv,api,api->UseCsrIndices(ortValue,
                            (int64_t *) innerIndicesBufferArr, innerIndicesBufferSize,
                            (int64_t *) indicesBufferArr, indicesBufferSize));

    // Return the pointer to the OrtValue
    return (jlong) ortValue;
}

/*
 * Class:     ai_onnxruntime_OnnxSparseTensor
 * Method:    createSparseTensorFromBuffer
 * Signature: (JJLjava/nio/Buffer;IJLjava/nio/Buffer;IJ[J[J[JII)J
 */
JNIEXPORT jlong JNICALL Java_ai_onnxruntime_OnnxSparseTensor_createSparseTensorFromBuffer
  (JNIEnv * jniEnv, jclass cls, jlong apiHandle, jlong allocatorHandle,
  jobject indicesBuffer, jint indicesBufferPos, jlong indicesBufferSize,
  jobject dataBuffer, jint dataBufferPos,
  jlongArray denseShape, jlongArray indicesShape, jlongArray valuesShape,
  jint onnxTypeJava, jint sparsityTypeJava) {
    (void) cls; // Required JNI parameters not needed by functions which don't need to access their host object.
    const OrtApi* api = (const OrtApi*) apiHandle;
    OrtAllocator* allocator = (OrtAllocator*) allocatorHandle;
    const OrtMemoryInfo* allocatorInfo;
    checkOrtStatus(jniEnv, api, api->AllocatorGetInfo(allocator,&allocatorInfo));

    // Convert types to ONNX C enums
    ONNXTensorElementDataType onnxType = convertToONNXDataFormat(onnxTypeJava);
    OrtSparseFormat sparsityType = convertToOrtSparseFormat(sparsityTypeJava);

    // Extract the buffers
    char* indicesBufferArr = (char*)(*jniEnv)->GetDirectBufferAddress(jniEnv,indicesBuffer);
    char* dataBufferArr = (char*)(*jniEnv)->GetDirectBufferAddress(jniEnv,dataBuffer);
    // Increment by bufferPos bytes
    indicesBufferArr = indicesBufferArr + indicesBufferPos;
    dataBufferArr = dataBufferArr + dataBufferPos;

    // Extract the dense shape information
    jboolean mkCopy;
    jlong* shapeArr = (*jniEnv)->GetLongArrayElements(jniEnv,denseShape,&mkCopy);
    jsize shapeLen = (*jniEnv)->GetArrayLength(jniEnv,denseShape);

    // Extract the value shape
    jlong* valuesShapeArr = (*jniEnv)->GetLongArrayElements(jniEnv,valuesShape,&mkCopy);
    jsize valuesShapeLen = (*jniEnv)->GetArrayLength(jniEnv,valuesShape);

    // Create the OrtValue
    OrtValue* ortValue;
    checkOrtStatus(jniEnv, api, api->CreateSparseTensorWithValuesAsOrtValue(allocatorInfo, dataBufferArr,
     (int64_t*) shapeArr, shapeLen, (int64_t*) valuesShapeArr, valuesShapeLen, onnxType, &ortValue));

    // Release shapes
    (*jniEnv)->ReleaseLongArrayElements(jniEnv,denseShape,shapeArr,JNI_ABORT);
    (*jniEnv)->ReleaseLongArrayElements(jniEnv,valuesShape,valuesShapeArr,JNI_ABORT);

    // Fill it with indices
    switch (sparsityType) {
        case ORT_SPARSE_COO: {
            // The cast is because we compute the offset in bytes in Java.
            checkOrtStatus(jniEnv,api,api->UseCooIndices(ortValue, (int64_t *) indicesBufferArr, indicesBufferSize));
            break;
        }
        case ORT_SPARSE_BLOCK_SPARSE: {
            // Extract the indices shape
            jlong* indicesShapeArr = (*jniEnv)->GetLongArrayElements(jniEnv,indicesShape,&mkCopy);
            jsize indicesShapeLen = (*jniEnv)->GetArrayLength(jniEnv,indicesShape);

            // The cast is because we compute the offset in bytes in Java.
            checkOrtStatus(jniEnv,api,api->UseBlockSparseIndices(ortValue, (int64_t *) indicesShapeArr, indicesShapeLen, (int32_t *) indicesBufferArr));

            // Release the indices shape
            (*jniEnv)->ReleaseLongArrayElements(jniEnv,indicesShape,indicesShapeArr,JNI_ABORT);
            break;
        }
        case ORT_SPARSE_CSRC:
        case ORT_SPARSE_UNDEFINED: {
            throwOrtException(jniEnv,convertErrorCode(ORT_NOT_IMPLEMENTED),"These types are unsupported by this method - ORT_SPARSE_CSRC, ORT_SPARSE_UNDEFINED");
            break;
        }
    }

    // Return the pointer to the OrtValue
    return (jlong) ortValue;
}
