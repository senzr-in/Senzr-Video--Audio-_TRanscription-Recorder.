#pragma once
#include <filesystem>
#include <cstdint>
#include <string>

namespace session_pipeline {

// ── Paths ─────────────────────────────────────────────────────────────────────
inline const std::filesystem::path RECORDINGS_DIR =
    "/opt/edge-gateway/recordings";
inline const std::filesystem::path MODEL_PATH =
    "/opt/edge-gateway/current/backend/models/yolov8.rknn";

// ── Camera ────────────────────────────────────────────────────────────────────
inline constexpr int    CAMERA_INDEX         = 0;
inline constexpr int    FRAME_WIDTH          = 640;
inline constexpr int    FRAME_HEIGHT         = 480;
inline constexpr double FPS                  = 20.0;
// Fast shutter anti-motion blur (false = auto exposure for bright, vivid feed)
inline constexpr bool   FAST_SHUTTER_MODE    = false;
inline constexpr int    MANUAL_EXPOSURE_VAL  = 250;

// ── Audio ─────────────────────────────────────────────────────────────────────
inline constexpr const char* ALSA_DEVICE   = "plughw:2,0";
inline constexpr int         CHANNELS      = 2;
inline constexpr int         SAMPLE_RATE   = 48000;
inline constexpr double      CHUNK_SECS    = 0.5;

// ── Detection thresholds ──────────────────────────────────────────────────────
inline constexpr int   MODEL_INPUT_SIZE    = 640;
inline constexpr int   PERSON_CLASS_ID     = 0;
inline constexpr float OBJ_THRESH          = 0.45f;
inline constexpr float EARLY_OBJ_THRESH    = 0.35f;
inline constexpr float NMS_THRESH          = 0.45f;
inline constexpr int   START_CONFIRM_FRAMES = 5;
inline constexpr int   STOP_CONFIRM_FRAMES  = 40;
inline constexpr int   DEBUG_EVERY_N_FRAMES = 10;

// ── YOLOv8 decode constants ───────────────────────────────────────────────────
inline constexpr int STRIDES[]  = {8, 16, 32};
inline constexpr int REG_MAX    = 16;
inline constexpr int NUM_CLASSES = 80;

// ── Transcription ─────────────────────────────────────────────────────────────
inline constexpr int WINDOW_SEC  = 5;
inline constexpr int OVERLAP_SEC = 2;
// Whisper model (ggml-tiny.bin equivalent to Python "tiny" int8)
inline constexpr const char* WHISPER_MODEL_PATH =
    "/opt/edge-gateway/models/ggml-tiny.bin";

// ── Upload ────────────────────────────────────────────────────────────────────
inline constexpr const char* S3_BUCKET         = "senzr-fota-storage";
inline constexpr const char* S3_REGION         = "us-east-1";
inline constexpr const char* S3_PREFIX         = "sessions";
inline constexpr int         MAX_UPLOAD_RETRIES = 3;

// ── Session metadata ──────────────────────────────────────────────────────────
// DEVICE_NAME: NOT hardcoded — resolved at runtime via senzr::Device::get_board_model()
// (reads /proc/device-tree/model). Works on any RK3588-based board.
inline constexpr const char* DEVICE_NAME = "";   // resolved at runtime
inline constexpr const char* MODEL_NAME  = "yolov8.rknn";

// ── Debug flags ───────────────────────────────────────────────────────────────
// Real NPU detection mode: person_detected = (best_score > OBJ_THRESH).
// Starts recording when person is confirmed for START_CONFIRM_FRAMES.
// Stops recording when person is missing for STOP_CONFIRM_FRAMES.
inline constexpr bool FORCE_PERSON_MODE = false;

} // namespace session_pipeline
