#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>
#include <regex>

namespace session_pipeline {

/**
 * PlateResult encapsulates cleaned, validated, and normalized number plate data.
 */
struct PlateResult {
    bool        is_valid{false};
    std::string raw_text;
    std::string formatted_plate;  // e.g. "KA 05 MJ 1234"
    std::string clean_number;     // e.g. "KA05MJ1234" (no spaces)
    std::string state_code;       // "KA"
    std::string rto_code;         // "05"
    std::string series;           // "MJ"
    std::string unique_digits;    // "1234"
    float       confidence{0.0f};
    bool        is_multiline{false};
    int         x{0};
    int         y{0};
    int         w{0};
    int         h{0};
};

class PlateUtils {
public:
    /**
     * Fix Problem 5: Night-time Headlight Glare & Low-Light Enhancement.
     * Applies CLAHE (Contrast Limited Adaptive Histogram Equalization) on Luminance
     * and subtle bilateral filtering to restore contrast of washed-out white/yellow plates.
     */
    static cv::Mat enhance_plate_for_ocr(const cv::Mat& plate_bgr);

    /**
     * Fix Problem 3: Multi-line Plate Handling (2-wheelers, trucks, auto-rickshaws).
     * Inspects bounding box aspect ratio:
     * - Single-line plates: aspect ratio >= 3.0 (e.g. 500mm x 120mm -> ~4.1)
     * - Two-line plates:   aspect ratio < 3.0  (e.g. 200mm x 100mm -> ~2.0)
     *
     * If two-line, splits into Top Line (State+RTO) and Bottom Line (Series+Number).
     */
    static bool is_multiline_plate(const cv::Mat& plate_crop);
    static std::vector<cv::Mat> split_plate_lines(const cv::Mat& plate_crop);

    /**
     * Fix Problem 4: HSRP Blue "IND" Strip Cleanup & License Plate Normalization.
     * - Strips leading "IND", "1ND", "IND-", "IN0", "I0", "ND" prefixes
     * - Fixes common OCR letter/digit confusions at known position indexes
     * - Validates Indian standard formats (State-RTO-Series-Number and BH series)
     */
    static std::string strip_hsrp_ind_prefix(const std::string& raw_text);
    static std::string normalize_plate_characters(const std::string& text);
    static PlateResult validate_and_format_plate(const std::string& raw_text, float confidence = 1.0f);

    /**
     * Performs character segmentation, template matching, and Indian ANPR normalization
     * on a cropped plate image to extract the actual registration number.
     */
    static PlateResult read_plate_text(const cv::Mat& plate_crop, bool multiline);
};

} // namespace session_pipeline
