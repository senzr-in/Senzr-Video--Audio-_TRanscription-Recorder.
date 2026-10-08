#include <senzr/senzr.hpp>
#include <cassert>
#include <iostream>

int main() {
    std::cout << "=== Unit Test: Multi-Platform Detection & Capabilities ===\n";

    auto& dev = senzr::Device::get_instance();
    auto plat = dev.get_platform();
    std::cout << "Detected Platform: " << senzr::platform_to_string(plat) << "\n";
    assert(plat != senzr::Platform::Unknown);

    auto pinfo_res = dev.get_platform_info();
    assert(pinfo_res.has_value());
    const auto& pinfo = pinfo_res.value();
    std::cout << "Platform Name: " << pinfo.platform_name << "\n";
    assert(!pinfo.platform_name.empty());
    assert(pinfo.cpu_cores > 0);

    // Test capability checks
    assert(dev.has_capability(senzr::Capability::Camera));
    assert(dev.has_capability(senzr::Capability::Gpio));

    // Test AI Inference Engine
    senzr::ai::InferenceEngine ai_engine;
    auto load_res = ai_engine.load_model({
        .model_path = "model.engine",
        .preferred_backend = senzr::ai::BackendType::Auto
    });
    assert(load_res.has_value());
    assert(ai_engine.is_loaded());

    senzr::ai::Tensor in_tensor("input", {{1, 3, 224, 224}}, senzr::ai::DataType::Float32);
    assert(in_tensor.shape().element_count() == 1 * 3 * 224 * 224);
    assert(in_tensor.byte_size() == 1 * 3 * 224 * 224 * 4);

    auto infer_res = ai_engine.infer(in_tensor);
    assert(infer_res.has_value());
    assert(!infer_res.value().output_tensors.empty());
    assert(infer_res.value().latency_ms > 0.0);

    // Test Camera Manager
    auto& cam_mgr = senzr::media::CameraManager::get_instance();
    auto cams = cam_mgr.discover_cameras();
    auto def_cam_res = cam_mgr.get_default_camera();
    if (!cams.empty()) {
        assert(def_cam_res.has_value());
        assert(!def_cam_res.value().id.empty());
    } else {
        assert(!def_cam_res.has_value());
        assert(def_cam_res.error().code() == senzr::ErrorCode::ResourceUnavailable);
    }

    std::cout << "=== Platform Unit Tests PASSED ===\n";
    return 0;
}
