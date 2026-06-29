#include "session_pipeline/run_session_pipeline.hpp"

namespace session_pipeline {

SessionPipeline::SessionPipeline() : running_(false) {}
void SessionPipeline::start() { running_ = true; }
void SessionPipeline::stop() { running_ = false; }
bool SessionPipeline::is_running() const { return running_.load(); }

}
