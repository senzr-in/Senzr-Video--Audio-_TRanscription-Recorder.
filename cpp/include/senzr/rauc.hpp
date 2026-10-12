#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>

namespace senzr::fota {

/**
 * @brief Information about an A/B boot partition slot managed by RAUC.
 */
struct RaucSlotInfo {
    std::string slot_name;    // e.g. "rootfs.0", "rootfs.1"
    std::string boot_status;  // "good", "bad", "unknown"
    std::string state;        // "active", "inactive", "booted"
    std::string installed_version;
};

} // namespace senzr::fota
