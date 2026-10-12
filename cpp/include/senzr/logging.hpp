#pragma once

#include "senzr/types.hpp"
#include <string>
#include <string_view>
#include <functional>
#include <memory>

namespace senzr::log {

/**
 * @brief Standardized log severity levels.
 */
enum class Level {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off
};

/**
 * @brief Custom log handler callback signature.
 */
using LogHandler = std::function<void(Level level, std::string_view tag, std::string_view message)>;

/**
 * @brief Configures SDK global logging behavior.
 * Thread-safe.
 */
void set_level(Level level) noexcept;
[[nodiscard]] Level get_level() noexcept;

/**
 * @brief Registers a custom log handler (e.g. for syslog, systemd journal, or file sink).
 */
void set_handler(LogHandler handler);

/**
 * @brief Internal log dispatcher.
 */
void write(Level level, std::string_view tag, std::string_view message);

// Convenience logging functions
inline void trace(std::string_view tag, std::string_view message) { write(Level::Trace, tag, message); }
inline void debug(std::string_view tag, std::string_view message) { write(Level::Debug, tag, message); }
inline void info(std::string_view tag, std::string_view message)  { write(Level::Info, tag, message); }
inline void warn(std::string_view tag, std::string_view message)  { write(Level::Warn, tag, message); }
inline void error(std::string_view tag, std::string_view message) { write(Level::Error, tag, message); }
inline void fatal(std::string_view tag, std::string_view message) { write(Level::Fatal, tag, message); }

} // namespace senzr::log
