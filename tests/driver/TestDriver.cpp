// tests/driver/TestDriver.cpp — 黑盒 SDK + 控制器层并发一致性测试。
// 核心验证：组合写 set_speed_mode 与整读 get_status 在并发下无撕裂
//（每次读到的 speed 与 mode 必须匹配同一业务动作）。

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "controller.h"

namespace {

constexpr int kThreads = 6;

// 一致性辅助：把 speed 唯一映射到 mode，用于检测撕裂。
// 若读到撕裂的状态，mode 将与 speed 不匹配。
sdk_mode_t mode_for(uint32_t speed) {
    switch (speed % 3) {
        case 0:
            return sdk_mode_t::on;
        case 1:
            return sdk_mode_t::emergency;
        default:
            return sdk_mode_t::off;
    }
}

}  // namespace

// 参数校验 + 业务约束
TEST(ControllerTest, ParamValidation) {
    driver_sdk sdk(1000);
    controller ctl(sdk);

    EXPECT_FALSE(ctl.set_speed(1001));            // 超上限被拒
    EXPECT_TRUE(ctl.set_speed(100));              // 合法通过
    EXPECT_FALSE(ctl.set_speed_mode(2000, sdk_mode_t::on));  // 组合写超限被拒
    EXPECT_TRUE(ctl.set_speed_mode(200, sdk_mode_t::on));
}

// 基础转发正确性
TEST(ControllerTest, BasicForward) {
    driver_sdk sdk(1000);
    controller ctl(sdk);

    EXPECT_TRUE(ctl.set_speed_mode(300, mode_for(300)));
    auto st = ctl.get_status();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(st->speed, 300);
    EXPECT_EQ(st->mode, mode_for(300));

    EXPECT_TRUE(ctl.set_counter(123));
    EXPECT_EQ(ctl.get_counter(), 123);
}

// 黑盒场景压测核心：写线程 set_speed_mode、读线程 get_status 并发运行，
// 断言随机读到的状态组合永远匹配（读锁整状态 + 写锁组合写 = 无撕裂）。
TEST(ControllerTest, ConcurrentComposedWriteNoTear) {
    driver_sdk sdk(1000);
    controller ctl(sdk);

    constexpr int kWriteThreads = 4;
    constexpr int kReadThreads = 4;
    constexpr int kIters = 4000;
    std::atomic<bool> tear_detected{false};

    std::vector<std::thread> ts;
    for (int w = 0; w < kWriteThreads; ++w) {
        ts.emplace_back([&, w] {
            for (int i = 0; i < kIters; ++i) {
                const uint32_t speed = static_cast<uint32_t>((w * 300 + i) % 1000);
                EXPECT_TRUE(ctl.set_speed_mode(speed, mode_for(speed)));
            }
        });
    }
    for (int r = 0; r < kReadThreads; ++r) {
        ts.emplace_back([&] {
            for (int i = 0; i < kIters; ++i) {
                auto st = ctl.get_status();
                if (st.has_value()) {
                    // 速度与模式不匹配 = 读到撕裂状态
                    if (st->mode != mode_for(st->speed)) {
                        tear_detected.store(true);
                        break;
                    }
                } else {
                    tear_detected.store(true);
                    break;
                }
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    EXPECT_FALSE(tear_detected.load());
}

// 每变量锁演示：counter 独立接口并发写读，最终值必为某一写者写入的合法值。
TEST(ControllerTest, ConcurrentCounterLastWriterWins) {
    driver_sdk sdk(1000);
    controller ctl(sdk);

    constexpr int kThreads = 6;
    constexpr int kIters = 2000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                ASSERT_TRUE(ctl.set_counter(static_cast<uint32_t>(t * 100 + i)));
                (void)ctl.get_counter();
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }

    const uint32_t final_value = ctl.get_counter();
    bool valid = false;
    for (int t = 0; t < kThreads && !valid; ++t) {
        for (int i = 0; i < kIters; ++i) {
            if (final_value == static_cast<uint32_t>(t * 100 + i)) {
                valid = true;
                break;
            }
        }
    }
    EXPECT_TRUE(valid) << "final counter not from any writer: " << final_value;
}

// 混合粒度并发：整驱动锁接口（set_speed_mode/get_status）与每变量锁接口（counter）
// 同时高强度运行，验证互不串扰：组合写无撕裂、counter last-writer-wins、无崩溃。
TEST(ControllerTest, MixedGranularityConcurrent) {
    driver_sdk sdk(1000);
    controller ctl(sdk);

    constexpr int kWriters = 3;
    constexpr int kReaders = 3;
    constexpr int kCounters = 4;
    constexpr int kIters = 3000;
    std::atomic<bool> tear{false};

    std::vector<std::thread> ts;
    for (int w = 0; w < kWriters; ++w) {
        ts.emplace_back([&, w] {
            for (int i = 0; i < kIters; ++i) {
                const uint32_t speed = static_cast<uint32_t>((w * 300 + i) % 1000);
                if (!ctl.set_speed_mode(speed, mode_for(speed))) {
                    tear.store(true);
                }
            }
        });
    }
    for (int r = 0; r < kReaders; ++r) {
        ts.emplace_back([&] {
            for (int i = 0; i < kIters; ++i) {
                auto st = ctl.get_status();
                if (!st.has_value() || st->mode != mode_for(st->speed)) {
                    tear.store(true);
                }
            }
        });
    }
    for (int c = 0; c < kCounters; ++c) {
        ts.emplace_back([&, c] {
            for (int i = 0; i < kIters; ++i) {
                // 每变量锁接口：与整驱动读并发，互不阻塞、各自一致
                if (!ctl.set_counter(static_cast<uint32_t>(c * 1000 + i))) {
                    tear.store(true);
                }
                (void)ctl.get_counter();
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    EXPECT_FALSE(tear.load());
}

// 参数校验压力：并发掺入超限与合法 set，合法必 true、超限必 false，最终状态合法
TEST(ControllerTest, ConcurrentRejectStress) {
    driver_sdk sdk(1000);
    controller ctl(sdk);
    std::atomic<bool> bad{false};

    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < 2000; ++i) {
                const uint32_t speed = static_cast<uint32_t>((t * 991 + i) % 2000);
                const bool ok = ctl.set_speed(speed);
                if (speed > 1000) {
                    if (ok) {
                        bad.store(true);  // 超限不应被接受
                    }
                } else if (!ok) {
                    bad.store(true);  // 合法不应被拒
                }
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }

    // 即使检查失败被拒写入，驱动状态也必须停留在某一合法历史值
    auto st = ctl.get_status();
    ASSERT_TRUE(st.has_value());
    EXPECT_LE(st->speed, 1000);
    EXPECT_FALSE(bad.load());
}