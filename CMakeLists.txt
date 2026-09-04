cmake_minimum_required(VERSION 3.20)
project(CplusThreadTMP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认使用 RelWithDebInfo：优化 + 保留调试符号
# - 性能接近 Release
# - 崩溃时仍能用 gdb/lldb 拿到符号和行号
# - 真正发布时单独切到 Release 即可（-DCMAKE_BUILD_TYPE=Release）
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# 调试信息：所有 build type 都保留 -g，便于事后排查
add_compile_options(-g)

# 源码与头文件
# 注意：file(GLOB_RECURSE) 是 CMake 反模式——CMake 不会主动感知新增的 .cpp。
# 缓解：CONFIGURE_DEPENDS 会在 build 时触发 cmake 重跑；如 build 已存在，
#      新增 .cpp 后请先 `cmake --build build --target rebuild_cache`。
# 项目成长后推荐：每个模块一个 CMakeLists.txt，用 add_subdirectory() 替代 GLOB。
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.cc")
file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS "src/include/*.h" "src/include/*.hpp")

# 主程序与单元测试共享的核心实现库（排除 main.cpp，避免链接时重复定义 main）
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "main\\.cpp$")
add_library(${PROJECT_NAME}_core STATIC ${PROJECT_SOURCES})
target_include_directories(${PROJECT_NAME}_core PUBLIC src src/include)

# 线程支持：mac/linux 统一显式链接 Threads::Threads
#（linux 上 std::thread 需要 -pthread，mac 自动提供；显式声明保证跨平台可移植）
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME}_core PUBLIC Threads::Threads)

# 主目标
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_core)

# 启用测试
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()