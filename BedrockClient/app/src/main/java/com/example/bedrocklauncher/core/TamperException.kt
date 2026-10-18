package com.example.bedrocklauncher.core

sealed class TamperException(msg: String) : SecurityException(msg) {
    class GameNotInstalled : TamperException("Minecraft chưa được cài đặt")
    class NoBaseline : TamperException("Chưa có baseline — hãy ghi baseline từ bản Minecraft sạch trước")
    class SignatureMismatch(val got: String) : TamperException("Chữ ký APK không khớp baseline: $got")
    class HashMismatch(val entry: String) : TamperException("SHA-256 sai: $entry")
    class MissingEntry(val entry: String) : TamperException("Thiếu thư viện so với baseline: $entry")
    class UnexpectedEntry(val entry: String) : TamperException("Thư viện lạ không có trong baseline: $entry")
    class ClientCorrupt : TamperException("libclient.so.enc bị sửa/hỏng (GCM tag lỗi)")
    class KeyMissing : TamperException("Không có khóa giải mã trong Keystore")
    class DebuggerDetected : TamperException("Phát hiện debugger/tracer")
}
