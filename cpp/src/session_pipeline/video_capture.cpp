#include "session_pipeline/video_capture.hpp"
#include "session_pipeline/config.hpp"
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstdlib>

#ifdef _WIN32
static inline void set_env_var(const char* name, const char* value) {
    _putenv_s(name, value);
}
#else
static inline void set_env_var(const char* name, const char* value) {
    setenv(name, value, 1);
}
#endif

namespace session_pipeline {

VideoCaptureWorker::VideoCaptureWorker(DetectFrameQueue& detect_q,
                                       RecordFrameQueue& record_q)
    : detect_q_(detect_q), record_q_(record_q) {}

static bool is_network_stream(const std::string& src) {
    return (src.rfind("rtsp://", 0) == 0 ||
            src.rfind("rtsps://", 0) == 0 ||
            src.rfind("http://", 0) == 0 ||
            src.rfind("https://", 0) == 0);
}

void VideoCaptureWorker::run(std::stop_token stoken) {
    using namespace std::chrono;

    // Check environment variable CAMERA_SOURCE first; fallback to config.hpp
    const char* env_source = std::getenv("CAMERA_SOURCE");
    std::string source = (env_source && std::string(env_source).length() > 0) ? env_source : CAMERA_SOURCE;
    bool is_net = is_network_stream(source);

    // If RTSP / network stream, configure FFmpeg backend for zero buffer lag and TCP transport
    if (is_net) {
        set_env_var("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp|fflags;nobuffer|max_delay;500000");
    }

    while (!stoken.stop_requested()) {
        cv::VideoCapture cap;
        int active_index = CAMERA_INDEX;

        bool opened = false;

        if (is_net) {
            std::cout << "[video_capture] Connecting to IP camera: " << source << " ...\n";
            cap.open(source, cv::CAP_FFMPEG);
            if (cap.isOpened()) {
                opened = true;
            } else {
                std::cerr << "[video_capture] WARNING: IP camera (" << source 
                          << ") unreachable / connection refused. Checking local camera fallback...\n";
                // Attempt local V4L2 fallback
                for (int idx : {0, 1, 2, 3, 4, 5}) {
                    cap.open(idx, cv::CAP_V4L2);
                    if (cap.isOpened()) {
                        active_index = idx;
                        is_net = false;
                        opened = true;
                        std::cout << "[video_capture] ✓ Successfully fell back to local camera /dev/video" 
                                  << active_index << "\n";
                        break;
                    }
                }
            }
        } else {
            // Local V4L2 device index
            int requested_index = CAMERA_INDEX;
            try { requested_index = std::stoi(source); } catch (...) {}

            for (int idx : {requested_index, 0, 1, 2, 3, 4, 5}) {
                cap.open(idx, cv::CAP_V4L2);
                if (cap.isOpened()) {
                    active_index = idx;
                    opened = true;
                    break;
                }
            }
        }

        if (!opened || !cap.isOpened()) {
            std::cerr << "[video_capture] ERROR: cannot open camera source ("
                      << (is_net ? source : ("/dev/video" + std::to_string(active_index)))
                      << "). Retrying in " << RECONNECT_DELAY_MS << "ms...\n";

            // Sleep with stop_token responsiveness
            for (int i = 0; i < RECONNECT_DELAY_MS / 100 && !stoken.stop_requested(); ++i) {
                std::this_thread::sleep_for(milliseconds(100));
            }
            continue;
        }

        // Configure capture parameters
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1); // Zero driver buffer lag

        if (!is_net) {
            cap.set(cv::CAP_PROP_FOURCC,
                    cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            cap.set(cv::CAP_PROP_FRAME_WIDTH,  FRAME_WIDTH);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT);
            cap.set(cv::CAP_PROP_FPS,          FPS);

            // Camera Exposure Mode (V4L2 only)
            const char* env_shutter = std::getenv("CAMERA_FAST_SHUTTER");
            bool use_fast_shutter = env_shutter ? (std::string(env_shutter) != "0") : FAST_SHUTTER_MODE;
            if (use_fast_shutter) {
                cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);
                int exp_val = MANUAL_EXPOSURE_VAL;
                const char* env_exp = std::getenv("CAMERA_EXPOSURE");
                if (env_exp) {
                    try { exp_val = std::stoi(env_exp); } catch (...) {}
                }
                cap.set(cv::CAP_PROP_EXPOSURE, exp_val);
                std::cout << "[video_capture] Fast shutter mode: manual exposure=" << exp_val << "\n";
            } else {
                cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 3);
                std::cout << "[video_capture] Auto-exposure enabled for bright, natural video\n";
            }
            std::cout << "[video_capture] started on /dev/video" << active_index << "\n";
        } else {
            std::cout << "[video_capture] IP camera stream opened successfully.\n";
        }

        cv::Mat frame;
        int empty_frame_count = 0;
        constexpr int MAX_EMPTY_FRAMES_BEFORE_RECONNECT = 40; // ~2 seconds of dropped frames

        while (!stoken.stop_requested()) {
            if (!cap.read(frame) || frame.empty()) {
                empty_frame_count++;
                if (empty_frame_count >= MAX_EMPTY_FRAMES_BEFORE_RECONNECT) {
                    std::cerr << "[video_capture] WARNING: Stream disconnected / stalled. Reconnecting...\n";
                    break;
                }
                std::this_thread::sleep_for(milliseconds(50));
                continue;
            }
            empty_frame_count = 0;

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
    }

    std::cout << "[video_capture] stopped\n";
}

} // namespace session_pipeline
