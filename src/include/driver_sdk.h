#ifndef DRIVER_SDK_H
#define DRIVER_SDK_H

#include <cstdint>

// 模拟三方厂商驱动 SDK（黑盒）：
// - 仅暴露 set/get 类接口，内部状态对外不可见（现实中以静态库形式提供）
// - 本 SDK 内部不加任何锁，模拟"无法修改的三方代码"；
//   并发安全必须由调用方（控制器层）在边界上用同步工具防护。

enum class sdk_mode_t : uint8_t { off, on, emergency };

// 第三方 SDK 返回的状态快照（一次 get_status 一口气返回；不含独立计数器）
struct sdk_status {
    sdk_mode_t mode = sdk_mode_t::off;
    uint32_t   speed = 0;
};

class driver_sdk {
public:
    // 打开设备；max_speed 为设备允许的最大速度
    explicit driver_sdk(uint32_t max_speed = 1000);
    ~driver_sdk();
    driver_sdk(const driver_sdk&) = delete;
    driver_sdk& operator=(const driver_sdk&) = delete;

    // 写接口（set 类）
    void set_speed(uint32_t speed);                      // 超上限抛 std::out_of_range
    void set_mode(sdk_mode_t mode);
    void set_speed_mode(uint32_t speed, sdk_mode_t mode);  // 组合设置：speed 与 mode 一次变更
    void set_counter(uint32_t counter);

    // 读接口（get 类）
    sdk_status get_status() const;
    uint32_t   get_speed() const;
    sdk_mode_t get_mode() const;
    uint32_t   get_counter() const;

private:
    uint32_t   max_speed_;   // 设备速度上限
    sdk_status status_;      // 状态寄存器组（mode/speed）；SDK 本身不保证线程安全
    uint32_t   counter_;     // 独立计数器寄存器；与状态组物理隔离，允许独立加锁访问
};

#endif  // DRIVER_SDK_H