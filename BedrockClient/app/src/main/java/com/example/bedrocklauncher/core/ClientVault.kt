package com.example.bedrocklauncher.core

import android.content.Context
import android.security.keystore.KeyProperties
import android.security.keystore.KeyProtection
import com.example.bedrocklauncher.BuildConfig
import java.io.File
import java.io.FileNotFoundException
import java.security.KeyStore
import javax.crypto.AEADBadTagException
import javax.crypto.Cipher
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * Lấy payload libclient.so:
 *  - assets/libclient.so.enc  : AES-256-GCM, khóa trong AndroidKeyStore (chế độ chuẩn)
 *  - không có .enc            : DEV MODE, đọc libclient.so thô từ nativeLibraryDir (chưa mã hóa)
 */
class ClientVault(private val ctx: Context, private val keyAlias: String = "client_key") {

    fun isEncrypted(): Boolean = runCatching { ctx.assets.open(ENC_NAME).close(); true }.getOrDefault(false)

    /** Trả plaintext ELF; caller PHẢI fill(0) sau khi nạp. */
    fun load(): ByteArray {
        val raw = try {
            ctx.assets.open(ENC_NAME).use { it.readBytes() }
        } catch (e: FileNotFoundException) {
            LauncherLog.w("DEV MODE: không có $ENC_NAME → dùng libclient.so thô (chưa mã hóa)")
            return File(ctx.applicationInfo.nativeLibraryDir, "libclient.so").readBytes()
        }
        require(raw.size > 28) { "File mã hóa quá nhỏ" }
        importDevKeyIfNeeded()
        return try {
            Cipher.getInstance("AES/GCM/NoPadding").run {
                init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, raw, 0, 12))
                doFinal(raw, 12, raw.size - 12)
            }
        } catch (e: AEADBadTagException) {
            throw TamperException.ClientCorrupt()
        }
    }

    private fun ks() = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }

    private fun key(): SecretKey = (ks().getKey(keyAlias, null) as? SecretKey) ?: throw TamperException.KeyMissing()

    /** Chỉ cho dev: nạp khóa từ BuildConfig vào Keystore lần đầu. Release nên cấp khóa từ server sau attestation. */
    private fun importDevKeyIfNeeded() {
        val hex = BuildConfig.DEV_CLIENT_KEY_HEX
        val store = ks()
        if (hex.length != 64 || store.containsAlias(keyAlias)) return
        val bytes = ByteArray(32) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        store.setEntry(
            keyAlias, KeyStore.SecretKeyEntry(SecretKeySpec(bytes, "AES")),
            KeyProtection.Builder(KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build()
        )
        bytes.fill(0)
        LauncherLog.w("Đã nạp khóa DEV vào Keystore")
    }

    companion object { const val ENC_NAME = "libclient.so.enc" }
}
