#pragma once

#include "senzr/export.hpp"
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace senzr {
inline namespace v1 {

/**
 * @brief Standardized SDK error codes covering all subsystem failure modes.
 */
enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    InvalidState,
    NotInitialized,
    AlreadyInitialized,
    NotSupported,
    PermissionDenied,
    Timeout,
    NetworkError,
    StorageError,
    HardwareError,
    FirmwareError,
    UpdateFailed,
    ResourceUnavailable,
    InternalError
};

/**
 * @brief Returns a static human-readable description for an ErrorCode.
 */
[[nodiscard]] constexpr std::string_view error_code_to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok:                  return "Ok";
        case ErrorCode::InvalidArgument:     return "Invalid Argument";
        case ErrorCode::InvalidState:        return "Invalid State";
        case ErrorCode::NotInitialized:      return "Not Initialized";
        case ErrorCode::AlreadyInitialized:  return "Already Initialized";
        case ErrorCode::NotSupported:        return "Not Supported";
        case ErrorCode::PermissionDenied:    return "Permission Denied";
        case ErrorCode::Timeout:             return "Operation Timed Out";
        case ErrorCode::NetworkError:        return "Network Error";
        case ErrorCode::StorageError:        return "Storage Error";
        case ErrorCode::HardwareError:       return "Hardware Error";
        case ErrorCode::FirmwareError:       return "Firmware Error";
        case ErrorCode::UpdateFailed:        return "Update Failed";
        case ErrorCode::ResourceUnavailable: return "Resource Unavailable";
        case ErrorCode::InternalError:       return "Internal Error";
    }
    return "Unknown Error";
}

/**
 * @brief Represents an SDK error with an ErrorCode and detailed contextual message.
 */
class SENZR_API Error {
public:
    Error() noexcept : code_(ErrorCode::Ok), message_() {}
    
    Error(ErrorCode code, std::string message = "")
        : code_(code), message_(std::move(message)) {
        if (message_.empty() && code != ErrorCode::Ok) {
            message_ = std::string(error_code_to_string(code));
        }
    }

    [[nodiscard]] constexpr ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] constexpr bool is_ok() const noexcept { return code_ == ErrorCode::Ok; }
    [[nodiscard]] constexpr bool is_error() const noexcept { return code_ != ErrorCode::Ok; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_error(); }

    [[nodiscard]] static Error ok() noexcept { return Error(ErrorCode::Ok, ""); }

private:
    ErrorCode code_{ErrorCode::Ok};
    std::string message_;
};

// Forward declaration
template <typename T>
class [[nodiscard]] Result;

/**
 * @brief Type-safe result monad returning either T or senzr::Error.
 *
 * Provides a modern C++20 monadic interface compatible with C++23 std::expected.
 */
template <typename T>
class [[nodiscard]] Result {
public:
    using value_type = T;
    using error_type = Error;

    // Value constructors
    Result(T value) : data_(std::move(value)) {}

    // Static factories
    static Result<T> success(T value) { return Result<T>(std::move(value)); }
    static Result<T> failure(ErrorCode code, std::string message = "") { return Result<T>(code, std::move(message)); }
    static Result<T> failure(Error error) { return Result<T>(std::move(error)); }

    // Error constructors
    Result(Error error) : data_(std::move(error)) {}
    Result(ErrorCode code, std::string message = "") : data_(Error(code, std::move(message))) {}

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] const T& value() const & {
        if (!has_value()) {
            throw std::runtime_error("senzr::Result accessed without value: " + error().message());
        }
        return std::get<T>(data_);
    }

    [[nodiscard]] T& value() & {
        if (!has_value()) {
            throw std::runtime_error("senzr::Result accessed without value: " + error().message());
        }
        return std::get<T>(data_);
    }

    [[nodiscard]] T&& value() && {
        if (!has_value()) {
            throw std::runtime_error("senzr::Result accessed without value: " + error().message());
        }
        return std::get<T>(std::move(data_));
    }

    [[nodiscard]] const T& operator*() const & noexcept {
        return std::get<T>(data_);
    }

    [[nodiscard]] T& operator*() & noexcept {
        return std::get<T>(data_);
    }

    [[nodiscard]] T&& operator*() && noexcept {
        return std::get<T>(std::move(data_));
    }

    [[nodiscard]] const T* operator->() const noexcept {
        return std::addressof(std::get<T>(data_));
    }

    [[nodiscard]] T* operator->() noexcept {
        return std::addressof(std::get<T>(data_));
    }

    template <typename U>
    [[nodiscard]] T value_or(U&& default_value) const & {
        return has_value() ? std::get<T>(data_) : static_cast<T>(std::forward<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] T value_or(U&& default_value) && {
        return has_value() ? std::get<T>(std::move(data_)) : static_cast<T>(std::forward<U>(default_value));
    }

    [[nodiscard]] const Error& error() const & noexcept {
        static const Error ok_error = Error::ok();
        if (const auto* err = std::get_if<Error>(&data_)) {
            return *err;
        }
        return ok_error;
    }

    [[nodiscard]] Error& error() & noexcept {
        static Error ok_error = Error::ok();
        if (auto* err = std::get_if<Error>(&data_)) {
            return *err;
        }
        return ok_error;
    }

    // ── Monadic Operations ────────────────────────────────────────────────────
    
    template <typename F>
    auto and_then(F&& f) const & {
        using ReturnType = std::invoke_result_t<F, const T&>;
        if (has_value()) {
            return std::forward<F>(f)(value());
        }
        return ReturnType(error());
    }

    template <typename F>
    auto and_then(F&& f) && {
        using ReturnType = std::invoke_result_t<F, T&&>;
        if (has_value()) {
            return std::forward<F>(f)(std::move(*this).value());
        }
        return ReturnType(error());
    }

    template <typename F>
    auto transform(F&& f) const & {
        using NewValueType = std::invoke_result_t<F, const T&>;
        if (has_value()) {
            return Result<NewValueType>(std::forward<F>(f)(value()));
        }
        return Result<NewValueType>(error());
    }

    template <typename F>
    auto transform(F&& f) && {
        using NewValueType = std::invoke_result_t<F, T&&>;
        if (has_value()) {
            return Result<NewValueType>(std::forward<F>(f)(std::move(*this).value()));
        }
        return Result<NewValueType>(error());
    }

    template <typename F>
    Result<T> or_else(F&& f) const & {
        if (!has_value()) {
            return std::forward<F>(f)(error());
        }
        return *this;
    }

private:
    std::variant<T, Error> data_;
};

/**
 * @brief Specialization for void Result representing pure success or failure.
 */
template <>
class [[nodiscard]] Result<void> {
public:
    using value_type = void;
    using error_type = Error;

    Result() noexcept : error_(Error::ok()) {}
    Result(Error error) noexcept : error_(std::move(error)) {}
    Result(ErrorCode code, std::string message = "") : error_(Error(code, std::move(message))) {}

    [[nodiscard]] bool has_value() const noexcept { return error_.is_ok(); }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
    
    [[nodiscard]] const Error& error() const & noexcept { return error_; }
    [[nodiscard]] Error& error() & noexcept { return error_; }
    [[nodiscard]] Error error() && noexcept { return std::move(error_); }

    static Result<void> success() noexcept { return Result<void>(); }
    static Result<void> failure(ErrorCode code, std::string message = "") { return Result<void>(code, std::move(message)); }
    static Result<void> failure(Error error) noexcept { return Result<void>(std::move(error)); }

    template <typename F>
    auto and_then(F&& f) const {
        using ReturnType = std::invoke_result_t<F>;
        if (has_value()) {
            return std::forward<F>(f)();
        }
        return ReturnType(error_);
    }

    template <typename F>
    Result<void> or_else(F&& f) const {
        if (!has_value()) {
            return std::forward<F>(f)(error_);
        }
        return *this;
    }

private:
    Error error_;
};

} // namespace v1
} // namespace senzr
