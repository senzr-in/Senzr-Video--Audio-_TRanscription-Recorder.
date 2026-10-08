# GPIO Physical Pin Validation & Electrical Mapping Specification

## 1. Pin Mapping & Validation State

| Logical Senzr Function | Header Pin # | RK3588 (Orange Pi 5 Max) | Jetson Nano (J41 Header) | Default Direction | Active Level | Debounce (ms) | Software Mapping | Physical Electrical Bench |
|---|---|---|---|---|---|---|---|---|
| `DOOR_RELAY` | Pin 7 | GPIO 139 (`GPIO4_B3`) | GPIO 216 (`AUD_MCLK`) | Output | Active HIGH | N/A | `HOST_VALIDATED` | `NOT_TESTED` |
| `BUZZER` | Pin 11 | GPIO 140 (`GPIO4_B4`) | GPIO 50 (`UART2_RTS`) | Output | Active HIGH | N/A | `HOST_VALIDATED` | `NOT_TESTED` |
| `STATUS_LED_GREEN` | Pin 12 | GPIO 61 (`GPIO1_D5`) | GPIO 79 (`I2S0_SCLK`) | Output | Active HIGH | N/A | `HOST_VALIDATED` | `NOT_TESTED` |
| `ERROR_LED_RED` | Pin 13 | GPIO 141 (`GPIO4_B5`) | GPIO 14 (`SPI1_SCK`) | Output | Active HIGH | N/A | `HOST_VALIDATED` | `NOT_TESTED` |
| `SAFETY_BEAM_SENSOR`| Pin 15 | GPIO 144 (`GPIO4_C0`) | GPIO 194 (`LCD_TE`) | Input | Active LOW | 50ms | `HOST_VALIDATED` | `NOT_TESTED` |
| `DOOR_CONTACT_SENSOR`| Pin 16 | GPIO 145 (`GPIO4_C1`) | GPIO 232 (`SPI1_CS1`) | Input | Active LOW | 50ms | `HOST_VALIDATED` | `NOT_TESTED` |
| `AUX_INPUT_1` | Pin 18 | GPIO 146 (`GPIO4_C2`) | GPIO 15 (`SPI1_CS0`) | Input | Active LOW | 20ms | `HOST_VALIDATED` | `NOT_TESTED` |
| `AUX_OUTPUT_1` | Pin 22 | GPIO 147 (`GPIO4_C3`) | GPIO 13 (`SPI1_MISO`) | Output | Active HIGH | N/A | `HOST_VALIDATED` | `NOT_TESTED` |
| `WIEGAND_D0` | Pin 29 | GPIO 148 (`GPIO4_C4`) | GPIO 149 (`CAM_AF_EN`)| Input | Interrupt Fall| N/A | `HOST_VALIDATED` | `NOT_TESTED` |
| `WIEGAND_D1` | Pin 31 | GPIO 149 (`GPIO4_C5`) | GPIO 200 (`GPIO_PZ0`) | Input | Interrupt Fall| N/A | `HOST_VALIDATED` | `NOT_TESTED` |
| `TAMPER_SWITCH` | Pin 32 | GPIO 150 (`GPIO4_C6`) | GPIO 168 (`LCD_BL_PWM`)| Input | Active HIGH | 100ms | `HOST_VALIDATED` | `NOT_TESTED` |
| `MANUAL_EXIT_BUTTON`| Pin 33 | GPIO 151 (`GPIO4_C7`) | GPIO 38 (`GPIO_PE6`) | Input | Active LOW | 50ms | `HOST_VALIDATED` | `NOT_TESTED` |

---

## 2. Software GPIO Validation (`HOST_VALIDATED`)

* **Logical-to-Sysfs Resolution**: Verified by `senzr::platform::map_logical_pin_to_sysfs()` for RK3588 and Jetson Nano platforms.
* **Safe Filesystem Access**: Verifies sysfs existence with `std::error_code` without throwing uncaught exceptions.
* **Non-blocking Pulse Threading**: Verified using `std::jthread` for asynchronous relay activation and timed auto-disarm.
* **Emergency Disarm**: Verified that `emergency_disarm()` sets pin low immediately.

---

## 3. Physical Hardware Bench Requirements (Required for `PHYSICAL_MEASURED`)

The following electrical measurements must be captured with calibrated laboratory instruments on live hardware:

1. **Relay Output Voltage & Timing (Pin 7)**:
   * Measure 3.3V logic high output level on trigger.
   * Measure pulse rise and fall times via oscilloscope (target: <50μs).
2. **Safety Beam Interlock Response Time**:
   * Channel 1: Safety Beam transition (Pin 15 HIGH -> LOW).
   * Channel 2: Relay output drop (Pin 7 HIGH -> LOW).
   * Measurement: Time delta between Ch1 edge and Ch2 edge (target: <2.0 ms).
3. **Contact Debounce & Noise Immunity**:
   * Inject 15ms simulated contact bounce on Pin 33; verify zero spurious multi-trigger events.
4. **Buzzer Frequency & Duty Cycle**:
   * Measure PWM frequency on Pin 11 across 100 consecutive activations.
