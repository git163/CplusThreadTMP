#ifndef THREAD_LOG_H
#define THREAD_LOG_H

#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace common {

// 线程安全的一行日志（英文）。内部静态互斥锁串行化输出，避免多线程日志交错。
// 日志语言遵循项目规范保持英文，便于日志系统统一检索。
inline void log_line(const std::string& msg) {
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << "[t" << std::this_thread::get_id() << "] " << msg << std::endl;
}

}  // namespace common

#endif  // THREAD_LOG_H