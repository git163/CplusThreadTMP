#ifndef SYNCHRONIZED_GUARD_H
#define SYNCHRONIZED_GUARD_H

#include <shared_mutex>
#include <utility>

namespace common {

// 通用并发访问守卫：不持有任何数据，只持一把读写锁，
// 通过 with_read / with_write 在锁内执行任意回调。
//
// 适用场景：驱动层是三方 SDK 黑盒、看不到内部原子变量时，
// 把锁加在调用方（如控制器层）边界，回调内调用 SDK 的 set/get，
// 跨多个 SDK 调用的组合（先读后算再写）也在同一把锁下保持原子。
//
// 粒度：一个 guard 就是"每驱动一把锁"；不同接口各自一个 guard 即"每变量一把锁"。
class synchronized_guard {
public:
    synchronized_guard() = default;
    ~synchronized_guard() = default;

    synchronized_guard(const synchronized_guard&) = delete;
    synchronized_guard& operator=(const synchronized_guard&) = delete;

    // 写锁：包住 set 类调用（写-写、读-写互斥）
    template <typename Fn>
    auto with_write(Fn&& fn) -> decltype(std::forward<Fn>(fn)()) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return std::forward<Fn>(fn)();
    }

    // 读锁：包住 get 类调用（读-读可并发）
    template <typename Fn>
    auto with_read(Fn&& fn) const -> decltype(std::forward<Fn>(fn)()) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return std::forward<Fn>(fn)();
    }

private:
    mutable std::shared_mutex mutex_;
};

}  // namespace common

#endif  // SYNCHRONIZED_GUARD_H