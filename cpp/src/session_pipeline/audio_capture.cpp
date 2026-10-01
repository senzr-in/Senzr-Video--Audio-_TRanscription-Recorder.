#include "session_pipeline/audio_capture.hpp"
#include "session_pipeline/config.hpp"
#include <stdio.h>    // popen / pclose
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <signal.h>
#include <cstring>

namespace session_pipeline {

AudioCaptureWorker::AudioCaptureWorker(AudioFrameQueue&    audio_q,
                                       TranscriptionQueue& transcription_q,
                                       std::atomic<bool>&  recording_active)
    : audio_q_(audio_q)
    , transcription_q_(transcription_q)
    , recording_active_(recording_active) {}

void AudioCaptureWorker::run(std::stop_token stoken) {
    // Build arecord command — mirrors Python exactly
    // arecord -D plughw:2,0 -c 2 -r 48000 -f S16_LE -t raw --quiet
    std::ostringstream cmd;
    cmd << "arecord"
        << " -D " << ALSA_DEVICE
        << " -c "  << CHANNELS
        << " -r "  << SAMPLE_RATE
        << " -f S16_LE"
        << " -t raw"
        << " --quiet"
        << " 2>/dev/null";

    const int bytes_per_frame = CHANNELS * 2;  // S16_LE = 2 bytes
    const int chunk_frames    = static_cast<int>(SAMPLE_RATE * CHUNK_SECS);
    const int chunk_bytes     = chunk_frames * bytes_per_frame;

    std::cout << "[audio_capture] started\n";

    FILE* proc = ::popen(cmd.str().c_str(), "r");
    if (!proc) {
        std::cerr << "[audio_capture] ERROR: failed to start arecord\n";
        return;
    }

    std::vector<char> raw_buf(chunk_bytes);

    while (!stoken.stop_requested()) {
        // Read exactly chunk_bytes from arecord stdout
        std::size_t bytes_read = ::fread(raw_buf.data(), 1, chunk_bytes, proc);

        if (bytes_read == 0) {
            // arecord ended or error — sleep briefly and check stop
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        double ts = std::chrono::duration_cast<std::chrono::duration<double>>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

        // Convert raw bytes to int16 samples
        AudioChunk chunk;
        chunk.timestamp = ts;
        const int16_t* samples = reinterpret_cast<const int16_t*>(raw_buf.data());
        const std::size_t n_samples = bytes_read / 2;
        chunk.pcm.assign(samples, samples + n_samples);

        // Always push to audio_frame_queue (drop-oldest if full)
        audio_q_.try_push(chunk);

        // Only push to transcription_queue while recording
        if (recording_active_.load()) {
            transcription_q_.try_push(std::move(chunk));
        }
    }

    ::pclose(proc);
    std::cout << "[audio_capture] stopped\n";
}

} // namespace session_pipeline
