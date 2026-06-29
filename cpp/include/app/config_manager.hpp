#pragma once
#include <string>
#include "app/models.hpp"

namespace app {

class ConfigManager {
public:
    explicit ConfigManager(std::string config_path = "database/config.json");
    ConfigModel read_config() const;
    void write_config(const ConfigModel& config) const;

private:
    std::string config_path_;
};

}
