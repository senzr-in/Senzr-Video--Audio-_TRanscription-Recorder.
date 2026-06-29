#pragma once
#include <string>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "app/config_manager.hpp"
#include "app/database.hpp"
#include "app/logger_utils.hpp"
#include "session_pipeline/run_session_pipeline.hpp"

namespace app {

struct StatusResponse {
    std::string status;
    std::string hostname;
    std::string ip_address;
    bool config_loaded{true};
    bool pipeline_running{false};
};

class ApiServer {
public:
    ApiServer();
    int run();

    StatusResponse get_status() const;
    ConfigModel get_config() const;
    ConfigModel update_config(const ConfigModel& config);
    std::string camera_on();
    std::string camera_off();
    std::string camera_status() const;

private:
    void register_routes(httplib::Server& server);
    nlohmann::json status_json() const;
    nlohmann::json config_json() const;

    ConfigManager config_manager_;
    Database database_;
    LoggerUtils logger_;
    session_pipeline::SessionPipeline pipeline_;
};

}
