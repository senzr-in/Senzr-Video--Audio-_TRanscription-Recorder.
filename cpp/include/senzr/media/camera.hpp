#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>
#include <vector>
#include <memory>

namespace senzr::media {

/**
 * @brief Common video pixel formats across Linux platforms.
 */
enum class PixelFormat {
    NV12,
    YUYV,
    RGB24,
    BGR24,
    MJPEG,
    H264
};

/**
 * @brief Video stream frame dimensions.
 */
struct VideoResolution {
    int width{1920};
    int height{1080};
};

/**
 * @brief Comprehensive camera hardware descriptor.
 */
struct CameraInfo {
    std::string id;          // e.g. "/dev/video0" or "csi:0" or "rtsp://..."
    std::string name;        // e.g. "USB Camera" or "IMX219 (CSI-2)"
    std::string bus_info;    // e.g. "usb-0000:01:00.0-1" or "tegra-cam:0"
    bool is_csi{false};
    bool is_usb{false};
    bool is_network{false};
    std::vector<VideoResolution> supported_resolutions;
    std::vector<PixelFormat> supported_formats;
};

/**
 * @brief Platform-agnostic camera discovery and stream manager.
 */
class CameraManager {
public:
    static CameraManager& get_instance();

    std::vector<CameraInfo> discover_cameras();
    Result<CameraInfo> get_default_camera();

private:
    CameraManager();
    ~CameraManager();
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace senzr::media
