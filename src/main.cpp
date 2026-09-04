// 多线程并发 RPC 演示：
// 模拟多个 RPC 客户端线程同时访问控制器层，覆盖两种锁粒度：
//   - 整驱动锁：set_speed / set_mode / set_speed_mode / get_status
//   - 每变量锁：counter 独立接口
// 三方 SDK（driver_sdk）是黑盒、内部无锁，并发防护全部在控制器边界完成。

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "controller.h"
#include "thread_log.h"

int main() {
    driver_sdk sdk;       // 三方黑盒 SDK
    controller ctl(sdk);  // 控制器层，锁在边界

    constexpr int kClients = 6;   // 模拟 RPC 并发通道数
    constexpr int kOps = 200;     // 每个通道的操作次数
    std::atomic<bool> ok_flag{true};

    std::vector<std::thread> clients;
    clients.reserve(kClients);
    for (int i = 0; i < kClients; ++i) {
        clients.emplace_back([&, i] {
            for (int j = 0; j < kOps; ++j) {
                const uint32_t speed = static_cast<uint32_t>((i * 50 + j * 7) % 1000);

                // 周期打日志，避免刷屏；日志应无交错（线程安全）
                if (j % 40 == 0) {
                    common::log_line("client " + std::to_string(i) +
                                     ": set_speed call, speed=" + std::to_string(speed));
                }
                if (!ctl.set_speed(speed)) {
                    ok_flag = false;
                }

                // 组合写：speed 与 mode 同动作变更
                if (j % 13 == 0) {
                    sdk_mode_t mode =
                        (j % 3 == 0) ? sdk_mode_t::on : sdk_mode_t::off;
                    if (!ctl.set_speed_mode(speed, mode)) {
                        ok_flag = false;
                    }
                }

                // 整驱动读
                if (j % 17 == 0) {
                    auto st = ctl.get_status();
                    if (!st.has_value()) {
                        ok_flag = false;
                    }
                }

                // 每变量锁：计数器接口与整驱动读互不阻塞
                if (j % 5 == 0) {
                    ctl.set_counter(static_cast<uint32_t>(j));
                    (void)ctl.get_counter();
                }
            }
        });
    }
    for (auto& t : clients) {
        t.join();
    }

    // 汇总最终状态（counter 是独立寄存器，经每变量锁单独读取）
    auto st = ctl.get_status();
    if (st.has_value()) {
        common::log_line("final status: speed=" + std::to_string(st->speed) +
                         " counter=" + std::to_string(ctl.get_counter()));
    }
    common::log_line(ok_flag.load() ? "demo passed" : "demo failed");
    return ok_flag.load() ? 0 : 1;
}