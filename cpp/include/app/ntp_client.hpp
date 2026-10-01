#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace app {

struct NtpTimeResult {
    bool success{false};
    uint64_t unix_timestamp_sec{0};
    uint32_t fractional_sec{0};
    double round_trip_delay_ms{0.0};
    std::string server_used;
};

class NtpClient {
public:
    explicit NtpClient(std::string default_server = "time.google.com",
                       int port = 123,
                       int timeout_sec = 3);

    // Queries an NTP server using RFC 5905 48-byte UDP packet
    std::optional<NtpTimeResult> query_ntp(const std::string& server = "");

    // Queries NTP and synchronizes the Linux system clock via clock_settime
    bool sync_system_clock(const std::string& server = "");

    // Tries multiple fallback NTP servers (Google, Cloudflare, AWS, NTP Pool) until one succeeds
    bool sync_with_fallbacks(const std::vector<std::string>& servers = {
        "time.google.com",
        "time.cloudflare.com",
        "time.aws.com",
        "pool.ntp.org"
    });

    // Formatted ISO 8601 UTC timestamp helper
    static std::string get_iso8601_timestamp();

    // Formatted IST (Indian Standard Time, UTC+5:30) helper
    static std::string get_ist_timestamp();

    // Formatted local timestamp based on active configured timezone
    static std::string get_local_timestamp();

    // Writes current synchronized system time to the hardware RTC chip (/dev/rtc0)
    static bool write_to_hardware_rtc();

    // Programmatically configures the Linux system timezone in code (e.g. "Asia/Kolkata")
    static bool set_system_timezone(const std::string& timezone = "Asia/Kolkata");

    // Queries current active timezone (from /etc/timezone or /etc/localtime or env)
    static std::string get_system_timezone();

    // Returns a list of supported worldwide timezones
    static std::vector<std::string> get_available_timezones();

private:
    std::string default_server_;
    int port_;
    int timeout_sec_;
};

} // namespace app
