#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include "senzr/ai/types.hpp"
#include <string>

namespace senzr {
inline namespace v1 {
namespace ai {

/**
 * @brief Automatic Number Plate Recognition (ANPR) utility engine.
 * Supports Indian HSRP and Bharat Series plate formats.
 */
class SENZR_API AnprEngine {
public:
    /**
     * @brief Strips leading HSRP blue strip artifacts ("IND", "1ND", "IND-", "IN0").
     */
    [[nodiscard]] static std::string strip_hsrp_prefix(const std::string& raw_text);

    /**
     * @brief Performs position-aware OCR ambiguity disambiguation (O<->0, I<->1, S<->5, Z<->2, B<->8).
     */
    [[nodiscard]] static std::string normalize_plate_characters(const std::string& text);

    /**
     * @brief Formats and validates plate according to Indian standard RTO or Bharat (BH) syntax.
     */
    [[nodiscard]] static PlateInfo validate_and_format_plate(const std::string& raw_text, float confidence = 1.0f);
};

} // namespace ai
} // namespace v1
} // namespace senzr
