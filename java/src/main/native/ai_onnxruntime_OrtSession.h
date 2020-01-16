/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class ai_onnxruntime_OrtSession */

#ifndef _Included_ai_onnxruntime_OrtSession
#define _Included_ai_onnxruntime_OrtSession
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    createSession
 * Signature: (JJLjava/lang/String;J)J
 */
JNIEXPORT jlong JNICALL Java_ai_onnxruntime_OrtSession_createSession__JJLjava_lang_String_2J
  (JNIEnv *, jobject, jlong, jlong, jstring, jlong);

/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    createSession
 * Signature: (JJ[BJ)J
 */
JNIEXPORT jlong JNICALL Java_ai_onnxruntime_OrtSession_createSession__JJ_3BJ
  (JNIEnv *, jobject, jlong, jlong, jbyteArray, jlong);

/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    getNumInputs
 * Signature: (JJ)J
 */
JNIEXPORT jlong JNICALL Java_ai_onnxruntime_OrtSession_getNumInputs
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    getInputNames
 * Signature: (JJJ)[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_ai_onnxruntime_OrtSession_getInputNames
  (JNIEnv *, jobject, jlong, jlong, jlong);

/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    getInputInfo
 * Signature: (JJJ)[Lai/onnxruntime/NodeInfo;
 */
JNIEXPORT jobjectArray JNICALL Java_ai_onnxruntime_OrtSession_getInputInfo
  (JNIEnv *, jobject, jlong, jlong, jlong);

/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    getNumOutputs
 * Signature: (JJ)J
 */
JNIEXPORT jlong JNICALL Java_ai_onnxruntime_OrtSession_getNumOutputs
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    getOutputNames
 * Signature: (JJJ)[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_ai_onnxruntime_OrtSession_getOutputNames
  (JNIEnv *, jobject, jlong, jlong, jlong);

/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    getOutputInfo
 * Signature: (JJJ)[Lai/onnxruntime/NodeInfo;
 */
JNIEXPORT jobjectArray JNICALL Java_ai_onnxruntime_OrtSession_getOutputInfo
  (JNIEnv *, jobject, jlong, jlong, jlong);

/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    run
 * Signature: (JJJ[Ljava/lang/String;[JJ[Ljava/lang/String;J)[Lai/onnxruntime/OnnxValue;
 */
JNIEXPORT jobjectArray JNICALL Java_ai_onnxruntime_OrtSession_run
  (JNIEnv *, jobject, jlong, jlong, jlong, jobjectArray, jlongArray, jlong, jobjectArray, jlong);

/*
 * Class:     ai_onnxruntime_OrtSession
 * Method:    closeSession
 * Signature: (JJ)V
 */
JNIEXPORT void JNICALL Java_ai_onnxruntime_OrtSession_closeSession
  (JNIEnv *, jobject, jlong, jlong);

#ifdef __cplusplus
}
#endif
#endif
