#pragma once
#include <string>
#include <filesystem>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace session_pipeline {

// ── VideoFrame ────────────────────────────────────────────────────────────────
// Mirrors Python: {"timestamp": float, "frame": np.ndarray (BGR, HWC, uint8)}
struct VideoFrame {
    std::vector<uint8_t> data;   // BGR pixel data, row-major
    int                  width  = 640;
    int                  height = 480;
    double               timestamp = 0.0;  // seconds since epoch

    // Convenience: total bytes
    std::size_t bytes() const {
        return static_cast<std::size_t>(width) * height * 3;
    }
};

// ── AudioChunk ────────────────────────────────────────────────────────────────
// Mirrors Python: {"timestamp": float, "raw": bytes (S16_LE, interleaved stereo)}
struct AudioChunk {
    std::vector<int16_t> pcm;        // interleaved stereo S16_LE samples
    double               timestamp = 0.0;
};

// ── PipelineEvent ─────────────────────────────────────────────────────────────
// Mirrors Python: {"event": "START_RECORDING"|"STOP_RECORDING", "timestamp": float}
enum class EventType {
    StartRecording,
    StopRecording
};

struct PipelineEvent {
    EventType type;
    double    timestamp = 0.0;
};

// ── UploadJob ─────────────────────────────────────────────────────────────────
// Mirrors Python: {"session_id": str, "file": str}
struct UploadJob {
    std::string session_id;
    std::string file_path;
};

// ── SessionInfo ───────────────────────────────────────────────────────────────
// Mirrors Python SessionInfo dataclass in session_pipeline/models.py
struct SessionInfo {
    std::string           session_id;
    std::filesystem::path session_dir;
    std::string           start_time;
    std::string           end_time;
    std::string           video_file        = "video.mp4";
    std::string           audio_file        = "audio.wav";
    std::string           transcript_file   = "transcript.txt";
    std::string           merged_video_file = "merged_video.mp4";
    std::string           transcript_status = "pending";
    std::string           upload_status     = "pending";
    std::string           device_name;
    std::string           model_name;
};

} // namespace session_pipeline
