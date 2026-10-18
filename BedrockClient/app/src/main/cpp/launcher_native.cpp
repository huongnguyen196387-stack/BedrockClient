// CMake: add_library(launcher_native SHARED launcher_native.cpp) ; target_link_libraries(launcher_native log dl)
#include <jni.h>
#include <android/dlext.h>
#include <android/log.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "LauncherNative", __VA_ARGS__)
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

static JavaVM* g_vm = nullptr;

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrocklauncher_core_NativeBridge_loadFromMemory(
        JNIEnv* env, jobject, jbyteArray elf, jstring jname) {
    const char* name = env->GetStringUTFChars(jname, nullptr);
    jsize len = env->GetArrayLength(elf);
    jbyte* data = env->GetByteArrayElements(elf, nullptr);
    bool ok = false;
    void* handle = nullptr;

    int fd = (int) syscall(__NR_memfd_create, name, MFD_CLOEXEC);
    if (fd < 0) { LOG("memfd_create lỗi: %s", strerror(errno)); goto done; }

    for (jsize off = 0; off < len;) {
        ssize_t n = write(fd, data + off, len - off);
        if (n <= 0) { LOG("write memfd lỗi: %s", strerror(errno)); goto done; }
        off += n;
    }

    {
        android_dlextinfo info{};
        info.flags = ANDROID_DLEXT_USE_LIBRARY_FD;
        info.library_fd = fd;
        handle = android_dlopen_ext(name, RTLD_NOW, &info);
        if (!handle) { LOG("dlopen lỗi: %s", dlerror()); goto done; }

        // dlopen không tự chạy JNI_OnLoad như System.load → gọi thủ công
        using OnLoad = jint (*)(JavaVM*, void*);
        auto onload = (OnLoad) dlsym(handle, "JNI_OnLoad");
        if (onload) {
            jint v = onload(g_vm, nullptr);
            if (v < JNI_VERSION_1_6) { LOG("JNI_OnLoad trả về %d", v); goto done; }
        }
        ok = true;
        LOG("Đã nạp %s (%d bytes) từ memfd", name, (int) len);
    }

done:
    memset(data, 0, len);                                  // xóa bản plaintext phía native
    env->ReleaseByteArrayElements(elf, data, 0);
    env->ReleaseStringUTFChars(jname, name);
    if (fd >= 0) close(fd);                                // mapping vẫn sống sau khi đóng fd
    return ok ? JNI_TRUE : JNI_FALSE;
}
