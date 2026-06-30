#include "app/config_manager.hpp"
<<<<<<< HEAD
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace app {

=======
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace app {

using json = nlohmann::json;

>>>>>>> fc6c84e (Half-way cpp conversion)
ConfigManager::ConfigManager(std::string config_path) : config_path_(std::move(config_path)) {}

ConfigModel ConfigManager::read_config() const {
    std::ifstream file(config_path_);
    if (!file.is_open()) return {};
<<<<<<< HEAD
    json j; file >> j;
=======
    json j;
    file >> j;
>>>>>>> fc6c84e (Half-way cpp conversion)
    ConfigModel cfg;
    cfg.devicename = j.value("devicename", "");
    cfg.mode = j.value("mode", "face");
    cfg.wifissid = j.value("wifissid", "");
    cfg.wifipassword = j.value("wifipassword", "");
    cfg.provisioningenabled = j.value("provisioningenabled", false);
    cfg.cameraconnected = j.value("cameraconnected", false);
    return cfg;
}

void ConfigManager::write_config(const ConfigModel& config) const {
<<<<<<< HEAD
=======
    std::filesystem::path p(config_path_);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
>>>>>>> fc6c84e (Half-way cpp conversion)
    json j = {
        {"devicename", config.devicename},
        {"mode", config.mode},
        {"wifissid", config.wifissid},
        {"wifipassword", config.wifipassword},
        {"provisioningenabled", config.provisioningenabled},
        {"cameraconnected", config.cameraconnected}
    };
    std::ofstream file(config_path_);
<<<<<<< HEAD
    file << j.dump(2);
=======
    file << j.dump(2) << std::endl;
>>>>>>> fc6c84e (Half-way cpp conversion)
}

}
