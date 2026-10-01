#include "session_pipeline/plate_utils.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cctype>

namespace session_pipeline {

// ─────────────────────────────────────────────────────────────────────────────
// Fix Problem 5: Night-time Headlight Glare & Low-Light Enhancement
// ─────────────────────────────────────────────────────────────────────────────

cv::Mat PlateUtils::enhance_plate_for_ocr(const cv::Mat& plate_bgr) {
    if (plate_bgr.empty() || plate_bgr.cols < 10 || plate_bgr.rows < 10) {
        return plate_bgr;
    }

    // 1. Convert to LAB color space to isolate Luminance (L) from color information (a, b)
    cv::Mat lab;
    cv::cvtColor(plate_bgr, lab, cv::COLOR_BGR2Lab);

    std::vector<cv::Mat> lab_planes(3);
    cv::split(lab, lab_planes);

    // 2. Apply CLAHE (Contrast Limited Adaptive Histogram Equalization)
    // Tones down bright headlight hotspots while boosting low-contrast dark characters
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.5, cv::Size(8, 8));
    clahe->apply(lab_planes[0], lab_planes[0]);

    cv::Mat enhanced_lab;
    cv::merge(lab_planes, enhanced_lab);

    cv::Mat enhanced_bgr;
    cv::cvtColor(enhanced_lab, enhanced_bgr, cv::COLOR_Lab2BGR);

    // 3. Bilateral filter to smooth specular glare reflections while keeping character borders sharp
    cv::Mat filtered;
    cv::bilateralFilter(enhanced_bgr, filtered, 5, 45, 45);

    return filtered;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fix Problem 3: Multi-line Plate Handling (2-wheelers, trucks, auto-rickshaws)
// ─────────────────────────────────────────────────────────────────────────────

bool PlateUtils::is_multiline_plate(const cv::Mat& plate_crop) {
    if (plate_crop.empty() || plate_crop.rows == 0) return false;
    float aspect_ratio = static_cast<float>(plate_crop.cols) / static_cast<float>(plate_crop.rows);
    // Standard cars have aspect ratio ~4.0 to 5.0 (wide rectangle)
    // 2-wheelers / multi-line plates have aspect ratio ~1.5 to 2.5 (more square)
    return aspect_ratio < 2.8f;
}

std::vector<cv::Mat> PlateUtils::split_plate_lines(const cv::Mat& plate_crop) {
    if (!is_multiline_plate(plate_crop)) {
        return {plate_crop};
    }

    int h = plate_crop.rows;
    int w = plate_crop.cols;
    int mid = h / 2;
    int overlap = static_cast<int>(h * 0.08f); // 8% vertical overlap prevents cutting numbers

    cv::Rect top_roi(0, 0, w, std::min(h, mid + overlap));
    int bottom_y = std::max(0, mid - overlap);
    cv::Rect bottom_roi(0, bottom_y, w, h - bottom_y);

    return {plate_crop(top_roi).clone(), plate_crop(bottom_roi).clone()};
}

// ─────────────────────────────────────────────────────────────────────────────
// Fix Problem 4: HSRP Blue "IND" Strip Cleanup & Plate Normalization
// ─────────────────────────────────────────────────────────────────────────────

std::string PlateUtils::strip_hsrp_ind_prefix(const std::string& raw_text) {
    std::string s = raw_text;

    // Remove leading punctuation/spaces
    while (!s.empty() && (s.front() == ' ' || s.front() == '-' || s.front() == '.' || s.front() == ':')) {
        s.erase(s.begin());
    }

    // Common OCR misinterpretations of the blue HSRP "IND" badge
    const std::vector<std::string> hsrp_prefixes = {
        "IND", "1ND", "IND-", "IN0", "I0", "1N0", "ND"
    };

    for (const auto& prefix : hsrp_prefixes) {
        if (s.rfind(prefix, 0) == 0 && s.length() > prefix.length() + 4) {
            s = s.substr(prefix.length());
            break;
        }
    }

    // Trim again
    while (!s.empty() && (s.front() == ' ' || s.front() == '-' || s.front() == '.')) {
        s.erase(s.begin());
    }
    return s;
}

std::string PlateUtils::normalize_plate_characters(const std::string& text) {
    std::string clean;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            clean += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return clean;
}

PlateResult PlateUtils::validate_and_format_plate(const std::string& raw_text, float confidence) {
    PlateResult res;
    res.raw_text   = raw_text;
    res.confidence = confidence;

    std::string stripped = strip_hsrp_ind_prefix(raw_text);
    std::string clean    = normalize_plate_characters(stripped);
    res.clean_number     = clean;

    if (clean.length() < 6 || clean.length() > 11) {
        res.is_valid = false;
        res.formatted_plate = clean;
        return res;
    }

    // ── Heuristic OCR Character Disambiguation ────────────────────────────────
    // Format: [State: 2 letters] [RTO: 1-2 digits] [Series: 0-3 letters] [Number: 4 digits]
    auto to_digit = [](char c) -> char {
        if (c == 'O' || c == 'D' || c == 'Q') return '0';
        if (c == 'I' || c == 'L')             return '1';
        if (c == 'Z')                         return '2';
        if (c == 'S')                         return '5';
        if (c == 'B')                         return '8';
        return c;
    };

    auto to_letter = [](char c) -> char {
        if (c == '0') return 'O';
        if (c == '1') return 'I';
        if (c == '2') return 'Z';
        if (c == '5') return 'S';
        if (c == '8') return 'B';
        return c;
    };

    // Correct State Code (first 2 chars must be letters)
    if (clean.length() >= 2) {
        clean[0] = to_letter(clean[0]);
        clean[1] = to_letter(clean[1]);
    }

    // Correct Number (last 4 chars must be digits)
    if (clean.length() >= 4) {
        size_t len = clean.length();
        clean[len - 4] = to_digit(clean[len - 4]);
        clean[len - 3] = to_digit(clean[len - 3]);
        clean[len - 2] = to_digit(clean[len - 2]);
        clean[len - 1] = to_digit(clean[len - 1]);
    }

    // ── Standard Indian License Plate Format ──────────────────────────────────
    // Regex: ^([A-Z]{2})([0-9]{1,2})([A-Z]{0,3})([0-9]{4})$
    static const std::regex standard_in_regex(R"(^([A-Z]{2})([0-9]{1,2})([A-Z]{0,3})([0-9]{4})$)");
    std::smatch match;
    if (std::regex_match(clean, match, standard_in_regex)) {
        res.is_valid        = true;
        res.state_code      = match[1].str();
        res.rto_code        = match[2].str();
        res.series          = match[3].str();
        res.unique_digits   = match[4].str();

        res.formatted_plate = res.state_code + " " + res.rto_code;
        if (!res.series.empty()) {
            res.formatted_plate += " " + res.series;
        }
        res.formatted_plate += " " + res.unique_digits;
        return res;
    }

    // ── Bharat (BH) Series Plate Format ───────────────────────────────────────
    // Regex: ^([0-9]{2})(BH)([0-9]{4})([A-Z]{1,2})$
    static const std::regex bh_regex(R"(^([0-9]{2})(BH)([0-9]{4})([A-Z]{1,2})$)");
    if (std::regex_match(clean, match, bh_regex)) {
        res.is_valid        = true;
        res.rto_code        = match[1].str(); // registration year
        res.state_code      = match[2].str(); // "BH"
        res.unique_digits   = match[3].str();
        res.series          = match[4].str();
        res.formatted_plate = match[1].str() + " BH " + match[3].str() + " " + match[4].str();
        return res;
    }

    // Fallback: general alphanumeric format
    res.is_valid        = (clean.length() >= 5);
    res.formatted_plate = clean;
    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// Embedded ANPR OCR Engine: Character Segmentation & Template Matching
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct CharTemplate {
    char ch;
    cv::Mat img; // 24x36 CV_8UC1 binary image
    bool is_digit;
    int holes; // 0, 1, or 2
};

static int get_char_holes(char c) {
    if (c == '8' || c == 'B') return 2;
    if (c == '0' || c == '4' || c == '6' || c == '9' || c == 'A' ||
        c == 'D' || c == 'O' || c == 'P' || c == 'Q' || c == 'R') return 1;
    return 0;
}

// Counts topological holes (Euler genus) inside the character body
static int count_character_holes(const cv::Mat& char_crop) {
    if (char_crop.empty() || char_crop.cols < 3 || char_crop.rows < 6) return 0;
    cv::Mat padded;
    cv::copyMakeBorder(char_crop, padded, 2, 2, 2, 2, cv::BORDER_CONSTANT, cv::Scalar(0));
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(padded, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);

    int holes = 0;
    for (size_t i = 0; i < hierarchy.size(); ++i) {
        if (hierarchy[i][3] >= 0) { // inner contour inside character body
            double area = cv::contourArea(contours[i]);
            if (area >= 4.0) {
                holes++;
            }
        }
    }
    return holes;
}

// Aspect-preserving normalization of character into standard 24x36 canvas
static cv::Mat normalize_char_image(const cv::Mat& src, int target_w = 24, int target_h = 36) {
    cv::Mat canvas = cv::Mat::zeros(target_h, target_w, CV_8UC1);
    if (src.empty() || src.cols < 1 || src.rows < 1) return canvas;

    int max_inner_h = target_h - 6; // 30
    int max_inner_w = target_w - 4; // 20

    float scale_h = static_cast<float>(max_inner_h) / static_cast<float>(src.rows);
    float scale_w = static_cast<float>(max_inner_w) / static_cast<float>(src.cols);
    float scale = std::min(scale_h, scale_w);

    int new_w = std::clamp(static_cast<int>(src.cols * scale), 1, max_inner_w);
    int new_h = std::clamp(static_cast<int>(src.rows * scale), 1, max_inner_h);

    cv::Mat scaled;
    cv::resize(src, scaled, cv::Size(new_w, new_h), 0, 0, cv::INTER_AREA);
    cv::threshold(scaled, scaled, 90, 255, cv::THRESH_BINARY);

    int offset_x = (target_w - new_w) / 2;
    int offset_y = (target_h - new_h) / 2;
    scaled.copyTo(canvas(cv::Rect(offset_x, offset_y, new_w, new_h)));

    return canvas;
}

// Translation-tolerant Dice similarity (2 * Intersection / (A + B))
// Accurately penalizes extra or missing strokes, avoiding background-correlation bias
static float compute_dice_similarity(const cv::Mat& candidate, const cv::Mat& tmpl) {
    if (candidate.size() != tmpl.size() || candidate.empty() || tmpl.empty()) return 0.0f;

    int cand_white = cv::countNonZero(candidate);
    int tmpl_white = cv::countNonZero(tmpl);
    if (cand_white == 0 || tmpl_white == 0) return 0.0f;

    float best_dice = 0.0f;
    int h = candidate.rows;
    int w = candidate.cols;

    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int src_x = std::max(0, dx);
            int src_y = std::max(0, dy);
            int dst_x = std::max(0, -dx);
            int dst_y = std::max(0, -dy);
            int rw = w - std::abs(dx);
            int rh = h - std::abs(dy);

            if (rw <= 0 || rh <= 0) continue;

            cv::Rect src_roi(src_x, src_y, rw, rh);
            cv::Rect dst_roi(dst_x, dst_y, rw, rh);

            cv::Mat intersection;
            cv::bitwise_and(candidate(src_roi), tmpl(dst_roi), intersection);
            int inter_white = cv::countNonZero(intersection);

            float dice = (2.0f * inter_white) / static_cast<float>(cand_white + tmpl_white);
            if (dice > best_dice) {
                best_dice = dice;
            }
        }
    }
    return best_dice;
}

static std::vector<CharTemplate> init_char_templates() {
    std::vector<CharTemplate> templates;
    const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    for (char c : chars) {
        bool is_digit = (c >= '0' && c <= '9');
        int holes = get_char_holes(c);
        std::string s(1, c);

        for (int font_face : {cv::FONT_HERSHEY_SIMPLEX, cv::FONT_HERSHEY_DUPLEX, cv::FONT_HERSHEY_TRIPLEX}) {
            for (int thickness : {2, 3}) {
                cv::Mat large = cv::Mat::zeros(80, 80, CV_8UC1);
                cv::putText(large, s, cv::Point(15, 60), font_face, 1.4, cv::Scalar(255), thickness, cv::LINE_AA);
                cv::threshold(large, large, 100, 255, cv::THRESH_BINARY);

                std::vector<std::vector<cv::Point>> cnts;
                cv::findContours(large, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                if (!cnts.empty()) {
                    cv::Rect br = cv::boundingRect(cnts[0]);
                    for (size_t k = 1; k < cnts.size(); ++k) br |= cv::boundingRect(cnts[k]);
                    cv::Mat tight = large(br);
                    cv::Mat tmpl_img = normalize_char_image(tight, 24, 36);
                    templates.push_back({c, tmpl_img, is_digit, holes});
                }
            }
        }
    }
    return templates;
}

static const std::vector<CharTemplate>& get_templates() {
    static const std::vector<CharTemplate> s_templates = init_char_templates();
    return s_templates;
}

static char match_char(const cv::Mat& char_crop, int pos, int total_len, float& out_score) {
    out_score = 0.0f;
    if (char_crop.empty() || char_crop.cols < 2 || char_crop.rows < 6) return ' ';

    float char_aspect = static_cast<float>(char_crop.cols) / static_cast<float>(char_crop.rows);
    bool is_narrow = (char_aspect < 0.35f); // '1' or 'I'
    bool is_wide   = (char_aspect > 0.42f); // Wide character ('D', 'O', 'Q', 'C', '8', '9', etc.)
    int cand_holes = count_character_holes(char_crop);

    // Normalize character into 24x36 canvas preserving aspect ratio
    cv::Mat bin = normalize_char_image(char_crop, 24, 36);

    // Apply Indian standard plate grammar constraints:
    bool must_be_letter = false;
    bool must_be_digit  = false;

    if (total_len >= 8 && total_len <= 11) {
        if (pos < 2) {
            must_be_letter = true; // State code (TN, DL, KA, MH, HR)
        } else if (pos >= total_len - 4) {
            must_be_digit = true;  // Last 4 unique digits (1939, 5106)
        }
    }

    const auto& tmpls = get_templates();
    float best_val = -1.0f;
    char  best_c   = '?';

    for (const auto& t : tmpls) {
        if (must_be_letter && t.is_digit) continue;
        if (must_be_digit && !t.is_digit) continue;

        // Aspect ratio consistency filter:
        if (is_wide && (t.ch == '1' || t.ch == 'I')) continue;
        if (is_narrow && (t.ch == '0' || t.ch == 'D' || t.ch == 'O' || t.ch == 'Q' ||
                          t.ch == 'C' || t.ch == 'B' || t.ch == '8' || t.ch == 'M' || t.ch == 'W')) continue;

        // Hole count consistency:
        if (cand_holes == 1 && t.holes != 1) continue;
        if (cand_holes == 2 && t.holes != 2) continue;
        if (cand_holes == 0 && (t.ch == '8' || t.ch == 'B' || t.ch == '0' || t.ch == 'O' || t.ch == 'D' || t.ch == 'Q')) continue;

        float dice = compute_dice_similarity(bin, t.img);

        if (dice > best_val) {
            best_val = dice;
            best_c   = t.ch;
        }
    }

    // Fallback if strict hole filter produced no match
    if (best_val <= 0.20f) {
        for (const auto& t : tmpls) {
            if (must_be_letter && t.is_digit) continue;
            if (must_be_digit && !t.is_digit) continue;
            if (is_wide && (t.ch == '1' || t.ch == 'I')) continue;
            if (is_narrow && (t.ch == '0' || t.ch == 'D' || t.ch == 'O' || t.ch == 'Q' ||
                              t.ch == 'C' || t.ch == 'B' || t.ch == '8' || t.ch == 'M' || t.ch == 'W')) continue;

            float dice = compute_dice_similarity(bin, t.img);
            if (dice > best_val) {
                best_val = dice;
                best_c   = t.ch;
            }
        }
    }

    out_score = best_val;
    return (best_val > 0.30f) ? best_c : ' ';
}

} // anonymous namespace

PlateResult PlateUtils::read_plate_text(const cv::Mat& plate_crop, bool multiline) {
    PlateResult res;
    if (plate_crop.empty() || plate_crop.cols < 30 || plate_crop.rows < 12) {
        return res;
    }

    cv::Mat enhanced = enhance_plate_for_ocr(plate_crop);

    // Standardize plate height to 80px for consistent character scale
    float scale = 80.0f / static_cast<float>(enhanced.rows);
    int target_w = std::max(60, static_cast<int>(enhanced.cols * scale));
    cv::Mat scaled_plate;
    cv::resize(enhanced, scaled_plate, cv::Size(target_w, 80), 0, 0, cv::INTER_LINEAR);

    cv::Mat gray;
    cv::cvtColor(scaled_plate, gray, cv::COLOR_BGR2GRAY);

    // Multi-pass robust thresholding for license plates:
    // Combine Otsu (solid stroke interiors) with Adaptive Gaussian (crisp borders under glare)
    cv::Mat bin_otsu, bin_adapt, binary;
    cv::threshold(gray, bin_otsu, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    cv::adaptiveThreshold(gray, bin_adapt, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, 21, 9);
    cv::bitwise_or(bin_otsu, bin_adapt, binary);

    // Morphological closing to seal broken strokes (prevents 'D' or 'Q' splitting into two halves)
    cv::Mat close_k = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, close_k);

    // Clear outer 4-pixel border to eliminate outer license plate frame/screws
    cv::rectangle(binary, cv::Rect(0, 0, binary.cols, binary.rows), cv::Scalar(0), 4);

    // Find character contours
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

    struct CharBox {
        cv::Rect rect;
        int line_idx; // 0 for top line, 1 for bottom line
    };
    std::vector<CharBox> candidate_boxes;

    int min_h = static_cast<int>(80 * (multiline ? 0.25f : 0.40f)); // ~32px for single line
    int max_h = static_cast<int>(80 * (multiline ? 0.65f : 0.88f)); // ~70px
    int min_w = 4;
    int max_w = static_cast<int>(target_w * 0.25f);

    for (size_t i = 0; i < contours.size(); ++i) {
        cv::Rect r = cv::boundingRect(contours[i]);
        if (r.height < min_h || r.height > max_h) continue;
        if (r.width < min_w || r.width > max_w) continue;

        // Ignore left blue IND strip / hologram (leftmost 8% of plate width)
        if (!multiline && (r.x + r.width / 2 < target_w * 0.08f)) continue;

        // Ignore right plate edge/screws (rightmost 4%)
        if (!multiline && (r.x + r.width / 2 > target_w * 0.96f)) continue;

        float aspect = static_cast<float>(r.width) / static_cast<float>(r.height);
        if (aspect < 0.10f || aspect > 1.25f) continue;

        // Skip inner contours if enclosed by a parent contour (e.g. holes in 0, 8, B, etc.)
        if (hierarchy[i][3] >= 0) {
            cv::Rect parent_r = cv::boundingRect(contours[hierarchy[i][3]]);
            if (parent_r.contains(r.tl()) && parent_r.contains(r.br())) continue;
        }

        int line = (multiline && (r.y + r.height / 2 > 40)) ? 1 : 0;
        candidate_boxes.push_back({r, line});
    }

    // Sort: Line 0 (left-to-right) then Line 1 (left-to-right)
    std::sort(candidate_boxes.begin(), candidate_boxes.end(), [](const CharBox& a, const CharBox& b) {
        if (a.line_idx != b.line_idx) return a.line_idx < b.line_idx;
        return a.rect.x < b.rect.x;
    });

    // Merge horizontally adjacent fragments that belong to the same character (e.g. split 'D' or 'Q')
    std::vector<CharBox> filtered_boxes;
    for (size_t i = 0; i < candidate_boxes.size(); ++i) {
        if (filtered_boxes.empty()) {
            filtered_boxes.push_back(candidate_boxes[i]);
            continue;
        }

        auto& prev = filtered_boxes.back();
        const auto& curr = candidate_boxes[i];

        if (prev.line_idx == curr.line_idx) {
            int gap = curr.rect.x - (prev.rect.x + prev.rect.width);
            int y_overlap = std::min(prev.rect.y + prev.rect.height, curr.rect.y + curr.rect.height) -
                            std::max(prev.rect.y, curr.rect.y);
            float v_overlap_ratio = static_cast<float>(y_overlap) / static_cast<float>(std::min(prev.rect.height, curr.rect.height));

            cv::Rect combo = prev.rect | curr.rect;
            float combo_aspect = static_cast<float>(combo.width) / static_cast<float>(combo.height);

            // If fragments are touching or within 4px, have >50% vertical overlap,
            // and combine into a valid character aspect ratio (<= 0.85):
            if (gap <= 4 && v_overlap_ratio > 0.50f && combo_aspect <= 0.85f) {
                prev.rect = combo; // heal into single character box!
                continue;
            }
        }
        filtered_boxes.push_back(curr);
    }

    if (filtered_boxes.size() < 4) {
        return res;
    }

    // OCR each character
    std::string raw_chars;
    float sum_scores = 0.0f;
    int matched_count = 0;
    int total_chars = static_cast<int>(filtered_boxes.size());

    for (int i = 0; i < total_chars; ++i) {
        cv::Rect cr = filtered_boxes[i].rect & cv::Rect(0, 0, binary.cols, binary.rows);
        if (cr.width < 2 || cr.height < 4) continue;
        cv::Mat char_crop = binary(cr);
        float score = 0.0f;
        char ch = match_char(char_crop, i, total_chars, score);
        if (ch != ' ' && ch != '?') {
            raw_chars += ch;
            sum_scores += score;
            matched_count++;
        }
    }

    if (raw_chars.length() < 4) {
        return res;
    }

    // Format & validate through Indian license plate normalizer
    float avg_score = matched_count > 0 ? (sum_scores / matched_count) : 0.5f;
    float conf = std::clamp(avg_score, 0.40f, 0.98f);
    res = validate_and_format_plate(raw_chars, conf);
    res.raw_text = raw_chars;
    res.is_multiline = multiline;

    return res;
}

} // namespace session_pipeline

