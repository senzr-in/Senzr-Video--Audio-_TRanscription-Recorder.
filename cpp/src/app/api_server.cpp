#include "app/api_server.hpp"
#include <iostream>
#include <sstream>

namespace app {

using json = nlohmann::json;

ApiServer::ApiServer() = default;

json ApiServer::status_json() const {
    return {
        {"status", "online"},
        {"hostname", "edge-gateway"},
        {"ip_address", "0.0.0.0"},
        {"config_loaded", true},
        {"pipeline_running", pipeline_.is_running()}
    };
}

json ApiServer::config_json() const {
    auto cfg = config_manager_.read_config();
    return {
        {"devicename", cfg.devicename},
        {"mode", cfg.mode},
        {"wifissid", cfg.wifissid},
        {"wifipassword", cfg.wifipassword},
        {"provisioningenabled", cfg.provisioningenabled},
        {"cameraconnected", cfg.cameraconnected}
    };
}

void ApiServer::register_routes(httplib::Server& server) {
    server.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(status_json().dump(), "application/json");
    });

    server.Get("/api/config", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(config_json().dump(), "application/json");
    });

    server.Post("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            res.status = 400;
            res.set_content(R"({"error":"invalid json"})", "application/json");
            return;
        }

        ConfigModel cfg;
        cfg.devicename = body.value("devicename", "");
        cfg.mode = body.value("mode", "face");
        cfg.wifissid = body.value("wifissid", "");
        cfg.wifipassword = body.value("wifipassword", "");
        cfg.provisioningenabled = body.value("provisioningenabled", false);
        cfg.cameraconnected = body.value("cameraconnected", false);

        this->update_config(cfg);
        res.set_content(config_json().dump(), "application/json");
    });

    server.Post("/api/camera/on", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"ok", true}, {"message", this->camera_on()}}.dump(), "application/json");
    });

    server.Post("/api/camera/off", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"ok", true}, {"message", this->camera_off()}}.dump(), "application/json");
    });

    server.Get("/api/camera/status", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"pipeline_running", pipeline_.is_running()}, {"state", this->camera_status()}}.dump(), "application/json");
    });
}

int ApiServer::run() {
    database_.init_db();
    logger_.write_file_log(std::string("{\"event\":\"api_server_start\",\"timestamp\":\"") + LoggerUtils::get_timestamp() + "\"}");

    httplib::Server server;
    register_routes(server);

    std::cout << "edge-gateway-backend ready\n";
    std::cout << "listening on 0.0.0.0:8000\n";
    server.listen("0.0.0.0", 8000);
    return 0;
}

StatusResponse ApiServer::get_status() const {
    StatusResponse out;
    out.status = "online";
    out.hostname = "edge-gateway";
    out.ip_address = "0.0.0.0";
    out.config_loaded = true;
    out.pipeline_running = pipeline_.is_running();
    return out;
}

ConfigModel ApiServer::get_config() const {
    return config_manager_.read_config();
}

ConfigModel ApiServer::update_config(const ConfigModel& config) {
    config_manager_.write_config(config);
    return config;
}

std::string ApiServer::camera_on() {
    if (pipeline_.is_running()) return "Pipeline already running";
    pipeline_.start();
    return "Pipeline started";
}

std::string ApiServer::camera_off() {
    if (!pipeline_.is_running()) return "Pipeline already stopped";
    pipeline_.stop();
    return "Pipeline stopped";
}

std::string ApiServer::camera_status() const {
    return pipeline_.is_running() ? "running" : "stopped";
}

}
