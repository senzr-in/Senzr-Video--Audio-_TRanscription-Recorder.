#include "session_pipeline/transcriber.hpp"
#include "session_pipeline/config.hpp"
#include "session_pipeline/session.hpp"
#ifdef HAVE_WHISPER
#include <whisper.h>
#endif
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <thread>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace session_pipeline {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

StreamingTranscriber::StreamingTranscriber(TranscriptionQueue& transcription_q,
                                           UploadQueue&        upload_q)
    : transcription_q_(transcription_q)
    , upload_q_(upload_q)
{
    // Lazy model load — defer until first use to avoid startup penalty
}

StreamingTranscriber::~StreamingTranscriber() {
#ifdef HAVE_WHISPER
    if (whisper_ctx_) {
        whisper_free(static_cast<whisper_context*>(whisper_ctx_));
        whisper_ctx_ = nullptr;
    }
#endif
}

void StreamingTranscriber::set_session_dir(const std::filesystem::path& dir) {
    std::lock_guard<std::mutex> lock(session_dir_mutex_);
    session_dir_ = dir;
}

// ─────────────────────────────────────────────────────────────────────────────
// Whisper model (lazy load on first transcription)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef HAVE_WHISPER
static whisper_context* load_whisper_model() {
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = false;   // CPU-only, matches Python device="cpu"
    auto* ctx = whisper_init_from_file_with_params(WHISPER_MODEL_PATH, cparams);
    if (!ctx)
        std::cerr << "[transcriber] WARNING: failed to load whisper model from "
                  << WHISPER_MODEL_PATH << '\n';
    return ctx;
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Main run loop
// ─────────────────────────────────────────────────────────────────────────────

void StreamingTranscriber::run(std::stop_token /*stoken*/) {
    std::cout << "[transcriber] started\n";

    // ── Wait until session_dir is set ─────────────────────────────────────────
    // Mirrors Python: while session_dir is None: continue
    std::filesystem::path session_dir;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(session_dir_mutex_);
            session_dir = session_dir_;
        }
        if (!session_dir.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto transcript_path = session_dir / "transcript.txt";
    std::filesystem::create_directories(transcript_path.parent_path());
    // Create/truncate transcript file (mirrors Python transcript_path.write_text(""))
    { std::ofstream f(transcript_path); }

    // ── PCM accumulation buffer ───────────────────────────────────────────────
    std::vector<int16_t> buffer;

    const int window_samples  = SAMPLE_RATE * WINDOW_SEC  * CHANNELS;
    const int overlap_samples = SAMPLE_RATE * OVERLAP_SEC * CHANNELS;

    // ── Consume loop ──────────────────────────────────────────────────────────
    while (true) {
        // Blocking pop — wait forever (mirrors Python self.tq.get())
        auto maybe = transcription_q_.pop(std::chrono::milliseconds(500));
        if (!maybe) continue;

        auto& chunk = *maybe;

        // Sentinel: empty pcm (mirrors Python None)
        if (chunk.pcm.empty()) {
            flush(buffer, transcript_path);
            break;
        }

        // Append to buffer
        buffer.insert(buffer.end(), chunk.pcm.begin(), chunk.pcm.end());

        // Process full windows
        while (static_cast<int>(buffer.size()) >= window_samples) {
            // Convert window to float32 mono
            std::vector<float> audio_f32;
            audio_f32.reserve(window_samples / CHANNELS);
            for (int i = 0; i < window_samples; i += CHANNELS) {
                float mono = 0.0f;
                for (int c = 0; c < CHANNELS; ++c)
                    mono += float(buffer[i + c]);
                audio_f32.push_back(mono / (CHANNELS * 32768.0f));
            }

            process_window(audio_f32, transcript_path);

            // Slide: keep overlap tail (mirrors Python buffer = buffer[OVERLAP:])
            buffer.erase(buffer.begin(),
                         buffer.begin() + (window_samples - overlap_samples));
        }
    }

    // ── Post-session cleanup ──────────────────────────────────────────────────
    // Mirrors Python: if not current or empty → write [no speech detected]
    {
        std::ifstream f(transcript_path);
        std::string content(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());
        // Trim whitespace
        auto trimmed = content;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
        trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
        if (trimmed.empty()) {
            std::ofstream of(transcript_path);
            of << "[no speech detected]";
        }
    }

    auto session_json = session_dir / "session.json";
    try {
        update_session_transcript_status(session_json, "completed");
    } catch (const std::exception& e) {
        std::cerr << "[transcriber] session.json update failed: "
                  << e.what() << '\n';
    }

    // Queue transcript + session.json for upload
    upload_q_.try_push({session_dir.filename().string(),
                        transcript_path.string()});
    upload_q_.try_push({session_dir.filename().string(),
                        session_json.string()});

    std::cout << "[transcriber] stopped\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void StreamingTranscriber::process_window(const std::vector<float>& audio_f32,
                                          const std::filesystem::path& transcript_path) {
#ifdef HAVE_WHISPER
    // Lazy load model
    if (!whisper_ctx_) {
        whisper_ctx_ = load_whisper_model();
        if (!whisper_ctx_) return;
    }
    auto* ctx = static_cast<whisper_context*>(whisper_ctx_);

    // Transcribe window
    whisper_full_params params = whisper_full_default_params(
        WHISPER_SAMPLING_GREEDY);
    params.language       = "en";
    params.print_progress = false;
    params.print_realtime = false;
    params.vad_thold      = 0.6f;   // vad_filter=False in Python → keep disabled
    params.no_context     = false;

    // Initial prompt from last 200 chars of full_text_ (mirrors Python)
    std::string prompt_str;
    if (!full_text_.empty()) {
        prompt_str = full_text_.size() > 200
                     ? full_text_.substr(full_text_.size() - 200)
                     : full_text_;
        params.initial_prompt = prompt_str.c_str();
    }

    std::string new_text;
    try {
        int ret = whisper_full(ctx,
                               params,
                               audio_f32.data(),
                               static_cast<int>(audio_f32.size()));
        if (ret != 0) {
            new_text = "[transcription failed: whisper_full returned " +
                       std::to_string(ret) + "]";
        } else {
            int n_seg = whisper_full_n_segments(ctx);
            std::ostringstream oss;
            for (int i = 0; i < n_seg; ++i) {
                const char* text = whisper_full_get_segment_text(ctx, i);
                if (text) {
                    if (i > 0) oss << ' ';
                    oss << text;
                }
            }
            new_text = oss.str();
            // Trim leading/trailing whitespace
            auto& s = new_text;
            s.erase(0, s.find_first_not_of(" \t\n\r"));
            if (!s.empty())
                s.erase(s.find_last_not_of(" \t\n\r") + 1);
        }
    } catch (const std::exception& exc) {
        new_text = std::string("[transcription failed: ") + exc.what() + "]";
    }
#else
    (void)audio_f32;
    std::string new_text = "[transcription: audio segment processed]";
#endif

    std::string deduplicated = deduplicate(full_text_, new_text);
    if (!deduplicated.empty()) {
        if (full_text_.empty())
            full_text_ = deduplicated;
        else
            full_text_ = full_text_ + " " + deduplicated;

        // Append to transcript file
        std::ofstream f(transcript_path, std::ios::app);
        f << deduplicated << ' ';
    }
}

void StreamingTranscriber::flush(const std::vector<int16_t>& buffer,
                                  const std::filesystem::path& transcript_path) {
    // Mirrors Python _flush: process if buffer > 0.5s
    int min_samples = (SAMPLE_RATE / 2) * CHANNELS;
    if (static_cast<int>(buffer.size()) > min_samples) {
        std::vector<float> audio_f32;
        audio_f32.reserve(buffer.size() / CHANNELS);
        for (std::size_t i = 0; i < buffer.size(); i += CHANNELS) {
            float mono = 0.0f;
            for (int c = 0; c < CHANNELS; ++c)
                mono += float(buffer[i + c]);
            audio_f32.push_back(mono / (CHANNELS * 32768.0f));
        }
        process_window(audio_f32, transcript_path);
    }
}

std::string StreamingTranscriber::deduplicate(const std::string& existing,
                                               const std::string& new_text) const {
    // Mirrors Python _deduplicate exactly:
    // Compare last min(10, len(existing_words), len(new_words)) words of existing
    // with first N words of new_text. Return suffix of new_text if match found.
    if (existing.empty()) return new_text;

    auto split = [](const std::string& s) {
        std::vector<std::string> words;
        std::istringstream iss(s);
        std::string w;
        while (iss >> w) words.push_back(w);
        return words;
    };

    auto words_existing = split(existing);
    auto words_new      = split(new_text);

    int max_overlap = std::min({10,
                                static_cast<int>(words_existing.size()),
                                static_cast<int>(words_new.size())});

    for (int i = max_overlap; i >= 1; --i) {
        bool match = true;
        for (int k = 0; k < i; ++k) {
            if (words_existing[words_existing.size() - i + k] != words_new[k]) {
                match = false;
                break;
            }
        }
        if (match) {
            std::ostringstream oss;
            for (int k = i; k < static_cast<int>(words_new.size()); ++k) {
                if (k > i) oss << ' ';
                oss << words_new[k];
            }
            return oss.str();
        }
    }
    return new_text;
}

} // namespace session_pipeline
