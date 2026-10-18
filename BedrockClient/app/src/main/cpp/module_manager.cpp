#include "module_manager.hpp"

#include <android/log.h>

#include <fstream>

#include "game_internals.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "modules", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "modules", __VA_ARGS__)

ModuleManager& ModuleManager::instance() {
    static ModuleManager inst;
    return inst;
}

ClientModule* ModuleManager::add(const char* name, const char* cat, std::function<bool(bool)> cb) {
    modules_.push_back(std::make_unique<ClientModule>(name, cat, std::move(cb)));
    return modules_.back().get();
}

void ModuleManager::init_modules() {
    if (!modules_.empty()) return;
    fps        = add("FPS", "HUD");
    cps        = add("CPS", "HUD");
    keystrokes = add("Keystrokes", "HUD");
    add("Zoom", "Visual", [](bool on) { gi::g_zoom_on.store(on); return true; });
    add("Free Third Person", "Visual", [](bool on) { gi::g_force_third_person.store(on); return true; });
    add("Fullbright", "Visual", [](bool on) {
        const bool ok = gi::toggle_fullbright(on);
        if (!ok) LOGW("Fullbright: không áp dụng được (chưa resolve signature hoặc mprotect lỗi)");
        return ok;
    });
    add("Hitboxes", "Visual", [](bool on) { gi::g_hitbox_on.store(on); return true; });
    fps->set_enabled(true);
    cps->set_enabled(true);
    keystrokes->set_enabled(true);
}

void ModuleManager::set_config_dir(const std::string& dir) {
    cfg_path_ = dir + "/client_modules.cfg";
    load();
}

void ModuleManager::load() {
    std::ifstream in(cfg_path_);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const bool val = line.substr(eq + 1) == "1";
        for (auto& m : modules_) if (m->name() == key) m->set_enabled(val);
    }
    LOGI("Đã nạp cấu hình: %s", cfg_path_.c_str());
}

void ModuleManager::save() const {
    if (cfg_path_.empty()) return;
    std::ofstream out(cfg_path_, std::ios::trunc);
    for (const auto& m : modules_) out << m->name() << '=' << (m->is_enabled() ? 1 : 0) << '\n';
}
