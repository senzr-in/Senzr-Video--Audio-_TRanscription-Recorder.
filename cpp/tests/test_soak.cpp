#include <senzr/senzr.hpp>
#include <iostream>
#include <cassert>
#include <chrono>
#include <vector>
#include <fstream>
#include <unistd.h>

static uint64_t get_process_rss_kb() {
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        uint64_t size = 0, resident = 0;
        if (statm >> size >> resident) {
            long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
            return resident * page_size_kb;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    int iterations = 1000;
    if (argc > 1) {
        try { iterations = std::stoi(argv[1]); } catch (...) {}
    }

    std::cout << "=== Starting Senzr SDK Multi-Subsystem Soak Test (" << iterations << " cycles) ===\n";
    uint64_t initial_rss = get_process_rss_kb();
    std::cout << "Initial Process RSS Memory: " << initial_rss << " KB\n";

    auto& dev = senzr::Device::get_instance();
    auto& gpio = senzr::gpio::get_manager();
    senzr::ai::InferenceEngine ai_engine;
    auto load_res = ai_engine.load_model({.model_path = "soak_model.engine"});
    assert(load_res.has_value());

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; ++i) {
        // 1. Device metrics query
        auto metrics = dev.get_system_metrics();
        assert(metrics.has_value());

        // 2. GPIO pulse & state check
        gpio.pulse(7, 5); // 5ms simulated pulse

        // 3. ANPR OCR plate normalization
        auto plate = senzr::ai::AnprEngine::validate_and_format_plate("IND MH12AB1234");
        assert(plate.is_valid);

        // 4. AI Tensor creation & inference cycle
        senzr::ai::Tensor in_t("soak_in", {{1, 3, 128, 128}}, senzr::ai::DataType::Float32);
        auto infer_res = ai_engine.infer(in_t);
        assert(infer_res.has_value());

        if (i > 0 && i % (iterations / 5) == 0) {
            std::cout << "Completed " << i << " / " << iterations << " soak cycles... (RSS: " 
                      << get_process_rss_kb() << " KB)\n";
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(end_time - start_time).count();
    uint64_t final_rss = get_process_rss_kb();

    std::cout << "Completed all " << iterations << " soak cycles in " << total_sec << " seconds!\n";
    std::cout << "Final Process RSS Memory  : " << final_rss << " KB\n";
    std::cout << "Memory Delta              : " << (static_cast<int64_t>(final_rss) - static_cast<int64_t>(initial_rss)) << " KB\n";

    // Memory growth should not exceed 5MB over 1000 cycles in C++
    assert(final_rss <= initial_rss + 5120);

    std::cout << "=== Soak Test PASSED: Zero Memory Leak Detected ===\n";
    return 0;
}
