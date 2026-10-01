#pragma once
#include <string>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "app/config_manager.hpp"
#include "app/database.hpp"
#include "app/logger_utils.hpp"
#include "app/connectivity_manager.hpp"
#include "app/gpio_manager.hpp"
#include "app/mqtt_access_client.hpp"
#include "app/updater.hpp"
#include "session_pipeline/run_session_pipeline.hpp"
#include <memory>

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

    // Exposed for testing
    StatusResponse get_status() const;
    ConfigModel    get_config() const;
    ConfigModel    update_config(const ConfigModel& config);
    std::string    camera_on();
    std::string    camera_off();

private:
    void register_routes(httplib::Server& srv);
    nlohmann::json status_json() const;
    nlohmann::json config_json() const;
    nlohmann::json camera_status_json() const;

    ConfigManager                   config_manager_;
    Database                        database_;
    LoggerUtils                     logger_;
    ConnectivityManager             connectivity_manager_;
    GpioManager                     gpio_manager_;
    MqttAccessClient                mqtt_client_;
    std::shared_ptr<app::OtaUpdater> ota_updater_;
    // Pipeline is initialised with the database so it can persist sessions.
    session_pipeline::SessionPipeline pipeline_{database_};
};

} // namespace app
