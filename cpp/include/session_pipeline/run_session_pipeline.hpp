#pragma once
#include <atomic>

namespace session_pipeline {

class SessionPipeline {
public:
    SessionPipeline();
    void start();
    void stop();
    bool is_running() const;

private:
    std::atomic<bool> running_;
};

}
