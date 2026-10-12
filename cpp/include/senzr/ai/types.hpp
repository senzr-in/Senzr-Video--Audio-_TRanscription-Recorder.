#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace senzr::ai {

/**
 * @brief Supported object detection model families in the SDK.
 */
enum class ModelType {
    YOLOv8,   // Current baseline model
    YOLO11s   // Next-gen high-precision vehicle, emergency & ANPR model
};

/**
 * @brief NPU hardware core selection for power & thermal control.
 */
enum class NpuCore {
    Auto  = 0,
    Core0 = 1,   // Single core (Low heat & power saving, default)
    Core1 = 2,   // Secondary core
    Core2 = 4,   // Secondary core
    All   = 7    // Triple core (Full 6.0 TOPS performance)
};

/**
 * @brief Categorization of detected traffic entities.
 */
enum class ObjectCategory {
    Vehicle,
    EmergencyVehicle,
    LicensePlate,
    Person,
    Face,
    Other
};

/**
 * @brief Pixel-coordinate rectangular bounding box.
 */
struct BoundingBox {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

/**
 * @brief Vision object detection result item.
 */
struct Detection {
    int class_id{0};
    std::string label{"person"};
    float confidence{0.0f};
    BoundingBox box;
};

/**
 * @brief Raw non-owning frame buffer view for inference passing.
 */
struct FrameView {
    const uint8_t* bgr_data{nullptr};
    int width{640};
    int height{480};
    int stride{0};
    double timestamp{0.0};
};

/**
 * @brief Vehicle Automatic Number Plate Recognition (ANPR) result.
 */
struct PlateInfo {
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

/**
 * @brief High-level traffic, emergency vehicle, and plate detection item.
 */
struct TrafficDetection {
    int class_id{0};
    std::string label;            // "car", "ambulance", "police", "fire_engine", "license_plate"
    float confidence{0.0f};
    BoundingBox box;
    
    ObjectCategory category{ObjectCategory::Vehicle};
    bool is_emergency{false};     // True for Ambulance, Police, Fire Engine
    
    bool has_plate{false};
    PlateInfo plate;
};

} // namespace senzr::ai
