#pragma once

#include "senzr/export.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace senzr {
inline namespace v1 {
namespace ai {

/**
 * @brief Pixel-coordinate rectangular bounding box.
 */
struct SENZR_API BoundingBox {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

/**
 * @brief Vision object detection result item.
 */
struct SENZR_API Detection {
    int class_id{0};
    std::string label{"person"};
    float confidence{0.0f};
    BoundingBox box;
};

/**
 * @brief Raw non-owning frame buffer view for inference passing.
 */
struct SENZR_API FrameView {
    const uint8_t* bgr_data{nullptr};
    int width{640};
    int height{480};
    int stride{0};
    double timestamp{0.0};
};

/**
 * @brief Vehicle Automatic Number Plate Recognition (ANPR) result.
 */
struct SENZR_API PlateInfo {
    bool is_valid{false};
    std::string raw_text;
    std::string formatted_plate;  // e.g. "KA 05 MJ 1234"
    std::string clean_number;     // e.g. "KA05MJ1234"
    std::string state_code;       // "KA"
    std::string rto_code;         // "05"
    std::string series;           // "MJ"
    std::string unique_digits;    // "1234"
    float confidence{0.0f};
    bool is_multiline{false};
    BoundingBox box;
};

} // namespace ai
} // namespace v1
} // namespace senzr
