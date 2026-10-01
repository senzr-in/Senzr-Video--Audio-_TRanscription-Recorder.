#pragma once
#include "session_pipeline/queues.hpp"
#include "session_pipeline/models.hpp"

namespace app {
class Database;
}

namespace session_pipeline {

// ── UploaderWorker ────────────────────────────────────────────────────────────
// Mirrors Python uploader.py: UploaderWorker
//
// Dequeues UploadJob items and uploads each file to S3 using libcurl.
// S3 key: sessions/{session_id}/{filename}
// Bucket: demoapp-static-files, region: eu-north-1
// Retries: 3, back-off: 2^attempt seconds
class UploaderWorker {
public:
    explicit UploaderWorker(UploadQueue& upload_q, app::Database* database = nullptr);
    void run(std::stop_token stoken);

private:
    bool upload_file(const std::string& file_path,
                     const std::string& session_id);

    UploadQueue&   upload_q_;
    app::Database* database_{nullptr};
};

} // namespace session_pipeline
