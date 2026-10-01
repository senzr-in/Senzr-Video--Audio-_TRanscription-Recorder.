#include "app/config_manager.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace app {

using json = nlohmann::json;

ConfigManager::ConfigManager(std::string config_path)
    : config_path_(std::move(config_path)) {}

ConfigModel ConfigManager::read_config() const {
    std::ifstream file(config_path_);
    if (!file.is_open()) return {};

    json j;
    file >> j;

    ConfigModel cfg;
    cfg.devicename         = j.value("devicename",         "");
    cfg.mode               = j.value("mode",               "object");
    cfg.timezone           = j.value("timezone",           "Asia/Kolkata");
    cfg.wifissid           = j.value("wifissid",           "");
    cfg.wifipassword       = j.value("wifipassword",       "");
    cfg.provisioningenabled = j.value("provisioningenabled", false);
    cfg.cameraconnected    = j.value("cameraconnected",    false);
    return cfg;
}

void ConfigManager::write_config(const ConfigModel& config) const {
    std::filesystem::path p(config_path_);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    json j = {
        {"devicename",          config.devicename},
        {"mode",                config.mode},
        {"timezone",            config.timezone},
        {"wifissid",            config.wifissid},
        {"wifipassword",        config.wifipassword},
        {"provisioningenabled", config.provisioningenabled},
        {"cameraconnected",     config.cameraconnected}
    };

    std::ofstream file(config_path_);
    file << j.dump(2) << std::endl;
}

} // namespace app
