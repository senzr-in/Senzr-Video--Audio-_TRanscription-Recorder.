#pragma once
#include <chrono>
#include "session_pipeline/models.hpp"

namespace session_pipeline {

// Mirrors Python events.py: make_event(event_type) -> dict
inline PipelineEvent make_event(EventType type) {
    using namespace std::chrono;
    double ts = duration_cast<duration<double>>(
                    system_clock::now().time_since_epoch())
                    .count();
    return PipelineEvent{type, ts};
}

// Constants matching Python queue string literals
inline constexpr const char* START_RECORDING = "START_RECORDING";
inline constexpr const char* STOP_RECORDING  = "STOP_RECORDING";

} // namespace session_pipeline
