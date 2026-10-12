#include "senzr/media/recorder.hpp"
#include <atomic>
#include <filesystem>

namespace senzr::media {

struct Recorder::Impl {
    RecorderConfig config;
    std::atomic<bool> recording{false};
    std::string current_session;
    std::string current_file;

    explicit Impl(RecorderConfig cfg) : config(std::move(cfg)) {}
};

Recorder::Recorder(RecorderConfig config)
    : pimpl_(std::make_unique<Impl>(std::move(config))) {}

Recorder::~Recorder() = default;
Recorder::Recorder(Recorder&&) noexcept = default;
Recorder& Recorder::operator=(Recorder&&) noexcept = default;

Result<void> Recorder::start(const std::string& session_id) {
    if (pimpl_->recording.load()) {
        return Result<void>(ErrorCode::AlreadyInitialized, "Recording is already in progress");
    }
    pimpl_->current_session = session_id;
    pimpl_->current_file = pimpl_->config.output_dir + "/" + session_id + ".mp4";
    pimpl_->recording.store(true);
    return Result<void>::success();
}

Result<void> Recorder::write_frame(const ai::FrameView& /*frame*/) {
    if (!pimpl_->recording.load()) {
        return Result<void>(ErrorCode::NotInitialized, "Recorder is not running");
    }
    return Result<void>::success();
}

Result<std::string> Recorder::stop() {
    if (!pimpl_->recording.load()) {
        return Result<std::string>(ErrorCode::NotInitialized, "Recorder is not running");
    }
    pimpl_->recording.store(false);
    return pimpl_->current_file;
}

bool Recorder::is_recording() const noexcept {
    return pimpl_ && pimpl_->recording.load();
}

} // namespace senzr::media
