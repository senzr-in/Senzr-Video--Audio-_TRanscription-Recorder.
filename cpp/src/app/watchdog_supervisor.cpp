#include "app/watchdog_supervisor.hpp"
#include <iostream>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#endif

namespace app {

WatchdogSupervisor::WatchdogSupervisor() = default;

WatchdogSupervisor::~WatchdogSupervisor() {
    stop();
}

bool WatchdogSupervisor::start(const std::string& dev_path, int timeout_seconds, int pet_interval_seconds) {
    if (running_.load()) return true;

    dev_path_ = dev_path;
    timeout_seconds_ = timeout_seconds;
    pet_interval_seconds_ = pet_interval_seconds;

#ifndef _WIN32
    watchdog_fd_ = ::open(dev_path_.c_str(), O_WRONLY);
    if (watchdog_fd_ >= 0) {
        int t = timeout_seconds_;
        ioctl(watchdog_fd_, WDIOC_SETTIMEOUT, &t);
        hardware_enabled_.store(true);
        std::cout << "[WatchdogSupervisor] Hardware watchdog opened on " << dev_path_ 
                  << " with timeout=" << timeout_seconds_ << "s" << std::endl;
    } else {
        std::cerr << "[WatchdogSupervisor] Failed to open " << dev_path_ 
                  << " (running in software heartbeat mode)" << std::endl;
        hardware_enabled_.store(false);
    }
#else
    hardware_enabled_.store(false);
#endif

    running_.store(true);
    worker_thread_ = std::thread(&WatchdogSupervisor::supervisor_loop, this);
    return true;
}

void WatchdogSupervisor::stop() {
    if (running_.load()) {
        running_.store(false);
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

#ifndef _WIN32
    if (watchdog_fd_ >= 0) {
        // Safe watchdog disarm on controlled clean shutdown: write 'V' before close
        ::write(watchdog_fd_, "V", 1);
        ::close(watchdog_fd_);
        watchdog_fd_ = -1;
        hardware_enabled_.store(false);
    }
#endif
}

void WatchdogSupervisor::register_thread(const std::string& name, int max_silence_seconds, bool required) {
    std::lock_guard<std::mutex> lock(mutex_);
    heartbeats_[name] = ThreadHeartbeat{
        name,
        std::chrono::steady_clock::now(),
        max_silence_seconds,
        required
    };
}

void WatchdogSupervisor::heartbeat(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = heartbeats_.find(name);
    if (it != heartbeats_.end()) {
        it->second.last_beat = std::chrono::steady_clock::now();
    } else {
        heartbeats_[name] = ThreadHeartbeat{
            name,
            std::chrono::steady_clock::now(),
            15,
            true
        };
    }
}

bool WatchdogSupervisor::check_all_threads_healthy() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    for (const auto& [name, beat] : heartbeats_) {
        if (!beat.required) continue;
        auto silence = std::chrono::duration_cast<std::chrono::seconds>(now - beat.last_beat).count();
        if (silence > beat.max_silence_seconds) {
            last_stalled_thread_ = name;
            std::cerr << "[WatchdogSupervisor] Thread stalled: '" << name 
                      << "' silent for " << silence << "s (max " << beat.max_silence_seconds << "s)!" << std::endl;
            return false;
        }
    }
    last_stalled_thread_.clear();
    return true;
}

void WatchdogSupervisor::supervisor_loop() {
    while (running_.load()) {
        bool healthy = check_all_threads_healthy();

        if (healthy) {
#ifndef _WIN32
            if (watchdog_fd_ >= 0) {
                // Pet hardware watchdog
                int dummy = 0;
                ioctl(watchdog_fd_, WDIOC_KEEPALIVE, &dummy);
            }
#endif
        } else {
            std::cerr << "[WatchdogSupervisor] SUSPENDING WATCHDOG PETTING! System will reset if stall persists." << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(pet_interval_seconds_));
    }
}

nlohmann::json WatchdogSupervisor::status_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json root;
    root["hardware_watchdog_active"] = hardware_enabled_.load();
    root["timeout_seconds"] = timeout_seconds_;
    root["last_stalled_thread"] = last_stalled_thread_;

    nlohmann::json threads = nlohmann::json::object();
    auto now = std::chrono::steady_clock::now();
    for (const auto& [name, beat] : heartbeats_) {
        auto silence = std::chrono::duration_cast<std::chrono::seconds>(now - beat.last_beat).count();
        threads[name] = {
            {"silence_seconds", silence},
            {"max_silence_seconds", beat.max_silence_seconds},
            {"required", beat.required},
            {"healthy", silence <= beat.max_silence_seconds}
        };
    }
    root["threads"] = threads;
    return root;
}

} // namespace app
