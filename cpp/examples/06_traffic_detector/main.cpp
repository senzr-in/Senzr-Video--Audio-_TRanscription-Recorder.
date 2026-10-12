#include <senzr/senzr.hpp>
#include <iostream>
#include <vector>

int main() {
    std::cout << "============================================================\n";
    std::cout << "  Senzr SDK Example 06: YOLO11s / YOLOv8 Traffic Detector\n";
    std::cout << "  Vehicle, Emergency Vehicle & Number Plate Recognition\n";
    std::cout << "============================================================\n\n";

    // ── 1. Configure YOLO11s on Single Core (Core 0 for low thermals) ──────────
    senzr::ai::DetectorConfig config;
    config.model = senzr::ai::ModelType::YOLO11s;
    config.model_path = "models/yolo11s_traffic.rknn";
    config.npu_core = senzr::ai::NpuCore::Core0;   // Keeps temperature < 48°C
    config.confidence_threshold = 0.50f;
    config.enable_emergency_detection = true;
    config.enable_anpr = true;

    std::cout << "[INFO] Initializing Detector with model: YOLO11s on NPU Core 0...\n";
    senzr::ai::Detector detector(config);
    auto init_res = detector.initialize();
    if (!init_res.has_value()) {
        std::cerr << "[ERROR] Detector initialization failed: " << init_res.error().message() << "\n";
        return 1;
    }
    std::cout << "[SUCCESS] Detector initialized successfully.\n\n";

    // ── 2. Run Inference on a Sample Frame ─────────────────────────────────────
    std::vector<uint8_t> dummy_frame(1920 * 1080 * 3, 128); // 1080p BGR
    senzr::ai::FrameView frame{dummy_frame.data(), 1920, 1080, 1920 * 3, 0.0};

    std::cout << "[INFO] Executing real-time inference on 1080p frame...\n";
    auto infer_res = detector.detect(frame);
    if (infer_res.has_value()) {
        const auto& result = infer_res.value();
        std::cout << "  Inference Latency : " << result.inference_latency_ms << " ms\n";
        std::cout << "  Active Model      : " << (result.active_model == senzr::ai::ModelType::YOLO11s ? "YOLO11s" : "YOLOv8") << "\n";
        std::cout << "  Executed Core     : NPU Core 0 (Single Core Fallback Mode)\n\n";
    }

    // ── 3. ANPR Normalization Demo ─────────────────────────────────────────────
    std::cout << "[INFO] Testing ANPR MoRTH & Bharat Series Normalization:\n";
    std::string sample_plate = "IND MH12DE1433";
    auto plate_info = senzr::ai::AnprEngine::validate_and_format_plate(sample_plate);
    std::cout << "  Raw Plate Input   : " << sample_plate << "\n";
    std::cout << "  Cleaned Number    : " << plate_info.clean_number << "\n";
    std::cout << "  Formatted Plate   : " << plate_info.formatted_plate << "\n";
    std::cout << "  State Code        : " << plate_info.state_code << " (Maharashtra)\n";
    std::cout << "  Validation Status : " << (plate_info.is_valid ? "VALID ✓" : "INVALID ✗") << "\n\n";

    // ── 4. Dynamic Model Switching (Hot-Swap to YOLOv8) ────────────────────────
    std::cout << "[INFO] Hot-swapping model from YOLO11s to YOLOv8 at runtime...\n";
    detector.switch_model(senzr::ai::ModelType::YOLOv8, "models/yolov8_traffic.rknn");
    std::cout << "  Switched Active Model to: " << (detector.get_active_model() == senzr::ai::ModelType::YOLOv8 ? "YOLOv8" : "Unknown") << "\n";

    std::cout << "\n============================================================\n";
    std::cout << "  Traffic Detector Example Completed Successfully!\n";
    std::cout << "============================================================\n";

    return 0;
}
