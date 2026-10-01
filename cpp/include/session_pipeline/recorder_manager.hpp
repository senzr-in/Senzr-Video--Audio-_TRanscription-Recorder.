#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include "session_pipeline/queues.hpp"
#include "session_pipeline/models.hpp"
#include "session_pipeline/transcriber.hpp"
#include "app/database.hpp"

namespace session_pipeline {

// ── RecorderManagerWorker ─────────────────────────────────────────────────────
// Mirrors Python recorder_manager.py: RecorderManagerWorker
//
// Waits for START_RECORDING events on event_queue.
// On each event: creates a session, writes video.mp4 + audio.wav,
// finalizes session.json, and queues all 3 files for upload.
class RecorderManagerWorker {
public:
    RecorderManagerWorker(RecordFrameQueue&   video_q,
                          AudioFrameQueue&    audio_q,
                          EventQueue&         event_q,
                          UploadQueue&        upload_q,
                          TranscriptionQueue& transcription_q,
                          std::atomic<bool>&  recording_active,
                          app::Database&      database,
                          StreamingTranscriber* transcriber = nullptr);

    void run(std::stop_token stoken);

    // Read by API for status
    std::string current_recording_file() const;
    std::string last_recorded_file() const;
    std::string current_session_id() const;

private:
    void record_session(std::stop_token stoken);

    RecordFrameQueue&   video_q_;
    AudioFrameQueue&    audio_q_;
    EventQueue&         event_q_;
    UploadQueue&        upload_q_;
    TranscriptionQueue& transcription_q_;
    std::atomic<bool>&  recording_active_;
    app::Database&      database_;
    StreamingTranscriber* transcriber_{nullptr};

    mutable std::mutex  file_mutex_;
    std::string         current_file_;
    std::string         current_session_id_;
    std::string         last_file_;
};

} // namespace session_pipeline
