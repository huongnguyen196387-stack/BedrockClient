package com.example.bedrocklauncher.core

/** Giải mã bitmask trạng thái hook từ native (khớp enum trong game_internals.hpp). */
object HookStatus {
    private val names = listOf(
        "eglSwapBuffers", "AInputQueue_getEvent", "getFov", "Gamma/Fullbright",
        "getPerspective", "RenderLevel", "GL texture hooks", "Installer hoàn tất",
    )

    fun describe(): String {
        if (!ClientCallbacks.nativeReady) return "Client chưa được nạp (chạy bước 2 trước)"
        val bits = ClientCallbacks.getHookStatus()
        return names.indices.joinToString("\n") { i -> (if (bits and (1 shl i) != 0) "[OK]  " else "[--]  ") + names[i] } +
            "\n\n[--] ở hook game nghĩa là signature không khớp DUY NHẤT (xem logcat tag 'gi'), " +
            "hoặc installer chưa chạy xong (game chưa nạp libminecraftpe.so)."
    }
}
