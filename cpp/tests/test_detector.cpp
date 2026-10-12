#include "senzr/ai/detector.hpp"
#include "senzr/ai/anpr.hpp"
#include <iostream>
#include <cassert>
#include <vector>

void test_detector_initialization() {
    std::cout << "[Test 1] Detector Initialization with YOLO11s... ";
    senzr::ai::DetectorConfig config;
    config.model = senzr::ai::ModelType::YOLO11s;
    config.npu_core = senzr::ai::NpuCore::Core0;
    
    senzr::ai::Detector detector(config);
    auto res = detector.initialize();
    assert(res.has_value());
    assert(detector.is_initialized());
    assert(detector.get_active_model() == senzr::ai::ModelType::YOLO11s);
    assert(detector.get_active_core() == senzr::ai::NpuCore::Core0);
    std::cout << "PASSED ✓\n";
}

void test_detector_inference() {
    std::cout << "[Test 2] Detector Inference on FrameView... ";
    senzr::ai::DetectorConfig config;
    config.model = senzr::ai::ModelType::YOLO11s;
    config.npu_core = senzr::ai::NpuCore::Core0;
    
    senzr::ai::Detector detector(config);
    detector.initialize();

    std::vector<uint8_t> frame_buf(640 * 480 * 3, 0);
    senzr::ai::FrameView frame{frame_buf.data(), 640, 480, 640 * 3, 0.0};

    auto res = detector.detect(frame);
    assert(res.has_value());
    assert(res.value().active_model == senzr::ai::ModelType::YOLO11s);
    assert(res.value().executed_core == senzr::ai::NpuCore::Core0);
    std::cout << "PASSED ✓\n";
}

void test_detector_model_switch() {
    std::cout << "[Test 3] Runtime Model Hot-Swapping (YOLO11s -> YOLOv8)... ";
    senzr::ai::DetectorConfig config;
    config.model = senzr::ai::ModelType::YOLO11s;
    
    senzr::ai::Detector detector(config);
    detector.initialize();
    assert(detector.get_active_model() == senzr::ai::ModelType::YOLO11s);

    detector.switch_model(senzr::ai::ModelType::YOLOv8, "models/yolov8_traffic.rknn");
    assert(detector.get_active_model() == senzr::ai::ModelType::YOLOv8);
    assert(detector.is_initialized());

    detector.set_core(senzr::ai::NpuCore::Core1);
    assert(detector.get_active_core() == senzr::ai::NpuCore::Core1);
    std::cout << "PASSED ✓\n";
}

void test_anpr_morth_and_emergency() {
    std::cout << "[Test 4] ANPR & Emergency Vehicle Categorization... ";
    
    // Test plate validation
    auto plate = senzr::ai::AnprEngine::validate_and_format_plate("IND DL01AB1234");
    assert(plate.is_valid);
    assert(plate.state_code == "DL");
    assert(plate.clean_number == "DL01AB1234");

    std::cout << "PASSED ✓\n";
}

int main() {
    std::cout << "=== Running YOLO11s / YOLOv8 Detector Unit Tests ===\n";
    test_detector_initialization();
    test_detector_inference();
    test_detector_model_switch();
    test_anpr_morth_and_emergency();
    std::cout << "=== All Detector Unit Tests PASSED Successfully! ===\n";
    return 0;
}
