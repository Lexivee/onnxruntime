/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class ai_onnxruntime_OrtEnvironment */

#ifndef _Included_ai_onnxruntime_OrtEnvironment
#define _Included_ai_onnxruntime_OrtEnvironment
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     ai_onnxruntime_OrtEnvironment
 * Method:    createHandle
 * Signature: (JILjava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_ai_onnxruntime_OrtEnvironment_createHandle
  (JNIEnv *, jclass, jlong, jint, jstring);

/*
 * Class:     ai_onnxruntime_OrtEnvironment
 * Method:    getDefaultAllocator
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_ai_onnxruntime_OrtEnvironment_getDefaultAllocator
  (JNIEnv *, jclass, jlong);

/*
 * Class:     ai_onnxruntime_OrtEnvironment
 * Method:    close
 * Signature: (JJ)V
 */
JNIEXPORT void JNICALL Java_ai_onnxruntime_OrtEnvironment_close
  (JNIEnv *, jclass, jlong, jlong);

/*
 * Class:     ai_onnxruntime_OrtEnvironment
 * Method:    setTelemetry
 * Signature: (JJZ)V
 */
JNIEXPORT void JNICALL Java_ai_onnxruntime_OrtEnvironment_setTelemetry
  (JNIEnv *, jclass, jlong, jlong, jboolean);

#ifdef __cplusplus
}
#endif
#endif
