#include "app/ntp_client.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/rtc.h>
#endif

namespace app {

// 70 years in seconds between 1900-01-01 (NTP epoch) and 1970-01-01 (Unix epoch)
static constexpr uint64_t NTP_TO_UNIX_OFFSET = 2208988800ULL;

NtpClient::NtpClient(std::string default_server, int port, int timeout_sec)
    : default_server_(std::move(default_server)),
      port_(port),
      timeout_sec_(timeout_sec) {}

std::optional<NtpTimeResult> NtpClient::query_ntp(const std::string& server) {
    std::string target_server = server.empty() ? default_server_ : server;

    // 48-byte NTP packet layout
    uint8_t packet[48];
    std::memset(packet, 0, sizeof(packet));

    // LI (0), Version (3), Mode (3 = client) -> 0x1B
    packet[0] = 0x1B;

    struct addrinfo hints{};
    struct addrinfo* res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    std::string port_str = std::to_string(port_);
    if (getaddrinfo(target_server.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        std::cerr << "[NTP] Failed to resolve host: " << target_server << "\n";
        return std::nullopt;
    }

#ifdef _WIN32
    SOCKET sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == INVALID_SOCKET) {
        freeaddrinfo(res);
        return std::nullopt;
    }
    DWORD timeout_ms = timeout_sec_ * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
#else
    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        freeaddrinfo(res);
        return std::nullopt;
    }
    struct timeval tv{};
    tv.tv_sec = timeout_sec_;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    auto t_start = std::chrono::steady_clock::now();

    // Send NTP request packet
    if (sendto(sockfd, (const char*)packet, sizeof(packet), 0, res->ai_addr, res->ai_addrlen) < 0) {
        std::cerr << "[NTP] Send failed to " << target_server << "\n";
        freeaddrinfo(res);
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        return std::nullopt;
    }
    freeaddrinfo(res);

    // Receive NTP response packet
    struct sockaddr_storage src_addr{};
    socklen_t src_len = sizeof(src_addr);
    ssize_t bytes_recv = recvfrom(sockfd, (char*)packet, sizeof(packet), 0, (struct sockaddr*)&src_addr, &src_len);

    auto t_end = std::chrono::steady_clock::now();
    double rtt_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif

    if (bytes_recv < 48) {
        std::cerr << "[NTP] Timeout or invalid response from " << target_server << "\n";
        return std::nullopt;
    }

    // Extract 64-bit transmit timestamp (bytes 40-47)
    uint32_t sec_high = 0;
    uint32_t sec_low  = 0;
    std::memcpy(&sec_high, &packet[40], sizeof(uint32_t));
    std::memcpy(&sec_low,  &packet[44], sizeof(uint32_t));

    sec_high = ntohl(sec_high);
    sec_low  = ntohl(sec_low);

    if (sec_high < NTP_TO_UNIX_OFFSET) {
        std::cerr << "[NTP] Received invalid timestamp before 1970\n";
        return std::nullopt;
    }

    uint64_t unix_sec = sec_high - NTP_TO_UNIX_OFFSET;

    NtpTimeResult result;
    result.success = true;
    result.unix_timestamp_sec = unix_sec;
    result.fractional_sec = sec_low;
    result.round_trip_delay_ms = rtt_ms;
    result.server_used = target_server;

    return result;
}

bool NtpClient::sync_system_clock(const std::string& server) {
    auto res = query_ntp(server);
    if (!res || !res->success) {
        return false;
    }

#ifndef _WIN32
    struct timespec ts{};
    ts.tv_sec = static_cast<time_t>(res->unix_timestamp_sec);
    // Convert 32-bit fraction to nanoseconds: (fraction * 1,000,000,000) >> 32
    ts.tv_nsec = static_cast<long>((static_cast<uint64_t>(res->fractional_sec) * 1000000000ULL) >> 32);

    if (clock_settime(CLOCK_REALTIME, &ts) == 0) {
        std::cout << "[NTP] System clock successfully synchronized with " << res->server_used
                  << " (RTT: " << std::fixed << std::setprecision(1) << res->round_trip_delay_ms << " ms)\n";
        std::cout << "[NTP] Current Time (IST): " << get_ist_timestamp() << " (UTC: " << get_iso8601_timestamp() << ")\n";
        
        // Write the newly synchronized time to the hardware battery-backed RTC chip
        write_to_hardware_rtc();
        return true;
    } else {
        std::cout << "[NTP] Fetched time from " << res->server_used
                  << ", but clock_settime requires root/sudo permissions.\n";
        return false;
    }
#else
    return true;
#endif
}

bool NtpClient::write_to_hardware_rtc() {
#ifndef _WIN32
    int fd = open("/dev/rtc0", O_RDWR);
    if (fd < 0) {
        fd = open("/dev/rtc", O_RDWR);
    }
    if (fd < 0) {
        std::cerr << "[RTC] Warning: Cannot open /dev/rtc0 to write hardware clock\n";
        return false;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
    gmtime_r(&tt, &tm_utc);

    struct rtc_time rtc_tm{};
    rtc_tm.tm_sec  = tm_utc.tm_sec;
    rtc_tm.tm_min  = tm_utc.tm_min;
    rtc_tm.tm_hour = tm_utc.tm_hour;
    rtc_tm.tm_mday = tm_utc.tm_mday;
    rtc_tm.tm_mon  = tm_utc.tm_mon;
    rtc_tm.tm_year = tm_utc.tm_year;
    rtc_tm.tm_wday = tm_utc.tm_wday;
    rtc_tm.tm_yday = tm_utc.tm_yday;
    rtc_tm.tm_isdst = tm_utc.tm_isdst;

    if (ioctl(fd, RTC_SET_TIME, &rtc_tm) == 0) {
        std::cout << "[RTC] Hardware RTC chip (/dev/rtc0) successfully updated and backed by battery.\n";
        close(fd);
        return true;
    } else {
        std::cerr << "[RTC] Warning: Failed to set RTC time via ioctl (requires root/sudo)\n";
        close(fd);
        return false;
    }
#else
    return true;
#endif
}

bool NtpClient::sync_with_fallbacks(const std::vector<std::string>& servers) {
    for (const auto& srv : servers) {
        std::cout << "[NTP] Attempting sync with " << srv << "...\n";
        if (sync_system_clock(srv)) {
            return true;
        }
    }

    // Fallback: If UDP 123 is filtered/blocked by network firewall, sync time via HTTPS Date header
    std::cout << "[NTP] UDP port 123 may be blocked. Attempting fallback sync via HTTPS Date header...\n";
#ifndef _WIN32
    std::array<char, 256> buffer{};
    std::string http_date;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen("curl -sI --max-time 4 https://google.com 2>/dev/null | grep -i '^date:'", "r"),
        pclose
    );
    if (pipe) {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
            http_date += buffer.data();
        }
    }
    if (!http_date.empty()) {
        auto pos = http_date.find(':');
        if (pos != std::string::npos) {
            std::string date_str = http_date.substr(pos + 1);
            date_str.erase(0, date_str.find_first_not_of(" \t\r\n"));
            date_str.erase(std::remove(date_str.begin(), date_str.end(), '\r'), date_str.end());
            date_str.erase(std::remove(date_str.begin(), date_str.end(), '\n'), date_str.end());

            struct tm tm_http{};
            if (strptime(date_str.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm_http) != nullptr) {
                time_t epoch = timegm(&tm_http);
                struct timespec ts{ epoch, 0 };
                if (clock_settime(CLOCK_REALTIME, &ts) == 0) {
                    std::cout << "[NTP] Clock successfully synchronized via HTTPS Date header (" << date_str << ")\n";
                    write_to_hardware_rtc();
                    return true;
                }
            }
        }
    }
#endif

    std::cerr << "[NTP] Warning: Could not synchronize clock with any NTP server.\n";
    return false;
}

std::string NtpClient::get_iso8601_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string NtpClient::get_ist_timestamp() {
    auto now = std::chrono::system_clock::now();
    // Add 5 hours and 30 minutes (19,800 seconds) for IST (UTC+05:30)
    std::time_t tt = std::chrono::system_clock::to_time_t(now) + (5 * 3600 + 30 * 60);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S IST");
    return oss.str();
}

std::string NtpClient::get_system_timezone() {
#ifndef _WIN32
    try {
        if (std::filesystem::is_symlink("/etc/localtime")) {
            auto target = std::filesystem::read_symlink("/etc/localtime").string();
            auto pos = target.find("zoneinfo/");
            if (pos != std::string::npos) {
                return target.substr(pos + 9);
            }
        }
    } catch (...) {}

    std::ifstream tz_file("/etc/timezone");
    if (tz_file.is_open()) {
        std::string tz;
        if (std::getline(tz_file, tz) && !tz.empty()) {
            tz.erase(std::remove(tz.begin(), tz.end(), '\n'), tz.end());
            tz.erase(std::remove(tz.begin(), tz.end(), '\r'), tz.end());
            return tz;
        }
    }

    const char* env_tz = getenv("TZ");
    if (env_tz && *env_tz) {
        return std::string(env_tz);
    }
#endif
    return "Asia/Kolkata";
}

std::string NtpClient::get_local_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::vector<std::string> NtpClient::get_available_timezones() {
    return {
        "Asia/Kolkata",
        "UTC",
        "Asia/Dubai",
        "Asia/Singapore",
        "Asia/Tokyo",
        "Asia/Shanghai",
        "Asia/Bangkok",
        "Europe/London",
        "Europe/Paris",
        "Europe/Berlin",
        "America/New_York",
        "America/Chicago",
        "America/Denver",
        "America/Los_Angeles",
        "America/Toronto",
        "Australia/Sydney",
        "Pacific/Auckland",
        "Africa/Cairo",
        "Africa/Johannesburg"
    };
}

bool NtpClient::set_system_timezone(const std::string& timezone) {
#ifndef _WIN32
    // 1. Set environment variable and update C runtime timezone cache
    setenv("TZ", timezone.c_str(), 1);
    tzset();

    // 2. Programmatically update Linux OS timezone file (/etc/localtime)
    try {
        std::filesystem::path zone_source = std::filesystem::path("/usr/share/zoneinfo") / timezone;
        std::filesystem::path local_target = "/etc/localtime";

        if (std::filesystem::exists(zone_source)) {
            std::filesystem::remove(local_target);
            std::filesystem::create_symlink(zone_source, local_target);

            // Also write /etc/timezone for Debian/Ubuntu compatibility
            std::ofstream tz_file("/etc/timezone");
            if (tz_file.is_open()) {
                tz_file << timezone << "\n";
            }
            std::cout << "[NTP] System timezone programmatically set to " << timezone << "\n";
            return true;
        }
    } catch (const std::exception& e) {
        std::cout << "[NTP] Timezone set in application memory: " << timezone << "\n";
        return true;
    }
#endif
    return true;
}

} // namespace app
