#ifndef SYNCHRONIZED_VALUE_H
#define SYNCHRONIZED_VALUE_H

#include <shared_mutex>
#include <type_traits>
#include <utility>

namespace common {

// 通用并发安全容器：把一个值 T 用一把读写锁保护。
//
// 用法（粒度由使用方决定）：
//   - 包一个变量，即"每变量一把锁"；
//   - 包一个聚合 struct（整个驱动状态），即"每驱动一把锁"，一次 get() 得到一致快照。
//
// 读锁 std::shared_lock（读-读可并发），写锁 std::unique_lock（写-写、读-写互斥），
// 适合 RPC "读多写少" 场景。
template <typename T>
class synchronized_value {
public:
    using value_type = T;

    // 就值构造：支持聚合、非默认可构造等任意参数
    template <typename... Args>
    explicit synchronized_value(Args&&... args)
        : value_(std::forward<Args>(args)...) {}

    // 不允许拷贝复制（内部持有锁与数据，语义存疑）
    synchronized_value(const synchronized_value&) = delete;
    synchronized_value& operator=(const synchronized_value&) = delete;

    // 移动：先锁住对方再搬移内部值，避免与其它线程并发读到自身
    synchronized_value(synchronized_value&& other) {
        std::unique_lock<std::shared_mutex> lock(other.mutex_);
        value_ = std::move(other.value_);
    }

    synchronized_value& operator=(synchronized_value&& other) {
        if (this != &other) {
            // 同时锁两把：std::scoped_lock 保证统一加锁顺序，避免互相 move 时死锁
            std::scoped_lock lock(mutex_, other.mutex_);
            value_ = std::move(other.value_);
        }
        return *this;
    }

    // —— set / get 基础接口 ——
    void set(const T& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        value_ = value;
    }

    void set(T&& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        value_ = std::move(value);
    }

    // 读锁返回副本：一次读取即一致快照
    T get() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return value_;
    }

    // 锁内只读回调：直接检查/读取内部值，避免拷贝大对象
    template <typename Fn>
    auto with_read(Fn&& fn) const
        -> decltype(std::forward<Fn>(fn)(std::declval<const T&>())) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return std::forward<Fn>(fn)(value_);
    }

    // 锁内读写回调：支持"读-改-写"原子操作（如自增、比较后更新）
    template <typename Fn>
    auto with_write(Fn&& fn) -> decltype(std::forward<Fn>(fn)(std::declval<T&>())) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return std::forward<Fn>(fn)(value_);
    }

private:
    mutable std::shared_mutex mutex_;
    T value_;
};

}  // namespace common

#endif  // SYNCHRONIZED_VALUE_H