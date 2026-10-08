#include "senzr/error.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <stdexcept>

struct UserPayload {
    int id;
    std::string role;
    double score;
};

int main() {
    std::cout << "=== Unit Test: senzr::Result<T> std::expected Compatibility ===\n";

    // 1. Basic Value and bool conversions
    senzr::Result<int> r_val(100);
    assert(r_val.has_value());
    assert(static_cast<bool>(r_val));
    assert(r_val.value() == 100);
    assert(*r_val == 100);

    // Mutable dereference
    *r_val = 200;
    assert(*r_val == 200);

    // 2. Struct member access via operator->
    senzr::Result<UserPayload> r_user(UserPayload{42, "admin", 99.5});
    assert(r_user.has_value());
    assert(r_user->id == 42);
    assert(r_user->role == "admin");
    assert(r_user->score > 99.0);

    // 3. Error construction and access
    senzr::Result<int> r_err(senzr::ErrorCode::Timeout, "Connection timed out");
    assert(!r_err.has_value());
    assert(!static_cast<bool>(r_err));
    assert(r_err.error().code() == senzr::ErrorCode::Timeout);
    assert(r_err.error().message() == "Connection timed out");

    // value() throws std::runtime_error on failure
    bool caught = false;
    try {
        (void)r_err.value();
    } catch (const std::runtime_error& ex) {
        caught = true;
        assert(std::string(ex.what()).find("Connection timed out") != std::string::npos);
    }
    assert(caught);

    // 4. value_or test
    assert(r_val.value_or(999) == 200);
    assert(r_err.value_or(999) == 999);

    // 5. Monadic transform (Success path)
    auto r_transformed = r_val.transform([](int v) {
        return "Score: " + std::to_string(v);
    });
    assert(r_transformed.has_value());
    assert(*r_transformed == "Score: 200");

    // Monadic transform (Error path - should NOT invoke callback)
    bool transform_invoked = false;
    auto r_err_transformed = r_err.transform([&](int v) {
        transform_invoked = true;
        return v * 10;
    });
    assert(!transform_invoked);
    assert(!r_err_transformed.has_value());
    assert(r_err_transformed.error().code() == senzr::ErrorCode::Timeout);

    // 6. Monadic and_then (Success -> Success)
    auto r_chained = r_val.and_then([](int v) -> senzr::Result<double> {
        return senzr::Result<double>::success(static_cast<double>(v) / 4.0);
    });
    assert(r_chained.has_value());
    assert(*r_chained == 50.0);

    // Monadic and_then (Success -> Error)
    auto r_chained_fail = r_val.and_then([](int /*v*/) -> senzr::Result<std::string> {
        return senzr::Result<std::string>::failure(senzr::ErrorCode::PermissionDenied, "Access denied");
    });
    assert(!r_chained_fail.has_value());
    assert(r_chained_fail.error().code() == senzr::ErrorCode::PermissionDenied);

    // Monadic and_then (Error -> skipped)
    bool and_then_invoked = false;
    auto r_err_chained = r_err.and_then([&](int v) -> senzr::Result<int> {
        and_then_invoked = true;
        return senzr::Result<int>::success(v + 1);
    });
    assert(!and_then_invoked);
    assert(!r_err_chained.has_value());
    assert(r_err_chained.error().code() == senzr::ErrorCode::Timeout);

    // 7. Result<void> std::expected behavior
    senzr::Result<void> r_void_ok = senzr::Result<void>::success();
    assert(r_void_ok.has_value());
    assert(static_cast<bool>(r_void_ok));

    senzr::Result<void> r_void_err = senzr::Result<void>::failure(senzr::ErrorCode::HardwareError, "Bus error");
    assert(!r_void_err.has_value());
    assert(r_void_err.error().code() == senzr::ErrorCode::HardwareError);

    auto r_void_chained = r_void_ok.and_then([]() -> senzr::Result<void> {
        return senzr::Result<void>::success();
    });
    assert(r_void_chained.has_value());

    std::cout << "=== All senzr::Result std::expected Tests PASSED ===\n";
    return 0;
}
