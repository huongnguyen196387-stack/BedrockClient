package com.example.bedrocklauncher.core

object NativeBridge {
    init { System.loadLibrary("launcher_native") }

    /** memfd_create + android_dlopen_ext + JNI_OnLoad; không ghi plaintext ra đĩa. */
    @JvmStatic external fun loadFromMemory(elf: ByteArray, name: String): Boolean
}
