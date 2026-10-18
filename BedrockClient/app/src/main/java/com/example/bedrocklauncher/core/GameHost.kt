package com.example.bedrocklauncher.core

import android.app.Activity
import android.content.Context

/** Điểm gắn engine ảo hóa (VirtualApp-like). Mặc định chỉ mở Minecraft bằng Intent. */
interface GameHost {
    fun launch(activity: Activity, gameCtx: Context): Boolean
}

class IntentGameHost : GameHost {
    override fun launch(activity: Activity, gameCtx: Context): Boolean {
        val intent = activity.packageManager.getLaunchIntentForPackage(MC_PACKAGE) ?: return false
        LauncherLog.w("IntentGameHost: game chạy ở TIẾN TRÌNH RIÊNG → hook của libclient.so KHÔNG có hiệu lực")
        activity.startActivity(intent)
        return true
    }
}
