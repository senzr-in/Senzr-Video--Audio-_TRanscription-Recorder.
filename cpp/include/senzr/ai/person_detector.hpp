#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include "senzr/ai/types.hpp"
#include <string>
#include <vector>
#include <memory>

namespace senzr::ai {

/**
 * @brief YOLOv8 NPU inference configuration.
 */
struct PersonDetectorConfig {
    std::string model_path{"models/yolov8n.rknn"};
    float confidence_threshold{0.45f};
    float nms_threshold{0.45f};
    int input_width{640};
    int input_height{640};
    int npu_core_mask{0}; // 0 = auto/tri-core
};

/**
 * @brief Hardware-accelerated YOLOv8 Person Detector utilizing the Rockchip RK3588 6 TOPS NPU.
 */
class PersonDetector {
public:
    explicit PersonDetector(PersonDetectorConfig config = {});
    ~PersonDetector();

    PersonDetector(const PersonDetector&) = delete;
    PersonDetector& operator=(const PersonDetector&) = delete;
    PersonDetector(PersonDetector&&) noexcept;
    PersonDetector& operator=(PersonDetector&&) noexcept;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    Result<void> initialize();
    [[nodiscard]] bool is_initialized() const noexcept;
    void close();

    // ── Inference ─────────────────────────────────────────────────────────────
    Result<std::vector<Detection>> detect(const FrameView& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace senzr::ai
