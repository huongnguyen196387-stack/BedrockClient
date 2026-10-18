// game_internals.cpp — module của libclient.so (ARM64, C++17)
// CMake: add_library(client SHARED client_hooks.cpp game_internals.cpp)
//        target_link_libraries(client dobby android EGL GLESv3 log dl)
//
// GHI CHÚ QUAN TRỌNG:
//  - Các signature/offset (getFov, gamma, getPerspective, RenderLevel, actor+0x208...) do bạn cung cấp;
//    mình KHÔNG xác minh được cho 1.26.50. Scanner sẽ từ chối nếu không khớp duy nhất.
//  - ActorFetchNearbyActorsSorted và Tessellator* KHÔNG có signature/prototype trong yêu cầu → để dạng
//    adapter (weak) bên dưới, bạn điền theo disassembly của chính bạn. Không bịa prototype.
#include <GLES3/gl3.h>
#include <android/log.h>
#include <fcntl.h>
#include <jni.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "dobby.h"
#include "game_internals.hpp"
#include "module_manager.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "gi", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "gi", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gi", __VA_ARGS__)

namespace gi {

// ═════════════════════════ 1. SIGNATURE SCANNER ═════════════════════════
struct Pattern { uint8_t b[256]; uint8_t m[256]; int len = 0; };   // m=1: so khớp, m=0: wildcard
struct Seg { uintptr_t begin, end; };

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_pattern(const char* s, Pattern& p) {
    p.len = 0;
    while (*s) {
        while (*s == ' ') ++s;
        if (!*s) break;
        if (p.len >= 256) return false;
        if (*s == '?') {                                  // "?" hoặc "??"
            p.b[p.len] = 0; p.m[p.len++] = 0;
            while (*s == '?') ++s;
        } else {
            const int h = hexv(s[0]), l = hexv(s[1]);
            if (h < 0 || l < 0) return false;
            p.b[p.len] = static_cast<uint8_t>(h << 4 | l); p.m[p.len++] = 1;
            s += 2;
        }
        if (*s && *s != ' ') return false;
    }
    return p.len >= 8 && p.m[p.len - 1] == 1;            // byte cuối phải cụ thể để BMH hoạt động
}

/** Đọc /proc/self/maps (không cấp phát heap), gom mọi vùng r-xp của lib. */
static int collect_exec_segments(const char* lib, Seg* out, int cap) {
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) { LOGE("open maps: %s", strerror(errno)); return 0; }
    char chunk[4096], line[640];
    size_t ll = 0; int n = 0; ssize_t r;
    const size_t libn = strlen(lib);
    auto handle = [&]() {
        line[ll] = 0;
        char* p = line; char* e;
        const unsigned long long a = strtoull(p, &e, 16);
        if (*e != '-') return;
        const unsigned long long b = strtoull(e + 1, &e, 16);
        while (*e == ' ') ++e;
        if (strncmp(e, "r-xp", 4) != 0) return;
        const char* slash = strrchr(line, '/');
        if (!slash || strlen(slash + 1) != libn || strcmp(slash + 1, lib) != 0) return;
        if (n < cap) out[n++] = {static_cast<uintptr_t>(a), static_cast<uintptr_t>(b)};
    };
    while ((r = read(fd, chunk, sizeof chunk)) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (chunk[i] == '\n') { handle(); ll = 0; }
            else if (ll < sizeof line - 1) line[ll++] = chunk[i];
        }
    }
    close(fd);
    return n;
}

/**
 * Trả về địa chỉ khớp DUY NHẤT (căn 4 byte, đúng ARM64) hoặc 0 nếu: không thấy / nhiều hơn 1 kết quả / lỗi.
 * Horspool có wildcard: độ dịch mặc định bị chặn bởi vị trí wildcard cuối (trước byte cuối).
 */
uintptr_t find_signature(const char* lib_name, const char* sig_pattern) {
    Pattern p;
    if (!parse_pattern(sig_pattern, p)) { LOGE("Signature sai định dạng: %s", sig_pattern); return 0; }
    Seg segs[16];
    const int ns = collect_exec_segments(lib_name, segs, 16);
    if (ns == 0) return 0;                                // lib chưa được nạp

    const int m = p.len;
    int last_wild = -1;
    for (int i = 0; i < m - 1; ++i) if (!p.m[i]) last_wild = i;
    const int def_shift = last_wild >= 0 ? m - 1 - last_wild : m;
    uint8_t shift[256];
    for (int c = 0; c < 256; ++c) shift[c] = static_cast<uint8_t>(def_shift > 255 ? 255 : def_shift);
    for (int j = last_wild + 1; j < m - 1; ++j) {
        const int s = m - 1 - j;
        shift[p.b[j]] = static_cast<uint8_t>(s > 255 ? 255 : s);
    }

    uintptr_t found = 0; int count = 0;
    for (int si = 0; si < ns; ++si) {
        const auto* base = reinterpret_cast<const uint8_t*>(segs[si].begin);
        const size_t size = segs[si].end - segs[si].begin;
        size_t pos = 0;
        while (pos + m <= size) {
            const uint8_t* w = base + pos;
            int k = m - 1;
            while (k >= 0 && (!p.m[k] || w[k] == p.b[k])) --k;
            if (k < 0) {
                const uintptr_t addr = reinterpret_cast<uintptr_t>(w);
                if ((addr & 3) == 0) {
                    if (++count > 1) { LOGW("Signature KHÔNG duy nhất (>=2 kết quả): %.40s...", sig_pattern); return 0; }
                    found = addr;
                }
                pos += 1;                                  // tiếp tục để phát hiện trùng lặp chồng lấn
            } else {
                pos += shift[w[m - 1]];
            }
        }
    }
    if (count == 1) LOGI("Signature OK @ %p (%s+0x%zx)", (void*)found, lib_name, (size_t)(found - segs[0].begin));
    else LOGW("Signature không tìm thấy: %.40s...", sig_pattern);
    return found;
}

// ═════════════════════════ Patch bộ nhớ an toàn ═════════════════════════
static bool patch_code(uintptr_t addr, const uint8_t* src, size_t n) {
    const uintptr_t page = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    const uintptr_t start = addr & ~(page - 1);
    const size_t len = ((addr + n + page - 1) & ~(page - 1)) - start;
    int prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    if (mprotect(reinterpret_cast<void*>(start), len, prot) != 0) {
        LOGW("mprotect RWX lỗi (%s), thử RW", strerror(errno));
        prot = PROT_READ | PROT_WRITE;
        if (mprotect(reinterpret_cast<void*>(start), len, prot) != 0) {
            LOGE("mprotect RW lỗi: %s", strerror(errno));
            return false;
        }
    }
    memcpy(reinterpret_cast<void*>(addr), src, n);
    __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(addr + n));
    if (mprotect(reinterpret_cast<void*>(start), len, PROT_READ | PROT_EXEC) != 0)
        LOGW("Không khôi phục được r-x: %s", strerror(errno));
    return true;
}

// ═════════════════════════ 2B. Fullbright (memory patch, không dùng Dobby) ═════════════════════════
static uintptr_t g_gamma_fn = 0;
static uint8_t   g_gamma_orig[12];
static std::atomic<bool> g_fb_on{false};

static constexpr uint8_t kFullbrightPatch[12] = {
    0x40, 0x8F, 0xA8, 0x52,   // movz w0, #0x447A, lsl #16   (0x447A0000 = 1000.0f)
    0x00, 0x00, 0x27, 0x1E,   // fmov s0, w0
    0xC0, 0x03, 0x5F, 0xD6    // ret
};

bool toggle_fullbright(bool enable) {
    if (!g_gamma_fn) { LOGW("Fullbright: chưa resolve được hàm gamma"); return false; }
    if (enable == g_fb_on.load()) return true;
    if (enable) {
        memcpy(g_gamma_orig, reinterpret_cast<void*>(g_gamma_fn), 12);   // sao lưu 12 byte gốc
        if (!patch_code(g_gamma_fn, kFullbrightPatch, 12)) return false;
    } else {
        if (!patch_code(g_gamma_fn, g_gamma_orig, 12)) return false;
    }
    g_fb_on.store(enable);
    LOGI("Fullbright %s", enable ? "ON" : "OFF");
    return true;
}

// ═════════════════════════ 2A. FOV / Zoom ═════════════════════════
using getFov_t = float (*)(void* self, float a, int enableVariableFOV);
static getFov_t orig_getFov = nullptr;

static float hook_getFov(void* self, float a, int enableVariableFOV) {
    const float r = orig_getFov(self, a, enableVariableFOV);          // float trả về qua s0
    if (r >= kHandFovLo && r <= kHandFovHi) return r;                 // giữ nguyên theo heuristic bạn mô tả
    if (g_zoom_on.load(std::memory_order_relaxed)) return kZoomFov;
    const float o = g_fov_override.load(std::memory_order_relaxed);
    return o > 0.f ? o : r;
}

// ═════════════════════════ 2C. Perspective ═════════════════════════
using getPerspective_t = int (*)(void* self);
static getPerspective_t orig_getPerspective = nullptr;

static int hook_getPerspective(void* self) {
    const int r = orig_getPerspective(self);
    g_PerspectiveState.store(r, std::memory_order_relaxed);
    return g_force_third_person.load(std::memory_order_relaxed) ? 1 : r;
}

// ═════════════════════════ 2D. Hitbox overlay ═════════════════════════
struct Vec3 { float x, y, z; };
struct AABB { Vec3 min, max; };

// ---- Adapter: điền theo reverse của bạn (weak stub để vẫn link được) ----
// Điền tối đa `cap` con trỏ Actor lân cận (không cấp phát heap trong hàm này nếu bạn tự kiểm soát được).
__attribute__((weak)) size_t game_fetch_nearby_actors(uintptr_t* out, size_t cap) { (void)out; (void)cap; return 0; }
__attribute__((weak)) Vec3   game_camera_origin()                                  { return {0, 0, 0}; }  // Tessellator thường theo toạ độ tương đối camera
__attribute__((weak)) void   tess_begin_lines()                                    {}
__attribute__((weak)) void   tess_color(float, float, float, float)                {}
__attribute__((weak)) void   tess_vertex(float, float, float)                      {}
__attribute__((weak)) void   tess_end()                                            {}

static bool safe_read(uintptr_t src, void* dst, size_t n) {          // không SIGSEGV nếu con trỏ rác
    if (src < 0x10000 || src >= (1ULL << 48)) return false;
    iovec l{dst, n}, r{reinterpret_cast<void*>(src), n};
    return process_vm_readv(getpid(), &l, 1, &r, 1, 0) == static_cast<ssize_t>(n);
}

static bool read_actor_aabb(uintptr_t actor, AABB& out) {
    uintptr_t state_vec = 0, comp = 0;
    if (!safe_read(actor + 0x208, &state_vec, 8)) return false;      // offset theo bạn cung cấp
    if (!safe_read(state_vec + 0x8, &comp, 8))    return false;
    if (!safe_read(comp + 0x0, &out, sizeof out)) return false;
    const float* f = &out.min.x;
    for (int i = 0; i < 6; ++i) if (!std::isfinite(f[i])) return false;
    const float dx = out.max.x - out.min.x, dy = out.max.y - out.min.y, dz = out.max.z - out.min.z;
    return dx > 0 && dy > 0 && dz > 0 && dx < 16 && dy < 16 && dz < 16;   // loại dữ liệu rác
}

static void draw_box(const AABB& b, const Vec3& o) {
    const float x0 = b.min.x - o.x, y0 = b.min.y - o.y, z0 = b.min.z - o.z;
    const float x1 = b.max.x - o.x, y1 = b.max.y - o.y, z1 = b.max.z - o.z;
    const float c[8][3] = {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},{x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}};
    static constexpr uint8_t e[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& ed : e) {
        tess_vertex(c[ed[0]][0], c[ed[0]][1], c[ed[0]][2]);
        tess_vertex(c[ed[1]][0], c[ed[1]][1], c[ed[1]][2]);
    }
}

// Prototype RenderLevel CHƯA XÁC MINH — kiểm tra trong IDA/Ghidra. Nếu có tham số float (v-reg), khai báo thêm cho đúng.
using RenderLevel_t = void (*)(void* a0, void* a1, void* a2, void* a3);
static RenderLevel_t orig_RenderLevel = nullptr;
constexpr size_t kMaxActors = 32;

static void hook_RenderLevel(void* a0, void* a1, void* a2, void* a3) {
    orig_RenderLevel(a0, a1, a2, a3);
    if (!g_hitbox_on.load(std::memory_order_relaxed)) return;
    thread_local bool busy = false;
    if (busy) return;
    busy = true;

    uintptr_t actors[kMaxActors];                                    // stack, không heap
    const size_t n = game_fetch_nearby_actors(actors, kMaxActors);
    if (n) {
        const Vec3 origin = game_camera_origin();
        // Không đụng vào depth/blend state: khung hộp bị địa hình che như hitbox debug của vanilla.
        tess_begin_lines();
        tess_color(1.f, 1.f, 1.f, 0.9f);
        for (size_t i = 0; i < n; ++i) {
            AABB box;
            if (read_actor_aabb(actors[i], box)) draw_box(box, origin);
        }
        tess_end();
    }
    busy = false;
}

// ═════════════════════════ 3. LRU TEXTURE CACHE (GL) ═════════════════════════
struct TexEntry { GLuint id = 0; uint32_t bytes = 0; uint64_t last_frame = 0; bool evictable = false; };
constexpr size_t kTexSlots = 4096;                                   // lũy thừa của 2
constexpr bool   kEvictGameTextures = false;                         // xem ghi chú dưới

class SpinLock {
    std::atomic_flag f_ = ATOMIC_FLAG_INIT;
public:
    void lock()   { while (f_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { f_.clear(std::memory_order_release); }
};

class TexCache {
public:
    void on_create(GLuint id, uint32_t bytes, bool evictable) {
        std::lock_guard<SpinLock> g(lk_);
        TexEntry* e = slot(id, true);
        if (!e) return;                                              // đầy → bỏ qua, không cấp phát
        total_ += bytes; total_ -= e->bytes;
        *e = {id, bytes, frame_, evictable};
    }
    void touch(GLuint id) {
        std::lock_guard<SpinLock> g(lk_);
        if (TexEntry* e = slot(id, false)) e->last_frame = frame_;
    }
    void on_delete(GLuint id) {
        std::lock_guard<SpinLock> g(lk_);
        if (TexEntry* e = slot(id, false)) { total_ -= e->bytes; *e = {}; e->id = kTomb; }
    }
    uint64_t total_bytes() const { return total_; }

    /** Gọi ở render thread (GL context current). Xóa tối đa `max_evict` texture cũ nhất, giới hạn công việc/frame. */
    void evict(uint64_t budget, int max_evict) {
        GLuint victims[64]; int nv = 0;
        {
            std::lock_guard<SpinLock> g(lk_);
            ++frame_;
            while (total_ > budget && nv < max_evict && nv < 64) {
                TexEntry* oldest = nullptr;
                for (auto& e : tab_)
                    if (e.id && e.id != kTomb && e.evictable && e.last_frame + 2 < frame_ &&
                        (!oldest || e.last_frame < oldest->last_frame)) oldest = &e;
                if (!oldest) break;
                victims[nv++] = oldest->id;
                total_ -= oldest->bytes; *oldest = {}; oldest->id = kTomb;
            }
        }
        if (nv) { glDeleteTextures(nv, victims); LOGI("LRU: giải phóng %d texture", nv); }
    }
private:
    static constexpr GLuint kTomb = 0xFFFFFFFFu;
    TexEntry* slot(GLuint id, bool insert) {
        size_t i = (id * 2654435761u) & (kTexSlots - 1), tomb = kTexSlots;
        for (size_t n = 0; n < kTexSlots; ++n, i = (i + 1) & (kTexSlots - 1)) {
            TexEntry& e = tab_[i];
            if (e.id == id) return &e;
            if (e.id == kTomb && tomb == kTexSlots) tomb = i;
            if (e.id == 0) return insert ? &tab_[tomb != kTexSlots ? tomb : i] : nullptr;
        }
        return insert && tomb != kTexSlots ? &tab_[tomb] : nullptr;
    }
    SpinLock lk_;
    std::array<TexEntry, kTexSlots> tab_{};
    uint64_t total_ = 0, frame_ = 0;
};
inline TexCache g_tex;

// Texture do CLIENT tạo (font ImGui, icon HUD...) mới được phép evict: client tự tạo lại được.
// Texture của game thì KHÔNG: game giữ ID và không biết ta xóa → nó sẽ vẽ ra đen/crash. Chỉ bật
// kEvictGameTextures nếu bạn có cơ chế báo game nạp lại.
struct ScopedClientTexture {
    static thread_local inline bool active = false;
    ScopedClientTexture()  { active = true; }
    ~ScopedClientTexture() { active = false; }
};

static void (*orig_glBindTexture)(GLenum, GLuint) = nullptr;
static void (*orig_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
static void (*orig_glDeleteTextures)(GLsizei, const GLuint*) = nullptr;
static thread_local GLuint t_bound2d = 0;

static void hook_glBindTexture(GLenum target, GLuint tex) {
    if (target == GL_TEXTURE_2D) { t_bound2d = tex; if (tex) g_tex.touch(tex); }
    orig_glBindTexture(target, tex);
}
static void hook_glTexImage2D(GLenum t, GLint lvl, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void* px) {
    if (t == GL_TEXTURE_2D && lvl == 0 && t_bound2d)
        g_tex.on_create(t_bound2d, static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4u,
                        ScopedClientTexture::active || kEvictGameTextures);
    orig_glTexImage2D(t, lvl, ifmt, w, h, b, f, ty, px);
}
static void hook_glDeleteTextures(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) g_tex.on_delete(ids[i]);
    orig_glDeleteTextures(n, ids);
}

// Áp lực bộ nhớ: JNI ghi cờ, render thread xử lý (lệnh GL chỉ hợp lệ trên GL thread).
inline std::atomic<int> g_trim_pending{0};   // 0 không, 1 vừa, 2 nghiêm trọng

static uint64_t texture_budget() {
    static const uint64_t ram = static_cast<uint64_t>(sysconf(_SC_PHYS_PAGES)) * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    return ram < (4ull << 30) ? (160ull << 20) : (384ull << 20);     // máy <4GB: ngưỡng thấp hơn
}

}  // namespace gi

// Được client_hooks.cpp gọi trong hook_eglSwapBuffers (ghi đè bản weak).
void client_texcache_flush_gl() {
    using namespace gi;
    const int trim = g_trim_pending.exchange(0, std::memory_order_relaxed);
    if (trim == 2)      g_tex.evict(0, 64);                          // RUNNING_CRITICAL / COMPLETE: xả hết phần evict được
    else if (trim == 1) g_tex.evict(texture_budget() / 2, 32);
    else                g_tex.evict(texture_budget() * 85 / 100, 8); // bình thường: tối đa 8 texture/frame
}

// ═════════════════════════ JNI: nhận tín hiệu onTrimMemory ═════════════════════════
// Android KHÔNG có hằng "TRIM_MEMORY_RECLAIM_CRITICAL". Mức thật: RUNNING_LOW=10, RUNNING_CRITICAL=15,
// UI_HIDDEN=20, BACKGROUND=40, MODERATE=60, COMPLETE=80.
static void JNICALL native_onTrimMemory(JNIEnv*, jclass, jint level) {
    const int sev = (level == 15 || level >= 60) ? 2 : (level >= 10 ? 1 : 0);
    if (sev) gi::g_trim_pending.store(sev, std::memory_order_relaxed);
    LOGI("onTrimMemory(%d) → mức %d", level, sev);
}

// ═════════════════════════ Cài hook (sau khi libminecraftpe.so được nạp) ═════════════════════════
namespace gi {

template <class Fn>
static bool install(const char* name, const char* lib, const char* sig, void* repl, Fn* orig) {
    const uintptr_t a = find_signature(lib, sig);
    if (!a) { LOGW("Bỏ qua %s (không có địa chỉ duy nhất)", name); return false; }
    if (DobbyHook(reinterpret_cast<void*>(a), repl, reinterpret_cast<void**>(orig)) != 0) {
        LOGE("DobbyHook %s thất bại", name); return false;
    }
    LOGI("Hooked %s @ %p", name, (void*)a);
    return true;
}

static void installer_thread() {
    constexpr const char* kLib = "libminecraftpe.so";
    for (int i = 0; i < 300; ++i) {                                  // chờ tối đa ~60s cho game nạp lib
        Seg s[1];
        if (collect_exec_segments(kLib, s, 1) > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (i == 299) { LOGE("libminecraftpe.so không xuất hiện, bỏ cài hook"); return; }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));      // để linker nạp xong các segment

    if (install("getFov", kLib,
        "? ? ? FC ? ? ? 6D ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? 91 08 40 20 1E",
        (void*)hook_getFov, &orig_getFov)) g_hook_status.fetch_or(kHookFov);

    g_gamma_fn = find_signature(kLib,
        "? ? ? A9 FD 03 00 91 ? ? ? F9 ? ? ? 52 ? ? ? F9 00 01 3F D6 ? ? ? A8 ? ? ? 14 ? ? ? A9 ? ? ? F9 FD 03 00 91 "
        "? ? ? F9 F3 03 01 2A ? ? ? 52 ? ? ? F9 00 01 3F D6 E1 03 13 2A ? ? ? F9 ? ? ? A8 ? ? ? 14 ? ? ? A9 ? ? ? A9");
    if (g_gamma_fn) g_hook_status.fetch_or(kHookGamma);

    if (install("getPerspective", kLib,
        "? ? ? A9 FD 03 00 91 ? ? ? F9 ? ? ? F9 ? ? ? F9 00 01 3F D6 ? ? ? F9 ? ? ? F9 ? ? ? A8 20 00 1F D6 ? ? ? A9 FD 03 00 91",
        (void*)hook_getPerspective, &orig_getPerspective)) g_hook_status.fetch_or(kHookPerspective);

    if (install("RenderLevel", kLib,
        "? ? ? FC ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 57 D0 3B D5",
        (void*)hook_RenderLevel, &orig_RenderLevel)) g_hook_status.fetch_or(kHookRenderLevel);

    // Theo dõi texture GL (libGLESv2.so xuất cả hàm ES3)
    auto gl = [](const char* sym, void* repl, void** orig) {
        if (void* a = DobbySymbolResolver("libGLESv2.so", sym)) DobbyHook(a, repl, orig);
        else LOGW("Không resolve được %s", sym);
    };
    gl("glBindTexture",    (void*)hook_glBindTexture,    reinterpret_cast<void**>(&orig_glBindTexture));
    gl("glTexImage2D",     (void*)hook_glTexImage2D,     reinterpret_cast<void**>(&orig_glTexImage2D));
    gl("glDeleteTextures", (void*)hook_glDeleteTextures, reinterpret_cast<void**>(&orig_glDeleteTextures));
    if (orig_glBindTexture && orig_glTexImage2D && orig_glDeleteTextures) g_hook_status.fetch_or(kHookGL);
    g_hook_status.fetch_or(kHookDone);
    LOGI("game_internals sẵn sàng");
}

}  // namespace gi

static void JNICALL native_setConfigDir(JNIEnv* env, jclass, jstring dir) {
    const char* s = dir ? env->GetStringUTFChars(dir, nullptr) : nullptr;
    if (s) { ModuleManager::instance().set_config_dir(s); env->ReleaseStringUTFChars(dir, s); }
}

static jint JNICALL native_getHookStatus(JNIEnv*, jclass) {
    return static_cast<jint>(gi::g_hook_status.load(std::memory_order_relaxed));
}

/**
 * Gọi từ JNI_OnLoad của client_hooks.cpp (sau init_system_hooks()).
 * FindClass hoạt động vì JNI_OnLoad được launcher gọi từ chính thread Java đang ở trong NativeBridge.
 */
extern "C" void gi_init(JavaVM* vm) {
    ModuleManager::instance().init_modules();
    JNIEnv* env = nullptr;
    if (vm && vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env) {
        jclass c = env->FindClass("com/example/bedrocklauncher/core/ClientCallbacks");
        if (!c) { env->ExceptionClear(); LOGW("Không thấy ClientCallbacks → bỏ listener onTrimMemory"); }
        else {
            const JNINativeMethod m[] = {
                {"onTrimMemory", "(I)V", reinterpret_cast<void*>(native_onTrimMemory)},
                {"setConfigDir", "(Ljava/lang/String;)V", reinterpret_cast<void*>(native_setConfigDir)},
                {"getHookStatus", "()I", reinterpret_cast<void*>(native_getHookStatus)}};
            if (env->RegisterNatives(c, m, 3) != JNI_OK) LOGE("RegisterNatives thất bại");
        }
    }
    std::thread(gi::installer_thread).detach();
}

/* ───── Kotlin phía launcher (chuyển tiếp tín hiệu hệ thống) ─────
object ClientCallbacks { @JvmStatic external fun onTrimMemory(level: Int) }

class App : Application() {
    override fun onTrimMemory(level: Int) { super.onTrimMemory(level); ClientCallbacks.onTrimMemory(level) }
}
*/
