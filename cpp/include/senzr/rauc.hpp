#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>

namespace senzr {
inline namespace v1 {
namespace fota {

/**
 * @brief Information about an A/B boot partition slot managed by RAUC.
 */
struct SENZR_API RaucSlotInfo {
    std::string slot_name;    // e.g. "rootfs.0", "rootfs.1"
    std::string boot_status;  // "good", "bad", "unknown"
    std::string state;        // "active", "inactive", "booted"
    std::string installed_version;
};

} // namespace fota
} // namespace v1
} // namespace senzr
