#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <memory>

namespace senzr::watchdog {

/**
 * @brief Hardware watchdog supervisor to maintain system stability and prevent deadlocks.
 */
class Supervisor {
public:
    static Supervisor& get_instance();

    // ── Watchdog Controls ─────────────────────────────────────────────────────
    Result<void> start(int timeout_seconds = 30);
    Result<void> feed();
    Result<void> stop();

    // ── Status ────────────────────────────────────────────────────────────────
    [[nodiscard]] bool is_active() const noexcept;
    [[nodiscard]] int get_timeout_seconds() const noexcept;

    // Backward-compatibility alias
    static Supervisor& instance() { return get_instance(); }

private:
    Supervisor();
    ~Supervisor();
    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

inline Supervisor& get_supervisor() {
    return Supervisor::get_instance();
}

} // namespace senzr::watchdog
