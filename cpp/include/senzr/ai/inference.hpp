#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include "senzr/platform.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace senzr::ai {

/**
 * @brief Inference execution acceleration backend.
 */
enum class BackendType {
    Auto = 0,
    Rknn,      // Rockchip RK3588 NPU
    TensorRT,  // NVIDIA Jetson Nano TensorRT
    Cuda,      // NVIDIA CUDA Direct
    Cpu        // Portable CPU Fallback
};

/**
 * @brief Supported tensor data formats.
 */
enum class DataType {
    Float32,
    Float16,
    Int8,
    Uint8,
    Int32
};

/**
 * @brief Multi-dimensional tensor shape representation.
 */
struct TensorShape {
    std::vector<int64_t> dims;

    size_t element_count() const {
        if (dims.empty()) return 0;
        size_t cnt = 1;
        for (auto d : dims) cnt *= static_cast<size_t>(d > 0 ? d : 1);
        return cnt;
    }
};

/**
 * @brief Platform-agnostic tensor buffer container.
 */
class Tensor {
public:
    Tensor();
    Tensor(std::string name, TensorShape shape, DataType dtype);
    ~Tensor();

    Tensor(const Tensor& other);
    Tensor& operator=(const Tensor& other);
    Tensor(Tensor&& other) noexcept;
    Tensor& operator=(Tensor&& other) noexcept;

    const std::string& name() const;
    const TensorShape& shape() const;
    DataType data_type() const;

    void set_data(const void* src, size_t byte_size);
    const void* data() const;
    void* mutable_data();
    size_t byte_size() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

/**
 * @brief Inference performance and output telemetry container.
 */
struct InferenceResult {
    std::vector<Tensor> output_tensors;
    double latency_ms{0.0};
    double pre_ms{0.0};
    double inference_core_ms{0.0};
    double post_ms{0.0};
    BackendType executed_backend{BackendType::Cpu};
};

/**
 * @brief AI Model loading and deployment configuration.
 */
struct InferenceConfig {
    std::string model_path;
    BackendType preferred_backend{BackendType::Auto};
    int target_core_or_device{0}; // Core mask for RKNN (0,1,2) or GPU index for CUDA
    int batch_size{1};
};

/**
 * @brief Vendor-neutral multi-platform inference engine.
 *
 * Automatically dispatches inference execution to the active accelerator:
 * - Rockchip RK3588: RKNN NPU Triple-Core
 * - NVIDIA Jetson Nano: TensorRT / CUDA Maxwell GPU
 * - Generic Linux / Host: Multi-threaded CPU Fallback
 */
class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    InferenceEngine(const InferenceEngine&) = delete;
    InferenceEngine& operator=(const InferenceEngine&) = delete;
    InferenceEngine(InferenceEngine&&) noexcept;
    InferenceEngine& operator=(InferenceEngine&&) noexcept;

    Result<void> load_model(const InferenceConfig& config);
    bool is_loaded() const;

    Result<InferenceResult> infer(const std::vector<Tensor>& inputs);
    Result<InferenceResult> infer(const Tensor& single_input);

    BackendType get_active_backend() const;
    const InferenceConfig& get_config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace senzr::ai
