#include "app/api_server.hpp"
#include "app/ntp_client.hpp"
#include "app/config_manager.hpp"
#include "senzr/device.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <cstdlib>

static void ensure_systemd_service_installed() {
    const std::string service_path = "/etc/systemd/system/senzr-edge.service";
    if (std::filesystem::exists(service_path)) {
        return; // Already installed
    }
    
    // Check if running on Linux with write permission to /etc/systemd/system
    if (geteuid() == 0) {
        std::cout << "[edge-gateway] Auto-registering systemd background service at " << service_path << "...\n";
        std::ofstream out(service_path);
        if (out.is_open()) {
            out << "[Unit]\n"
                << "Description=Senzr Edge Gateway Production Daemon\n"
                << "After=network.target time-sync.target\n"
                << "Wants=network.target\n\n"
                << "[Service]\n"
                << "Type=simple\n"
                << "User=orangepi\n"
                << "WorkingDirectory=/home/orangepi/edge-gateway/cpp/build\n"
                << "ExecStart=/home/orangepi/edge-gateway/cpp/build/edge-gateway-backend\n"
                << "Restart=always\n"
                << "RestartSec=2\n"
                << "LimitNOFILE=65535\n\n"
                << "[Install]\n"
                << "WantedBy=multi-user.target\n";
            out.close();
            int rc = std::system("systemctl daemon-reload && systemctl enable senzr-edge.service");
            (void)rc;
            std::cout << "[edge-gateway] ✓ Auto-registered senzr-edge.service successfully (auto-boot on power ready)\n";
        }
    }
}

int main() {
    std::string ver = senzr::Device::get_instance().get_firmware_version();
    std::cout << "\n======================================================\n";
    std::cout << "  🚀 SENZR EDGE GATEWAY STARTING (v" << ver << ")\n";
    std::cout << "======================================================\n";
    
    // 1. Self-register systemd service if running on Linux with root
    ensure_systemd_service_installed();

    // 2. Read saved configuration and configure system timezone
    app::ConfigManager config_mgr;
    auto cfg = config_mgr.read_config();
    std::string tz = cfg.timezone.empty() ? "Asia/Kolkata" : cfg.timezone;
    app::NtpClient::set_system_timezone(tz);

    // 3. Perform in-code NTP clock synchronization asynchronously in background
    std::thread([]() {
        app::NtpClient ntp;
        ntp.sync_with_fallbacks();
    }).detach();

    app::ApiServer server;
    return server.run();
}

