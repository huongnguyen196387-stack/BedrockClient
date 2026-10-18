// Test trên host Linux: nhúng trực tiếp game_internals.cpp để gọi cả hàm static
#include "game_internals.cpp"
#include "client_state.hpp"
#include <cassert>
#include <cstdarg>
#include <dlfcn.h>
#include <vector>

// ---- stub phần Android ----
int __android_log_print(int, const char*, const char* f, ...) { return 0; }
static std::vector<GLuint> g_deleted;
void glDeleteTextures(GLsizei n, const GLuint* ids) { for (int i = 0; i < n; ++i) g_deleted.push_back(ids[i]); }
int DobbyHook(void*, void*, void**) { return 0; }
void* DobbySymbolResolver(const char*, const char*) { return nullptr; }
const char* JNIEnv::GetStringUTFChars(jstring, jboolean*) { return nullptr; }
void JNIEnv::ReleaseStringUTFChars(jstring, const char*) {}
jclass JNIEnv::FindClass(const char*) { return nullptr; }
void JNIEnv::ExceptionClear() {}
jint JNIEnv::RegisterNatives(jclass, const JNINativeMethod*, jint) { return 0; }
jint JavaVM::GetEnv(void**, jint) { return 0; }

static std::string hex_of(const uint8_t* p, int n, int wild_every = 0, int wild_run = 0) {
    std::string s; char b[8];
    for (int i = 0; i < n; ++i) {
        if (wild_every && (i % wild_every) < wild_run) s += "? ";
        else { snprintf(b, sizeof b, "%02X ", p[i]); s += b; }
    }
    return s;
}

int main() {
    // 1) Scanner: lấy 32 byte đầu của một hàm trong libc làm signature
    void* fn = dlsym(RTLD_DEFAULT, "getpid");
    const uint8_t* code = (const uint8_t*)fn;
    std::string exact = hex_of(code, 32);
    uintptr_t r = gi::find_signature("libc.so.6", exact.c_str());
    printf("exact   -> %p (kỳ vọng %p)\n", (void*)r, fn);
    assert(r == (uintptr_t)fn);

    // wildcard xen kẽ: 3 byte '?' mỗi 8 byte, vẫn phải khớp đúng 1 chỗ
    std::string wild = hex_of(code, 32, 8, 3);
    uintptr_t r2 = gi::find_signature("libc.so.6", wild.c_str());
    printf("wildcard-> %p\n", (void*)r2);
    assert(r2 == (uintptr_t)fn);

    // quá chung (nhiều kết quả) → phải trả 0
    uintptr_t r3 = gi::find_signature("libc.so.6", "? ? ? ? ? ? ? C3");
    printf("ambiguous-> %p (kỳ vọng 0)\n", (void*)r3);
    assert(r3 == 0);

    // không tồn tại / lib sai / pattern sai định dạng
    assert(gi::find_signature("libc.so.6", "DE AD BE EF 12 34 56 78 9A BC DE F0 11 22 33 44") == 0);
    assert(gi::find_signature("libkhongco.so", exact.c_str()) == 0);
    assert(gi::find_signature("libc.so.6", "ZZ 11 22 33 44 55 66 77") == 0);
    assert(gi::find_signature("libc.so.6", "11 22 33") == 0);  // quá ngắn
    puts("scanner OK");

    // 2) Fullbright patch/restore trên trang RX ẩn danh
    const long pg = sysconf(_SC_PAGESIZE);
    uint8_t* page = (uint8_t*)mmap(nullptr, pg, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    // ghi dữ liệu gốc: đổi quyền tạm để chuẩn bị
    mprotect(page, pg, PROT_READ | PROT_WRITE);
    for (int i = 0; i < 12; ++i) page[i] = 0xA0 + i;
    mprotect(page, pg, PROT_READ | PROT_EXEC);
    uint8_t orig[12]; memcpy(orig, page, 12);

    gi::g_gamma_fn = (uintptr_t)page;
    assert(gi::toggle_fullbright(true));
    assert(memcmp(page, gi::kFullbrightPatch, 12) == 0);
    assert(gi::toggle_fullbright(true));                 // idempotent
    assert(gi::toggle_fullbright(false));
    assert(memcmp(page, orig, 12) == 0);
    gi::g_gamma_fn = 0;
    assert(!gi::toggle_fullbright(true));                // chưa resolve → false
    puts("fullbright patch OK");

    // 3) LRU texture cache
    gi::TexCache c;
    for (GLuint id = 1; id <= 5; ++id) c.on_create(id, 100, true);
    c.on_create(99, 100, false);                         // texture game: không bao giờ bị evict
    for (int f = 0; f < 5; ++f) c.evict(UINT64_MAX, 0);  // tăng frame_
    c.touch(1); c.touch(2);                              // 1,2 mới dùng
    for (int f = 0; f < 3; ++f) c.evict(UINT64_MAX, 0);
    g_deleted.clear();
    c.evict(300, 8);                                     // tổng 600 > 300 → xóa ≥3 cái cũ nhất, không đụng 99
    printf("evicted:"); for (auto id : g_deleted) printf(" %u", id); printf("\n");
    for (auto id : g_deleted) assert(id != 99);
    assert(!g_deleted.empty() && c.total_bytes() <= 300 + 0);
    // 1,2 được touch nên phải được giữ lại lâu hơn 3,4,5
    bool has12 = false; for (auto id : g_deleted) if (id == 1 || id == 2) has12 = true;
    assert(!has12 || g_deleted.size() >= 4);
    puts("LRU OK");

    // 4) SPSC ring
    client::SpscRing<8> ring; client::InputEvt e{1, 2, 3.f, 4.f};
    for (int i = 0; i < 8; ++i) assert(ring.push(e));
    assert(!ring.push(e));                               // đầy → từ chối, không chặn
    client::InputEvt o; int n = 0; while (ring.pop(o)) { assert(o.key == 2); ++n; }
    assert(n == 8 && !ring.pop(o));
    puts("ring OK");

    // 5) CPS
    client::CpsCounter cps;
    for (int i = 0; i < 7; ++i) cps.on_click(1'000'000'000LL + i * 100'000'000LL);
    assert(client::g_cps.load() == 7.f);
    cps.on_click(3'000'000'000LL);                       // các click cũ rơi ngoài cửa sổ 1s
    assert(client::g_cps.load() == 1.f);
    puts("cps OK");
    puts("ALL PASS");
}
