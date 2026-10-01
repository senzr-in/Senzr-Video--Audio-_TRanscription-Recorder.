#include "session_pipeline/video_capture.hpp"
#include "session_pipeline/config.hpp"
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <thread>
#include <iostream>

namespace session_pipeline {

VideoCaptureWorker::VideoCaptureWorker(DetectFrameQueue& detect_q,
                                       RecordFrameQueue& record_q)
    : detect_q_(detect_q), record_q_(record_q) {}

void VideoCaptureWorker::run(std::stop_token stoken) {
    cv::VideoCapture cap;
    int active_index = CAMERA_INDEX;

    // Try configured index first, then scan other video indices (e.g. /dev/video2 on RK3588)
    for (int idx : {CAMERA_INDEX, 2, 1, 0, 3, 4, 5}) {
        cap.open(idx, cv::CAP_V4L2);
        if (cap.isOpened()) {
            active_index = idx;
            break;
        }
    }

    if (!cap.isOpened()) {
        std::cerr << "[video_capture] ERROR: cannot open camera on any video index\n";
        return;
    }

    cap.set(cv::CAP_PROP_BUFFERSIZE,   1); // Zero driver buffer lag
    cap.set(cv::CAP_PROP_FOURCC,
            cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  FRAME_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT);
    cap.set(cv::CAP_PROP_FPS,          FPS);

    // Camera Exposure Mode
    const char* env_shutter = std::getenv("CAMERA_FAST_SHUTTER");
    bool use_fast_shutter = env_shutter ? (std::string(env_shutter) != "0") : FAST_SHUTTER_MODE;
    if (use_fast_shutter) {
        // V4L2 manual exposure mode (1 = manual)
        cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);
        int exp_val = MANUAL_EXPOSURE_VAL;
        const char* env_exp = std::getenv("CAMERA_EXPOSURE");
        if (env_exp) {
            try { exp_val = std::stoi(env_exp); } catch (...) {}
        }
        cap.set(cv::CAP_PROP_EXPOSURE, exp_val);
        std::cout << "[video_capture] Fast shutter mode: manual exposure=" << exp_val << "\n";
    } else {
        // Auto-exposure mode (3 = Aperture Priority / Auto in V4L2)
        cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 3);
        std::cout << "[video_capture] Auto-exposure enabled for bright, natural video\n";
    }

    std::cout << "[video_capture] started on /dev/video" << active_index << "\n";

    cv::Mat frame;
    using namespace std::chrono;

    while (!stoken.stop_requested()) {
        if (!cap.read(frame) || frame.empty()) {
            std::this_thread::sleep_for(milliseconds(50));
            continue;
        }

        // Ensure 640×480 BGR uint8 (C-contiguous in OpenCV by default)
        if (frame.cols != FRAME_WIDTH || frame.rows != FRAME_HEIGHT) {
            cv::resize(frame, frame, {FRAME_WIDTH, FRAME_HEIGHT});
        }

        double ts = duration_cast<duration<double>>(
                        system_clock::now().time_since_epoch())
                        .count();

        VideoFrame vf;
        vf.width     = frame.cols;
        vf.height    = frame.rows;
        vf.timestamp = ts;
        vf.data.assign(frame.datastart, frame.dataend);

        // Fan-out: silently drop if queue is full (matches Python put_nowait)
        detect_q_.try_push(vf);
        record_q_.try_push(std::move(vf));
    }

    cap.release();
    std::cout << "[video_capture] stopped\n";
}

} // namespace session_pipeline
