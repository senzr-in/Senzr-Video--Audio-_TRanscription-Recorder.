#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <cmath>          // <-- needed for std::sqrt
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// =====================================================
// CLI options
// =====================================================

struct BenchmarkOptions {
    std::optional<std::string> session_dir;
    int camera_duration_sec = 10;
    int audio_duration_sec = 5;
    bool skip_camera = false;
    bool skip_audio = false;
    bool skip_transcription = false;
};

BenchmarkOptions parse_args(int argc, char** argv) {
    BenchmarkOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto starts_with = [](const std::string& s, const std::string& prefix) {
            return s.rfind(prefix, 0) == 0;
        };

        if (starts_with(arg, "--session-dir=")) {
            opts.session_dir = arg.substr(std::string("--session-dir=").size());
        } else if (arg == "--session-dir" && i + 1 < argc) {
            opts.session_dir = argv[++i];
        } else if (starts_with(arg, "--camera-duration=")) {
            opts.camera_duration_sec = std::stoi(arg.substr(std::string("--camera-duration=").size()));
        } else if (arg == "--camera-duration" && i + 1 < argc) {
            opts.camera_duration_sec = std::stoi(argv[++i]);
        } else if (starts_with(arg, "--audio-duration=")) {
            opts.audio_duration_sec = std::stoi(arg.substr(std::string("--audio-duration=").size()));
        } else if (arg == "--audio-duration" && i + 1 < argc) {
            opts.audio_duration_sec = std::stoi(argv[++i]);
        } else if (arg == "--skip-camera") {
            opts.skip_camera = true;
        } else if (arg == "--skip-audio") {
            opts.skip_audio = true;
        } else if (arg == "--skip-transcription") {
            opts.skip_transcription = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Edge Gateway C++ Benchmark\n\n"
                << "Options:\n"
                << "  --session-dir <path>       Use specific session directory\n"
                << "  --camera-duration <sec>    Camera benchmark duration (seconds)\n"
                << "  --audio-duration <sec>     Audio benchmark duration (seconds)\n"
                << "  --skip-camera              Skip camera benchmark\n"
                << "  --skip-audio               Skip audio benchmark\n"
                << "  --skip-transcription       Skip transcription benchmark\n";
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
        }
    }

    return opts;
}

// =====================================================
// Small utility helpers
// =====================================================

std::string now_iso8601() {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto tt = clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string run_command_capture_output(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run command: " + cmd);
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.append(buffer);
    }
    int rc = pclose(pipe);
    if (rc == -1) {
        throw std::runtime_error("Failed to close pipe: " + cmd);
    }
    return result;
}

int run_command_exit_code(const std::string& cmd) {
    int status = std::system(cmd.c_str());
    if (status == -1) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

// =====================================================
// Session discovery & artifact validation
// =====================================================

struct SessionInfo {
    fs::path dir;
    fs::path video_path;
    fs::path audio_path;
    fs::path session_json_path;
    fs::path transcript_txt_path;
};

bool looks_like_session_dir(const fs::directory_entry& entry) {
    if (!entry.is_directory()) return false;
    fs::path p = entry.path();

    fs::path video = p / "video.mp4";
    fs::path audio = p / "audio.wav";
    fs::path session_json = p / "session.json";
    fs::path transcript_txt = p / "transcript.txt";

    return fs::exists(video) && fs::exists(audio) &&
           fs::exists(session_json) && fs::exists(transcript_txt);
}

std::optional<SessionInfo> find_latest_session(const fs::path& base) {
    std::optional<SessionInfo> best;
    std::chrono::system_clock::time_point best_time{};

    for (const auto& entry : fs::directory_iterator(base)) {
        if (!looks_like_session_dir(entry)) continue;

        auto p = entry.path();
        auto t = fs::last_write_time(p);

        auto tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            t - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );

        if (!best || tp > best_time) {
            best_time = tp;
            SessionInfo info;
            info.dir = p;
            info.video_path = p / "video.mp4";
            info.audio_path = p / "audio.wav";
            info.session_json_path = p / "session.json";
            info.transcript_txt_path = p / "transcript.txt";
            best = info;
        }
    }

    return best;
}

SessionInfo load_session(const std::optional<std::string>& session_dir_opt) {
    fs::path base = fs::current_path();

    if (session_dir_opt) {
        fs::path p = *session_dir_opt;
        if (!fs::exists(p) || !fs::is_directory(p)) {
            throw std::runtime_error("Specified session directory does not exist or is not a directory: " + p.string());
        }
        SessionInfo info;
        info.dir = p;
        info.video_path = p / "video.mp4";
        info.audio_path = p / "audio.wav";
        info.session_json_path = p / "session.json";
        info.transcript_txt_path = p / "transcript.txt";
        return info;
    } else {
        auto latest = find_latest_session(base);
        if (!latest) {
            throw std::runtime_error("No session directories found under: " + base.string());
        }
        return *latest;
    }
}

bool verify_artifact(const fs::path& p, const char* label, std::ostream& report) {
    if (fs::exists(p)) {
        report << "  [" << label << "] OK: " << p.string() << "\n";
        return true;
    } else {
        report << "  [" << label << "] MISSING: " << p.string() << "\n";
        return false;
    }
}

// =====================================================
// Camera benchmark
// =====================================================

struct CameraBenchmarkResult {
    bool ran = false;
    double fps = 0.0;
    int captured_frames = 0;
    int dropped_frames = 0;
    std::string raw_summary;
};

CameraBenchmarkResult run_camera_benchmark(int duration_sec, const fs::path& session_dir) {
    CameraBenchmarkResult result;
    result.ran = false;

    if (!fs::exists("/dev/video0")) {
        result.raw_summary = "Camera device /dev/video0 not found; skipping camera benchmark.";
        return result;
    }

    result.ran = true;

    fs::path tmp_video = session_dir / "benchmark_camera_tmp.mp4";

    std::ostringstream cmd;
    cmd << "ffmpeg -loglevel error -y -hide_banner "
        << "-f v4l2 -framerate 30 -video_size 640x480 -i /dev/video0 "
        << "-t " << duration_sec << " "
        << tmp_video.string();

    auto start = std::chrono::steady_clock::now();
    int rc = run_command_exit_code(cmd.str());
    auto end = std::chrono::steady_clock::now();

    if (rc != 0) {
        result.raw_summary = "ffmpeg capture failed with exit code " + std::to_string(rc);
        return result;
    }

    double elapsed = std::chrono::duration<double>(end - start).count();
    if (elapsed <= 0.0) elapsed = duration_sec;

    std::ostringstream ffprobe_cmd;
    ffprobe_cmd << "ffprobe -v error -count_frames -select_streams v:0 "
                << "-show_entries stream=nb_read_frames "
                << "-of default=nokey=1:noprint_wrappers=1 "
                << tmp_video.string();

    std::string ffprobe_out;
    try {
        ffprobe_out = run_command_capture_output(ffprobe_cmd.str());
    } catch (const std::exception& ex) {
        result.raw_summary = std::string("ffprobe failed: ") + ex.what();
        return result;
    }

    int frames = 0;
    try {
        frames = std::stoi(ffprobe_out);
    } catch (...) {
        frames = 0;
    }

    result.captured_frames = frames;
    result.fps = (elapsed > 0.0 && frames > 0) ? static_cast<double>(frames) / elapsed : 0.0;

    int expected_frames = static_cast<int>(30.0 * elapsed);
    result.dropped_frames = expected_frames > frames ? (expected_frames - frames) : 0;

    std::ostringstream summary;
    summary << "Captured " << frames << " frames in " << elapsed << "s "
            << "(FPS ~ " << std::fixed << std::setprecision(2) << result.fps << "). "
            << "Estimated dropped frames: " << result.dropped_frames << ".";
    result.raw_summary = summary.str();

    try {
        fs::remove(tmp_video);
    } catch (...) {}

    return result;
}

// =====================================================
// Audio benchmark
// =====================================================

struct AudioBenchmarkResult {
    bool ran = false;
    int sample_rate = 0;
    int channels = 0;
    double duration_sec = 0.0;
    double peak_amplitude = 0.0;
    double rms_amplitude = 0.0;
    std::string raw_summary;
};

#pragma pack(push, 1)
struct WavHeader {
    char riff[4];
    uint32_t overall_size;
    char wave[4];
    char fmt_chunk_marker[4];
    uint32_t length_of_fmt;
    uint16_t format_type;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_chunk_header[4];
    uint32_t data_size;
};
#pragma pack(pop)

AudioBenchmarkResult parse_wav_and_analyze(const fs::path& wav_path) {
    AudioBenchmarkResult result;

    std::ifstream f(wav_path, std::ios::binary);
    if (!f.is_open()) {
        result.raw_summary = "Unable to open WAV file: " + wav_path.string();
        return result;
    }

    WavHeader header{};
    f.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!f) {
        result.raw_summary = "Failed to read WAV header: " + wav_path.string();
        return result;
    }

    if (std::string(header.riff, 4) != "RIFF" || std::string(header.wave, 4) != "WAVE") {
        result.raw_summary = "Invalid WAV RIFF/WAVE header: " + wav_path.string();
        return result;
    }

    result.sample_rate = static_cast<int>(header.sample_rate);
    result.channels = static_cast<int>(header.channels);
    int bits_per_sample = static_cast<int>(header.bits_per_sample);

    double duration = 0.0;
    if (header.byterate > 0) {
        duration = static_cast<double>(header.data_size) / static_cast<double>(header.byterate);
    }
    result.duration_sec = duration;

    if (bits_per_sample != 16) {
        result.raw_summary = "WAV bits-per-sample != 16; waveform analysis skipped.";
        result.peak_amplitude = 0.0;
        result.rms_amplitude = 0.0;
        return result;
    }

    const size_t num_samples = header.data_size /
                               (header.channels * (bits_per_sample / 8));
    if (num_samples == 0) {
        result.raw_summary = "WAV data_size indicates 0 samples.";
        return result;
    }

    double peak = 0.0;
    double sum_sq = 0.0;
    const double max_val = 32768.0;

    std::vector<int16_t> frame(header.channels);

    for (size_t i = 0; i < num_samples; ++i) {
        for (int ch = 0; ch < result.channels; ++ch) {
            int16_t sample = 0;
            f.read(reinterpret_cast<char*>(&sample), sizeof(sample));
            if (!f) break;
            double normalized = static_cast<double>(sample) / max_val;
            if (std::abs(normalized) > peak) peak = std::abs(normalized);
            sum_sq += normalized * normalized;
        }
        if (!f) break;
    }

    double rms = 0.0;
    if (num_samples > 0) {
        rms = std::sqrt(sum_sq / static_cast<double>(num_samples * result.channels));
    }

    result.peak_amplitude = peak;
    result.rms_amplitude = rms;
    result.ran = true;

    std::ostringstream summary;
    summary << "Sample rate: " << result.sample_rate << " Hz, channels: " << result.channels
            << ", duration: " << std::fixed << std::setprecision(2) << result.duration_sec << " s, "
            << "peak amplitude: " << std::fixed << std::setprecision(3) << result.peak_amplitude << ", "
            << "RMS amplitude: " << std::fixed << std::setprecision(3) << result.rms_amplitude << ".";
    result.raw_summary = summary.str();

    return result;
}

AudioBenchmarkResult run_audio_benchmark(int duration_sec, const fs::path& session_dir) {
    AudioBenchmarkResult result;
    result.ran = false;

    fs::path tmp_wav = session_dir / "benchmark_audio_tmp.wav";

    std::ostringstream cmd;
    cmd << "arecord -q -f cd -d " << duration_sec << " " << tmp_wav.string();

    int rc = run_command_exit_code(cmd.str());
    if (rc != 0) {
        result.raw_summary = "arecord failed with exit code " + std::to_string(rc);
        return result;
    }

    result = parse_wav_and_analyze(tmp_wav);

    try {
        fs::remove(tmp_wav);
    } catch (...) {}

    return result;
}

// =====================================================
// Transcription benchmark
// =====================================================

struct TranscriptionBenchmarkResult {
    bool ran = false;
    double wer = 0.0;
    double precision = 0.0;
    double recall = 0.0;
    std::string raw_summary;
};

std::vector<std::string> tokenize_words(const std::string& text) {
    std::vector<std::string> words;
    std::string cur;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            if (!cur.empty()) {
                words.push_back(cur);
                cur.clear();
            }
        }
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}

int edit_distance(const std::vector<std::string>& ref,
                  const std::vector<std::string>& hyp) {
    const size_t m = ref.size();
    const size_t n = hyp.size();

    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));
    for (size_t i = 0; i <= m; ++i) dp[i][0] = static_cast<int>(i);
    for (size_t j = 0; j <= n; ++j) dp[0][j] = static_cast<int>(j);

    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            int cost = (ref[i - 1] == hyp[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1,
                dp[i][j - 1] + 1,
                dp[i - 1][j - 1] + cost
            });
        }
    }
    return dp[m][n];
}

TranscriptionBenchmarkResult run_transcription_benchmark(
    const fs::path& transcript_txt_path) {

    TranscriptionBenchmarkResult result;
    result.ran = false;

    if (!fs::exists(transcript_txt_path)) {
        result.raw_summary = "Transcript file not found: " + transcript_txt_path.string();
        return result;
    }

    std::ifstream f(transcript_txt_path);
    if (!f.is_open()) {
        result.raw_summary = "Unable to open transcript file: " + transcript_txt_path.string();
        return result;
    }

    std::ostringstream oss;
    oss << f.rdbuf();
    std::string transcript = oss.str();

    // NOTE: Replace these with the exact phrases from benchmark.py.
    std::vector<std::string> reference_phrases = {
        "hello this is the edge gateway benchmark",
        "the quick brown fox jumps over the lazy dog",
        "edge gateway camera and audio test",
        "please speak clearly for the transcription benchmark"
    };

    std::ostringstream ref_oss;
    for (size_t i = 0; i < reference_phrases.size(); ++i) {
        if (i > 0) ref_oss << " ";
        ref_oss << reference_phrases[i];
    }
    std::string reference_text = ref_oss.str();

    auto ref_tokens = tokenize_words(reference_text);
    auto hyp_tokens = tokenize_words(transcript);

    if (ref_tokens.empty()) {
        result.raw_summary = "Reference phrases are empty; cannot compute WER.";
        return result;
    }

    int dist = edit_distance(ref_tokens, hyp_tokens);
    result.wer = static_cast<double>(dist) /
                 static_cast<double>(ref_tokens.size());

    std::map<std::string, int> ref_counts;
    std::map<std::string, int> hyp_counts;

    for (const auto& w : ref_tokens) ref_counts[w]++;
    for (const auto& w : hyp_tokens) hyp_counts[w]++;

    int true_positive = 0;
    int false_positive = 0;
    int false_negative = 0;

    for (const auto& kv : hyp_counts) {
        const auto& w = kv.first;
        int hyp_c = kv.second;
        int ref_c = ref_counts[w];
        if (ref_c > 0) {
            true_positive += std::min(hyp_c, ref_c);
            if (hyp_c > ref_c) {
                false_positive += (hyp_c - ref_c);
            }
        } else {
            false_positive += hyp_c;
        }
    }

    for (const auto& kv : ref_counts) {
        const auto& w = kv.first;
        int ref_c = kv.second;
        int hyp_c = hyp_counts[w];
        if (hyp_c < ref_c) {
            false_negative += (ref_c - hyp_c);
        }
    }

    double prec = 0.0;
    double rec = 0.0;

    if (true_positive + false_positive > 0) {
        prec = static_cast<double>(true_positive) /
               static_cast<double>(true_positive + false_positive);
    }
    if (true_positive + false_negative > 0) {
        rec = static_cast<double>(true_positive) /
              static_cast<double>(true_positive + false_negative);
    }

    result.precision = prec;
    result.recall = rec;
    result.ran = true;

    std::ostringstream summary;
    summary << "WER: " << std::fixed << std::setprecision(3) << result.wer
            << ", precision: " << std::fixed << std::setprecision(3) << result.precision
            << ", recall: " << std::fixed << std::setprecision(3) << result.recall << ".";
    result.raw_summary = summary.str();

    return result;
}

// =====================================================
// Report generation
// =====================================================

void write_report(const fs::path& out_path,
                  const SessionInfo& session,
                  const BenchmarkOptions& opts,
                  const CameraBenchmarkResult& cam_res,
                  const AudioBenchmarkResult& aud_res,
                  const TranscriptionBenchmarkResult& tr_res) {
    std::ofstream out(out_path);
    if (!out.is_open()) {
        throw std::runtime_error("Unable to write benchmark report: " + out_path.string());
    }

    out << "Edge Gateway Benchmark Report (C++)\n";
    out << "Generated at: " << now_iso8601() << "\n";
    out << "\n";

    out << "====================\n";
    out << "Session Identity\n";
    out << "====================\n";
    out << "Session directory: " << session.dir.string() << "\n";
    out << "Artifacts:\n";
    verify_artifact(session.video_path, "video.mp4", out);
    verify_artifact(session.audio_path, "audio.wav", out);
    verify_artifact(session.session_json_path, "session.json", out);
    verify_artifact(session.transcript_txt_path, "transcript.txt", out);
    out << "\n";

    out << "====================\n";
    out << "Camera FPS Benchmark\n";
    out << "====================\n";
    if (opts.skip_camera) {
        out << "Camera benchmark skipped (CLI option --skip-camera).\n";
    } else if (!cam_res.ran) {
        out << "Camera benchmark not run: " << cam_res.raw_summary << "\n";
    } else {
        out << "Benchmark duration: " << opts.camera_duration_sec << " s\n";
        out << "Captured frames: " << cam_res.captured_frames << "\n";
        out << "Estimated FPS: " << std::fixed << std::setprecision(2) << cam_res.fps << "\n";
        out << "Estimated dropped frames: " << cam_res.dropped_frames << "\n";
        out << "Summary: " << cam_res.raw_summary << "\n";
    }
    out << "\n";

    out << "============================\n";
    out << "Audio Sample Rate Benchmark\n";
    out << "============================\n";
    if (opts.skip_audio) {
        out << "Audio benchmark skipped (CLI option --skip-audio).\n";
    } else if (!aud_res.ran) {
        out << "Audio benchmark not run: " << aud_res.raw_summary << "\n";
    } else {
        out << "Benchmark duration: " << opts.audio_duration_sec << " s\n";
        out << "Sample rate: " << aud_res.sample_rate << " Hz\n";
        out << "Channels: " << aud_res.channels << "\n";
        out << "Measured duration: " << std::fixed << std::setprecision(2)
            << aud_res.duration_sec << " s\n";
        out << "Peak amplitude: " << std::fixed << std::setprecision(3)
            << aud_res.peak_amplitude << "\n";
        out << "RMS amplitude: " << std::fixed << std::setprecision(3)
            << aud_res.rms_amplitude << "\n";
        out << "Summary: " << aud_res.raw_summary << "\n";
    }
    out << "\n";

    out << "=================================\n";
    out << "Transcription Accuracy Benchmark\n";
    out << "=================================\n";
    if (opts.skip_transcription) {
        out << "Transcription benchmark skipped (CLI option --skip-transcription).\n";
    } else if (!tr_res.ran) {
        out << "Transcription benchmark not run: " << tr_res.raw_summary << "\n";
    } else {
        out << "WER: " << std::fixed << std::setprecision(3) << tr_res.wer << "\n";
        out << "Precision: " << std::fixed << std::setprecision(3) << tr_res.precision << "\n";
        out << "Recall: " << std::fixed << std::setprecision(3) << tr_res.recall << "\n";
        out << "Summary: " << tr_res.raw_summary << "\n";
    }
    out << "\n";

    out << "===========================\n";
    out << "Session Transcript Preview\n";
    out << "===========================\n";
    if (!fs::exists(session.transcript_txt_path)) {
        out << "Transcript file not found.\n";
    } else {
        std::ifstream tf(session.transcript_txt_path);
        if (!tf.is_open()) {
            out << "Unable to open transcript.\n";
        } else {
            out << "First 20 lines:\n";
            std::string line;
            int count = 0;
            while (std::getline(tf, line) && count < 20) {
                out << "  " << line << "\n";
                ++count;
            }
            if (tf && !tf.eof()) {
                out << "  ... (truncated) ...\n";
            }
        }
    }

    out.flush();
}

// =====================================================
// main()
// =====================================================

int main(int argc, char** argv) {
    try {
        BenchmarkOptions opts = parse_args(argc, argv);

        SessionInfo session = load_session(opts.session_dir);

        CameraBenchmarkResult cam_res;
        AudioBenchmarkResult aud_res;
        TranscriptionBenchmarkResult tr_res;

        if (!opts.skip_camera) {
            cam_res = run_camera_benchmark(opts.camera_duration_sec, session.dir);
        }

        if (!opts.skip_audio) {
            aud_res = run_audio_benchmark(opts.audio_duration_sec, session.dir);
        }

        if (!opts.skip_transcription) {
            tr_res = run_transcription_benchmark(session.transcript_txt_path);
        }

        fs::path out_path = fs::current_path() / "benchmark.txt";
        write_report(out_path, session, opts, cam_res, aud_res, tr_res);

        std::cout << "Benchmark complete. Report written to: "
                  << out_path.string() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Benchmark failed: " << ex.what() << "\n";
        return 1;
    }
}
