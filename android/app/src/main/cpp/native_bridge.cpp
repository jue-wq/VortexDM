#include <jni.h>

extern "C" JNIEXPORT jstring JNICALL
Java_com_vortexdm_app_NativeBridge_coreVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("0.1.0");
}
