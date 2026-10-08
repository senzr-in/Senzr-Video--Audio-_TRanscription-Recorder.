#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

namespace app {

struct ThreadHeartbeat {
    std::string name;
    std::chrono::steady_clock::time_point last_beat;
    int max_silence_seconds{15};
    bool required{true};
};

class WatchdogSupervisor {
public:
    static WatchdogSupervisor& instance() {
        static WatchdogSupervisor inst;
        return inst;
    }

    WatchdogSupervisor();
    ~WatchdogSupervisor();

    // Start hardware watchdog petting thread with specified timeout
    bool start(const std::string& dev_path = "/dev/watchdog", int timeout_seconds = 10, int pet_interval_seconds = 3);
    void stop();

    // Register a thread for heartbeat tracking
    void register_thread(const std::string& name, int max_silence_seconds = 15, bool required = true);
    
    // Beat heart for a thread
    void heartbeat(const std::string& name);

    // Check if all registered threads are healthy
    bool check_all_threads_healthy();

    // JSON inspection of health and watchdog status
    nlohmann::json status_json() const;

private:
    void supervisor_loop();

    std::string dev_path_{"/dev/watchdog"};
    int timeout_seconds_{10};
    int pet_interval_seconds_{3};
    int watchdog_fd_{-1};

    std::atomic<bool> running_{false};
    std::atomic<bool> hardware_enabled_{false};
    std::thread worker_thread_;

    mutable std::mutex mutex_;
    std::map<std::string, ThreadHeartbeat> heartbeats_;
    std::string last_stalled_thread_;
};

} // namespace app
