package com.example.bedrocklauncher.core

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Debug
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.security.MessageDigest
import java.util.zip.ZipFile

const val MC_PACKAGE = "com.mojang.minecraftpe"

class Baseline(val signers: Set<String>, val libs: Map<String, String>) {
    fun toJson(): String = JSONObject()
        .put("signers", JSONArray(signers.toList()))
        .put("libs", JSONObject(libs as Map<*, *>))
        .toString(2)

    companion object {
        fun fromJson(s: String): Baseline {
            val o = JSONObject(s)
            val sg = o.getJSONArray("signers")
            val l = o.getJSONObject("libs")
            return Baseline(
                (0 until sg.length()).map { sg.getString(it) }.toSet(),
                l.keys().asSequence().associateWith { l.getString(it) }
            )
        }
    }
}

/**
 * Baseline kiểu TOFU: người dùng bấm "Ghi baseline" khi Minecraft đang là bản sạch; các lần sau mọi
 * thư viện native (base + split APK) và chữ ký phải khớp. Baseline nằm trong bộ nhớ riêng của app —
 * chống sửa thường, KHÔNG chống được kẻ có root.
 */
class IntegrityChecker(private val ctx: Context) {
    private val baselineFile = File(ctx.filesDir, "baseline.json")

    fun hasBaseline() = baselineFile.exists()

    fun captureBaseline(): Baseline {
        checkNoDebugger()
        val info = gameInfo()
        val b = Baseline(currentSigners(), hashNativeLibs(info.sourceDir, info.splitSourceDirs))
        baselineFile.writeText(b.toJson())
        LauncherLog.i("Baseline đã ghi: ${b.libs.size} thư viện, ${b.signers.size} chữ ký")
        return b
    }

    fun verifyAll() {
        checkNoDebugger()
        val info = gameInfo()
        if (!hasBaseline()) throw TamperException.NoBaseline()
        val base = Baseline.fromJson(baselineFile.readText())

        currentSigners().firstOrNull { it !in base.signers }?.let { throw TamperException.SignatureMismatch(it) }

        val cur = hashNativeLibs(info.sourceDir, info.splitSourceDirs)
        for ((entry, expected) in base.libs) {
            val actual = cur[entry] ?: throw TamperException.MissingEntry(entry)
            if (!actual.equals(expected, ignoreCase = true)) throw TamperException.HashMismatch(entry)
        }
        cur.keys.firstOrNull { it !in base.libs }?.let { throw TamperException.UnexpectedEntry(it) }
        LauncherLog.i("Integrity OK (${base.libs.size} thư viện)")
    }

    private fun gameInfo() = try {
        ctx.packageManager.getApplicationInfo(MC_PACKAGE, 0)
    } catch (e: PackageManager.NameNotFoundException) {
        throw TamperException.GameNotInstalled()
    }

    private fun currentSigners(): Set<String> {
        val pi = ctx.packageManager.getPackageInfo(MC_PACKAGE, PackageManager.GET_SIGNING_CERTIFICATES)
        val signers = pi.signingInfo?.apkContentsSigners ?: throw TamperException.SignatureMismatch("none")
        return signers.map { sha256Hex(it.toByteArray()) }.toSet()
    }

    /** Hash trực tiếp trong APK vì extractNativeLibs=false khiến nativeLibraryDir thường rỗng. */
    private fun hashNativeLibs(base: String, splits: Array<String>?): Map<String, String> {
        val abi = Build.SUPPORTED_64_BIT_ABIS.firstOrNull() ?: Build.SUPPORTED_ABIS[0]
        val prefix = "lib/$abi/"
        val out = sortedMapOf<String, String>()
        for (path in listOf(base) + (splits?.toList() ?: emptyList())) ZipFile(path).use { zip ->
            for (e in zip.entries()) {
                if (e.isDirectory || !e.name.startsWith(prefix) || !e.name.endsWith(".so")) continue
                out[e.name] = zip.getInputStream(e).use { sha256Hex(it) }
            }
        }
        return out
    }

    private fun checkNoDebugger() {
        val tracer = runCatching {
            File("/proc/self/status").readLines().first { it.startsWith("TracerPid:") }
                .substringAfter(':').trim().toInt()
        }.getOrDefault(0)
        if (Debug.isDebuggerConnected() || tracer != 0) throw TamperException.DebuggerDetected()
    }

    companion object {
        fun sha256Hex(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).toHex()
        fun sha256Hex(s: InputStream): String {
            val md = MessageDigest.getInstance("SHA-256")
            val buf = ByteArray(64 * 1024)
            while (true) { val n = s.read(buf); if (n < 0) break; md.update(buf, 0, n) }
            return md.digest().toHex()
        }
        private fun ByteArray.toHex() = joinToString("") { "%02x".format(it) }
    }
}
