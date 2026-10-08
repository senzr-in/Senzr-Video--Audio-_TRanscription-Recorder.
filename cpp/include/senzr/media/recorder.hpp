#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include "senzr/ai/types.hpp"
#include <string>
#include <memory>

namespace senzr {
inline namespace v1 {
namespace media {

/**
 * @brief Video recorder encoding configuration.
 */
struct SENZR_API RecorderConfig {
    std::string output_dir{"/opt/edge-gateway/recordings"};
    int width{640};
    int height{480};
    double fps{30.0};
    std::string container_format{"mp4"};
};

/**
 * @brief Thread-safe event-driven media recorder.
 */
class SENZR_API Recorder {
public:
    explicit Recorder(RecorderConfig config = {});
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) noexcept;
    Recorder& operator=(Recorder&&) noexcept;

    // ── Recording Lifecycle ───────────────────────────────────────────────────
    [[nodiscard]] Result<void> start(const std::string& session_id);
    [[nodiscard]] Result<void> write_frame(const ai::FrameView& frame);
    [[nodiscard]] Result<std::string> stop(); // Returns path to recorded MP4 file
    [[nodiscard]] bool is_recording() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace media
} // namespace v1
} // namespace senzr
