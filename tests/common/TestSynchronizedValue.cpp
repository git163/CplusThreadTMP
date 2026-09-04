// tests/common/TestSynchronizedValue.cpp — synchronized_value / synchronized_guard 单元测试
// 与并发压力测试（验证 last-writer-wins、无丢失更新、混合读写无崩溃）。

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "synchronized_guard.h"
#include "synchronized_value.h"

namespace {

constexpr int kThreads = 8;
constexpr int kIters = 3000;

using common::synchronized_guard;
using common::synchronized_value;

}  // namespace

// —— synchronized_value 基础 ——

TEST(SynchronizedValueTest, BasicSetGet) {
    synchronized_value<int> v(10);
    EXPECT_EQ(v.get(), 10);
    v.set(42);
    EXPECT_EQ(v.get(), 42);
    v.set(7);
    EXPECT_EQ(v.get(), 7);
}

TEST(SynchronizedValueTest, WithWriteMutatesInPlace) {
    synchronized_value<int> v(0);
    const int ret = v.with_write([](int& x) {
        x += 10;
        return x;
    });
    EXPECT_EQ(ret, 10);
    EXPECT_EQ(v.get(), 10);
}

TEST(SynchronizedValueTest, WithReadInspects) {
    synchronized_value<int> v(5);
    const bool positive = v.with_read([](const int& x) { return x > 0; });
    EXPECT_TRUE(positive);
}

TEST(SynchronizedValueTest, MoveTransfersValue) {
    synchronized_value<std::string> a("hello");
    synchronized_value<std::string> b(std::move(a));
    EXPECT_EQ(b.get(), "hello");
}

TEST(SynchronizedValueTest, MoveAssignTransfersValue) {
    synchronized_value<int> a(1), b(2);
    b = std::move(a);
    EXPECT_EQ(b.get(), 1);  // 目标拿到源值
    // int 的移动语义等价于拷贝：源保持其既有值（仍是 1），不表示"清空/还原"
    EXPECT_EQ(a.get(), 1);
}

// —— synchronized_value 并发压力 ——

// last-writer-wins：多线程并发 set 后，最终值必为某一写者的值。
TEST(SynchronizedValueTest, ConcurrentSetLastWriterWins) {
    synchronized_value<int> v(0);
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                v.set(t);
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    const int final_value = v.get();
    EXPECT_GE(final_value, 0);
    EXPECT_LT(final_value, kThreads);
}

// with_write 原子自增：无丢失更新（若读改写被并发撕裂，最终值会小于期望）。
TEST(SynchronizedValueTest, ConcurrentIncrementNoLoss) {
    synchronized_value<uint64_t> counter(0);
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < kIters; ++i) {
                counter.with_write([](uint64_t& x) { ++x; });
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    EXPECT_EQ(counter.get(), static_cast<uint64_t>(kThreads) * kIters);
}

// 混合读写混合：读到的值始终是合法集合内的值，且无崩溃。
TEST(SynchronizedValueTest, ConcurrentMixedReadWrite) {
    synchronized_value<int> v(0);
    std::atomic<bool> bad{false};
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                if (i % 3 == 0) {
                    v.set(t);
                } else {
                    const int x = v.get();
                    if (x < 0 || x >= kThreads) {
                        bad.store(true);
                    }
                }
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    EXPECT_FALSE(bad.load());
}

// —— synchronized_guard 基础 ——

TEST(SynchronizedGuardTest, GuardWithReadWrite) {
    int shared = 0;
    synchronized_guard g;
    g.with_write([&] { shared = 5; });
    const int read = g.with_read([&] { return shared; });
    EXPECT_EQ(read, 5);
}

// 回调返回值透传
TEST(SynchronizedGuardTest, GuardReturnValueThrough) {
    synchronized_guard g;
    const std::string s = g.with_write([] { return std::string("ok"); });
    EXPECT_EQ(s, "ok");
}

// —— synchronized_guard 并发压力：模拟黑盒（无锁内部变量只经 guard 访问）——

TEST(SynchronizedGuardTest, ConcurrentGuardedBlackBox) {
    // 模拟黑盒 SDK 内部的局部变量：自身无锁，外部全靠 guard 保护
    uint32_t black_box = 0;
    synchronized_guard g;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                if (i % 3 == 0) {
                    g.with_write([&] { black_box = static_cast<uint32_t>(t); });
                } else {
                    g.with_read([&] { (void)black_box; });
                }
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    // last-writer-wins：最终值必为某一写者的 id
    EXPECT_LT(black_box, static_cast<uint32_t>(kThreads));
}

// —— 结构化一致快照：with_write 写 (a,b) 对、get 读整份，断言字段间无撕裂 ——
TEST(SynchronizedValueTest, StructuredSnapshotNoTear) {
    struct pair_t {
        int a = 0;
        int b = 0;
    };
    synchronized_value<pair_t> v;
    std::atomic<bool> tear{false};
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                if (i % 2 == 0) {
                    v.with_write([&](pair_t& p) {
                        p.a = t;
                        p.b = t;  // 两个字段必须同时更新
                    });
                } else {
                    const pair_t p = v.get();
                    if (p.a != p.b) {
                        tear.store(true);  // 读到不同写者的字段 = 撕裂
                    }
                }
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    EXPECT_FALSE(tear.load());
}

// 读重：绝大多数 get、少量 set，读到的值始终必须是合法集合内的值
TEST(SynchronizedValueTest, ConcurrentReadHeavy) {
    synchronized_value<int> v(0);
    std::atomic<bool> bad{false};
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                if (i % 100 == 0) {
                    v.set(t);
                } else {
                    const int x = v.get();
                    if (x < 0 || x >= kThreads) {
                        bad.store(true);
                    }
                }
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    EXPECT_FALSE(bad.load());
}

// 写重：绝大多数 set、少量 get，最终值 last-writer-wins 仍在合法集合
TEST(SynchronizedValueTest, ConcurrentWriteHeavy) {
    synchronized_value<int> v(0);
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                if (i % 100 == 0) {
                    (void)v.get();
                } else {
                    v.set(t);
                }
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    const int final_value = v.get();
    EXPECT_GE(final_value, 0);
    EXPECT_LT(final_value, kThreads);
}

// 大量线程短突发：验证无崩溃且 last-writer-wins
TEST(SynchronizedValueTest, ConcurrentManyThreadsBurst) {
    synchronized_value<int> v(0);
    constexpr int kMany = 32;
    constexpr int kBurst = 100;
    std::vector<std::thread> ts;
    for (int t = 0; t < kMany; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kBurst; ++i) {
                v.set(t);
                (void)v.get();
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    EXPECT_GE(v.get(), 0);
    EXPECT_LT(v.get(), kMany);
}

// —— synchronized_guard 下 check-then-act 原子：容量限制的容器多线程添加，永不越界 ——
TEST(SynchronizedGuardTest, GuardCheckThenActAtomic) {
    std::vector<int> history;
    synchronized_guard g;
    constexpr int kLimit = 1000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                g.with_write([&] {
                    // "检查 + 追加"必须在同一把锁下，否则并发会写超限
                    if (history.size() < kLimit) {
                        history.push_back(t);
                    }
                });
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    EXPECT_LE(history.size(), kLimit);
}

// guard 下无锁变量的读写争用：混合读写无崩溃、最终为合法写者值
TEST(SynchronizedGuardTest, GuardMixedReadWrite) {
    int shared = 0;
    synchronized_guard g;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                if (i % 4 == 0) {
                    g.with_write([&] { shared = t; });
                } else {
                    // 读锁保证读到完整写者值（无撕裂）
                    g.with_read([&] { return shared; });
                }
            }
        });
    }
    for (auto& th : ts) {
        th.join();
    }
    EXPECT_GE(shared, 0);
    EXPECT_LT(shared, kThreads);
}