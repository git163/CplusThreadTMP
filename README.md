# CplusThreadTMP

C++ 多线程示例项目（占位描述，后续补充）

## 环境要求

- CMake ≥ 3.20
- 支持 C++17 的编译器（GCC ≥ 7、Clang ≥ 5、MSVC ≥ 19.14）
- 可选但推荐：GDB 或 LLDB 用于调试；VSCode + 推荐的扩展以获得最佳体验。

## 构建

```bash
cmake -S . -B build
cmake --build build -j
```

默认构建类型为 `RelWithDebInfo` —— 优化但保留调试信息，崩溃时仍能产生可用的堆栈。真正的发布构建：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

## 运行

```bash
./build/CplusThreadTMP
```

## 测试

```bash
ctest --test-dir build --output-on-failure
```

测试文件放在 `tests/` 目录下，命名遵循 `Test*.cpp` 模式 —— `tests/CMakeLists.txt` 通过 `gtest_discover_tests` 自动发现，新增一个测试只需新建 `tests/TestMyModule.cpp`。

## 调试

用 VSCode 打开项目根目录（`code .`）。项目自带启动配置（`.vscode/launch.json`）：

- **(gdb) Launch CplusThreadTMP** / **(lldb) Launch CplusThreadTMP** —— 构建并调试主程序。Linux 选 gdb，macOS 选 lldb。
- **(gdb) Run unit_tests** / **(lldb) Run unit_tests** —— 直接构建并调试测试程序。可在任意 `Test*.cpp` 中设断点，按 F5 即可。

前置任务为 `cmake build`，开始调试时二进制总是最新的。

如果不使用 VSCode，可从命令行附加：

```bash
gdb --args ./build/CplusThreadTMP     # Linux
lldb ./build/CplusThreadTMP           # macOS
```

## 项目结构

- `docs/` —— 设计文档和计划（参考 `docs/plan-template.md`）
- `src/` —— 实现（`.cpp`）
- `src/include/` —— 公共头文件（Google C++ Style）
- `tests/` —— GTest 单元测试（`Test*.cpp`）

## 约定

见项目根目录的 `CLAUDE.md`。