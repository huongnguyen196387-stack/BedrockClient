package com.example.bedrocklauncher.core

import android.content.Context

/**
 * Container cấp tiến trình launcher: kiểm tra toàn vẹn → createPackageContext của game →
 * giải mã trong RAM → nạp libclient.so từ memfd.
 *
 * GIỚI HẠN: libclient.so được nạp vào tiến trình CỦA LAUNCHER. Minecraft chạy ở tiến trình riêng nên
 * hook chỉ có tác dụng khi game được dựng trong chính tiến trình này (engine kiểu VirtualApp gắn vào GameHost).
 */
class VirtualAppContainer(
    private val ctx: Context,
    private val checker: IntegrityChecker,
    private val vault: ClientVault,
) {
    sealed interface Result {
        data class Ready(val gameContext: Context) : Result
        data class Blocked(val reason: TamperException) : Result
        data class Failed(val cause: Throwable) : Result
    }

    @Volatile private var loaded = false

    @Synchronized
    fun prepare(): Result {
        LauncherLog.i("Container: chuẩn bị $MC_PACKAGE")
        return try {
            checker.verifyAll()

            val gameCtx = ctx.createPackageContext(MC_PACKAGE, Context.CONTEXT_INCLUDE_CODE or Context.CONTEXT_IGNORE_SECURITY)
            LauncherLog.i("Game ClassLoader: ${gameCtx.classLoader}")

            if (!loaded) {
                val elf = vault.load()
                try {
                    if (!NativeBridge.loadFromMemory(elf, "libclient.so"))
                        return Result.Failed(IllegalStateException("dlopen libclient.so thất bại (xem logcat tag LauncherNative)"))
                } finally {
                    elf.fill(0)
                }
                loaded = true
                ClientCallbacks.nativeReady = true
                ClientCallbacks.setConfigDir(ctx.filesDir.absolutePath)
            }
            LauncherLog.i("libclient.so đã nạp (encrypted=${vault.isEncrypted()})")
            Result.Ready(gameCtx)
        } catch (t: TamperException) {
            LauncherLog.e("ANTI-TAMPER: ${t.message}", t)
            Result.Blocked(t)
        } catch (t: Throwable) {
            LauncherLog.e("Lỗi không mong đợi", t)
            Result.Failed(t)
        }
    }
}
