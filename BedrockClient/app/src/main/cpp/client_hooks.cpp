// client_hooks.cpp — libclient.so (ARM64, C++17)
// CMake: add_library(client SHARED client_hooks.cpp)
//        target_link_libraries(client dobby android EGL log dl)
#include <EGL/egl.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <jni.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

#include "client_state.hpp"
#include "game_internals.hpp"
#include "dobby.h"  // int DobbyHook(void* addr, void* replace, void** origin);
                    // void* DobbySymbolResolver(const char* image, const char* sym);

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "client", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "client", __VA_ARGS__)

// ── Các hàm do module khác của client cung cấp (weak stub để link được) ──
void client_texcache_flush_gl();   // game_internals.cpp
void render_client_ui();           // client_ui.cpp


namespace client {

// ───────────────────────── Hook 1: eglSwapBuffers ─────────────────────────
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay dpy, EGLSurface surface);
eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    thread_local bool in_hook = false;  // chống đệ quy nếu code UI gọi lại EGL
    if (!in_hook) {
        in_hook = true;

        // FPS từ delta time giữa 2 frame, làm mượt bằng EMA (không cấp phát, không khóa)
        thread_local Clock::time_point last{};
        const auto now = Clock::now();
        if (last.time_since_epoch().count() != 0) {
            const float dt = std::chrono::duration<float>(now - last).count();
            if (dt > 0.f) {
                const float inst = 1.f / dt;
                const float prev = g_fps.load(std::memory_order_relaxed);
                g_fps.store(prev == 0.f ? inst : prev + 0.1f * (inst - prev), std::memory_order_relaxed);
            }
        }
        last = now;

        client_texcache_flush_gl();
        render_client_ui();  // ImGui NewFrame → vẽ → RenderDrawData (context GL đang current ở thread này)

        in_hook = false;
    }
    return orig_eglSwapBuffers(dpy, surface);
}

// ───────────────────────── Hook 2: AInputQueue_getEvent ─────────────────────────
typedef int32_t (*AInputQueue_getEvent_t)(AInputQueue* queue, AInputEvent** out_event);
AInputQueue_getEvent_t orig_AInputQueue_getEvent = nullptr;

static uint32_t key_bit(int32_t k) {
    switch (k) {
        case AKEYCODE_W: return 1;  case AKEYCODE_A: return 2;
        case AKEYCODE_S: return 4;  case AKEYCODE_D: return 8;
        case AKEYCODE_SPACE: return 16;
        default: return 0;
    }
}

int32_t hook_AInputQueue_getEvent(AInputQueue* queue, AInputEvent** out_event) {
    static CpsCounter cps;  // chỉ một thread đọc queue nên không cần khóa

    for (;;) {  // lặp để bỏ qua event bị tiêu thụ và lấy event kế tiếp
        const int32_t ret = orig_AInputQueue_getEvent(queue, out_event);
        if (ret < 0 || !out_event || !*out_event) return ret;

        AInputEvent* ev = *out_event;
        bool consumed = false;

        switch (AInputEvent_getType(ev)) {
            case AINPUT_EVENT_TYPE_MOTION: {
                const int32_t action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
                const float x = AMotionEvent_getX(ev, 0), y = AMotionEvent_getY(ev, 0);
                const bool on_menu = point_in_menu(x, y);

                if (action == AMOTION_EVENT_ACTION_DOWN && !g_menu_open.load(std::memory_order_relaxed) &&
                    x < kToggleZonePx && y < kToggleZonePx) {
                    g_menu_open.store(true, std::memory_order_relaxed);   // chạm góc → mở menu
                    consumed = true;
                    break;
                }
                if (action == AMOTION_EVENT_ACTION_DOWN && !on_menu)
                    cps.on_click(AMotionEvent_getEventTime(ev));

                if (g_menu_open.load(std::memory_order_relaxed)) {
                    g_input_ring.push({action, 0, x, y});
                    consumed = on_menu;  // chạm vào menu → nuốt; ngoài menu vẫn xuống game
                }
                break;
            }
            case AINPUT_EVENT_TYPE_KEY: {
                const int32_t code = AKeyEvent_getKeyCode(ev);
                const int32_t act  = AKeyEvent_getAction(ev);
                if (act == AKEY_EVENT_ACTION_DOWN && code == AKEYCODE_INSERT)
                    g_menu_open.store(!g_menu_open.load(), std::memory_order_relaxed);
                if (const uint32_t b = key_bit(code)) {
                    if (act == AKEY_EVENT_ACTION_DOWN) g_keys_down.fetch_or(b, std::memory_order_relaxed);
                    else if (act == AKEY_EVENT_ACTION_UP) g_keys_down.fetch_and(~b, std::memory_order_relaxed);
                }
                if (g_menu_open.load(std::memory_order_relaxed)) {
                    g_input_ring.push({act, code, 0, 0});
                    consumed = true;  // menu mở → phím không xuống game (tránh nhân vật di chuyển)
                }
                break;
            }
            default: break;
        }

        if (!consumed) return ret;
        AInputQueue_finishEvent(queue, ev, /*handled=*/1);  // bắt buộc, nếu không queue kẹt
    }
}

// ───────────────────────── Khởi tạo hook ─────────────────────────
static bool hook_one(const char* lib, const char* sym, void* repl, void** orig) {
    void* addr = DobbySymbolResolver(lib, sym);
    if (!addr) { LOGE("Không resolve được %s!%s", lib, sym); return false; }
    if (DobbyHook(addr, repl, orig) != 0) { LOGE("DobbyHook thất bại: %s", sym); return false; }
    LOGI("Hooked %s!%s @ %p", lib, sym, addr);
    return true;
}

}  // namespace client

bool init_system_hooks() {
    static std::once_flag once;
    static std::atomic<bool> ok{false};
    std::call_once(once, [] {
        using namespace client;
        const bool a = hook_one("libEGL.so", "eglSwapBuffers",
                                reinterpret_cast<void*>(hook_eglSwapBuffers),
                                reinterpret_cast<void**>(&orig_eglSwapBuffers));
        const bool b = hook_one("libandroid.so", "AInputQueue_getEvent",
                                reinterpret_cast<void*>(hook_AInputQueue_getEvent),
                                reinterpret_cast<void**>(&orig_AInputQueue_getEvent));
        if (a) gi::g_hook_status.fetch_or(gi::kHookEgl);
        if (b) gi::g_hook_status.fetch_or(gi::kHookInput);
        ok.store(a && b);
    });
    return ok.load();
}

// Launcher gọi JNI_OnLoad thủ công sau khi dlopen từ memfd
extern "C" void gi_init(JavaVM*);  // game_internals.cpp

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    if (!init_system_hooks()) LOGE("init_system_hooks thất bại");
    gi_init(vm);
    return JNI_VERSION_1_6;
}
