package com.example.bedrocklauncher

import android.app.Application
import com.example.bedrocklauncher.core.ClientCallbacks
import com.example.bedrocklauncher.core.LauncherLog

class LauncherApp : Application() {
    override fun onCreate() {
        super.onCreate()
        LauncherLog.init(this)
        LauncherLog.i("LauncherApp created, pid=${android.os.Process.myPid()}")
    }

    // Native tự ánh xạ mức: RUNNING_CRITICAL(15)/MODERATE+(≥60) → xả hết; RUNNING_LOW(10)+ → xả một nửa
    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (!ClientCallbacks.nativeReady) return
        try { ClientCallbacks.onTrimMemory(level) } catch (t: Throwable) { LauncherLog.e("Forward onTrimMemory($level) lỗi", t) }
    }
}
