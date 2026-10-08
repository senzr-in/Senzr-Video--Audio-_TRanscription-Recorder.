#pragma once
#include <string>

namespace app {

// ── DetectionEventRow ─────────────────────────────────────────────────────────
// Populated by PersonDetectionWorker preview callback and stored in
// the detection_events SQLite table.
struct DetectionEventRow {
    std::string session_id;   // FK → sessions.session_id; empty if not recording

    // Event classification
    // PERSON_DETECTED | PERSON_LOST | PLATE_DETECTED
    // RECORDING_START | RECORDING_STOP
    std::string event_type;

    double      timestamp    = 0.0;  // VideoFrame.timestamp (Unix epoch float)
    int         frame_number = 0;
    float       confidence   = 0.0f; // highest detection score 0.0–1.0
    std::string vision_mode;         // "person" | "plate" | "both"

    // Person bounding box (pixel coords in original 640×480 frame)
    int bbox_x = 0, bbox_y = 0, bbox_w = 0, bbox_h = 0;

    // Plate-specific fields (zero/empty for person events)
    // Aligned to session_pipeline::PlateResult struct fields
    std::string plate_raw;       // PlateResult.raw_text
    std::string plate_clean;     // PlateResult.clean_number e.g. "KA05MJ1234"
    std::string plate_formatted; // PlateResult.formatted_plate e.g. "KA 05 MJ 1234"
    bool        plate_valid     = false;  // PlateResult.is_valid
    bool        plate_multiline = false;  // PlateResult.is_multiline
    int         plate_x         = 0;     // PlateResult.x
    int         plate_y         = 0;     // PlateResult.y
};

// ── UploadQueueRow ────────────────────────────────────────────────────────────
// One row from the upload_queue table.
// Used by crash-recovery on startup: pending rows are re-pushed to the
// in-memory UploadQueue so UploaderWorker picks them up automatically.
struct UploadQueueRow {
    int         id          = 0;
    std::string session_id;
    std::string file_path;
    std::string file_type;  // "video" | "audio" | "session_json"
    std::string s3_key;     // "sessions/{session_id}/{filename}"
    std::string status;     // "pending" | "in_flight" | "done" | "failed"
    int         attempts    = 0;
};

// ── TelemetryRow ──────────────────────────────────────────────────────────────
// Periodic health snapshot (written every 60 seconds by a background timer).
// RK3588-specific thermal zones:
//   zone 0 → CPU big cores
//   zone 2 → NPU
//   zone 3 → GPU Mali-G610
struct TelemetryRow {
    float cpu_temp_c  = 0.0f;  // /sys/class/thermal/thermal_zone0/temp ÷ 1000
    float npu_temp_c  = 0.0f;  // /sys/class/thermal/thermal_zone2/temp ÷ 1000
    float gpu_temp_c  = 0.0f;  // /sys/class/thermal/thermal_zone3/temp ÷ 1000

    int   ram_used_mb   = 0;
    int   ram_total_mb  = 0;
    float disk_used_gb  = 0.0f;
    float disk_total_gb = 0.0f;
    int   uptime_sec    = 0;

    // Pipeline state at heartbeat moment
    bool  camera_active    = false;
    bool  recording_active = false;
    float fps_average      = 0.0f;
    int   detection_count  = 0;   // events logged since last heartbeat
    std::string vision_mode;      // "person" | "plate" | "both"
};

// ── AccessCredentialRow ───────────────────────────────────────────────────────
// Offline access control credential for RFID, BLE tokens, and license plates.
struct AccessCredentialRow {
    int         id              = 0;
    std::string credential_hash; // SHA-256 of BLE passkey / RFID card UID
    std::string holder_name;
    std::string role            = "resident"; // "resident" | "visitor" | "emergency" | "staff"
    std::string plate_number;    // Normalized vehicle plate (e.g. "CA12345")
    int64_t     valid_from      = 0;          // Unix epoch seconds
    int64_t     valid_until     = 0;          // Unix epoch seconds
    bool        is_revoked      = false;
};

} // namespace app
