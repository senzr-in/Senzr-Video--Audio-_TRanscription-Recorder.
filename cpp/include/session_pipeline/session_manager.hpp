#pragma once
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include "session_pipeline/config.hpp"
#include "session_pipeline/session.hpp"

namespace session_pipeline {

class SessionManager {
public:
    explicit SessionManager(std::filesystem::path base_dir = RECORDINGS_DIR)
        : base_dir_(std::move(base_dir)) {}

    std::pair<SessionInfo, std::filesystem::path> create_session() {
        auto [sid, sdir] = session_pipeline::create_session(base_dir_);
        SessionInfo info;
        info.session_id  = sid;
        info.session_dir = sdir;
        info.video_file  = "video.mp4";
        info.audio_file  = "audio.wav";
        session_ = info;
        return {*session_, sdir};
    }

    std::filesystem::path get_video_path() const {
        if (!session_) throw std::runtime_error("Session not created");
        return session_->session_dir / session_->video_file;
    }

    std::filesystem::path get_audio_path() const {
        if (!session_) throw std::runtime_error("Session not created");
        return session_->session_dir / session_->audio_file;
    }

    std::filesystem::path get_session_json_path() const {
        if (!session_) throw std::runtime_error("Session not created");
        return session_->session_dir / "session.json";
    }

private:
    std::filesystem::path base_dir_;
    std::optional<SessionInfo> session_;
};

} // namespace session_pipeline
