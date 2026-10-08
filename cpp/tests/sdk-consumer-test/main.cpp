#include <senzr/senzr.hpp>
#include <iostream>
#include <cassert>

int main() {
    std::cout << "=== External SDK Consumer Integration Test ===\n";

    // 1. Version Check
    std::cout << "Senzr SDK Version: " << senzr::version_string() << "\n";
    assert(std::string(senzr::version_string()) == "1.1.0");

    // 2. Logging Check
    senzr::log::set_level(senzr::log::Level::Info);
    senzr::log::info("ConsumerApp", "Logging configured from external consumer");

    // 3. Device API Check
    auto& dev = senzr::Device::get_instance();
    auto info_res = dev.get_device_info();
    if (info_res) {
        std::cout << "Device MAC: " << info_res.value().mac_address << "\n";
    }

    // 4. GPIO API Check
    auto& gpio = senzr::gpio::get_manager();
    auto pins = gpio.get_all_pins();
    std::cout << "Discovered GPIO pins: " << pins.size() << "\n";

    // 5. ANPR Engine Check
    auto plate = senzr::ai::AnprEngine::validate_and_format_plate("KA05MJ1234");
    assert(plate.is_valid);
    assert(plate.formatted_plate == "KA 05 MJ 1234");
    std::cout << "ANPR validated plate: " << plate.formatted_plate << "\n";

    // 6. Media Recorder Check
    senzr::media::Recorder recorder;
    assert(!recorder.is_recording());

    // 7. Multi-Platform & Capability Check
    auto plat = dev.get_platform();
    std::cout << "Consumer Detected Platform: " << senzr::platform_to_string(plat) << "\n";

    // 8. Vendor-Neutral AI Inference Check
    senzr::ai::InferenceEngine engine;
    auto load_res = engine.load_model({.model_path = "model.engine"});
    assert(load_res.has_value());
    senzr::ai::Tensor in_t("in", {{1, 3, 300, 300}}, senzr::ai::DataType::Float32);
    auto infer_res = engine.infer(in_t);
    assert(infer_res.has_value());
    std::cout << "Consumer AI Inference Latency: " << infer_res.value().latency_ms << " ms\n";

    std::cout << "=== External Consumer Test PASSED ===\n";
    return 0;
}
