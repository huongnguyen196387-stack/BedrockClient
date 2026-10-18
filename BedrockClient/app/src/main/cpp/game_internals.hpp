// game_internals.hpp — công tắc module và API công khai của game_internals.cpp
#pragma once
#include <atomic>
#include <cstdint>

namespace gi {
inline std::atomic<bool>  g_zoom_on{false};
inline std::atomic<float> g_fov_override{0.f};          // 0 = tắt
inline std::atomic<bool>  g_force_third_person{false};
inline std::atomic<int>   g_PerspectiveState{0};
inline std::atomic<bool>  g_hitbox_on{false};

constexpr float kZoomFov   = 15.0f;
constexpr float kHandFovLo = 60.0f;                     // heuristic theo mô tả, chưa kiểm chứng
constexpr float kHandFovHi = 70.0f;

// Trạng thái hook (bit) — Kotlin đọc qua ClientCallbacks.getHookStatus()
enum : uint32_t {
    kHookEgl = 1u << 0, kHookInput = 1u << 1, kHookFov = 1u << 2, kHookGamma = 1u << 3,
    kHookPerspective = 1u << 4, kHookRenderLevel = 1u << 5, kHookGL = 1u << 6, kHookDone = 1u << 7,
};
inline std::atomic<uint32_t> g_hook_status{0};

uintptr_t find_signature(const char* lib_name, const char* sig_pattern);
bool toggle_fullbright(bool enable);
}  // namespace gi
