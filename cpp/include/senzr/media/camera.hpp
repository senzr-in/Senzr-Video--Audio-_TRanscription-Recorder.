#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>
#include <vector>
#include <memory>

namespace senzr {
inline namespace v1 {
namespace media {

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
struct SENZR_API VideoResolution {
    int width{1920};
    int height{1080};
};

/**
 * @brief Comprehensive camera hardware descriptor.
 */
struct SENZR_API CameraInfo {
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
class SENZR_API CameraManager {
public:
    static CameraManager& get_instance();

    [[nodiscard]] std::vector<CameraInfo> discover_cameras();
    [[nodiscard]] Result<CameraInfo> get_default_camera();

    ~CameraManager();
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;
    CameraManager(CameraManager&&) noexcept;
    CameraManager& operator=(CameraManager&&) noexcept;

private:
    CameraManager();

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace media
} // namespace v1
} // namespace senzr
