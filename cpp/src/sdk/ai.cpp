#include "senzr/ai/person_detector.hpp"
#include "senzr/ai/anpr.hpp"
#include "session_pipeline/plate_utils.hpp"
#include <iostream>

namespace senzr::ai {

struct PersonDetector::Impl {
    PersonDetectorConfig config;
    bool initialized{false};

    explicit Impl(PersonDetectorConfig cfg) : config(std::move(cfg)) {}
};

PersonDetector::PersonDetector(PersonDetectorConfig config)
    : pimpl_(std::make_unique<Impl>(std::move(config))) {}

PersonDetector::~PersonDetector() = default;
PersonDetector::PersonDetector(PersonDetector&&) noexcept = default;
PersonDetector& PersonDetector::operator=(PersonDetector&&) noexcept = default;

Result<void> PersonDetector::initialize() {
    pimpl_->initialized = true;
    return Result<void>::success();
}

bool PersonDetector::is_initialized() const noexcept {
    return pimpl_ && pimpl_->initialized;
}

void PersonDetector::close() {
    if (pimpl_) pimpl_->initialized = false;
}

Result<std::vector<Detection>> PersonDetector::detect(const FrameView& frame) {
    if (!is_initialized()) {
        return Result<std::vector<Detection>>(ErrorCode::NotInitialized, "PersonDetector is not initialized");
    }
    if (!frame.bgr_data || frame.width <= 0 || frame.height <= 0) {
        return Result<std::vector<Detection>>(ErrorCode::InvalidArgument, "Invalid frame buffer provided");
    }

    std::vector<Detection> results;
    // Simulated detection output or pass to RKNN
    return results;
}

// ── AnprEngine Implementation ────────────────────────────────────────────────

std::string AnprEngine::strip_hsrp_prefix(const std::string& raw_text) {
    return session_pipeline::PlateUtils::strip_hsrp_ind_prefix(raw_text);
}

std::string AnprEngine::normalize_plate_characters(const std::string& text) {
    return session_pipeline::PlateUtils::normalize_plate_characters(text);
}

PlateInfo AnprEngine::validate_and_format_plate(const std::string& raw_text, float confidence) {
    auto res = session_pipeline::PlateUtils::validate_and_format_plate(raw_text, confidence);
    PlateInfo info;
    info.is_valid         = res.is_valid;
    info.raw_text         = res.raw_text;
    info.formatted_plate  = res.formatted_plate;
    info.clean_number     = res.clean_number;
    info.state_code       = res.state_code;
    info.rto_code         = res.rto_code;
    info.series           = res.series;
    info.unique_digits    = res.unique_digits;
    info.confidence       = res.confidence;
    info.is_multiline     = res.is_multiline;
    info.box              = BoundingBox{res.x, res.y, res.w, res.h};
    return info;
}

} // namespace senzr::ai
