#pragma once
#include <string>
#include <vector>
#include <optional>
#include "app/models.hpp"

namespace app {

class LoggerUtils {
public:
    explicit LoggerUtils(std::string log_path = "logs/changes.log", std::string lease_file = "/var/lib/misc/dnsmasq.leases");
    static std::string get_timestamp();
    static std::vector<std::string> diff_configs(const ConfigModel& old_config, const ConfigModel& new_config);
    std::optional<std::string> get_mac_from_ip(const std::string& client_ip) const;
    void write_file_log(const std::string& json_line) const;

private:
    std::string log_path_;
    std::string lease_file_;
    void ensure_log_dir() const;
};

}
