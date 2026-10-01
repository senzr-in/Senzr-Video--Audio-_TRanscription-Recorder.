#pragma once
#include "session_pipeline/queues.hpp"
#include "session_pipeline/models.hpp"
#ifdef HAVE_RKNN
#include <rknn_api.h>
#else
using rknn_context = void*;
#endif
#include <opencv2/core.hpp>
#include <array>
#include <vector>
#include <functional>
#include <stop_token>
#include <nlohmann/json.hpp>
#include "session_pipeline/plate_utils.hpp"

namespace session_pipeline {

// ── PersonDetectionWorker ─────────────────────────────────────────────────────
// Mirrors Python person_detection.py: PersonDetectionWorker
//
// Runs YOLOv8 inference on the RK3588 NPU (RKNN runtime).
// Decodes outputs with DFL + NMS. Posts START_RECORDING events to event_queue.
//
// NOTE: FORCED_PERSON_MODE is active (FORCE_PERSON_MODE == true in config.hpp).
// person_seen is always set to START_CONFIRM_FRAMES regardless of detection
// score. This is a known Python debug artifact — preserved deliberately.
class PersonDetectionWorker {
public:
    PersonDetectionWorker(DetectFrameQueue& detect_q,
                          EventQueue&       event_q);
    ~PersonDetectionWorker();

    void run(std::stop_token stoken);

    using PreviewCallback = std::function<void(const std::vector<uint8_t>& jpeg_bytes, const nlohmann::json& meta)>;
    void set_preview_callback(PreviewCallback cb) { preview_cb_ = std::move(cb); }
    void set_vision_mode(const std::string& mode) { vision_mode_ = mode; }

private:
    struct LetterboxInfo {
        int   pad_left{0};
        int   pad_top{0};
        float scale{1.0f};
        int   orig_w{640};
        int   orig_h{480};
    };

    // ── YOLOv8 decode helpers (C++ ports of Python numpy functions) ───────────
    static LetterboxInfo letterbox(const cv::Mat& src, cv::Mat& dst,
                                   int new_shape, cv::Scalar color = {114, 114, 114});

    static float sigmoid_clip(float x);

    // DFL: softmax over REG_MAX values × [0..REG_MAX-1] projection
    static float dfl_single(const float* logits, int reg_max);

    // NMS: greedy by score descending, IoU threshold = NMS_THRESH
    static std::vector<int> nms(
        const std::vector<std::array<float, 4>>& boxes,
        const std::vector<float>&                 scores,
        float                                     iou_thres);

    // Full decode: 9 RKNN outputs (NCHW) → unpadded boxes, scores, class ids
    void decode_yolov8(
        const std::vector<std::vector<float>>& outputs,
        std::vector<std::array<float, 4>>&     out_boxes,
        std::vector<float>&                    out_scores,
        std::vector<int>&                      out_cls_ids,
        const LetterboxInfo&                   lb_info) const;

    // Number plate candidate localization & OCR extraction
    std::vector<PlateResult> locate_and_read_plates(cv::Mat& frame);

    DetectFrameQueue& detect_q_;
    EventQueue&       event_q_;
    rknn_context      ctx_{0};

    // State
    int             person_seen_{0};
    int             person_missing_{0};
    int             frame_counter_{0};
    bool            is_recording_{false};
    std::string     vision_mode_{"both"}; // "person", "plate", "both"
    PreviewCallback preview_cb_;
};

} // namespace session_pipeline
