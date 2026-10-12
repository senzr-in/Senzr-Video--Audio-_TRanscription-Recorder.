#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include "senzr/ai/types.hpp"
#include <string>
#include <vector>
#include <memory>

namespace senzr::ai {

/**
 * @brief Unified Detector Configuration supporting YOLOv8 and YOLO11s.
 */
struct DetectorConfig {
    ModelType model{ModelType::YOLO11s};                      // Default: YOLO11s
    std::string model_path{"models/yolo11s_traffic.rknn"};
    NpuCore npu_core{NpuCore::Core0};                         // Default: Single core to reduce thermal load
    float confidence_threshold{0.50f};
    float nms_threshold{0.45f};
    int input_width{640};
    int input_height{640};

    // Feature Toggles
    bool enable_vehicle_detection{true};
    bool enable_emergency_detection{true};
    bool enable_anpr{true};
    bool enable_person_detection{false};  // Disabled by default; easily enabled in code
    bool enable_face_detection{false};    // Disabled by default; easily enabled in code
};

/**
 * @brief Complete inference result telemetry and detections.
 */
struct TrafficInferenceResult {
    std::vector<TrafficDetection> detections;
    int total_vehicles{0};
    int emergency_vehicles{0};
    int plates_recognized{0};
    int persons_detected{0};
    int faces_detected{0};
    double inference_latency_ms{0.0};
    NpuCore executed_core{NpuCore::Core0};
    ModelType active_model{ModelType::YOLO11s};
};

/**
 * @brief Hardware-accelerated Vision Detector supporting YOLO11s & YOLOv8 on RK3588 NPU.
 */
class Detector {
public:
    explicit Detector(DetectorConfig config = {});
    ~Detector();

    Detector(const Detector&) = delete;
    Detector& operator=(const Detector&) = delete;
    Detector(Detector&&) noexcept;
    Detector& operator=(Detector&&) noexcept;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    Result<void> initialize();
    [[nodiscard]] bool is_initialized() const noexcept;
    void close();

    // ── Inference ─────────────────────────────────────────────────────────────
    Result<TrafficInferenceResult> detect(const FrameView& frame);
    Result<TrafficInferenceResult> detect(const uint8_t* bgr_data, int width, int height, int stride = 0);

    // ── Runtime Dynamic Controls (Zero Application Restart) ───────────────────
    void switch_model(ModelType model, const std::string& model_path = "");
    void set_core(NpuCore core);
    void set_confidence_threshold(float threshold);
    void enable_emergency_detection(bool enable);
    void enable_anpr(bool enable);
    void enable_person_detection(bool enable);
    void enable_face_detection(bool enable);

    [[nodiscard]] ModelType get_active_model() const;
    [[nodiscard]] NpuCore get_active_core() const;
    [[nodiscard]] const DetectorConfig& get_config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace senzr::ai
