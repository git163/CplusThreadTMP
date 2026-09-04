#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <cstdint>
#include <optional>

#include "driver_sdk.h"
#include "synchronized_guard.h"

// 控制器层：外部分布式 RPC 的并发入口。
//
// 由于三方 SDK（driver_sdk）是黑盒、内部无锁，并发防护放在本层边界：
// - state_guard_：整个驱动共享一把读写锁（"每驱动一把锁"），保证组合写与一致快照；
// - counter_guard_：独立高频接口单独一把锁（"每变量/接口一把锁"），互不阻塞整驱动。
//
// 每个 RPC 方法统一：参数校验 + try/catch + 英文日志，出错返回 false / nullopt。
class controller {
public:
    explicit controller(driver_sdk& sdk);

    // —— 整驱动锁：写接口 ——
    bool set_speed(uint32_t speed);
    bool set_mode(sdk_mode_t mode);
    bool set_speed_mode(uint32_t speed, sdk_mode_t mode);  // 组合写，原子（同一把锁）

    // —— 整驱动锁：读接口（读锁，读-读并发）——
    std::optional<sdk_status> get_status() const;
    std::optional<uint32_t>   get_speed() const;

    // —— 每变量（接口）锁：计数器独立锁，不阻塞整驱动 ——
    bool     set_counter(uint32_t value);
    uint32_t get_counter() const;

private:
    driver_sdk& sdk_;
    common::synchronized_guard state_guard_;    // 整驱动共享锁
    common::synchronized_guard counter_guard_;  // 独立接口独立锁
};

#endif  // CONTROLLER_H