#include "session_pipeline/session.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <stdexcept>

namespace session_pipeline {

using json = nlohmann::json;

// ── Timestamp helper ──────────────────────────────────────────────────────────

[[maybe_unused]] static std::string utc_now_iso() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &tt);
#else
    gmtime_r(&tt, &tm_utc);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// Session ID: session_YYYYMMDD_HHMMSS (UTC) — matches Python format exactly
static std::string make_session_id() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &tt);
#else
    gmtime_r(&tt, &tm_utc);
#endif
    std::ostringstream oss;
    oss << "session_" << std::put_time(&tm_utc, "%Y%m%d_%H%M%S");
    return oss.str();
}

// ── create_session ────────────────────────────────────────────────────────────

SessionHandle create_session(const std::filesystem::path& recordings_dir) {
    std::filesystem::create_directories(recordings_dir);

    SessionHandle h;
    h.session_id  = make_session_id();
    h.session_dir = recordings_dir / h.session_id;
    std::filesystem::create_directories(h.session_dir);
    return h;
}

// ── write_session_json ────────────────────────────────────────────────────────

void write_session_json(const std::filesystem::path& session_dir,
                        const SessionInfo& info) {
    json j = {
        {"session_id",          info.session_id},
        {"session_path",        info.session_dir.string()},
        {"start_time",          info.start_time},
        {"end_time",            info.end_time},
        {"device",              info.device_name},
        {"model",               info.model_name},
        {"video_file",          info.video_file},
        {"audio_file",          info.audio_file},
        {"transcript_file",     info.transcript_file},
        {"merged_video_file",   info.merged_video_file},
        {"transcript_status",   info.transcript_status},
        {"merge_status",        "pending"},
        {"artifacts", {
            {"video",      info.video_file},
            {"audio",      info.audio_file},
            {"transcript", info.transcript_file}
        }}
    };

    auto path = session_dir / "session.json";
    std::ofstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot write session.json: " + path.string());
    f << j.dump(2) << '\n';
}

// ── read_session_json ─────────────────────────────────────────────────────────

SessionInfo read_session_json(const std::filesystem::path& session_dir) {
    auto path = session_dir / "session.json";
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot read session.json: " + path.string());

    json j;
    f >> j;

    SessionInfo info;
    info.session_id       = j.value("session_id",        "");
    info.session_dir      = j.value("session_path",      "");
    info.start_time       = j.value("start_time",        "");
    info.end_time         = j.value("end_time",          "");
    info.device_name      = j.value("device",            "");
    info.model_name       = j.value("model",             "");
    info.video_file       = j.value("video_file",        "video.mp4");
    info.audio_file       = j.value("audio_file",        "audio.wav");
    info.transcript_file  = j.value("transcript_file",   "transcript.txt");
    info.transcript_status= j.value("transcript_status", "pending");
    return info;
}

// ── update_session_transcript_status ─────────────────────────────────────────

void update_session_transcript_status(
    const std::filesystem::path& session_json_path,
    const std::string& status)
{
    json j;
    {
        std::ifstream f(session_json_path);
        if (f.is_open()) {
            f >> j;
        }
    }
    j["transcript_status"] = status;
    j["transcript_file"]   = "transcript.txt";
    {
        std::ofstream f(session_json_path);
        f << j.dump(2) << '\n';
    }
}

} // namespace session_pipeline
