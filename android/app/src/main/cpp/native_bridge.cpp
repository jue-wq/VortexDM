#include "core/network/http_request_policy.hpp"

#include <jni.h>

#include <string>
#include <string_view>

namespace {

jstring to_java_string(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_vortexdm_app_NativeBridge_coreVersion(JNIEnv* env, jobject) {
    return to_java_string(env, "0.1.0");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortexdm_app_NativeBridge_isSupportedUrl(
    JNIEnv* env, jobject, jstring url
) {
    if (url == nullptr) {
        return JNI_FALSE;
    }

    const char* chars = env->GetStringUTFChars(url, nullptr);
    if (chars == nullptr) {
        return JNI_FALSE;
    }

    // Use the explicit JNI byte length so an embedded NUL cannot truncate the
    // URL before the shared policy sees the complete input.
    const jsize length = env->GetStringUTFLength(url);
    const bool supported = length >= 0 &&
        vortex::HttpRequestPolicy::is_supported_url(
            std::string_view(chars, static_cast<std::size_t>(length))
        );
    env->ReleaseStringUTFChars(url, chars);
    return supported ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_vortexdm_app_NativeBridge_rangeHeader(
    JNIEnv* env, jobject, jlong first, jlong last
) {
    if (first < 0 || last < 0) {
        return nullptr;
    }

    const auto header = vortex::HttpRequestPolicy::range_header({
        static_cast<std::uint64_t>(first),
        static_cast<std::uint64_t>(last)
    });
    return header ? to_java_string(env, *header) : nullptr;
}
