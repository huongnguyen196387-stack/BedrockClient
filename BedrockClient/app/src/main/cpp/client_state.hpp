// client_state.hpp — trạng thái dùng chung giữa input hook (producer) và render thread (consumer)
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace client {

constexpr float kToggleZonePx = 90.f;   // chạm góc trên-trái (90x90 px) để mở menu; phím INSERT cũng được

using Clock = std::chrono::steady_clock;

// ───────────────────────── Trạng thái dùng chung (lock-free) ─────────────────────────
inline std::atomic<float>    g_fps{0.f};
inline std::atomic<float>    g_cps{0.f};
inline std::atomic<bool>     g_menu_open{false};
inline std::atomic<uint32_t> g_keys_down{0};        // bit: W=1 A=2 S=4 D=8 SPACE=16
// Vùng menu (px) do render thread ghi, input thread đọc. Mỗi trường là atomic riêng; sai lệch 1 frame vô hại.
inline std::atomic<float> g_menu_x0{0}, g_menu_y0{0}, g_menu_x1{0}, g_menu_y1{0};

// Hàng đợi SPSC: input thread (producer) → render thread (consumer). ImGui chỉ được đụng ở render thread.
struct InputEvt { int32_t action; int32_t key; float x, y; };

template <size_t N>
class SpscRing {
    static_assert((N & (N - 1)) == 0, "N phải là lũy thừa của 2");
public:
    bool push(const InputEvt& e) {
        auto h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) == N) return false;  // đầy → bỏ, không chặn
        buf_[h & (N - 1)] = e;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(InputEvt& e) {
        auto t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        e = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<InputEvt, N> buf_{};
    std::atomic<size_t> head_{0}, tail_{0};
};
inline SpscRing<256> g_input_ring;

/** Render thread gọi trong render_client_ui(): xả hàng đợi vào ImGui IO. */
template <class F> inline void drain_input(F&& feed) {
    InputEvt e;
    while (g_input_ring.pop(e)) feed(e);
}

// CPS: cửa sổ trượt 1 giây, ghi từ input thread duy nhất của AInputQueue.
class CpsCounter {
    static constexpr size_t kCap = 64;
public:
    void on_click(int64_t now_ns) {
        stamps_[idx_++ & (kCap - 1)] = now_ns;
        int n = 0;
        for (auto s : stamps_) if (s != 0 && now_ns - s <= 1'000'000'000LL) ++n;
        g_cps.store(static_cast<float>(n), std::memory_order_relaxed);
    }
private:
    std::array<int64_t, kCap> stamps_{};
    size_t idx_ = 0;
};

inline bool point_in_menu(float x, float y) {
    return g_menu_open.load(std::memory_order_relaxed) &&
           x >= g_menu_x0.load(std::memory_order_relaxed) && x <= g_menu_x1.load(std::memory_order_relaxed) &&
           y >= g_menu_y0.load(std::memory_order_relaxed) && y <= g_menu_y1.load(std::memory_order_relaxed);
}

}  // namespace client
