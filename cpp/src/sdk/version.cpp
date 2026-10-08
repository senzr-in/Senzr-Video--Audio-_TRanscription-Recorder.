#include "senzr/version.hpp"

namespace senzr {
inline namespace v1 {

Version version() noexcept {
    return Version{SENZR_SDK_VERSION_MAJOR, SENZR_SDK_VERSION_MINOR, SENZR_SDK_VERSION_PATCH};
}

const char* version_string() noexcept {
    return SENZR_SDK_VERSION_STRING;
}

} // namespace v1
} // namespace senzr
