#include "app/api_server.hpp"
#include "app/ntp_client.hpp"
#include "app/config_manager.hpp"
#include <iostream>

int main() {
    std::cout << "[edge-gateway] Starting Edge Gateway...\n";
    
    // 1. Read saved configuration and configure system timezone
    app::ConfigManager config_mgr;
    auto cfg = config_mgr.read_config();
    std::string tz = cfg.timezone.empty() ? "Asia/Kolkata" : cfg.timezone;
    app::NtpClient::set_system_timezone(tz);

    // 2. Perform in-code NTP clock synchronization across fallback servers
    app::NtpClient ntp;
    ntp.sync_with_fallbacks();

    app::ApiServer server;
    return server.run();
}

