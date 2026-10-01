#pragma once
#include <atomic>
#include "session_pipeline/queues.hpp"
#include "session_pipeline/models.hpp"

namespace session_pipeline {

// ── AudioCaptureWorker ────────────────────────────────────────────────────────
// Mirrors Python audio_capture.py: AudioCaptureWorker
//
// Launches arecord subprocess and reads 0.5s chunks continuously.
// Feeds audio_frame_queue always; feeds transcription_queue only when recording.
class AudioCaptureWorker {
public:
    AudioCaptureWorker(AudioFrameQueue&    audio_q,
                       TranscriptionQueue& transcription_q,
                       std::atomic<bool>&  recording_active);

    void run(std::stop_token stoken);

private:
    AudioFrameQueue&    audio_q_;
    TranscriptionQueue& transcription_q_;
    std::atomic<bool>&  recording_active_;
};

} // namespace session_pipeline
