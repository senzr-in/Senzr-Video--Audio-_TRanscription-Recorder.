#pragma once
#include <atomic>
#include <mutex>
#include <filesystem>
#include <string>
#include "session_pipeline/queues.hpp"
#include "session_pipeline/models.hpp"

namespace session_pipeline {

// ── StreamingTranscriber ──────────────────────────────────────────────────────
// Mirrors Python transcriber.py: StreamingTranscriber(threading.Thread)
//
// Consumes AudioChunks from transcription_queue.
// Accumulates a PCM buffer; every WINDOW_SEC seconds processes a window
// through whisper.cpp (tiny model, CPU inference).
// Deduplicates overlapping text. Appends to transcript.txt.
// On sentinel (empty pcm chunk): flushes buffer and finalises session.
class StreamingTranscriber {
public:
    StreamingTranscriber(TranscriptionQueue& transcription_q,
                         UploadQueue&        upload_q);
    ~StreamingTranscriber();

    void set_session_dir(const std::filesystem::path& dir);
    void run(std::stop_token stoken);

private:
    void process_window(const std::vector<float>& audio_f32,
                        const std::filesystem::path& transcript_path);
    void flush(const std::vector<int16_t>& buffer,
               const std::filesystem::path& transcript_path);
    std::string deduplicate(const std::string& existing,
                            const std::string& new_text) const;

    TranscriptionQueue& transcription_q_;
    UploadQueue&        upload_q_;

    // session_dir is set after pipeline starts recording
    // Protected by mutex to allow set from another thread
    mutable std::mutex    session_dir_mutex_;
    std::filesystem::path session_dir_;

    std::string full_text_;

    // Whisper model handle (opaque pointer to avoid pulling whisper.h everywhere)
    void* whisper_ctx_{nullptr};
};

} // namespace session_pipeline
