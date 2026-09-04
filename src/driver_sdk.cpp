#include "driver_sdk.h"

#include <stdexcept>

driver_sdk::driver_sdk(uint32_t max_speed) : max_speed_(max_speed), counter_(0) {}

driver_sdk::~driver_sdk() = default;

void driver_sdk::set_speed(uint32_t speed) {
    if (speed > max_speed_) {
        throw std::out_of_range("speed out of range");
    }
    status_.speed = speed;
}

void driver_sdk::set_mode(sdk_mode_t mode) {
    status_.mode = mode;
}

// 组合设置：两个字段一次变更，保证调用方在写锁内使用时 speed 与 mode 来自同一业务动作
void driver_sdk::set_speed_mode(uint32_t speed, sdk_mode_t mode) {
    set_speed(speed);
    status_.mode = mode;
}

void driver_sdk::set_counter(uint32_t counter) {
    counter_ = counter;
}

sdk_status driver_sdk::get_status() const {
    return status_;
}

uint32_t driver_sdk::get_speed() const {
    return status_.speed;
}

sdk_mode_t driver_sdk::get_mode() const {
    return status_.mode;
}

uint32_t driver_sdk::get_counter() const {
    return counter_;
}