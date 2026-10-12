#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include "senzr/ai/types.hpp"
#include <string>

namespace senzr::ai {

/**
 * @brief Automatic Number Plate Recognition (ANPR) utility engine for MoRTH Indian standard & Bharat series plates.
 */
class AnprEngine {
public:
    /**
     * @brief Strips leading HSRP blue strip artifacts ("IND", "1ND", "IND-", "IN0").
     */
    static std::string strip_hsrp_prefix(const std::string& raw_text);

    /**
     * @brief Performs position-aware OCR ambiguity disambiguation (O<->0, I<->1, S<->5, Z<->2, B<->8).
     */
    static std::string normalize_plate_characters(const std::string& text);

    /**
     * @brief Formats and validates plate according to Indian standard RTO or Bharat (BH) syntax.
     */
    static PlateInfo validate_and_format_plate(const std::string& raw_text, float confidence = 1.0f);
};

} // namespace senzr::ai
