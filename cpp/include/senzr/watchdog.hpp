#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <memory>

namespace senzr {
inline namespace v1 {
namespace watchdog {

/**
 * @brief Hardware watchdog supervisor to maintain system stability and prevent deadlocks.
 */
class SENZR_API Supervisor {
public:
    static Supervisor& get_instance();

    // ── Watchdog Controls ─────────────────────────────────────────────────────
    [[nodiscard]] Result<void> start(int timeout_seconds = 30);
    [[nodiscard]] Result<void> feed();
    [[nodiscard]] Result<void> stop();

    // ── Status ────────────────────────────────────────────────────────────────
    [[nodiscard]] bool is_active() const noexcept;
    [[nodiscard]] int get_timeout_seconds() const noexcept;

    // Backward-compatibility alias
    static Supervisor& instance() { return get_instance(); }

    ~Supervisor();
    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;
    Supervisor(Supervisor&&) noexcept;
    Supervisor& operator=(Supervisor&&) noexcept;

private:
    Supervisor();

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

[[nodiscard]] inline Supervisor& get_supervisor() {
    return Supervisor::get_instance();
}

} // namespace watchdog
} // namespace v1
} // namespace senzr
