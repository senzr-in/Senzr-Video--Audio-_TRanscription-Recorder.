#include "session_pipeline/person_detection.hpp"
#include "session_pipeline/config.hpp"
#include "session_pipeline/events.hpp"
#include <filesystem>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <unordered_set>

namespace session_pipeline {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

PersonDetectionWorker::PersonDetectionWorker(DetectFrameQueue& detect_q,
                                             EventQueue&       event_q)
    : detect_q_(detect_q), event_q_(event_q)
{
#ifdef HAVE_RKNN
    if (!std::filesystem::exists(MODEL_PATH)) {
        throw std::runtime_error(
            std::string("[person_detection] Missing RKNN model: ") +
            MODEL_PATH.string());
    }

    int ret = rknn_init(&ctx_, const_cast<char*>(MODEL_PATH.string().c_str()),
                        0, 0, nullptr);
    if (ret != RKNN_SUCC) {
        throw std::runtime_error(
            "[person_detection] Failed to load RKNN model: " +
            std::to_string(ret));
    }

    rknn_core_mask core_mask = RKNN_NPU_CORE_AUTO;
    const char* env_core = std::getenv("RKNN_NPU_CORE");
    if (env_core) {
        std::string c(env_core);
        if (c == "0")      core_mask = RKNN_NPU_CORE_0;
        else if (c == "1") core_mask = RKNN_NPU_CORE_1;
        else if (c == "2") core_mask = RKNN_NPU_CORE_2;
        else if (c == "all" || c == "0,1,2" || c == "7") core_mask = RKNN_NPU_CORE_0_1_2;
        std::cout << "[person_detection] NPU core pinned to: " << c << '\n';
    } else {
        std::cout << "[person_detection] NPU core set to AUTO (tri-core load balanced)\n";
    }

    ret = rknn_set_core_mask(ctx_, core_mask);
    if (ret != RKNN_SUCC) {
        rknn_destroy(ctx_);
        throw std::runtime_error(
            "[person_detection] Failed to init RKNN runtime: " +
            std::to_string(ret));
    }
#else
    std::cout << "[person_detection] Built without RKNN support (fallback mode active)\n";
#endif
}

PersonDetectionWorker::~PersonDetectionWorker() {
#ifdef HAVE_RKNN
    if (ctx_) rknn_destroy(ctx_);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// YOLOv8 decode helpers — C++ ports of Python numpy functions
// ─────────────────────────────────────────────────────────────────────────────

PersonDetectionWorker::LetterboxInfo PersonDetectionWorker::letterbox(
    const cv::Mat& src, cv::Mat& dst, int new_shape, cv::Scalar color)
{
    int h = src.rows, w = src.cols;
    float scale = std::min(float(new_shape) / h, float(new_shape) / w);
    int nw = int(std::round(w * scale));
    int nh = int(std::round(h * scale));

    cv::Mat resized;
    cv::resize(src, resized, {nw, nh}, 0, 0, cv::INTER_LINEAR);

    int pad_left = (new_shape - nw) / 2;
    int pad_top  = (new_shape - nh) / 2;

    dst = cv::Mat(new_shape, new_shape, CV_8UC3, color);
    resized.copyTo(dst(cv::Rect(pad_left, pad_top, nw, nh)));

    return LetterboxInfo{pad_left, pad_top, scale, w, h};
}

float PersonDetectionWorker::sigmoid_clip(float x) {
    // Mirrors Python: 1 / (1 + exp(-clip(x, -88, 88)))
    x = std::max(-88.0f, std::min(88.0f, x));
    return 1.0f / (1.0f + std::exp(-x));
}

float PersonDetectionWorker::dfl_single(const float* logits, int reg_max) {
    // Softmax over reg_max values × [0..reg_max-1] projection
    float max_v = *std::max_element(logits, logits + reg_max);
    float sum   = 0.0f;
    std::vector<float> e(reg_max);
    for (int i = 0; i < reg_max; ++i) {
        e[i] = std::exp(logits[i] - max_v);
        sum += e[i];
    }
    sum = std::max(sum, 1e-9f);
    float proj = 0.0f;
    for (int i = 0; i < reg_max; ++i)
        proj += (e[i] / sum) * float(i);
    return proj;
}

std::vector<int> PersonDetectionWorker::nms(
    const std::vector<std::array<float, 4>>& boxes,
    const std::vector<float>&                 scores,
    float                                     iou_thres)
{
    if (boxes.empty()) return {};

    std::vector<int> order(boxes.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return scores[a] > scores[b]; });

    std::vector<float> areas(boxes.size());
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        float w = std::max(0.0f, boxes[i][2] - boxes[i][0]);
        float h = std::max(0.0f, boxes[i][3] - boxes[i][1]);
        areas[i] = w * h;
    }

    std::vector<bool> suppressed(boxes.size(), false);
    std::vector<int>  keep;

    for (std::size_t oi = 0; oi < order.size(); ++oi) {
        int i = order[oi];
        if (suppressed[i]) continue;
        keep.push_back(i);
        for (std::size_t oj = oi + 1; oj < order.size(); ++oj) {
            int j = order[oj];
            if (suppressed[j]) continue;
            float xx1 = std::max(boxes[i][0], boxes[j][0]);
            float yy1 = std::max(boxes[i][1], boxes[j][1]);
            float xx2 = std::min(boxes[i][2], boxes[j][2]);
            float yy2 = std::min(boxes[i][3], boxes[j][3]);
            float inter = std::max(0.0f, xx2 - xx1) *
                          std::max(0.0f, yy2 - yy1);
            float iou   = inter / (areas[i] + areas[j] - inter + 1e-6f);
            if (iou > iou_thres) suppressed[j] = true;
        }
    }
    return keep;
}

void PersonDetectionWorker::decode_yolov8(
    const std::vector<std::vector<float>>& outputs,
    std::vector<std::array<float, 4>>&     out_boxes,
    std::vector<float>&                    out_scores,
    std::vector<int>&                      out_cls_ids,
    const LetterboxInfo&                   lb_info) const
{
    // STRIDES = {8, 16, 32}
    // outputs layout: [box0, cls0, obj0, box1, cls1, obj1, box2, cls2, obj2]
    // Tensors are NCHW layout from RKNN compiler:
    // Box shape: [1, 64, fh, fw] -> channel c at spatial idx is at box_data[c * n + idx]
    // Cls shape: [1, 80, fh, fw] -> channel c at spatial idx is at cls_data[c * n + idx]
    // Obj shape: [1,  1, fh, fw] -> obj_data[idx]
    constexpr int strides[] = {8, 16, 32};
    const int input_sz = MODEL_INPUT_SIZE;

    for (int si = 0; si < 3; ++si) {
        int stride = strides[si];
        int fw = input_sz / stride;
        int fh = input_sz / stride;
        int n  = fw * fh;

        const float* box_data = outputs[si * 3 + 0].data();  // [64 × n]
        const float* cls_data = outputs[si * 3 + 1].data();  // [NUM_CLASSES × n]
        const float* obj_data = outputs[si * 3 + 2].data();  // [n]

        for (int idx = 0; idx < n; ++idx) {
            // Fast check on Person class logit (COCO class 0)
            float person_logit = cls_data[PERSON_CLASS_ID * n + idx];
            float person_prob  = sigmoid_clip(person_logit);
            if (person_prob < EARLY_OBJ_THRESH) continue;

            float obj_prob = sigmoid_clip(obj_data[idx]);
            float score    = obj_prob * person_prob;
            if (score < EARLY_OBJ_THRESH) continue;

            // Verify person is the dominant class among COCO classes
            bool is_person_class = true;
            for (int c = 1; c < NUM_CLASSES; ++c) {
                if (cls_data[c * n + idx] > person_logit) {
                    is_person_class = false;
                    break;
                }
            }
            if (!is_person_class) continue;

            // Grid cell centre in 640x640 model input coordinates
            int gy = idx / fw;
            int gx = idx % fw;
            float cx = (float(gx) + 0.5f) * stride;
            float cy = (float(gy) + 0.5f) * stride;

            // DFL decode: 4 coordinates (l, t, r, b) × 16 bins (REG_MAX)
            // Memory layout is NCHW: channel c at spatial cell idx is box_data[c * n + idx]
            float dfl_dist[4];
            for (int k = 0; k < 4; ++k) {
                float logits[REG_MAX];
                for (int m = 0; m < REG_MAX; ++m) {
                    int c = k * REG_MAX + m;
                    logits[m] = box_data[c * n + idx];
                }
                dfl_dist[k] = dfl_single(logits, REG_MAX) * stride;
            }

            float l = dfl_dist[0];
            float t = dfl_dist[1];
            float r = dfl_dist[2];
            float b = dfl_dist[3];

            float x1 = cx - l;
            float y1 = cy - t;
            float x2 = cx + r;
            float y2 = cy + b;

            // Un-pad and scale back to original camera resolution (e.g. 640x480)
            x1 = (x1 - lb_info.pad_left) / lb_info.scale;
            y1 = (y1 - lb_info.pad_top)  / lb_info.scale;
            x2 = (x2 - lb_info.pad_left) / lb_info.scale;
            y2 = (y2 - lb_info.pad_top)  / lb_info.scale;

            // Clamp to camera frame dimensions
            x1 = std::max(0.0f, std::min(float(lb_info.orig_w), x1));
            y1 = std::max(0.0f, std::min(float(lb_info.orig_h), y1));
            x2 = std::max(0.0f, std::min(float(lb_info.orig_w), x2));
            y2 = std::max(0.0f, std::min(float(lb_info.orig_h), y2));

            out_boxes.push_back({x1, y1, x2, y2});
            out_scores.push_back(score);
            out_cls_ids.push_back(PERSON_CLASS_ID);
        }
    }

    // NMS
    auto keep = nms(out_boxes, out_scores, NMS_THRESH);

    std::vector<std::array<float, 4>> kept_boxes;
    std::vector<float>                kept_scores;
    std::vector<int>                  kept_ids;
    for (int k : keep) {
        if (out_scores[k] < OBJ_THRESH) continue;
        kept_boxes.push_back(out_boxes[k]);
        kept_scores.push_back(out_scores[k]);
        kept_ids.push_back(out_cls_ids[k]);
        if (kept_boxes.size() >= 20) break;
    }
    out_boxes   = std::move(kept_boxes);
    out_scores  = std::move(kept_scores);
    out_cls_ids = std::move(kept_ids);
}

// ─────────────────────────────────────────────────────────────────────────────
// Number plate candidate localization & OCR extraction
// ─────────────────────────────────────────────────────────────────────────────

std::vector<PlateResult> PersonDetectionWorker::locate_and_read_plates(cv::Mat& frame) {
    std::vector<PlateResult> results;
    if (frame.empty() || frame.cols < 60 || frame.rows < 40) return results;

    // Standardize processing coordinate space to 640-width for scale-invariant candidate detection
    float scale = 640.0f / static_cast<float>(frame.cols);
    int proc_h = static_cast<int>(frame.rows * scale);
    cv::Mat small_frame;
    cv::resize(frame, small_frame, cv::Size(640, proc_h), 0, 0, cv::INTER_LINEAR);

    cv::Mat gray;
    cv::cvtColor(small_frame, gray, cv::COLOR_BGR2GRAY);

    // 1. Black-Hat morphology to emphasize dark characters on bright license plate background
    cv::Mat blackhat;
    cv::Mat rect_kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(13, 5));
    cv::morphologyEx(gray, blackhat, cv::MORPH_BLACKHAT, rect_kernel);

    // 2. Sobel vertical edge filter on black-hat (license plate characters have intense vertical strokes)
    cv::Mat grad_x, abs_grad_x;
    cv::Sobel(blackhat, grad_x, CV_16S, 1, 0, 3);
    cv::convertScaleAbs(grad_x, abs_grad_x);

    // 3. Gaussian blur
    cv::Mat blurred;
    cv::GaussianBlur(abs_grad_x, blurred, cv::Size(7, 7), 0);

    // 4. Otsu thresholding
    cv::Mat thresh;
    cv::threshold(blurred, thresh, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    // 5. Morphological Close with horizontal element to bridge individual characters into a solid plate box
    cv::Mat morphed;
    cv::Mat close_kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(21, 5));
    cv::morphologyEx(thresh, morphed, cv::MORPH_CLOSE, close_kernel);

    // 6. Color segmentation mask for White and Yellow plates (in HSV)
    cv::Mat hsv, white_mask, yellow_mask, color_mask;
    cv::cvtColor(small_frame, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(0, 0, 100), cv::Scalar(180, 80, 255), white_mask);
    cv::inRange(hsv, cv::Scalar(12, 50, 90), cv::Scalar(38, 255, 255), yellow_mask);
    color_mask = white_mask | yellow_mask;
    cv::morphologyEx(color_mask, color_mask, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));

    // Combine gradient candidates with color regions
    cv::Mat combined;
    cv::bitwise_and(morphed, color_mask, combined);
    int non_zero = cv::countNonZero(combined);
    cv::Mat search_mask = (non_zero > 120) ? combined : morphed;

    // 7. Find candidate contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(search_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Rect> candidate_rects;
    for (const auto& cnt : contours) {
        cv::Rect r = cv::boundingRect(cnt);

        // Aspect ratio: 1.1 to 6.2 (covers two-wheeler square plates ~1.2-2.0 and car rectangular plates ~3.5-5.5)
        float aspect = static_cast<float>(r.width) / static_cast<float>(r.height);
        if (aspect < 1.1f || aspect > 6.2f) continue;

        // Size in 640-proc coordinates
        if (r.width < 45 || r.height < 14) continue;
        if (r.width > 600 || r.height > 250) continue;

        double area = cv::contourArea(cnt);
        double rect_area = static_cast<double>(r.width * r.height);
        if (rect_area <= 0) continue;
        double solidity = area / rect_area;
        if (solidity < 0.35) continue; // DO NOT reject clean plates > 0.98

        candidate_rects.push_back(r);
    }

    // Sort candidate rects by area descending (largest/clearest plate first)
    std::sort(candidate_rects.begin(), candidate_rects.end(), [](const cv::Rect& a, const cv::Rect& b) {
        return a.area() > b.area();
    });

    float inv_scale = 1.0f / scale;
    int tested = 0;

    for (const auto& cr : candidate_rects) {
        if (++tested > 12) break; // test top 12 candidate regions

        // Map back to original full-resolution frame
        int orig_x = std::clamp(static_cast<int>(cr.x * inv_scale), 0, frame.cols - 1);
        int orig_y = std::clamp(static_cast<int>(cr.y * inv_scale), 0, frame.rows - 1);
        int orig_w = std::clamp(static_cast<int>(cr.width * inv_scale), 10, frame.cols - orig_x);
        int orig_h = std::clamp(static_cast<int>(cr.height * inv_scale), 10, frame.rows - orig_y);

        // Add 6% padding around candidate crop to prevent cutting outer digits
        int pad_x = static_cast<int>(orig_w * 0.06f);
        int pad_y = static_cast<int>(orig_h * 0.08f);
        orig_x = std::max(0, orig_x - pad_x);
        orig_y = std::max(0, orig_y - pad_y);
        orig_w = std::min(frame.cols - orig_x, orig_w + 2 * pad_x);
        orig_h = std::min(frame.rows - orig_y, orig_h + 2 * pad_y);

        cv::Rect full_roi(orig_x, orig_y, orig_w, orig_h);
        cv::Mat plate_crop = frame(full_roi).clone();

        bool multiline = PlateUtils::is_multiline_plate(plate_crop);
        PlateResult pr = PlateUtils::read_plate_text(plate_crop, multiline);

        // Only accept if plate validated with a genuine Indian state code, or high confidence alphanumeric
        bool is_genuine_plate = pr.is_valid;
        if (!is_genuine_plate && pr.clean_number.length() >= 8 && pr.confidence >= 0.70f) {
            std::string st = pr.clean_number.substr(0, 2);
            static const std::unordered_set<std::string> valid_states = {
                "AN", "AP", "AR", "AS", "BR", "CG", "CH", "DD", "DL", "DN",
                "GA", "GJ", "HP", "HR", "JH", "JK", "KA", "KL", "LA", "LD",
                "MH", "ML", "MN", "MP", "MZ", "NL", "OD", "PB", "PY", "RJ",
                "SK", "TN", "TR", "TS", "UK", "UP", "WB"
            };
            if (valid_states.count(st)) {
                is_genuine_plate = true;
            }
        }

        if (is_genuine_plate) {
            pr.x = orig_x;
            pr.y = orig_y;
            pr.w = orig_w;
            pr.h = orig_h;
            pr.is_multiline = multiline;

            // Non-maximum suppression against already found plates
            bool overlap = false;
            for (const auto& existing : results) {
                cv::Rect e_roi(existing.x, existing.y, existing.w, existing.h);
                if ((e_roi & full_roi).area() > 0.4f * std::min(e_roi.area(), full_roi.area())) {
                    overlap = true;
                    break;
                }
            }

            if (!overlap) {
                results.push_back(pr);
                if (results.size() >= 3) break;
            }
        }
    }

    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main run loop
// ─────────────────────────────────────────────────────────────────────────────

void PersonDetectionWorker::run(std::stop_token stoken) {
    std::cout << "[person_detection] started\n";
    if constexpr (FORCE_PERSON_MODE) {
        std::cout << "[person_detection] WARNING: FORCED PERSON MODE ACTIVE (debug)\n";
    }

#ifdef HAVE_RKNN
    // Query model I/O info once
    rknn_input_output_num io_num{};
    int query_ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (query_ret != RKNN_SUCC) {
        std::cerr << "[person_detection] rknn_query failed: " << query_ret << '\n';
    }
    const int n_outputs = static_cast<int>(io_num.n_output);

    // Pre-allocate output buffer structures outside the loop to avoid per-frame allocations
    std::vector<rknn_output> raw_outputs(n_outputs);
    std::vector<std::vector<float>> outputs(n_outputs);
#endif

    cv::Mat lb;

    while (!stoken.stop_requested()) {
        auto maybe = detect_q_.pop(std::chrono::milliseconds(200));
        if (!maybe) continue;

        const VideoFrame& vf = *maybe;
        ++frame_counter_;

        double ts = vf.timestamp;
        float best_score = 0.0f;

        cv::Mat bgr(vf.height, vf.width, CV_8UC3,
                    const_cast<uint8_t*>(vf.data.data()));

        std::vector<std::array<float, 4>> boxes;
        std::vector<float>                scores;
        std::vector<int>                  cls_ids;
        std::vector<PlateResult>          plates;

        std::string cur_mode = vision_mode_;

        // 1. Person Detection (if mode is "person" or "both")
        if (cur_mode == "person" || cur_mode == "both") {
#ifdef HAVE_RKNN
        // ── Preprocess (letterbox + BGR→RGB) ─────────────────────────────────
        LetterboxInfo lb_info = letterbox(bgr, lb, MODEL_INPUT_SIZE);
        cv::cvtColor(lb, lb, cv::COLOR_BGR2RGB);

        // ── RKNN inference ────────────────────────────────────────────────────
        rknn_input inputs[1]{};
        inputs[0].index        = 0;
        inputs[0].type         = RKNN_TENSOR_UINT8;
        inputs[0].size         = MODEL_INPUT_SIZE * MODEL_INPUT_SIZE * 3;
        inputs[0].fmt          = RKNN_TENSOR_NHWC;
        inputs[0].pass_through = 0;
        inputs[0].buf          = lb.data;

        rknn_inputs_set(ctx_, 1, inputs);

        // Run inference on NPU
        int ret = rknn_run(ctx_, nullptr);
        if (ret != RKNN_SUCC) {
            std::cerr << "[person_detection] rknn_run failed: " << ret << '\n';
            continue;
        }

        // Fetch outputs
        for (auto& o : raw_outputs) {
            o.want_float = 1;
            o.is_prealloc = 0;
        }
        rknn_outputs_get(ctx_, n_outputs, raw_outputs.data(), nullptr);

        // Copy into output vectors
        for (int i = 0; i < n_outputs; ++i) {
            const float* p = static_cast<const float*>(raw_outputs[i].buf);
            outputs[i].assign(p, p + raw_outputs[i].size / sizeof(float));
        }
        rknn_outputs_release(ctx_, n_outputs, raw_outputs.data());

        // ── Debug print every DEBUG_EVERY_N_FRAMES ────────────────────────────
        if (frame_counter_ % DEBUG_EVERY_N_FRAMES == 0) {
            std::cout << "[DEBUG] raw output count=" << n_outputs << '\n';
            for (int i = 0; i < n_outputs; ++i) {
                float max_s = *std::max_element(outputs[i].begin(), outputs[i].end());
                std::cout << "[DEBUG] output[" << i << "] size="
                          << outputs[i].size() << " max=" << max_s << '\n';
            }
        }

        // ── Decode (NCHW DFL + NMS + un-padding) ─────────────────────────────
        decode_yolov8(outputs, boxes, scores, cls_ids, lb_info);

        // Best person detection score (NMS returns boxes sorted descending by score)
        if (!scores.empty()) {
            best_score = scores[0];
        }
#else
        best_score = 1.0f;
        boxes.push_back({100.0f, 60.0f, 260.0f, 380.0f});
        scores.push_back(0.92f);
        cls_ids.push_back(0);
#endif
        }

        // 2. Number Plate Detection (if mode is "plate" or "both")
        if (cur_mode == "plate" || cur_mode == "both") {
            plates = locate_and_read_plates(bgr);

            // Print detected number plates to console
            for (const auto& pr : plates) {
                if (!pr.clean_number.empty()) {
                    static std::string last_logged_plate = "";
                    static auto last_log_time = std::chrono::steady_clock::now();
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time).count();

                    if (pr.clean_number != last_logged_plate || elapsed_sec >= 2) {
                        last_logged_plate = pr.clean_number;
                        last_log_time = now;
                        std::cout << "\n======================================================\n"
                                  << " [ANPR] >>> NUMBER PLATE DETECTED: " << pr.formatted_plate << " <<<\n"
                                  << " [ANPR] Clean: " << pr.clean_number
                                  << " | State: " << (pr.state_code.empty() ? "-" : pr.state_code)
                                  << " | RTO: " << (pr.rto_code.empty() ? "-" : pr.rto_code)
                                  << " | Series: " << (pr.series.empty() ? "-" : pr.series)
                                  << " | Digits: " << (pr.unique_digits.empty() ? "-" : pr.unique_digits) << '\n'
                                  << " [ANPR] Confidence: " << static_cast<int>(pr.confidence * 100.0f) << "%\n"
                                  << "======================================================\n\n" << std::flush;
                    }
                }
            }
        }

        // ── Person tracking & state machine ──────────────────────────────────
        if (cur_mode != "plate") {
            if constexpr (FORCE_PERSON_MODE) {
                person_seen_    = START_CONFIRM_FRAMES;
                person_missing_ = 0;
            } else {
                bool person_detected = (best_score > OBJ_THRESH);
                if (person_detected) {
                    ++person_seen_;
                    person_missing_ = 0;
                } else {
                    person_seen_ = 0;
                    ++person_missing_;
                }
            }

            // Emit START_RECORDING
            if (person_seen_ >= START_CONFIRM_FRAMES) {
                if (!is_recording_) {
                    std::cout << "[person_detection] START_RECORDING ts=" << ts
                              << " seen=" << person_seen_
                              << " best_score=" << best_score << '\n';
                    event_q_.try_push(make_event(EventType::StartRecording));
                    is_recording_ = true;
                }
                person_seen_ = 0;
            }

            // Emit STOP_RECORDING
            if (person_missing_ >= STOP_CONFIRM_FRAMES) {
                if (is_recording_) {
                    std::cout << "[person_detection] STOP_RECORDING ts=" << ts
                              << " missing=" << person_missing_ << '\n';
                    event_q_.try_push(make_event(EventType::StopRecording));
                    is_recording_ = false;
                }
                person_missing_ = 0;
            }
        }

        // ── Live Preview & Visual Overlay ─────────────────────────────────────
        if (preview_cb_) {
            cv::Mat preview = bgr.clone();

            // Draw person boxes (Green) - only confirmed detections
            int confirmed_persons = 0;
            for (size_t i = 0; i < boxes.size(); ++i) {
                if (scores[i] < OBJ_THRESH) continue;
                confirmed_persons++;

                int x1 = std::clamp(static_cast<int>(boxes[i][0]), 0, preview.cols - 1);
                int y1 = std::clamp(static_cast<int>(boxes[i][1]), 0, preview.rows - 1);
                int x2 = std::clamp(static_cast<int>(boxes[i][2]), 0, preview.cols - 1);
                int y2 = std::clamp(static_cast<int>(boxes[i][3]), 0, preview.rows - 1);

                cv::rectangle(preview, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 255, 0), 2);

                int conf_pct = static_cast<int>(scores[i] * 100.0f);
                std::string label = "Person " + std::to_string(conf_pct) + "%";

                int baseline = 0;
                cv::Size sz = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.45, 1, &baseline);
                int top_y = std::max(0, y1 - sz.height - 4);
                cv::rectangle(preview, cv::Point(x1, top_y),
                              cv::Point(x1 + sz.width + 4, y1), cv::Scalar(0, 255, 0), cv::FILLED);
                cv::putText(preview, label, cv::Point(x1 + 2, y1 - 2),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
            }

            // Draw plate boxes (Amber / Yellow)
            for (const auto& pr : plates) {
                cv::Rect r(pr.x, pr.y, pr.w, pr.h);
                cv::rectangle(preview, r, cv::Scalar(0, 215, 255), 2);

                int conf_pct = static_cast<int>(pr.confidence * 100.0f);
                std::string text_disp = pr.is_valid ? pr.formatted_plate : (pr.clean_number.empty() ? "Plate" : pr.clean_number);
                std::string label = text_disp + " (" + std::to_string(conf_pct) + "%)";
                if (pr.is_multiline) {
                    label += " [2-LINE]";
                }

                int baseline = 0;
                cv::Size sz = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.44, 1, &baseline);
                int top_y = std::max(0, pr.y - sz.height - 4);
                cv::rectangle(preview, cv::Point(pr.x, top_y),
                              cv::Point(pr.x + sz.width + 4, pr.y), cv::Scalar(0, 215, 255), cv::FILLED);
                cv::putText(preview, label, cv::Point(pr.x + 2, pr.y - 2),
                            cv::FONT_HERSHEY_SIMPLEX, 0.44, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
            }

            // Draw Top HUD Bar
            cv::Rect hud_bar(0, 0, preview.cols, 26);
            cv::Mat hud_roi = preview(hud_bar);
            cv::Mat black_overlay(hud_roi.size(), hud_roi.type(), cv::Scalar(15, 15, 20));
            cv::addWeighted(hud_roi, 0.35, black_overlay, 0.65, 0.0, hud_roi);

            std::string plate_tag = "";
            if (!plates.empty()) {
                const auto& top_p = plates[0];
                plate_tag = " [" + (top_p.is_valid ? top_p.formatted_plate : top_p.clean_number) + "]";
            }

            std::string hud_text = "MODE: " + cur_mode +
                                   " | PERSONS: " + std::to_string(confirmed_persons) +
                                   " | PLATES: " + std::to_string(plates.size()) + plate_tag +
                                   " | " + (is_recording_ ? "REC [LIVE]" : "MONITORING");
            cv::putText(preview, hud_text, cv::Point(10, 18),
                        cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);

            // Encode to JPEG
            std::vector<uint8_t> jpeg_buf;
            std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 75};
            cv::imencode(".jpg", preview, jpeg_buf, params);

            // Construct detection metadata JSON
            nlohmann::json meta;
            meta["timestamp"] = ts;
            meta["frame"] = frame_counter_;
            meta["mode"] = cur_mode;
            meta["is_recording"] = is_recording_;
            meta["persons"] = nlohmann::json::array();
            for (size_t i = 0; i < boxes.size(); ++i) {
                if (scores[i] < OBJ_THRESH) continue;
                meta["persons"].push_back({
                    {"x", static_cast<int>(boxes[i][0])},
                    {"y", static_cast<int>(boxes[i][1])},
                    {"w", static_cast<int>(boxes[i][2] - boxes[i][0])},
                    {"h", static_cast<int>(boxes[i][3] - boxes[i][1])},
                    {"score", scores[i]}
                });
            }
            meta["plates"] = nlohmann::json::array();
            for (const auto& pr : plates) {
                meta["plates"].push_back({
                    {"x", pr.x},
                    {"y", pr.y},
                    {"w", pr.w},
                    {"h", pr.h},
                    {"formatted", pr.formatted_plate},
                    {"clean", pr.clean_number},
                    {"confidence", pr.confidence},
                    {"valid", pr.is_valid},
                    {"multiline", pr.is_multiline}
                });
            }

            preview_cb_(jpeg_buf, meta);
        }

        if (frame_counter_ % DEBUG_EVERY_N_FRAMES == 0) {
            std::cout << "[DEBUG] mode=" << cur_mode
                      << " persons=" << boxes.size()
                      << " plates=" << plates.size()
                      << " person_best=" << best_score << '\n';
            std::cout << "[DETECT] seen=" << person_seen_
                      << " missing=" << person_missing_
                      << " is_recording=" << (is_recording_ ? "true" : "false") << '\n';
        }

        // Heartbeat every 30 frames
        if (frame_counter_ % 30 == 0) {
            std::cout << "[person_detection] heartbeat frame=" << frame_counter_
                      << " best_score=" << best_score
                      << " seen=" << person_seen_
                      << " missing=" << person_missing_
                      << " is_recording=" << (is_recording_ ? "true" : "false") << '\n';
        }
    }

    std::cout << "[person_detection] stopped\n";
}

} // namespace session_pipeline

