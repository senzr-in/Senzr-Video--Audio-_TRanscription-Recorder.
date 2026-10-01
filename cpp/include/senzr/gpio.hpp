#pragma once

#include "app/gpio_manager.hpp"
#include <string>

namespace senzr::gpio {

using PinMode = app::PinMode;
using PinState = app::PinState;
using GpioPin = app::PinInfo;
using Manager = app::GpioManager;

inline Manager& get_manager() {
    return Manager::instance();
}

} // namespace senzr::gpio
