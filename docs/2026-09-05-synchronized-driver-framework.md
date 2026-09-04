# 控制器层 + 驱动层多线程并发安全通用框架

- 日期: 2026-09-05
- 作者: tshua
- 状态: 已批准
- 关联: `src/include/synchronized_value.h`、`src/include/synchronized_guard.h`、`src/include/controller.h`、`src/driver_sdk.cpp`

## 背景

架构分两层：外部通过 RPC 调用**控制器层**，控制器层再访问**驱动层**（驱动层主要提供 `set` / `get` 接口）。

RPC 服务通常是多线程并发的（每个请求一个线程/协程），这会导致：
- 同时 `set`：写-写竞争；
- 同时 `get` 和 `set`：读-写竞争。

如果驱动状态不加防护，会产生数据竞争（UB）、读到撕裂值、逻辑不一致。更需要**通用、可移植**的并发安全访问框架。

关键约束：驱动层可能是**三方 SDK 黑盒**，只能看到其 set/get 接口、看不到内部原子变量 → 防护只能加在**控制器层方法**上。

## 目标
- 提供一个通用工具类 `synchronized_value<T>`（包住已知变量/状态）。
- 提供一个通用工具类 `synchronized_guard`（不持有数据，锁住黑盒 SDK 的调用序列）。
- 提供示例**驱动层**（模拟黑盒三方 SDK）和**控制器层**，演示两种锁粒度在 RPC 多线程场景的正确用法。
- 提供单元测试（含并发压力测试、撕裂检测、TSAN 可选验证）。
- 全程中文注释、英文日志，遵循 `CLAUDE.md` 规范。
- 非目标：不做 RPC 框架选型；不做 lock-free 优化。

## 方案

采用**读写锁（`std::shared_mutex`）**保护驱动状态访问：
- `get`/读：`std::shared_lock`（多个读者可并发，读-读不互斥）
- `set`/写：`std::unique_lock`（写-写互斥、读-写互斥）

为什么用读写锁而非普通 `std::mutex`：驱动场景典型为"读多写少"（状态轮询 get 频繁、set 偶发），`shared_mutex` 允许读-读并发，避免 get 全部串行化。

### 架构 / 流程

```
外部 RPC 多线程
   │  并发调用
   ▼
控制器层 controller（参数校验 + 日志 + try/catch）
   │  经 synchronized_guard 加锁后
   ▼
三方 SDK driver_sdk（黑盒，内部无锁，无法修改）
```

防护点放在"控制器边界"：SDK 是黑盒，锁只能由调用方持有。跨多个 SDK 调用的组合（先读后算再写）也在同一把锁下原子。

### 核心工具 1：`synchronized_value<T>`（`src/include/synchronized_value.h`）

持有数据、包住已知变量/状态；"每包装一个值配一把 `std::shared_mutex`"。

- `set(const T&)` / `set(T&&)`：写锁写值
- `get() const`：读锁返回副本（一致快照）
- `with_read(fn) const`：读锁内只读回调（避免拷贝大对象）
- `with_write(fn)`：写锁内修改回调（"读-改-写"原子操作）
- 禁拷贝、支持移动（移动互锁避免死锁）
- 适用：驱动内有可见状态变量/聚合 struct 时直接包装。

### 核心工具 2：`synchronized_guard`（`src/include/synchronized_guard.h`）

**不持有数据**，只持一把 `mutable std::shared_mutex`，提供 `with_read(fn)` / `with_write(fn)` 在锁内执行任意回调。适用三方 SDK 黑盒场景——锁在控制器边界，回调内调用 SDK 的 set/get。

```cpp
class synchronized_guard {
public:
    template <typename Fn> auto with_write(Fn&& fn) {
        std::unique_lock<std::shared_mutex> lk(mtx_);
        return std::forward<Fn>(fn)();
    }
    template <typename Fn> auto with_read(Fn&& fn) const {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return std::forward<Fn>(fn)();
    }
private:
    mutable std::shared_mutex mtx_;
};
```

### 锁粒度（两种都支持，示例均已演示）

- **每驱动一把锁**：controller 持一个 `synchronized_guard`，所有接口共享 → 简单、组合写/一致快照有保证、无多锁死锁风险。示例：`controller::state_guard_` 保护 `set_speed/set_mode/set_speed_mode/get_status`。
- **每变量（接口）一把锁**：独立接口各自 `synchronized_guard`（或多个 `synchronized_value<T>`）→ 不同接口互不阻塞，但跨接口一致性无保证。示例：`controller::counter_guard_` 独立保护 counter 读写。

### 示例分层

- `src/include/driver_sdk.h` / `src/driver_sdk.cpp`：模拟三方厂商驱动 SDK（黑盒）。仅暴露 set/get，内部 `status_` 隐藏且不加锁（模拟他人静态库）。
- `src/include/controller.h` / `src/controller.cpp`：控制器层（RPC 入口）。持 `state_guard_`（整驱动锁）+ `counter_guard_`（每接口锁）；每个 RPC 方法做参数校验、try/catch、英文日志。

## 影响范围

- CMake 工程调整：被测实现抽成共享静态库 `${PROJECT_NAME}_core`（排除 `src/main.cpp`），主程序与单元测试共同链接，避免重复定义 `main`（见 `CMakeLists.txt`、`tests/CMakeLists.txt`）。
- 新增文件（顶层与 tests 均为 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`，新文件自动纳入；tests 链接 `_core` 库后自动获得 `../src/include`）：
  - `src/include/synchronized_value.h`
  - `src/include/synchronized_guard.h`
  - `src/include/thread_log.h`
  - `src/include/driver_sdk.h` / `src/driver_sdk.cpp`
  - `src/include/controller.h` / `src/controller.cpp`
  - `src/main.cpp`（重写为多线程演示）
  - `tests/common/TestSynchronizedValue.cpp`
  - `tests/driver/TestDriver.cpp`
  - `docs/2026-09-05-synchronized-driver-framework.md`（本文档，即当前 plan）
- 兼容性：纯新增，无既有 API 破坏。

## 关键设计点 / 权衡

1. `synchronized_value` 的 `with_read/with_write` 与 `synchronized_guard` 的 `with_read/with_write` 语义一致（RAII 锁、回调返回值透传），学习成本低。
2. 跨接口一致性：整驱动锁天然保证；每接口锁不保证跨接口快照——需要时对该接口归入整驱动锁。
3. `get()` 返回副本：对小状态快照开销可忽略；大状态用 `with_read`，需要零拷贝共享时用 `shared_ptr<const state>`。
4. 加锁本身不抛异常（RAII）；回调体/拷贝可能抛异常，统一由控制器 try/catch 处理（符合 `CLAUDE.md` 第 12 条）。

## 风险与对策

- 风险 1：`std::shared_mutex` 非严格写者优先，极端读多场景写者等待较长。对策：驱动场景读多写少可接受；需求苛刻时替换为写者优先读写锁或 `std::mutex`（接口不变）。
- 风险 2：并发测试平时不跑在 TSAN 下，数据竞争不易暴露。对策：压力测试做强一致性断言（last-writer-wins、无丢失更新、撕裂检测）；并给出 TSAN 构建命令（见测试计划）。

## 替代方案

- 普通 `std::mutex`：简单，但 get 也全部串行，读并发收益丢失。
- `std::atomic`：仅适用单一标量；多字段状态、读-改-写组合不适用；对黑盒 SDK 无法使用。
- 无锁（SEQLock / 双缓冲）：读无锁但复杂易错，违背"简单清晰"原则；对三方黑盒 SDK 不可行。
- 结论：`std::shared_mutex` + `synchronized_value/synchronized_guard` 在通用性、安全、性能间最均衡。

## 实施步骤（已完成）

- [x] 编写 plan 文档
- [x] 实现 `synchronized_value.h`、`synchronized_guard.h`、`thread_log.h`
- [x] 实现模拟黑盒 SDK `driver_sdk.h/cpp`
- [x] 实现控制器层 `controller.h/cpp`
- [x] 重写 `src/main.cpp` 为多线程并发 RPC 演示
- [x] 编写 `tests/common/TestSynchronizedValue.cpp`、`tests/driver/TestDriver.cpp`
- [ ] 构建 + ctest 验证（见下）

## 测试计划

- 单元测试：`synchronized_value` 基础 set/get、`with_read/with_write`、移动语义；`synchronized_guard` 回调透传；controller 参数校验与基础转发。
- 并发压力：多线程并发 `set` 断言 last-writer-wins；`with_write` 自增断言无丢失更新；混合读写断言读值始终合法且无崩溃；guard 保护"黑盒"内部变量。
- 撕裂检测：控制器并发 `set_speed_mode`（speed↔mode 唯一映射）+ `get_status`，断言读到的组合永远匹配。
- 验证命令：
  - 构建：`cmake --build build -j`
  - 测试：`ctest --test-dir build --output-on-failure`
  - 运行演示：`./build/CplusThreadTMP`
  - 可选 TSAN：`cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"` 后构建并 ctest

## 引用

- 项目 `CLAUDE.md`（C++17 规范、中文注释/英文日志、通用代码封装为工具）
- `docs/plan-template.md`