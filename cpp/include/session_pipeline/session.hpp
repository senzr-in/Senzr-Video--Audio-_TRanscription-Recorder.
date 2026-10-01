#pragma once
#include <filesystem>
#include <string>
#include "session_pipeline/models.hpp"

namespace session_pipeline {

// ── Session creation ──────────────────────────────────────────────────────────
// Mirrors Python session.py: create_session() -> (session_id, session_dir)
// Session ID format: session_YYYYMMDD_HHMMSS (UTC)
struct SessionHandle {
    std::string           session_id;
    std::filesystem::path session_dir;
};

SessionHandle create_session(
    const std::filesystem::path& recordings_dir =
        "/opt/edge-gateway/recordings");

// ── JSON read/write ───────────────────────────────────────────────────────────
// Mirrors Python session.py: write_session_json / read_session_json
void        write_session_json(const std::filesystem::path& session_dir,
                               const SessionInfo& info);

SessionInfo read_session_json(const std::filesystem::path& session_dir);

// Update only transcript_status in an existing session.json
void update_session_transcript_status(
    const std::filesystem::path& session_json_path,
    const std::string& status);

} // namespace session_pipeline
