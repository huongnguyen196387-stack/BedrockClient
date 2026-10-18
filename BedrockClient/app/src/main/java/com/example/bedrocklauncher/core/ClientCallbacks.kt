package com.example.bedrocklauncher.core

/** Hai native method này được libclient.so tự đăng ký (RegisterNatives) trong JNI_OnLoad. */
object ClientCallbacks {
    @Volatile var nativeReady: Boolean = false
        internal set

    @JvmStatic external fun onTrimMemory(level: Int)
    @JvmStatic external fun setConfigDir(path: String)
    @JvmStatic external fun getHookStatus(): Int
}
