#include "app/logger_utils.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace app {

LoggerUtils::LoggerUtils(std::string log_path, std::string lease_file)
    : log_path_(std::move(log_path)), lease_file_(std::move(lease_file)) {}

std::string LoggerUtils::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::vector<std::string> LoggerUtils::diff_configs(const ConfigModel& old_config, const ConfigModel& new_config) {
    std::vector<std::string> changed;
    if (old_config.devicename != new_config.devicename) changed.push_back("devicename");
    if (old_config.mode != new_config.mode) changed.push_back("mode");
    if (old_config.wifissid != new_config.wifissid) changed.push_back("wifissid");
    if (old_config.wifipassword != new_config.wifipassword) changed.push_back("wifipassword");
    if (old_config.provisioningenabled != new_config.provisioningenabled) changed.push_back("provisioningenabled");
    if (old_config.cameraconnected != new_config.cameraconnected) changed.push_back("cameraconnected");
    return changed;
}

std::optional<std::string> LoggerUtils::get_mac_from_ip(const std::string&) const {
    return std::nullopt;
}

void LoggerUtils::ensure_log_dir() const {
    std::filesystem::path p(log_path_);
    std::filesystem::create_directories(p.parent_path());
}

void LoggerUtils::write_file_log(const std::string& json_line) const {
    ensure_log_dir();
    std::ofstream file(log_path_, std::ios::app);
    file << json_line << '\n';
}

}
