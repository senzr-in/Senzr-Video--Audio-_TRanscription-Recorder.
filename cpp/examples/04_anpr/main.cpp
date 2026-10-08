#include <senzr/senzr.hpp>
#include <iostream>

int main() {
    std::cout << "=== Example 04: ANPR Indian & Bharat License Plate Normalization ===\n";

    // 1. Standard Indian State plate test
    std::string raw_plate = "IND KA05MJ1234";
    std::cout << "Raw Plate Input   : " << raw_plate << "\n";

    std::string stripped = senzr::ai::AnprEngine::strip_hsrp_prefix(raw_plate);
    std::cout << "HSRP Prefix Strip : " << stripped << "\n";

    auto plate_info = senzr::ai::AnprEngine::validate_and_format_plate(raw_plate);
    std::cout << "Parsed Plate Info :\n"
              << "  Is Valid        : " << (plate_info.is_valid ? "TRUE" : "FALSE") << "\n"
              << "  Formatted Plate : " << plate_info.formatted_plate << "\n"
              << "  State Code      : " << plate_info.state_code << "\n"
              << "  RTO Code        : " << plate_info.rto_code << "\n"
              << "  Series          : " << plate_info.series << "\n"
              << "  Unique Digits   : " << plate_info.unique_digits << "\n\n";

    // 2. Bharat (BH) series test
    std::string bh_plate = "22BH1234AA";
    auto bh_info = senzr::ai::AnprEngine::validate_and_format_plate(bh_plate);
    std::cout << "Bharat Series Plate (" << bh_plate << ") -> " << bh_info.formatted_plate << "\n";

    return 0;
}
