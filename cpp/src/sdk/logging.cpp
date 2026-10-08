#include "senzr/logging.hpp"
#include <iostream>
#include <atomic>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace senzr {
inline namespace v1 {
namespace log {

namespace {
    std::atomic<Level> g_log_level{Level::Info};
    std::mutex g_log_mutex;
    LogHandler g_custom_handler{nullptr};

    const char* level_to_string(Level lvl) noexcept {
        switch (lvl) {
            case Level::Trace: return "TRACE";
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO";
            case Level::Warn:  return "WARN";
            case Level::Error: return "ERROR";
            case Level::Fatal: return "FATAL";
            case Level::Off:   return "OFF";
        }
        return "UNKNOWN";
    }
}

void set_level(Level level) noexcept {
    g_log_level.store(level);
}

Level get_level() noexcept {
    return g_log_level.load();
}

void set_handler(LogHandler handler) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_custom_handler = std::move(handler);
}

void write(Level level, std::string_view tag, std::string_view message) {
    if (level < g_log_level.load() || level == Level::Off) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_custom_handler) {
        g_custom_handler(level, tag, message);
        return;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    std::cout << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
              << " [" << level_to_string(level) << "] ["
              << tag << "] " << message << std::endl;
}

} // namespace log
} // namespace v1
} // namespace senzr
