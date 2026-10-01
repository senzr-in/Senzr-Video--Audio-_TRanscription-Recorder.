#include "session_pipeline/run_session_pipeline.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace session_pipeline {

SessionPipeline::SessionPipeline(app::Database& database)
    : database_(database) {}
SessionPipeline::~SessionPipeline() { stop(); }

// ─────────────────────────────────────────────────────────────────────────────
// start()
// ─────────────────────────────────────────────────────────────────────────────

void SessionPipeline::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (running_.load()) return;

    // ── Reset queues for a fresh start/stop cycle ─────────────────────────────
    // std::mutex is not movable/copyable, so we call reset() instead of
    // move-assigning new queue objects.
    detect_frame_q_.reset();
    record_frame_q_.reset();
    audio_frame_q_.reset();
    event_q_.reset();
    transcription_q_.reset();
    upload_q_.reset();

    recording_active_.store(false);

    // ── Construct workers ─────────────────────────────────────────────────────
    video_worker_ = std::make_unique<VideoCaptureWorker>(
        detect_frame_q_, record_frame_q_);

    audio_worker_ = std::make_unique<AudioCaptureWorker>(
        audio_frame_q_, transcription_q_, recording_active_);

    detect_worker_ = std::make_unique<PersonDetectionWorker>(
        detect_frame_q_, event_q_);
    detect_worker_->set_vision_mode(vision_mode_);
    detect_worker_->set_preview_callback(
        [this](const std::vector<uint8_t>& jpeg, const nlohmann::json& meta) {
            {
                std::lock_guard<std::mutex> lk(preview_mutex_);
                latest_preview_jpeg_ = jpeg;
                latest_detections_json_ = meta;
            }

            // Persist detections to SQLite database
            double ts = meta.value("timestamp", 0.0);
            int frame = meta.value("frame", 0);
            std::string mode = meta.value("mode", "person");
            std::string session_id = recorder_worker_ ? recorder_worker_->current_session_id() : "";

            // Plate detections & snapshot capture
            if (meta.contains("plates") && meta["plates"].is_array() && !meta["plates"].empty()) {
                static std::string last_logged_plate = "";
                static double last_plate_event_ts = 0.0;

                for (const auto& p : meta["plates"]) {
                    std::string clean = p.value("clean", "");
                    std::string formatted = p.value("formatted", "");
                    if (clean.empty() && formatted.empty()) continue;

                    // Log event on new plate or every 3 seconds for continuous presence
                    if (clean != last_logged_plate || (ts - last_plate_event_ts) >= 3.0) {
                        last_logged_plate = clean;
                        last_plate_event_ts = ts;

                        app::DetectionEventRow row;
                        row.session_id      = session_id;
                        row.event_type      = "PLATE_DETECTED";
                        row.timestamp       = ts;
                        row.frame_number    = frame;
                        row.confidence      = p.value("confidence", 0.0f);
                        row.vision_mode     = mode;
                        row.plate_raw       = clean;
                        row.plate_clean     = clean;
                        row.plate_formatted = formatted;
                        row.plate_valid     = p.value("valid", false);
                        row.plate_multiline = p.value("multiline", false);
                        row.plate_x         = p.value("x", 0);
                        row.plate_y         = p.value("y", 0);
                        row.bbox_w          = p.value("w", 0);
                        row.bbox_h          = p.value("h", 0);

                        // Save high-resolution plate capture snapshot image
                        try {
                            std::filesystem::path plate_dir = "/opt/edge-gateway/recordings/plates";
                            std::error_code ec;
                            std::filesystem::create_directories(plate_dir, ec);

                            if (!jpeg.empty()) {
                                std::string fname = "plate_" + clean + "_" + std::to_string(static_cast<long long>(ts)) + ".jpg";
                                std::filesystem::path img_path = plate_dir / fname;
                                std::ofstream ofs(img_path, std::ios::binary);
                                if (ofs.is_open()) {
                                    ofs.write(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
                                    std::cout << "[ANPR] Captured plate snapshot saved to: " << img_path.string() << "\n";

                                    if (plate_callback_) {
                                        plate_callback_(clean, row.confidence, fname);
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "[ANPR] Failed to save plate snapshot: " << e.what() << "\n";
                        }

                        database_.log_detection_event(row);
                        std::cout << "[ANPR] Event created: PLATE_DETECTED -> " << formatted
                                  << " (conf: " << static_cast<int>(row.confidence * 100.0f) << "%)\n";
                    }
                }
            }

            // Person detections & snapshot capture (log periodically, e.g. every 3 seconds)
            static double last_person_log_ts = 0.0;
            if (meta.contains("persons") && meta["persons"].is_array() && !meta["persons"].empty()) {
                if (ts - last_person_log_ts >= 3.0) {
                    last_person_log_ts = ts;
                    const auto& p = meta["persons"][0];
                    app::DetectionEventRow row;
                    row.session_id   = session_id;
                    row.event_type   = "PERSON_DETECTED";
                    row.timestamp    = ts;
                    row.frame_number = frame;
                    row.confidence   = p.value("score", 0.0f);
                    row.vision_mode  = mode;
                    row.bbox_x       = p.value("x", 0);
                    row.bbox_y       = p.value("y", 0);
                    row.bbox_w       = p.value("w", 0);
                    row.bbox_h       = p.value("h", 0);

                    // Save high-resolution person capture snapshot to disk with timestamp
                    try {
                        std::filesystem::path person_dir = "/opt/edge-gateway/recordings/persons";
                        std::error_code ec;
                        std::filesystem::create_directories(person_dir, ec);

                        if (!jpeg.empty()) {
                            std::string fname = "person_" + std::to_string(static_cast<long long>(ts)) + ".jpg";
                            std::filesystem::path img_path = person_dir / fname;
                            std::ofstream ofs(img_path, std::ios::binary);
                            if (ofs.is_open()) {
                                ofs.write(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
                                std::cout << "[PERSON] Captured person snapshot saved to: " << img_path.string() << "\n";
                            }
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[PERSON] Failed to save person snapshot: " << e.what() << "\n";
                    }

                    database_.log_detection_event(row);
                    std::cout << "[PERSON] Event created: PERSON_DETECTED (conf: "
                              << static_cast<int>(row.confidence * 100.0f) << "%)\n";
                }
            }
        }
    );

    transcriber_worker_ = std::make_unique<StreamingTranscriber>(
        transcription_q_, upload_q_);

    recorder_worker_ = std::make_unique<RecorderManagerWorker>(
        record_frame_q_,
        audio_frame_q_,
        event_q_,
        upload_q_,
        transcription_q_,
        recording_active_,
        database_,
        transcriber_worker_.get());   // mirrors Python: recorder.transcriber = transcriber

    uploader_worker_ = std::make_unique<UploaderWorker>(upload_q_, &database_);

    // ── Crash recovery: re-queue any pending uploads from SQLite ──────────────
    // Any rows that were 'pending' or 'in_flight' before the last restart
    // (in_flight was reset to 'pending' in init_db()) are pushed back into
    // the in-memory queue so UploaderWorker handles them without special casing.
    for (const auto& row : database_.get_pending_upload_jobs()) {
        upload_q_.try_push({row.session_id, row.file_path});
    }

    // ── Launch threads ────────────────────────────────────────────────────────
    // std::jthread automatically passes a stop_token to the callable
    video_thread_ = std::jthread(
        [this](std::stop_token st) { video_worker_->run(st); });

    audio_thread_ = std::jthread(
        [this](std::stop_token st) { audio_worker_->run(st); });

    detect_thread_ = std::jthread(
        [this](std::stop_token st) { detect_worker_->run(st); });

    recorder_thread_ = std::jthread(
        [this](std::stop_token st) { recorder_worker_->run(st); });

    transcriber_thread_ = std::jthread(
        [this](std::stop_token st) { transcriber_worker_->run(st); });

    uploader_thread_ = std::jthread(
        [this](std::stop_token st) { uploader_worker_->run(st); });

    running_.store(true);
    std::cout << "[pipeline] all workers started\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// stop()
//
// Shutdown sequence (mirrors Python stop() but adds proper queue shutdown):
//   1. Request stop on all jthreads (sets stop_token)
//   2. Unblock all threads waiting on queues
//   3. jthread destructors auto-join (no manual join needed)
//   4. Destroy worker objects
// ─────────────────────────────────────────────────────────────────────────────

void SessionPipeline::stop() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!running_.load()) return;

    running_.store(false);

    // Step 1: request stop on all threads
    if (video_thread_)      video_thread_->request_stop();
    if (audio_thread_)      audio_thread_->request_stop();
    if (detect_thread_)     detect_thread_->request_stop();
    if (recorder_thread_)   recorder_thread_->request_stop();
    if (transcriber_thread_)transcriber_thread_->request_stop();
    if (uploader_thread_)   uploader_thread_->request_stop();

    // Step 2: unblock threads blocked on queue wait
    shutdown_all_queues();

    // Step 3: join (jthread destructors do this automatically)
    video_thread_.reset();
    audio_thread_.reset();
    detect_thread_.reset();
    recorder_thread_.reset();
    transcriber_thread_.reset();
    uploader_thread_.reset();

    // Step 4: destroy workers
    video_worker_.reset();
    audio_worker_.reset();
    detect_worker_.reset();
    recorder_worker_.reset();
    transcriber_worker_.reset();
    uploader_worker_.reset();

    std::cout << "[pipeline] all workers stopped\n";
}

void SessionPipeline::shutdown_all_queues() {
    detect_frame_q_.shutdown();
    record_frame_q_.shutdown();
    audio_frame_q_.shutdown();
    event_q_.shutdown();
    transcription_q_.shutdown();
    upload_q_.shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// State accessors
// ─────────────────────────────────────────────────────────────────────────────

bool SessionPipeline::is_running() const {
    return running_.load();
}

bool SessionPipeline::is_recording() const {
    return recording_active_.load();
}

std::string SessionPipeline::current_recording_file() const {
    if (!recorder_worker_) return "";
    return recorder_worker_->current_recording_file();
}

std::string SessionPipeline::last_recorded_file() const {
    if (!recorder_worker_) return "";
    return recorder_worker_->last_recorded_file();
}

std::vector<uint8_t> SessionPipeline::get_latest_preview_jpeg() const {
    std::lock_guard<std::mutex> lk(preview_mutex_);
    return latest_preview_jpeg_;
}

nlohmann::json SessionPipeline::get_latest_detections_json() const {
    std::lock_guard<std::mutex> lk(preview_mutex_);
    return latest_detections_json_;
}

void SessionPipeline::set_vision_mode(const std::string& mode) {
    std::lock_guard<std::mutex> lk(preview_mutex_);
    vision_mode_ = mode;
    if (detect_worker_) {
        detect_worker_->set_vision_mode(mode);
    }
}

void SessionPipeline::set_plate_detected_callback(PlateDetectedCallback cb) {
    plate_callback_ = std::move(cb);
}

} // namespace session_pipeline
