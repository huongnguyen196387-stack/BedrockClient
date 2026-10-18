// module_manager.hpp — quản lý bật/tắt module + lưu cấu hình
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ClientModule {
public:
    // on_change trả về false nếu không áp dụng được (vd. signature chưa resolve) → trạng thái giữ nguyên
    ClientModule(std::string name, std::string category, std::function<bool(bool)> on_change = {})
        : name_(std::move(name)), category_(std::move(category)), on_change_(std::move(on_change)) {}

    bool is_enabled() const { return enabled_.load(std::memory_order_relaxed); }
    bool set_enabled(bool state) {
        if (state == is_enabled()) return true;
        if (on_change_ && !on_change_(state)) return false;
        enabled_.store(state, std::memory_order_relaxed);
        return true;
    }
    const std::string& name() const { return name_; }
    const std::string& category() const { return category_; }

private:
    std::string name_, category_;
    std::function<bool(bool)> on_change_;
    std::atomic<bool> enabled_{false};
};

class ModuleManager {
public:
    static ModuleManager& instance();
    void init_modules();                                  // idempotent
    void set_config_dir(const std::string& dir);          // đặt thư mục và nạp cấu hình
    void save() const;
    const std::vector<std::unique_ptr<ClientModule>>& modules() const { return modules_; }

    // Con trỏ cache để UI không phải tra cứu theo tên mỗi frame
    ClientModule *fps = nullptr, *cps = nullptr, *keystrokes = nullptr;

private:
    ClientModule* add(const char* name, const char* cat, std::function<bool(bool)> cb = {});
    void load();
    std::vector<std::unique_ptr<ClientModule>> modules_;
    std::string cfg_path_;
};
