// client_ui.cpp — HUD (FPS/CPS/Keystrokes) + menu ImGui, chạy trong render thread (hook eglSwapBuffers)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "backends/imgui_impl_opengl3.h"
#include "client_state.hpp"
#include "game_internals.hpp"
#include "imgui.h"
#include "module_manager.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ui", __VA_ARGS__)

namespace {

bool g_inited = false;
EGLContext g_ctx = EGL_NO_CONTEXT;
std::chrono::steady_clock::time_point g_last{};

void ensure_backend(EGLContext ctx, float scale) {
    if (!g_inited) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;                          // không ghi imgui.ini
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(scale);
        io.FontGlobalScale = scale;
        g_inited = true;
    } else {
        ImGui_ImplOpenGL3_Shutdown();                      // context GL đổi (vd. app resume) → tạo lại GPU object
    }
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_ctx = ctx;
    LOGI("ImGui backend sẵn sàng (ctx=%p, scale=%.2f)", ctx, scale);
}

void feed_input(ImGuiIO& io) {
    client::drain_input([&](const client::InputEvt& e) {
        if (e.key != 0) return;                            // phím: chưa map vào ImGui (menu chỉ dùng cảm ứng)
        switch (e.action) {                                // AMOTION_EVENT_ACTION_*: DOWN=0 UP=1 MOVE=2 CANCEL=3
            case 0: io.AddMousePosEvent(e.x, e.y); io.AddMouseButtonEvent(0, true); break;
            case 2: io.AddMousePosEvent(e.x, e.y); break;
            case 1: case 3:
                io.AddMousePosEvent(e.x, e.y); io.AddMouseButtonEvent(0, false);
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);   // bỏ hover sau khi nhả tay
                break;
            default: break;
        }
    });
}

void draw_hud(float h, float s) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    auto& mm = ModuleManager::instance();
    const float x = client::kToggleZonePx + 12.f;
    float y = 10.f * s;
    char buf[32];

    // Vùng chạm mở menu (gợi ý trực quan)
    if (!client::g_menu_open.load(std::memory_order_relaxed)) {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(client::kToggleZonePx, client::kToggleZonePx / 2), IM_COL32(0, 0, 0, 90), 8.f);
        dl->AddText(ImVec2(10, 10), IM_COL32(255, 255, 255, 200), "MENU");
    }
    if (mm.fps && mm.fps->is_enabled()) {
        snprintf(buf, sizeof buf, "FPS: %.0f", client::g_fps.load(std::memory_order_relaxed));
        dl->AddText(ImVec2(x, y), IM_COL32(255, 255, 255, 230), buf);
        y += ImGui::GetFontSize() * 1.3f;
    }
    if (mm.cps && mm.cps->is_enabled()) {
        snprintf(buf, sizeof buf, "CPS: %.0f", client::g_cps.load(std::memory_order_relaxed));
        dl->AddText(ImVec2(x, y), IM_COL32(255, 255, 255, 230), buf);
    }
    if (mm.keystrokes && mm.keystrokes->is_enabled()) {    // chỉ nhận phím từ bàn phím ngoại vi
        const uint32_t k = client::g_keys_down.load(std::memory_order_relaxed);
        const float b = 46.f * s, g = 4.f * s, bx = 20.f * s, by = h - 3.f * b - 4.f * g - 20.f * s;
        auto key = [&](const char* t, float cx, float cy, float w, bool down) {
            dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + w, cy + b), down ? IM_COL32(255, 255, 255, 200) : IM_COL32(0, 0, 0, 110), 6.f);
            const ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(cx + (w - ts.x) / 2, cy + (b - ts.y) / 2), down ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 230), t);
        };
        key("W", bx + b + g, by, b, k & 1);
        key("A", bx, by + b + g, b, k & 2);
        key("S", bx + b + g, by + b + g, b, k & 4);
        key("D", bx + 2 * (b + g), by + b + g, b, k & 8);
        key("SPACE", bx, by + 2 * (b + g), 3 * b + 2 * g, k & 16);
    }
}

void draw_menu(float w, float h) {
    auto& mm = ModuleManager::instance();
    ImGui::SetNextWindowSize(ImVec2(w * 0.55f, h * 0.8f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Bedrock Client", nullptr, ImGuiWindowFlags_NoCollapse);

    const ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();   // báo vùng menu cho input hook
    client::g_menu_x0.store(p.x); client::g_menu_y0.store(p.y);
    client::g_menu_x1.store(p.x + sz.x); client::g_menu_y1.store(p.y + sz.y);

    const char* cat = nullptr;
    for (auto& m : mm.modules()) {
        if (!cat || strcmp(cat, m->category().c_str()) != 0) { cat = m->category().c_str(); ImGui::SeparatorText(cat); }
        bool on = m->is_enabled();
        if (ImGui::Checkbox(m->name().c_str(), &on)) m->set_enabled(on);   // thất bại → checkbox tự quay lại frame sau
    }
    ImGui::SeparatorText("FOV");
    float fov = gi::g_fov_override.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("FOV (0 = off)", &fov, 0.f, 110.f, "%.0f")) gi::g_fov_override.store(fov);

    ImGui::Spacing();
    if (ImGui::Button("Close")) { client::g_menu_open.store(false); mm.save(); }
    ImGui::End();
}

}  // namespace

// Được hook_eglSwapBuffers gọi mỗi frame (GL context của game đang current ở thread này).
void render_client_ui() {
    const EGLDisplay dpy = eglGetCurrentDisplay();
    const EGLSurface surf = eglGetCurrentSurface(EGL_DRAW);
    const EGLContext ctx = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT) return;
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    if (w < 320 || h < 240) return;                        // bỏ qua surface phụ nhỏ

    const float scale = std::clamp(static_cast<float>(h) / 540.f, 1.f, 4.f);
    if (!g_inited || ctx != g_ctx) ensure_backend(ctx, scale);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = g_last.time_since_epoch().count() ? std::clamp(std::chrono::duration<float>(now - g_last).count(), 0.001f, 0.1f) : 1.f / 60.f;
    g_last = now;

    feed_input(io);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    draw_hud(static_cast<float>(h), scale);
    if (client::g_menu_open.load(std::memory_order_relaxed)) draw_menu(static_cast<float>(w), static_cast<float>(h));
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
