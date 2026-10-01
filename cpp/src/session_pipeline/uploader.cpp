#include "session_pipeline/uploader.hpp"
#include "session_pipeline/config.hpp"
#include <curl/curl.h>
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cstdlib>   // getenv
#include <string>
#include <sstream>

#include "app/database.hpp"

namespace session_pipeline {

// ─────────────────────────────────────────────────────────────────────────────
// S3 upload via libcurl + AWS SigV4
//
// Requires environment variables (standard AWS SDK convention):
//   AWS_ACCESS_KEY_ID
//   AWS_SECRET_ACCESS_KEY
//   AWS_SESSION_TOKEN  (optional)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

static std::size_t curl_read_cb(void* ptr, std::size_t size,
                                 std::size_t nmemb, void* stream) {
    return std::fread(ptr, size, nmemb, static_cast<FILE*>(stream));
}

static std::size_t curl_write_cb(void* ptr, std::size_t size,
                                  std::size_t nmemb, void* /*stream*/) {
    // Discard response body
    return size * nmemb;
}

} // anonymous namespace

UploaderWorker::UploaderWorker(UploadQueue& upload_q, app::Database* database)
    : upload_q_(upload_q)
    , database_(database)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void UploaderWorker::run(std::stop_token stoken) {
    std::cout << "[uploader] started\n";

    while (!stoken.stop_requested()) {
        auto maybe = upload_q_.pop(std::chrono::milliseconds(500));
        if (!maybe) continue;

        const auto& job = *maybe;

        if (job.file_path.empty()) {
            std::cout << "[uploader] invalid job, skipping\n";
            continue;
        }

        // 3 retries with 2^attempt back-off — mirrors Python exactly
        bool upload_succeeded = false;
        for (int attempt = 1; attempt <= MAX_UPLOAD_RETRIES; ++attempt) {
            bool ok = upload_file(job.file_path, job.session_id);
            if (ok) {
                upload_succeeded = true;
                if (database_) {
                    database_->mark_upload_done(job.session_id, job.file_path);
                }
                break;
            }

            std::cerr << "[uploader] attempt " << attempt << '/'
                      << MAX_UPLOAD_RETRIES << " failed: "
                      << job.file_path << '\n';

            if (attempt < MAX_UPLOAD_RETRIES) {
                int sleep_sec = 1 << attempt;  // 2^attempt
                std::this_thread::sleep_for(std::chrono::seconds(sleep_sec));
            }
        }

        if (!upload_succeeded && database_) {
            database_->mark_upload_failed(job.session_id, job.file_path, "Exceeded max upload retries");
        }
    }

    curl_global_cleanup();
    std::cout << "[uploader] stopped\n";
}

bool UploaderWorker::upload_file(const std::string& file_path,
                                  const std::string& session_id) {
    std::filesystem::path p(file_path);
    if (!std::filesystem::exists(p)) {
        std::cerr << "[uploader] file not found: " << file_path << '\n';
        return false;
    }

    const char* env_bucket = std::getenv("S3_BUCKET");
    if (!env_bucket) env_bucket = std::getenv("AWS_BUCKET_NAME");
    std::string bucket = (env_bucket && *env_bucket) ? env_bucket : S3_BUCKET;

    const char* env_region = std::getenv("S3_REGION");
    if (!env_region) env_region = std::getenv("AWS_REGION");
    std::string region = (env_region && *env_region) ? env_region : S3_REGION;

    // S3 URL: https://{bucket}.s3.{region}.amazonaws.com/{prefix}/{session}/{file}
    std::string key = std::string(S3_PREFIX) + "/" + session_id + "/" +
                      p.filename().string();
    std::ostringstream url_oss;
    url_oss << "https://" << bucket
            << ".s3." << region << ".amazonaws.com/"
            << key;
    std::string url = url_oss.str();

    FILE* f = std::fopen(file_path.c_str(), "rb");
    if (!f) {
        std::cerr << "[uploader] cannot open: " << file_path << '\n';
        return false;
    }

    auto file_size = static_cast<curl_off_t>(std::filesystem::file_size(p));

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(f);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD,         1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION,   curl_read_cb);
    curl_easy_setopt(curl, CURLOPT_READDATA,       f);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, file_size);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      nullptr);

    // AWS SigV4 authentication (libcurl built-in, curl >= 7.75)
    curl_easy_setopt(curl, CURLOPT_AWS_SIGV4,
                     ("aws:amz:" + region + ":s3").c_str());

    const char* access_key = std::getenv("AWS_ACCESS_KEY_ID");
    const char* secret_key = std::getenv("AWS_SECRET_ACCESS_KEY");
    if (access_key && secret_key) {
        std::string userpwd = std::string(access_key) + ":" + secret_key;
        curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    std::fclose(f);

    bool ok = (res == CURLE_OK && (http_code == 200 || http_code == 204));
    if (ok) {
        std::cout << "[uploader] ✓ " << p.filename().string()
                  << " → s3://" << bucket << "/" << key << '\n';
    } else {
        std::cerr << "[uploader] HTTP " << http_code
                  << " curl=" << res << " for " << file_path << '\n';
    }
    return ok;
}

} // namespace session_pipeline
