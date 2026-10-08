#pragma once

#include "senzr/export.hpp"

#define SENZR_SDK_VERSION_MAJOR 1
#define SENZR_SDK_VERSION_MINOR 1
#define SENZR_SDK_VERSION_PATCH 0
#define SENZR_SDK_VERSION_STRING "1.1.0"

namespace senzr {
inline namespace v1 {

/**
 * @brief Semantic version structure.
 */
struct Version {
    int major{SENZR_SDK_VERSION_MAJOR};
    int minor{SENZR_SDK_VERSION_MINOR};
    int patch{SENZR_SDK_VERSION_PATCH};
};

/**
 * @brief Returns the SDK version structure programmatically.
 */
[[nodiscard]] SENZR_API Version version() noexcept;

/**
 * @brief Returns the human-readable SDK version string (e.g. "1.1.0").
 */
[[nodiscard]] SENZR_API const char* version_string() noexcept;

} // namespace v1
} // namespace senzr
