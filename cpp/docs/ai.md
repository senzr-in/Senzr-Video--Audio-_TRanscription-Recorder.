# AI Vision & Edge NPU Engine

## Overview

The `senzr::ai` subsystem enables high-throughput inference on Rockchip RK3588 triple-core NPU (6 TOPS peak compute).

## Subsystems

1. **`senzr::ai::PersonDetector`**:
   - Multi-core inference scheduling
   - Zero-copy tensor preprocessing
   - Person & pose estimation

2. **`senzr::ai::AnprEngine`**:
   - OCR character bounding box parsing
   - Indian standard plate normalization (`STATE-RTO-SERIES-NUMBER`)
   - Bharat series recognition (`YY BH #### XX`)
   - High Security Registration Plate (HSRP) prefix stripping (`IND`, `IN`, `IND.`)

## Code Example

```cpp
#include <senzr/ai/anpr.hpp>

auto plate_info = senzr::ai::AnprEngine::validate_and_format_plate("IND KA05MJ1234");
if (plate_info.is_valid) {
    std::cout << "Clean Plate: " << plate_info.formatted_plate << "\n";
}
```
