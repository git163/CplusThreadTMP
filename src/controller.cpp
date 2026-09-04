#include "controller.h"

#include <string>

#include "thread_log.h"

namespace {

// 设备允许的最大速度（示例业务约束）
constexpr uint32_t kMaxSpeed = 1000;

// 枚举转字符串，便于英文日志
const char* mode_str(sdk_mode_t mode) {
    switch (mode) {
        case sdk_mode_t::off:
            return "off";
        case sdk_mode_t::on:
            return "on";
        case sdk_mode_t::emergency:
            return "emergency";
    }
    return "unknown";
}

}  // namespace

controller::controller(driver_sdk& sdk) : sdk_(sdk) {}

bool controller::set_speed(uint32_t speed) {
    if (speed > kMaxSpeed) {
        common::log_line("reject set_speed out of range: " + std::to_string(speed));
        return false;
    }
    return state_guard_.with_write([&] {
        try {
            sdk_.set_speed(speed);
            common::log_line("set_speed ok: " + std::to_string(speed));
            return true;
        } catch (const std::exception& e) {
            common::log_line(std::string("set_speed fail: ") + e.what());
            return false;
        }
    });
}

bool controller::set_mode(sdk_mode_t mode) {
    return state_guard_.with_write([&] {
        try {
            sdk_.set_mode(mode);
            common::log_line(std::string("set_mode ok: ") + mode_str(mode));
            return true;
        } catch (const std::exception& e) {
            common::log_line(std::string("set_mode fail: ") + e.what());
            return false;
        }
    });
}

bool controller::set_speed_mode(uint32_t speed, sdk_mode_t mode) {
    if (speed > kMaxSpeed) {
        common::log_line("reject set_speed_mode out of range: " + std::to_string(speed));
        return false;
    }
    // 组合写必须与读共用同一把锁，保证读到的 speed 与 mode 来自同一业务动作
    return state_guard_.with_write([&] {
        try {
            sdk_.set_speed_mode(speed, mode);
            common::log_line("set_speed_mode ok: speed=" + std::to_string(speed) +
                             " mode=" + mode_str(mode));
            return true;
        } catch (const std::exception& e) {
            common::log_line(std::string("set_speed_mode fail: ") + e.what());
            return false;
        }
    });
}

std::optional<sdk_status> controller::get_status() const {
    return state_guard_.with_read([&] {
        try {
            return std::optional<sdk_status>(sdk_.get_status());
        } catch (const std::exception& e) {
            common::log_line(std::string("get_status fail: ") + e.what());
            return std::optional<sdk_status>(std::nullopt);
        }
    });
}

std::optional<uint32_t> controller::get_speed() const {
    return state_guard_.with_read([&] {
        try {
            return std::optional<uint32_t>(sdk_.get_speed());
        } catch (const std::exception& e) {
            common::log_line(std::string("get_speed fail: ") + e.what());
            return std::optional<uint32_t>(std::nullopt);
        }
    });
}

// 每变量锁：计数器接口独立锁，写读互不阻塞整驱动
bool controller::set_counter(uint32_t value) {
    return counter_guard_.with_write([&] {
        try {
            sdk_.set_counter(value);
            return true;
        } catch (const std::exception& e) {
            common::log_line(std::string("set_counter fail: ") + e.what());
            return false;
        }
    });
}

uint32_t controller::get_counter() const {
    return counter_guard_.with_read([&] {
        try {
            return sdk_.get_counter();
        } catch (const std::exception& e) {
            common::log_line(std::string("get_counter fail: ") + e.what());
            return 0u;
        }
    });
}