#pragma once
#include <atomic>
#include <mutex>
#include <optional>
#include <memory>
#include <thread>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "session_pipeline/queues.hpp"
#include "session_pipeline/models.hpp"
#include "session_pipeline/video_capture.hpp"
#include "session_pipeline/audio_capture.hpp"
#include "session_pipeline/person_detection.hpp"
#include "session_pipeline/recorder_manager.hpp"
#include "session_pipeline/transcriber.hpp"
#include "session_pipeline/uploader.hpp"
#include "app/database.hpp"

namespace session_pipeline {

// ── SessionPipeline ───────────────────────────────────────────────────────────
// Mirrors Python run_session_pipeline.py: SessionPipeline
//
// Owns all queues, shared state, workers, and threads.
// start() / stop() are thread-safe.
class SessionPipeline {
public:
    // database is injected so the pipeline can persist session data offline.
    explicit SessionPipeline(app::Database& database);
    ~SessionPipeline();

    void start();
    void stop();
    bool is_running()   const;
    bool is_recording() const;

    // For API layer (/api/camera/status)
    std::string current_recording_file() const;
    std::string last_recorded_file()     const;

    // For live vision streaming & detection testing (/api/vision/*)
    std::vector<uint8_t> get_latest_preview_jpeg() const;
    nlohmann::json       get_latest_detections_json() const;
    void                 set_vision_mode(const std::string& mode);

    using PlateDetectedCallback = std::function<void(const std::string& plate, float conf, const std::string& snapshot_fname)>;
    void                 set_plate_detected_callback(PlateDetectedCallback cb);

private:
    void shutdown_all_queues();

    // Injected database reference (owned by ApiServer)
    app::Database& database_;

    // ── Shared queues (owned by pipeline) ────────────────────────────────────
    DetectFrameQueue   detect_frame_q_{2};  // 2 frames max: drops backlog so preview is always live real-time
    RecordFrameQueue   record_frame_q_{120};
    AudioFrameQueue    audio_frame_q_{500};
    EventQueue         event_q_{20};
    TranscriptionQueue transcription_q_{200};
    UploadQueue        upload_q_{100};

    // ── Shared state ──────────────────────────────────────────────────────────
    std::atomic<bool>  recording_active_{false};
    std::atomic<bool>  running_{false};
    mutable std::mutex state_mutex_;

    // ── Workers (valid only while running) ───────────────────────────────────
    std::unique_ptr<VideoCaptureWorker>    video_worker_;
    std::unique_ptr<AudioCaptureWorker>    audio_worker_;
    std::unique_ptr<PersonDetectionWorker> detect_worker_;
    std::unique_ptr<RecorderManagerWorker> recorder_worker_;
    std::unique_ptr<StreamingTranscriber>  transcriber_worker_;
    std::unique_ptr<UploaderWorker>        uploader_worker_;

    // ── Threads ───────────────────────────────────────────────────────────────
    // std::optional because jthread has no default constructor
    std::optional<std::jthread> video_thread_;
    std::optional<std::jthread> audio_thread_;
    std::optional<std::jthread> detect_thread_;
    std::optional<std::jthread> recorder_thread_;
    std::optional<std::jthread> transcriber_thread_;
    std::optional<std::jthread> uploader_thread_;

    // ── Live Stream & Vision Testing State ────────────────────────────────────
    mutable std::mutex   preview_mutex_;
    std::vector<uint8_t> latest_preview_jpeg_;
    nlohmann::json       latest_detections_json_{{"status", "waiting_frames"}};
    std::string          vision_mode_{"both"};
    PlateDetectedCallback plate_callback_;
};

} // namespace session_pipeline
