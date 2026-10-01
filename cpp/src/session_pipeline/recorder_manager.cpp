#include "session_pipeline/recorder_manager.hpp"
#include "session_pipeline/session.hpp"
#include "session_pipeline/config.hpp"
#include "session_pipeline/events.hpp"
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <cstring>
#include <thread>
#include <chrono>

// WAV header writing helpers
namespace {

struct WavHeader {
    char    riff[4]        = {'R','I','F','F'};
    uint32_t chunk_size    = 0;
    char    wave[4]        = {'W','A','V','E'};
    char    fmt[4]         = {'f','m','t',' '};
    uint32_t subchunk1_size= 16;
    uint16_t audio_format  = 1;   // PCM
    uint16_t num_channels  = 2;
    uint32_t sample_rate   = 48000;
    uint32_t byte_rate     = 48000 * 2 * 2;
    uint16_t block_align   = 4;
    uint16_t bits_per_sample = 16;
    char    data[4]        = {'d','a','t','a'};
    uint32_t data_size     = 0;
};

class WavWriter {
public:
    bool open(const std::filesystem::path& path,
              int channels, int sample_rate, int bits_per_sample) {
        file_.open(path, std::ios::binary);
        if (!file_.is_open()) return false;
        channels_        = channels;
        sample_rate_     = sample_rate;
        bits_per_sample_ = bits_per_sample;
        data_bytes_      = 0;
        // Write placeholder header — updated on close()
        WavHeader hdr;
        hdr.num_channels   = static_cast<uint16_t>(channels);
        hdr.sample_rate    = static_cast<uint32_t>(sample_rate);
        hdr.bits_per_sample= static_cast<uint16_t>(bits_per_sample);
        hdr.block_align    = static_cast<uint16_t>(channels * bits_per_sample / 8);
        hdr.byte_rate      = sample_rate * hdr.block_align;
        file_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        return true;
    }

    void write_samples(const int16_t* samples, std::size_t n) {
        if (!file_.is_open()) return;
        file_.write(reinterpret_cast<const char*>(samples),
                    static_cast<std::streamsize>(n * sizeof(int16_t)));
        data_bytes_ += static_cast<uint32_t>(n * sizeof(int16_t));
    }

    void close() {
        if (!file_.is_open()) return;
        // Patch chunk sizes
        uint32_t file_size = 36 + data_bytes_;
        file_.seekp(4);
        file_.write(reinterpret_cast<const char*>(&file_size), 4);
        file_.seekp(40);
        file_.write(reinterpret_cast<const char*>(&data_bytes_), 4);
        file_.close();
    }

private:
    std::ofstream file_;
    int      channels_         = 2;
    int      sample_rate_      = 48000;
    int      bits_per_sample_  = 16;
    uint32_t data_bytes_       = 0;
};

} // anonymous namespace

namespace session_pipeline {

RecorderManagerWorker::RecorderManagerWorker(
    RecordFrameQueue&   video_q,
    AudioFrameQueue&    audio_q,
    EventQueue&         event_q,
    UploadQueue&        upload_q,
    TranscriptionQueue& transcription_q,
    std::atomic<bool>&  recording_active,
    app::Database&      database,
    StreamingTranscriber* transcriber)
    : video_q_(video_q)
    , audio_q_(audio_q)
    , event_q_(event_q)
    , upload_q_(upload_q)
    , transcription_q_(transcription_q)
    , recording_active_(recording_active)
    , database_(database)
    , transcriber_(transcriber)
{}

std::string RecorderManagerWorker::current_recording_file() const {
    std::lock_guard<std::mutex> lock(file_mutex_);
    return current_file_;
}

std::string RecorderManagerWorker::last_recorded_file() const {
    std::lock_guard<std::mutex> lock(file_mutex_);
    return last_file_;
}

std::string RecorderManagerWorker::current_session_id() const {
    std::lock_guard<std::mutex> lock(file_mutex_);
    return current_session_id_;
}

void RecorderManagerWorker::run(std::stop_token stoken) {
    std::cout << "[recorder_manager] started\n";

    while (!stoken.stop_requested()) {
        auto maybe = event_q_.pop(std::chrono::milliseconds(200));
        if (!maybe) continue;

        if (maybe->type == EventType::StartRecording) {
            record_session(stoken);
        }
    }

    std::cout << "[recorder_manager] stopped\n";
}

void RecorderManagerWorker::record_session(std::stop_token stoken) {
    using namespace std::chrono;

    // ── Create session ────────────────────────────────────────────────────────
    auto [session_id, session_dir] = create_session(RECORDINGS_DIR);

    // Wire session_dir into transcriber — mirrors Python:
    // self.transcriber.session_dir = session_dir
    if (transcriber_) {
        transcriber_->set_session_dir(session_dir);
    }

    auto started_at = system_clock::now();
    std::string start_time;
    {
        std::time_t tt  = system_clock::to_time_t(started_at);
        std::tm tm_utc{};
        gmtime_r(&tt, &tm_utc);
        std::ostringstream oss;
        oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
        start_time = oss.str();
    }

    auto video_path      = session_dir / "video.mp4";
    auto audio_path      = session_dir / "audio.wav";
    auto session_json_path = session_dir / "session.json";

    // ── Persist session to SQLite (offline-first, crash-safe) ─────────────────
    SessionInfo db_info;
    db_info.session_id  = session_id;
    db_info.start_time  = start_time;
    db_info.device_name = DEVICE_NAME;
    db_info.model_name  = MODEL_NAME;
    db_info.video_file  = "video.mp4";
    db_info.audio_file  = "audio.wav";
    database_.insert_session(db_info, session_dir.string());

    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        current_file_       = video_path.string();
        current_session_id_ = session_id;
    }
    std::cout << "[recorder_manager] session started → " << session_id << '\n';

    // ── Open writers ──────────────────────────────────────────────────────────
    auto fourcc = cv::VideoWriter::fourcc('m','p','4','v');
    cv::VideoWriter writer;
    WavWriter       wav;
    std::string     end_time;
    int             debug_frame_count = 0;

    try {
        writer.open(video_path.string(), fourcc, FPS,
                    {FRAME_WIDTH, FRAME_HEIGHT});
        if (!writer.isOpened()) {
            throw std::runtime_error(
                "[recorder_manager] Failed to open VideoWriter: " +
                video_path.string());
        }
        std::cout << "[recorder_manager] writer opened path=" << video_path
                  << " size=(" << FRAME_WIDTH << "," << FRAME_HEIGHT
                  << ") fps=" << FPS << '\n';

        if (!wav.open(audio_path, CHANNELS, SAMPLE_RATE, 16)) {
            throw std::runtime_error(
                "[recorder_manager] Failed to open WAV: " +
                audio_path.string());
        }

        // Set recording flag (mirrors Python self.recording_flag.set())
        recording_active_.store(true);

        // Drain stale audio before starting (mirrors Python while not empty: get_nowait())
        audio_q_.drain();

        // ── Record loop (20ms polling — mirrors Python time.sleep(0.02)) ──────
        while (!stoken.stop_requested()) {

            // Drain all pending video frames
            while (true) {
                auto vf = video_q_.try_pop();
                if (!vf) break;

                auto& f = *vf;
                if (f.data.empty()) continue;

                // Validate: 3-channel BGR, uint8, correct size
                cv::Mat frame(f.height, f.width, CV_8UC3, f.data.data());
                if (frame.cols != FRAME_WIDTH || frame.rows != FRAME_HEIGHT) {
                    cv::resize(frame, frame, {FRAME_WIDTH, FRAME_HEIGHT});
                }

                if (debug_frame_count < 5) {
                    std::cout << "[recorder_manager] frame shape=("
                              << frame.rows << "," << frame.cols
                              << ",3) dtype=uint8\n";
                    ++debug_frame_count;
                }

                try {
                    writer.write(frame);
                } catch (const std::exception& e) {
                    std::cerr << "[recorder_manager] video write error: "
                              << e.what() << '\n';
                }
            }

            // Drain all pending audio chunks
            while (true) {
                auto ac = audio_q_.try_pop();
                if (!ac) break;
                wav.write_samples(ac->pcm.data(), ac->pcm.size());
            }

            // Check for STOP_RECORDING event
            auto ev = event_q_.try_pop();
            if (ev && ev->type == EventType::StopRecording) {
                break;
            }

            std::this_thread::sleep_for(milliseconds(20));
        }

        {
            std::time_t tt = system_clock::to_time_t(system_clock::now());
            std::tm tm_utc{};
            gmtime_r(&tt, &tm_utc);
            std::ostringstream oss;
            oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
            end_time = oss.str();
        }

    } catch (const std::exception& e) {
        std::cerr << "[recorder_manager] ERROR: " << e.what() << '\n';
    }

    // ── Finalize (always runs) ────────────────────────────────────────────────
    recording_active_.store(false);
    writer.release();
    wav.close();

    if (end_time.empty()) {
        std::time_t tt = system_clock::to_time_t(system_clock::now());
        std::tm tm_utc{};
        gmtime_r(&tt, &tm_utc);
        std::ostringstream oss;
        oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
        end_time = oss.str();
    }

    // ── Write session.json ────────────────────────────────────────────────────
    SessionInfo info;
    info.session_id     = session_id;
    info.session_dir    = session_dir;
    info.start_time     = start_time;
    info.end_time       = end_time;
    info.device_name    = DEVICE_NAME;
    info.model_name     = MODEL_NAME;

    try {
        write_session_json(session_dir, info);
    } catch (const std::exception& e) {
        std::cerr << "[recorder_manager] session.json write failed: "
                  << e.what() << '\n';
    }

    // ── Queue uploads (video, audio, session.json) ────────────────────────────
    // Dual-write: in-memory queue (fast delivery) + SQLite (crash persistence)
    upload_q_.try_push({session_id, video_path.string()});
    upload_q_.try_push({session_id, audio_path.string()});
    upload_q_.try_push({session_id, session_json_path.string()});

    // SQLite durability layer — survives power cuts
    database_.enqueue_upload(session_id, video_path.string(),      "video");
    database_.enqueue_upload(session_id, audio_path.string(),      "audio");
    database_.enqueue_upload(session_id, session_json_path.string(), "session_json");

    // Update session end time in SQLite
    {
        using namespace std::chrono;
        auto duration_sec = static_cast<double>(
            duration_cast<seconds>(system_clock::now() - started_at).count());
        database_.update_session_end(session_id, end_time, duration_sec);
    }

    // ── Signal transcriber (None sentinel = empty AudioChunk) ─────────────────
    // Python: _safe_put(transcription_queue, None)
    // C++: push a sentinel AudioChunk with empty pcm
    transcription_q_.try_push(AudioChunk{});  // empty pcm = sentinel

    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        last_file_          = current_file_;
        current_file_       = "";
        current_session_id_ = "";
    }

    std::cout << "[recorder_manager] session finalized → " << session_id << '\n';
}

} // namespace session_pipeline
