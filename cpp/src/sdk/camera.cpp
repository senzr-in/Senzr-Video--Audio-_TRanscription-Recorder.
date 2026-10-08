#include "senzr/media/camera.hpp"
#include <filesystem>
#include <fstream>

namespace senzr {
inline namespace v1 {
namespace media {

struct CameraManager::Impl {
    std::vector<CameraInfo> cameras;

    void refresh_cameras() {
        cameras.clear();
        
        // 1. Probing V4L2 USB / CSI devices (/dev/video*)
        for (int i = 0; i < 8; ++i) {
            std::string dev_path = "/dev/video" + std::to_string(i);
            if (std::filesystem::exists(dev_path)) {
                CameraInfo cam;
                cam.id = dev_path;
                cam.name = "Linux V4L2 Video Device (" + dev_path + ")";
                cam.is_usb = true;
                cam.supported_resolutions = {
                    {1920, 1080},
                    {1280, 720},
                    {640, 480}
                };
                cam.supported_formats = {
                    PixelFormat::NV12,
                    PixelFormat::YUYV,
                    PixelFormat::MJPEG
                };
                cameras.push_back(cam);
            }
        }

        // 2. Check CSI camera device tree (Jetson / RK3588 CSI-2 MIPI)
        if (std::filesystem::exists("/sys/class/video4linux/v4l-subdev0") ||
            std::filesystem::exists("/dev/v4l/by-id")) {
            CameraInfo csi_cam;
            csi_cam.id = "csi:0";
            csi_cam.name = "MIPI CSI-2 Onboard Camera Sensor";
            csi_cam.is_csi = true;
            csi_cam.supported_resolutions = {
                {3840, 2160},
                {1920, 1080},
                {1280, 720}
            };
            csi_cam.supported_formats = {
                PixelFormat::NV12,
                PixelFormat::RGB24
            };
            cameras.push_back(csi_cam);
        }
    }
};

CameraManager& CameraManager::get_instance() {
    static CameraManager inst;
    return inst;
}

CameraManager::CameraManager() : pimpl_(std::make_unique<Impl>()) {
    pimpl_->refresh_cameras();
}

CameraManager::~CameraManager() = default;
CameraManager::CameraManager(CameraManager&&) noexcept = default;
CameraManager& CameraManager::operator=(CameraManager&&) noexcept = default;

std::vector<CameraInfo> CameraManager::discover_cameras() {
    pimpl_->refresh_cameras();
    return pimpl_->cameras;
}

Result<CameraInfo> CameraManager::get_default_camera() {
    pimpl_->refresh_cameras();
    if (pimpl_->cameras.empty()) {
        return Result<CameraInfo>::failure(ErrorCode::ResourceUnavailable, "No video camera hardware detected (/dev/video* or CSI)");
    }
    return Result<CameraInfo>::success(pimpl_->cameras.front());
}

} // namespace media
} // namespace v1
} // namespace senzr
