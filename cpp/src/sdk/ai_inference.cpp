#include "senzr/ai/inference.hpp"
#include "platform/common/platform_detect.hpp"
#include "senzr/logging.hpp"
#include <chrono>
#include <cstring>
#include <vector>
#include <numeric>

namespace senzr {
inline namespace v1 {
namespace ai {

// ── Tensor Implementation ────────────────────────────────────────────────────

struct Tensor::Impl {
    std::string name;
    TensorShape shape;
    DataType dtype{DataType::Float32};
    std::vector<uint8_t> buffer;

    size_t get_element_size() const {
        switch (dtype) {
            case DataType::Float32:
            case DataType::Int32:   return 4;
            case DataType::Float16: return 2;
            case DataType::Int8:
            case DataType::Uint8:   return 1;
            default:                return 4;
        }
    }
};

Tensor::Tensor() : impl_(std::make_shared<Impl>()) {}

Tensor::Tensor(std::string name, TensorShape shape, DataType dtype)
    : impl_(std::make_shared<Impl>()) {
    impl_->name = std::move(name);
    impl_->shape = std::move(shape);
    impl_->dtype = dtype;
    size_t sz = impl_->shape.element_count() * impl_->get_element_size();
    impl_->buffer.resize(sz, 0);
}

Tensor::~Tensor() = default;
Tensor::Tensor(const Tensor& other) = default;
Tensor& Tensor::operator=(const Tensor& other) = default;
Tensor::Tensor(Tensor&& other) noexcept = default;
Tensor& Tensor::operator=(Tensor&& other) noexcept = default;

const std::string& Tensor::name() const { return impl_->name; }
const TensorShape& Tensor::shape() const { return impl_->shape; }
DataType Tensor::data_type() const { return impl_->dtype; }

void Tensor::set_data(const void* src, size_t byte_size) {
    if (!src || byte_size == 0) return;
    if (impl_->buffer.size() < byte_size) {
        impl_->buffer.resize(byte_size);
    }
    std::memcpy(impl_->buffer.data(), src, byte_size);
}

const void* Tensor::data() const { return impl_->buffer.data(); }
void* Tensor::mutable_data() { return impl_->buffer.data(); }
size_t Tensor::byte_size() const { return impl_->buffer.size(); }


// ── InferenceEngine Implementation ───────────────────────────────────────────

struct InferenceEngine::Impl {
    InferenceConfig config;
    bool is_loaded{false};
    BackendType active_backend{BackendType::Cpu};

    Result<void> init(const InferenceConfig& cfg) {
        config = cfg;

        // Auto-detect optimal backend if set to Auto
        if (config.preferred_backend == BackendType::Auto) {
            Platform plat = platform::detect_host_platform();
            if (plat == Platform::RockchipRK3588) {
                active_backend = BackendType::Rknn;
            } else if (plat == Platform::NvidiaJetsonNano) {
                active_backend = BackendType::TensorRT;
            } else {
                active_backend = BackendType::Cpu;
            }
        } else {
            active_backend = config.preferred_backend;
        }

        log::info("InferenceEngine", "Loaded model " + config.model_path + 
                  " using backend: " + (active_backend == BackendType::Rknn ? "RKNN (RK3588 NPU)" :
                                       active_backend == BackendType::TensorRT ? "TensorRT (Jetson GPU)" :
                                       active_backend == BackendType::Cuda ? "CUDA (NVIDIA GPU)" : "CPU Software"));
        is_loaded = true;
        return Result<void>::success();
    }

    Result<InferenceResult> run_inference(const std::vector<Tensor>& inputs) {
        if (!is_loaded) {
            return Result<InferenceResult>::failure(ErrorCode::InvalidState, "AI Model not loaded");
        }

        auto start = std::chrono::steady_clock::now();
        InferenceResult res;
        res.executed_backend = active_backend;
        res.pre_ms = 0.5;
        
        // Simulate hardware-accelerated latency profiles based on active backend
        if (active_backend == BackendType::Rknn) {
            res.inference_core_ms = 3.2; // ~3.2ms on RK3588 Triple NPU
        } else if (active_backend == BackendType::TensorRT) {
            res.inference_core_ms = 8.5; // ~8.5ms on Jetson Nano FP16
        } else {
            res.inference_core_ms = 18.0; // CPU fallback
        }
        res.post_ms = 0.8;

        // Construct standard mock output tensor with shape matching input batch
        for (const auto& in_t : inputs) {
            Tensor out_t(in_t.name() + "_out", TensorShape{{1, 84, 8400}}, DataType::Float32);
            res.output_tensors.push_back(std::move(out_t));
        }

        auto end = std::chrono::steady_clock::now();
        res.latency_ms = std::chrono::duration<double, std::milli>(end - start).count() + res.inference_core_ms;
        return Result<InferenceResult>::success(res);
    }
};

InferenceEngine::InferenceEngine() : pimpl_(std::make_unique<Impl>()) {}
InferenceEngine::~InferenceEngine() = default;
InferenceEngine::InferenceEngine(InferenceEngine&&) noexcept = default;
InferenceEngine& InferenceEngine::operator=(InferenceEngine&&) noexcept = default;

Result<void> InferenceEngine::load_model(const InferenceConfig& config) {
    return pimpl_->init(config);
}

bool InferenceEngine::is_loaded() const {
    return pimpl_->is_loaded;
}

Result<InferenceResult> InferenceEngine::infer(const std::vector<Tensor>& inputs) {
    return pimpl_->run_inference(inputs);
}

Result<InferenceResult> InferenceEngine::infer(const Tensor& single_input) {
    return pimpl_->run_inference({single_input});
}

BackendType InferenceEngine::get_active_backend() const {
    return pimpl_->active_backend;
}

const InferenceConfig& InferenceEngine::get_config() const {
    return pimpl_->config;
}

} // namespace ai
} // namespace v1
} // namespace senzr
