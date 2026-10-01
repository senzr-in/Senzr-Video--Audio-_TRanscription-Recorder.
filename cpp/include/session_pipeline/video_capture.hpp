#pragma once
#include <thread>
#include "session_pipeline/queues.hpp"
#include "session_pipeline/models.hpp"

namespace session_pipeline {

// ── VideoCaptureWorker ────────────────────────────────────────────────────────
// Mirrors Python video_capture.py: VideoCaptureWorker
//
// Captures frames from /dev/video0 (V4L2, MJPG, 640×480 @ 20fps) and fans
// them out to two downstream queues — one for detection, one for recording.
class VideoCaptureWorker {
public:
    VideoCaptureWorker(DetectFrameQueue& detect_q,
                       RecordFrameQueue& record_q);

    // Entry point — pass std::stop_token from std::jthread
    void run(std::stop_token stoken);

private:
    DetectFrameQueue& detect_q_;
    RecordFrameQueue& record_q_;
};

} // namespace session_pipeline
