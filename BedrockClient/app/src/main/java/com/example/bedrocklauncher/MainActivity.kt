package com.example.bedrocklauncher

import android.content.Context
import android.graphics.Typeface
import android.os.Bundle
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.bedrocklauncher.core.*
import java.util.concurrent.Executors

class MainActivity : AppCompatActivity() {
    private val io = Executors.newSingleThreadExecutor()
    private lateinit var status: TextView
    private lateinit var checker: IntegrityChecker
    private lateinit var container: VirtualAppContainer
    private val host: GameHost = IntentGameHost()
    @Volatile private var gameCtx: Context? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        checker = IntegrityChecker(this)
        container = VirtualAppContainer(this, checker, ClientVault(this))

        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad, pad, pad) }
        status = TextView(this).apply { textSize = 11f; typeface = Typeface.MONOSPACE; setTextIsSelectable(true) }

        fun button(label: String, action: () -> String) = Button(this).apply {
            text = label
            setOnClickListener {
                isEnabled = false
                io.execute {
                    val msg = try { action() } catch (t: Throwable) { LauncherLog.e("Tác vụ lỗi", t); "Lỗi: ${t.message}" }
                    runOnUiThread { isEnabled = true; refresh(msg) }
                }
            }
        }.also { root.addView(it, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)) }

        button("1. Ghi baseline (khi Minecraft là bản sạch)") {
            val b = checker.captureBaseline(); "Đã ghi baseline: ${b.libs.size} thư viện"
        }
        button("2. Kiểm tra toàn vẹn & nạp client") {
            when (val r = container.prepare()) {
                is VirtualAppContainer.Result.Ready -> { gameCtx = r.gameContext; "Client đã sẵn sàng" }
                is VirtualAppContainer.Result.Blocked -> "CHẶN: ${r.reason.message}"
                is VirtualAppContainer.Result.Failed -> "Thất bại: ${r.cause.message}"
            }
        }
        button("3. Chạy Minecraft") {
            val g = gameCtx
            if (g == null) "Hãy chạy bước 2 trước"
            else if (host.launch(this, g)) "Đã mở Minecraft" else "Không mở được Minecraft"
        }

        button("4. Trạng thái hook") { HookStatus.describe() }

        root.addView(ScrollView(this).apply { addView(status) }, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        setContentView(root)
        refresh("Sẵn sàng. baseline=${checker.hasBaseline()}")
    }

    private fun refresh(msg: String) { status.text = "$msg\n\n${LauncherLog.tail()}" }

    override fun onDestroy() { super.onDestroy(); io.shutdown() }
}
