#include <senzr/senzr.hpp>
#include <iostream>
#include <cassert>

int main() {
    std::cout << "=== Testing Unsupported Capabilities & ErrorCode::NotSupported ===\n";

    auto& dev = senzr::Device::get_instance();

    // Check capability handling
    bool has_cuda = dev.has_capability(senzr::Capability::Cuda);
    bool has_rknn = dev.has_capability(senzr::Capability::Rknn);

    std::cout << "Target CUDA support: " << (has_cuda ? "YES" : "NO") << "\n";
    std::cout << "Target RKNN support: " << (has_rknn ? "YES" : "NO") << "\n";

    // Test service control with invalid action
    auto invalid_res = dev.control_service("invalid_action_xyz");
    assert(!invalid_res.has_value());
    assert(invalid_res.error().code() == senzr::ErrorCode::InvalidArgument);

    // Test ensure non-existent service
    auto svc_res = dev.ensure_service_installed("non_existent_dummy_service_1234");
    assert(!svc_res.has_value());
    assert(svc_res.error().code() == senzr::ErrorCode::NotSupported);

    // Test invalid GPIO pin
    auto& gpio = senzr::gpio::get_manager();
    auto invalid_gpio = gpio.pulse(999, 100);
    // Invalid pin should fail gracefully without crashing
    (void)invalid_gpio;

    std::cout << "=== Unsupported Capabilities Tests PASSED ===\n";
    return 0;
}
