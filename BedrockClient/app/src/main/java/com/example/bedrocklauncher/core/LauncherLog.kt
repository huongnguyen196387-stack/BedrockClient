package com.example.bedrocklauncher.core

import android.content.Context
import android.util.Log
import java.io.File
import java.io.RandomAccessFile

object LauncherLog {
    private const val TAG = "BedrockLauncher"
    private var file: File? = null

    fun init(ctx: Context) {
        file = File(ctx.filesDir, "logs/launcher.log").apply { parentFile?.mkdirs() }
    }

    @Synchronized
    private fun write(level: String, msg: String, t: Throwable?) {
        val f = file ?: return
        if (f.length() > 2_000_000) f.writeText("")
        f.appendText("${System.currentTimeMillis()} [$level] $msg\n" + (t?.let { Log.getStackTraceString(it) + "\n" } ?: ""))
    }

    fun i(m: String) { Log.i(TAG, m); write("I", m, null) }
    fun w(m: String) { Log.w(TAG, m); write("W", m, null) }
    fun e(m: String, t: Throwable? = null) { Log.e(TAG, m, t); write("E", m, t) }

    /** ~4 KB cuối của log để hiển thị trên màn hình. */
    @Synchronized
    fun tail(): String {
        val f = file?.takeIf { it.exists() } ?: return ""
        RandomAccessFile(f, "r").use { r ->
            val n = minOf(4096L, r.length())
            r.seek(r.length() - n)
            return ByteArray(n.toInt()).also { r.readFully(it) }.toString(Charsets.UTF_8)
        }
    }
}
