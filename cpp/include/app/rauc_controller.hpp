#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace app {

struct RaucSlotInfo {
    std::string name;       // "rootfs.0" (Slot A) or "rootfs.1" (Slot B)
    std::string device;     // e.g. "/dev/mmcblk0p2"
    std::string status;     // "active", "inactive"
    std::string boot_status;// "good", "bad"
    std::string bundle_ver;
};

class RaucController {
public:
    static RaucController& instance() {
        static RaucController inst;
        return inst;
    }

    RaucController();
    ~RaucController();

    // Check if RAUC daemon is available
    bool is_rauc_available() const;

    // Trigger atomic dual A/B rootfs update bundle installation (.raucb)
    bool install_bundle(const std::string& bundle_path, std::string& error_msg);

    // Get current active rootfs slot (Slot A or Slot B)
    std::string get_active_slot() const;

    // Mark current booted slot as good (resets U-Boot bootcounter)
    bool mark_boot_good();

    // Get comprehensive status of all slots
    std::vector<RaucSlotInfo> get_slot_status() const;

    // JSON serialization
    nlohmann::json status_json() const;

private:
    static std::string exec_cmd(const std::string& cmd);
};

} // namespace app
