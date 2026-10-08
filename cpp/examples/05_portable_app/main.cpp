#include <senzr/senzr.hpp>
#include <iostream>

int main() {
    std::cout << "======================================================\n";
    std::cout << "  🚀 SENZR MULTI-PLATFORM PORTABLE SDK APPLICATION    \n";
    std::cout << "======================================================\n";

    // 1. Version and Logging
    std::cout << "SDK Version: " << senzr::version_string() << "\n";
    senzr::log::set_level(senzr::log::Level::Info);

    // 2. Multi-Platform Identification
    auto& device = senzr::Device::get_instance();
    auto plat = device.get_platform();
    std::cout << "\n[PLATFORM DETECTION]\n";
    std::cout << "  Active Target Platform : " << senzr::platform_to_string(plat) << "\n";

    auto plat_info_res = device.get_platform_info();
    if (plat_info_res) {
        const auto& pinfo = plat_info_res.value();
        std::cout << "  Architecture           : " << pinfo.architecture << "\n";
        std::cout << "  CPU Model              : " << pinfo.cpu_model << " (" << pinfo.cpu_cores << " Cores)\n";
        std::cout << "  System Memory          : " << pinfo.ram_total_mb << " MB\n";
        std::cout << "  Hardware Accelerator   : " << pinfo.accelerator.name << "\n";
        std::cout << "  Compute Capacity       : " << pinfo.accelerator.tops_or_tflops 
                  << (pinfo.accelerator.type == senzr::AcceleratorType::RockchipNpu ? " TOPS" : " TFLOPS (FP16)") << "\n";
    }

    // 3. Capability Discovery
    std::cout << "\n[CAPABILITY AUDIT]\n";
    for (auto cap : {
        senzr::Capability::Camera,
        senzr::Capability::Gpio,
        senzr::Capability::Npu,
        senzr::Capability::Cuda,
        senzr::Capability::TensorRT,
        senzr::Capability::Rknn,
        senzr::Capability::HardwareVideoEncoder,
        senzr::Capability::Watchdog
    }) {
        std::cout << "  - " << senzr::capability_to_string(cap) << ": "
                  << (device.has_capability(cap) ? "✅ AVAILABLE" : "❌ NOT SUPPORTED") << "\n";
    }

    // 4. Vendor-Neutral AI Inference
    std::cout << "\n[VENDOR-NEUTRAL AI INFERENCE ENGINE]\n";
    senzr::ai::InferenceEngine ai_engine;
    auto load_res = ai_engine.load_model({
        .model_path = "/opt/senzr/models/yolov8s.engine",
        .preferred_backend = senzr::ai::BackendType::Auto
    });

    if (load_res) {
        senzr::ai::Tensor input_tensor("images", {{1, 3, 640, 640}}, senzr::ai::DataType::Float32);
        auto infer_res = ai_engine.infer(input_tensor);
        if (infer_res) {
            const auto& r = infer_res.value();
            std::cout << "  Inference executed successfully!\n";
            std::cout << "  Backend Used  : " << (r.executed_backend == senzr::ai::BackendType::Rknn ? "RKNN (RK3588 NPU)" :
                                                r.executed_backend == senzr::ai::BackendType::TensorRT ? "TensorRT (Jetson Nano GPU)" :
                                                r.executed_backend == senzr::ai::BackendType::Cuda ? "CUDA (NVIDIA GPU)" : "CPU Software") << "\n";
            std::cout << "  Core Latency  : " << r.inference_core_ms << " ms\n";
            std::cout << "  Total Latency : " << r.latency_ms << " ms\n";
            std::cout << "  Output Tensors: " << r.output_tensors.size() << "\n";
        }
    }

    // 5. Vendor-Neutral Camera Discovery
    std::cout << "\n[CAMERA & MEDIA SUBSYSTEM]\n";
    auto& camera_mgr = senzr::media::CameraManager::get_instance();
    auto cameras = camera_mgr.discover_cameras();
    std::cout << "  Discovered Cameras: " << cameras.size() << "\n";
    for (const auto& cam : cameras) {
        std::cout << "    * " << cam.id << " (" << cam.name << ") [CSI: " << (cam.is_csi ? "YES" : "NO") 
                  << ", USB: " << (cam.is_usb ? "YES" : "NO") << "]\n";
    }

    // 6. Platform-Agnostic GPIO Control
    std::cout << "\n[GPIO HARDWARE CONTROL]\n";
    auto& gpio = senzr::gpio::get_manager();
    std::cout << "  Triggering non-blocking pulse on Door Relay (Header Pin 7)...\n";
    gpio.pulse(7, 1000);
    std::cout << "  Relay pulse dispatched successfully.\n";

    std::cout << "\n=== Multi-Platform Portable Application PASSED ===\n";
    return 0;
}
