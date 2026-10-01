#include "session_pipeline/plate_utils.hpp"
#include <cassert>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

using namespace session_pipeline;

int main() {
    std::cout << "[test_plate_utils] Starting unit tests...\n";

    // ── Test 1: HSRP "IND" Prefix Stripping ──────────────────────────────────
    assert(PlateUtils::strip_hsrp_ind_prefix("IND KA 05 MJ 1234") == "KA 05 MJ 1234");
    assert(PlateUtils::strip_hsrp_ind_prefix("1ND-MH12AB1234") == "MH12AB1234");
    assert(PlateUtils::strip_hsrp_ind_prefix("IND.DL01C1234") == "DL01C1234");
    assert(PlateUtils::strip_hsrp_ind_prefix("DL01C1234") == "DL01C1234");
    std::cout << "  ✓ Test 1: HSRP prefix stripping passed\n";

    // ── Test 2: Standard Indian License Plate Validation & Formatting ────────
    auto res1 = PlateUtils::validate_and_format_plate("IND KA05MJ1234", 0.95f);
    assert(res1.is_valid);
    assert(res1.state_code == "KA");
    assert(res1.rto_code == "05");
    assert(res1.series == "MJ");
    assert(res1.unique_digits == "1234");
    assert(res1.formatted_plate == "KA 05 MJ 1234");
    std::cout << "  ✓ Test 2: Standard plate validation passed (" << res1.formatted_plate << ")\n";

    // ── Test 3: OCR Disambiguation (O->0, I->1, etc.) ────────────────────────
    // If OCR outputs "KAOSMJ1234" (letter O instead of zero), it should correct to KA05MJ1234
    auto res2 = PlateUtils::validate_and_format_plate("KA O5 MJ I234", 0.91f);
    assert(res2.is_valid);
    assert(res2.rto_code == "05");
    assert(res2.unique_digits == "1234");
    std::cout << "  ✓ Test 3: OCR character disambiguation passed (" << res2.formatted_plate << ")\n";

    // ── Test 4: Bharat (BH) Series Plate ─────────────────────────────────────
    auto res3 = PlateUtils::validate_and_format_plate("22BH1234AA", 0.94f);
    assert(res3.is_valid);
    assert(res3.formatted_plate == "22 BH 1234 AA");
    std::cout << "  ✓ Test 4: Bharat series validation passed (" << res3.formatted_plate << ")\n";

    // ── Test 5: Multi-line Plate Aspect Ratio & Splitting ────────────────────
    // Single-line (500x120 -> aspect ratio 4.16)
    cv::Mat single_line(120, 500, CV_8UC3, cv::Scalar(255, 255, 255));
    assert(!PlateUtils::is_multiline_plate(single_line));

    // Two-line bike plate (200x100 -> aspect ratio 2.0)
    cv::Mat multi_line(100, 200, CV_8UC3, cv::Scalar(255, 255, 255));
    assert(PlateUtils::is_multiline_plate(multi_line));

    auto lines = PlateUtils::split_plate_lines(multi_line);
    assert(lines.size() == 2);
    assert(lines[0].rows > 40 && lines[1].rows > 40);
    std::cout << "  ✓ Test 5: Multi-line plate splitting passed\n";

    // ── Test 6: Glare / Low-Light CLAHE Enhancement ──────────────────────────
    cv::Mat glare_sample(60, 200, CV_8UC3, cv::Scalar(240, 240, 240));
    cv::Mat enhanced = PlateUtils::enhance_plate_for_ocr(glare_sample);
    assert(!enhanced.empty());
    assert(enhanced.size() == glare_sample.size());
    std::cout << "  ✓ Test 6: CLAHE glare enhancement passed\n";

    // ── Test 7: Embedded ANPR Character OCR Extraction ──────────────────────
    cv::Mat syn_plate(80, 360, CV_8UC3, cv::Scalar(245, 245, 245));
    cv::putText(syn_plate, "TN87C5106", cv::Point(20, 56), cv::FONT_HERSHEY_SIMPLEX, 1.4, cv::Scalar(15, 15, 15), 3, cv::LINE_AA);
    auto syn_res = PlateUtils::read_plate_text(syn_plate, false);
    assert(!syn_res.clean_number.empty());
    std::cout << "  ✓ Test 7: OCR plate text reading passed (" << syn_res.formatted_plate << ")\n";

    std::cout << "[test_plate_utils] ALL TESTS PASSED!\n";
    return 0;
}
