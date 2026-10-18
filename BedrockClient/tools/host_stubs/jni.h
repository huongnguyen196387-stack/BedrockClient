#pragma once
#include <cstdint>
typedef int32_t jint; typedef int8_t jbyte; typedef int32_t jsize; typedef uint8_t jboolean;
struct _jobject{}; typedef _jobject* jobject; typedef _jobject* jclass; typedef _jobject* jstring; typedef _jobject* jbyteArray;
#define JNIEXPORT __attribute__((visibility("default")))
#define JNICALL
#define JNI_VERSION_1_6 0x00010006
#define JNI_OK 0
#define JNI_TRUE 1
#define JNI_FALSE 0
struct JNINativeMethod { const char* name; const char* signature; void* fnPtr; };
struct JNIEnv {
  const char* GetStringUTFChars(jstring, jboolean*); void ReleaseStringUTFChars(jstring, const char*);
  jsize GetArrayLength(jbyteArray); jbyte* GetByteArrayElements(jbyteArray, jboolean*); void ReleaseByteArrayElements(jbyteArray, jbyte*, jint);
  jclass FindClass(const char*); void ExceptionClear(); jint RegisterNatives(jclass, const JNINativeMethod*, jint);
};
struct JavaVM { jint GetEnv(void**, jint); };
