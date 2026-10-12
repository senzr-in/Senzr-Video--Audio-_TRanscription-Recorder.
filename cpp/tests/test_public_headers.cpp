#include "senzr/types.hpp"
#include "senzr/platform.hpp"
#include "senzr/error.hpp"
#include "senzr/version.hpp"
#include "senzr/device.hpp"
#include "senzr/gpio.hpp"
#include "senzr/storage.hpp"
#include "senzr/network.hpp"
#include "senzr/watchdog.hpp"
#include "senzr/ble.hpp"
#include "senzr/fota.hpp"
#include "senzr/rauc.hpp"
#include "senzr/ai/inference.hpp"
#include "senzr/media/camera.hpp"
#include "senzr/senzr.hpp"

#include <iostream>
#include <cassert>

int main() {
    std::cout << "Testing Senzr SDK standalone public headers...\n";

    // 1. Version Check
    auto v = senzr::version();
    assert(v.major == 1);
    assert(v.minor == 1);
    assert(v.patch == 0);
    assert(std::string(senzr::version_string()) == "1.1.0");

    // 2. Error Model Check
    senzr::Error ok_err = senzr::Error::ok();
    assert(ok_err.is_ok());
    assert(!ok_err.is_error());

    senzr::Error fail_err(senzr::ErrorCode::InvalidArgument, "Custom failure");
    assert(fail_err.is_error());
    assert(fail_err.code() == senzr::ErrorCode::InvalidArgument);
    assert(fail_err.message() == "Custom failure");

    // Result<T> Success
    senzr::Result<int> res_int(42);
    assert(res_int.has_value());
    assert(res_int.value() == 42);

    // Result<T> Error
    senzr::Result<int> res_err(fail_err);
    assert(!res_err.has_value());
    assert(res_err.error().code() == senzr::ErrorCode::InvalidArgument);
    assert(res_err.value_or(100) == 100);

    // Result<void> Success & Error
    senzr::Result<void> res_void_ok = senzr::Result<void>::success();
    assert(res_void_ok.has_value());

    senzr::Result<void> res_void_err(senzr::ErrorCode::Timeout, "Timed out");
    assert(!res_void_err.has_value());
    assert(res_void_err.error().code() == senzr::ErrorCode::Timeout);

    std::cout << "All public SDK headers compiled and validated independently!\n";
    return 0;
}
