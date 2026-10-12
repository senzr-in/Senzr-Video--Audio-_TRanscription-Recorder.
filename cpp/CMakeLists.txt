cmake_minimum_required(VERSION 3.20)
project(edge_gateway_cpp LANGUAGES CXX)

# ── C++ Standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O2)
    endif()
endif()

# ── Required dependencies ─────────────────────────────────────────────────────

# nlohmann/json (header-only, find installed or use third_party/)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found via CMake — using third_party/")
endif()

# SQLite3
find_package(SQLite3 REQUIRED)
if(TARGET SQLite::SQLite3 AND NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

# OpenCV
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio imgcodecs)
message(STATUS "OpenCV version: ${OpenCV_VERSION}")

# libcurl (for S3 upload via SigV4)
find_package(CURL REQUIRED)

# pthreads
find_package(Threads REQUIRED)

# ── Optional dependencies ─────────────────────────────────────────────────────

# OpenSSL (for httplib TLS)
find_package(OpenSSL QUIET)

# RKNN runtime (Rockchip NPU — ARM64, RK3588)
find_library(RKNN_LIB rknnrt
    HINTS /usr/lib /usr/local/lib /usr/lib/aarch64-linux-gnu)
find_path(RKNN_INCLUDE_DIR rknn_api.h
    HINTS /usr/include /usr/local/include
          /usr/include/rknn /usr/local/include/rknn)

if(RKNN_LIB AND RKNN_INCLUDE_DIR)
    message(STATUS "RKNN runtime: ${RKNN_LIB}")
    set(RKNN_FOUND TRUE)
else()
    message(WARNING "RKNN runtime not found — PersonDetectionWorker will not build")
    set(RKNN_FOUND FALSE)
endif()

# whisper.cpp (replaces Python faster-whisper)
find_library(WHISPER_LIB whisper
    HINTS /usr/local/lib /usr/lib)
find_path(WHISPER_INCLUDE_DIR whisper.h
    HINTS /usr/local/include /usr/include)

if(WHISPER_LIB AND WHISPER_INCLUDE_DIR)
    message(STATUS "whisper.cpp: ${WHISPER_LIB}")
    set(WHISPER_FOUND TRUE)
else()
    message(WARNING "whisper.cpp not found — StreamingTranscriber will not build")
    set(WHISPER_FOUND FALSE)
endif()

# ALSA (optional — audio capture uses arecord subprocess, but ALSA may be needed)
find_library(ALSA_LIB asound)

# Mosquitto MQTT (for edge-gateway-updater)
find_library(MOSQUITTO_LIB mosquitto)
find_path(MOSQUITTO_INCLUDE_DIR mosquitto.h)
if(MOSQUITTO_LIB)
    message(STATUS "libmosquitto : ${MOSQUITTO_LIB}")
    set(MOSQUITTO_FOUND TRUE)
else()
    message(STATUS "libmosquitto not found — updater will run in S3-polling mode")
    set(MOSQUITTO_FOUND FALSE)
endif()

# ── Source files ──────────────────────────────────────────────────────────────

set(APP_SOURCES
    src/main.cpp
    src/platform/common/platform_detect.cpp
    src/platform/rockchip/rk3588_backend.cpp
    src/platform/jetson/jetson_backend.cpp
    src/app/api_server.cpp
    src/app/config_manager.cpp
    src/app/database.cpp
    src/app/logger_utils.cpp
    src/app/models.cpp
    src/app/ntp_client.cpp
    src/app/connectivity_manager.cpp
    src/app/gpio_manager.cpp
    src/app/mqtt_access_client.cpp
    src/app/updater.cpp
    src/app/watchdog_supervisor.cpp
    src/app/ble_access_manager.cpp
    src/app/rauc_controller.cpp
    src/sdk/device.cpp
    src/sdk/fota.cpp
)

# GCC 11 workaround for large httplib lambda templates
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set_source_files_properties(src/app/api_server.cpp PROPERTIES COMPILE_OPTIONS "-O1")
endif()


set(PIPELINE_SOURCES
    src/session_pipeline/session.cpp
    src/session_pipeline/video_capture.cpp
    src/session_pipeline/audio_capture.cpp
    src/session_pipeline/recorder_manager.cpp
    src/session_pipeline/transcriber.cpp
    src/session_pipeline/uploader.cpp
    src/session_pipeline/run_session_pipeline.cpp
    src/session_pipeline/person_detection.cpp
    src/session_pipeline/plate_utils.cpp
)

# ── Main executable ───────────────────────────────────────────────────────────

add_executable(edge-gateway-backend
    ${APP_SOURCES}
    ${PIPELINE_SOURCES}
)

target_include_directories(edge-gateway-backend PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${OpenCV_INCLUDE_DIRS}
)

if(RKNN_FOUND)
    target_include_directories(edge-gateway-backend PRIVATE ${RKNN_INCLUDE_DIR})
endif()

if(WHISPER_FOUND)
    target_include_directories(edge-gateway-backend PRIVATE ${WHISPER_INCLUDE_DIR})
endif()

target_link_libraries(edge-gateway-backend PRIVATE
    SQLite3::SQLite3
    ${OpenCV_LIBS}
    CURL::libcurl
    Threads::Threads
)

if(nlohmann_json_FOUND)
    target_link_libraries(edge-gateway-backend PRIVATE nlohmann_json::nlohmann_json)
endif()

if(RKNN_FOUND)
    target_link_libraries(edge-gateway-backend PRIVATE ${RKNN_LIB})
    target_compile_definitions(edge-gateway-backend PRIVATE HAVE_RKNN)
endif()

if(WHISPER_FOUND)
    target_link_libraries(edge-gateway-backend PRIVATE ${WHISPER_LIB})
    target_compile_definitions(edge-gateway-backend PRIVATE HAVE_WHISPER)
endif()

if(OPENSSL_FOUND)
    target_compile_definitions(edge-gateway-backend PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(edge-gateway-backend PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(ALSA_LIB)
    target_link_libraries(edge-gateway-backend PRIVATE ${ALSA_LIB})
endif()

if(MOSQUITTO_LIB)
    target_link_libraries(edge-gateway-backend PRIVATE ${MOSQUITTO_LIB})
    target_compile_definitions(edge-gateway-backend PRIVATE HAVE_MOSQUITTO)
endif()

# Copy frontend assets to binary build directory
add_custom_target(copy_frontend ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/frontend
        ${CMAKE_CURRENT_BINARY_DIR}/frontend
    COMMENT "Syncing frontend files to build directory"
)
add_dependencies(edge-gateway-backend copy_frontend)

# ── Benchmark executable ──────────────────────────────────────────────────────

add_executable(edge-gateway-benchmark
    src/benchmark.cpp
)

target_include_directories(edge-gateway-benchmark PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(edge-gateway-benchmark PRIVATE
    SQLite3::SQLite3
    Threads::Threads
)

if(nlohmann_json_FOUND)
    target_link_libraries(edge-gateway-benchmark PRIVATE nlohmann_json::nlohmann_json)
endif()

if(OPENSSL_FOUND)
    target_compile_definitions(edge-gateway-benchmark PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(edge-gateway-benchmark PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# ── Multi-Platform Platform Selection ─────────────────────────────────────────

set(SENZR_PLATFORM "auto" CACHE STRING "Target edge computing platform: auto, rockchip, jetson, generic")
set_property(CACHE SENZR_PLATFORM PROPERTY STRINGS auto rockchip jetson generic)

if(SENZR_PLATFORM STREQUAL "rockchip")
    add_compile_definitions(SENZR_PLATFORM_ROCKCHIP)
    message(STATUS "Senzr Target Platform : Rockchip RK3588 (Explicit)")
elseif(SENZR_PLATFORM STREQUAL "jetson")
    add_compile_definitions(SENZR_PLATFORM_JETSON)
    message(STATUS "Senzr Target Platform : NVIDIA Jetson Nano (Explicit)")
elseif(SENZR_PLATFORM STREQUAL "generic")
    add_compile_definitions(SENZR_PLATFORM_GENERIC)
    message(STATUS "Senzr Target Platform : Generic Linux (Explicit)")
else()
    message(STATUS "Senzr Target Platform : Auto-Detect (Dynamic Runtime HAL)")
endif()

# ── Senzr OS SDK Library (libsenzr) ───────────────────────────────────────────

set(SENZR_SDK_SOURCES
    src/platform/common/platform_detect.cpp
    src/platform/rockchip/rk3588_backend.cpp
    src/platform/jetson/jetson_backend.cpp
    src/sdk/device.cpp
    src/sdk/version.cpp
    src/sdk/logging.cpp
    src/sdk/gpio.cpp
    src/sdk/storage.cpp
    src/sdk/fota.cpp
    src/sdk/network.cpp
    src/sdk/watchdog.cpp
    src/sdk/ble.cpp
    src/sdk/mqtt.cpp
    src/sdk/ai.cpp
    src/sdk/ai_inference.cpp
    src/sdk/detector.cpp
    src/sdk/media.cpp
    src/sdk/camera.cpp
    src/app/updater.cpp
    src/app/gpio_manager.cpp
    src/app/connectivity_manager.cpp
    src/app/ntp_client.cpp
    src/app/database.cpp
    src/app/watchdog_supervisor.cpp
    src/app/ble_access_manager.cpp
    src/app/rauc_controller.cpp
    src/app/mqtt_access_client.cpp
    src/session_pipeline/plate_utils.cpp
)

add_library(senzr STATIC
    ${SENZR_SDK_SOURCES}
)
add_library(Senzr::SDK ALIAS senzr)

target_include_directories(senzr PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(senzr PUBLIC
    SQLite3::SQLite3
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    ${OpenCV_LIBS}
)

if(nlohmann_json_FOUND)
    target_link_libraries(senzr PUBLIC nlohmann_json::nlohmann_json)
endif()

if(MOSQUITTO_LIB)
    target_link_libraries(senzr PUBLIC ${MOSQUITTO_LIB})
    target_compile_definitions(senzr PUBLIC HAVE_MOSQUITTO)
endif()

# ── Senzr OS Control CLI (senzr-ctl) ──────────────────────────────────────────

add_executable(senzr-ctl
    src/tools/senzr_ctl.cpp
)

target_include_directories(senzr-ctl PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(senzr-ctl PRIVATE
    Senzr::SDK
)

# ── OTA Updater Executable (edge-gateway-updater / senzr-fota-daemon) ──────────

add_executable(edge-gateway-updater
    src/updater_main.cpp
)

target_include_directories(edge-gateway-updater PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(edge-gateway-updater PRIVATE
    Senzr::SDK
)

# Alias for Senzr OS naming convention
add_custom_target(senzr-fota-daemon ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink edge-gateway-updater senzr-fota-daemon
    DEPENDS edge-gateway-updater
)

# ── SDK Examples ──────────────────────────────────────────────────────────────

add_executable(senzr_app_example
    examples/senzr_app_example.cpp
)
target_link_libraries(senzr_app_example PRIVATE Senzr::SDK)

add_executable(example_01_device_info examples/01_device_info/main.cpp)
target_link_libraries(example_01_device_info PRIVATE Senzr::SDK)

add_executable(example_02_gpio_relay examples/02_gpio_relay/main.cpp)
target_link_libraries(example_02_gpio_relay PRIVATE Senzr::SDK)

add_executable(example_03_storage examples/03_storage/main.cpp)
target_link_libraries(example_03_storage PRIVATE Senzr::SDK)

add_executable(example_04_anpr examples/04_anpr/main.cpp)
target_link_libraries(example_04_anpr PRIVATE Senzr::SDK)

add_executable(example_05_portable_app examples/05_portable_app/main.cpp)
target_link_libraries(example_05_portable_app PRIVATE Senzr::SDK)

add_executable(example_06_traffic_detector examples/06_traffic_detector/main.cpp)
target_link_libraries(example_06_traffic_detector PRIVATE Senzr::SDK)

# ── Tests ─────────────────────────────────────────────────────────────────────

enable_testing()

# Public SDK Standalone Headers test
add_executable(test_public_headers tests/test_public_headers.cpp src/sdk/version.cpp)
target_include_directories(test_public_headers PRIVATE include)
add_test(NAME public_headers COMMAND test_public_headers)

# ThreadSafeQueue unit test
add_executable(test_queues tests/test_queues.cpp)
target_include_directories(test_queues PRIVATE include)
target_link_libraries(test_queues PRIVATE Threads::Threads)
add_test(NAME queues COMMAND test_queues)

# Config manager unit test
add_executable(test_config tests/test_config.cpp src/app/config_manager.cpp)
target_include_directories(test_config PRIVATE include ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(nlohmann_json_FOUND)
    target_link_libraries(test_config PRIVATE nlohmann_json::nlohmann_json)
endif()
add_test(NAME config COMMAND test_config)

# Session unit test
add_executable(test_session tests/test_session.cpp src/session_pipeline/session.cpp)
target_include_directories(test_session PRIVATE include ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(nlohmann_json_FOUND)
    target_link_libraries(test_session PRIVATE nlohmann_json::nlohmann_json)
endif()
add_test(NAME session COMMAND test_session)

# OTA Updater unit test
add_executable(test_updater tests/test_updater.cpp src/app/updater.cpp)
target_include_directories(test_updater PRIVATE include ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(test_updater PRIVATE
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
if(nlohmann_json_FOUND)
    target_link_libraries(test_updater PRIVATE nlohmann_json::nlohmann_json)
endif()
if(MOSQUITTO_LIB)
    target_link_libraries(test_updater PRIVATE ${MOSQUITTO_LIB})
    target_compile_definitions(test_updater PRIVATE HAVE_MOSQUITTO)
endif()
add_test(NAME updater COMMAND test_updater)

# Plate Utils unit test
add_executable(test_plate_utils tests/test_plate_utils.cpp src/session_pipeline/plate_utils.cpp)
target_include_directories(test_plate_utils PRIVATE include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(test_plate_utils PRIVATE ${OpenCV_LIBS})
add_test(NAME plate_utils COMMAND test_plate_utils)

# Platform & Multi-Platform Capabilities unit test
add_executable(test_platform tests/test_platform.cpp)
target_link_libraries(test_platform PRIVATE Senzr::SDK)
add_test(NAME platform COMMAND test_platform)

# Unsupported capability & error model test
add_executable(test_unsupported tests/test_unsupported.cpp)
target_link_libraries(test_unsupported PRIVATE Senzr::SDK)
add_test(NAME unsupported COMMAND test_unsupported)

# YOLO11s / YOLOv8 Detector unit test
add_executable(test_detector tests/test_detector.cpp)
target_link_libraries(test_detector PRIVATE Senzr::SDK)
add_test(NAME detector COMMAND test_detector)

# Multi-Subsystem Soak & Memory Growth Test
add_executable(test_soak tests/test_soak.cpp)
target_link_libraries(test_soak PRIVATE Senzr::SDK)
add_test(NAME soak COMMAND test_soak 200)

# BLE Mobile Access Challenge-Response unit test
add_executable(test_ble tests/test_ble.cpp src/app/ble_access_manager.cpp src/app/database.cpp src/app/gpio_manager.cpp)
target_include_directories(test_ble PRIVATE include src ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(test_ble PRIVATE
    Senzr::SDK
    SQLite3::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
if(nlohmann_json_FOUND)
    target_link_libraries(test_ble PRIVATE nlohmann_json::nlohmann_json)
endif()
add_test(NAME ble COMMAND test_ble)

# ── Install & Export Targets ──────────────────────────────────────────────────

include(CMakePackageConfigHelpers)

install(TARGETS senzr EXPORT SenzrTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(TARGETS edge-gateway-backend edge-gateway-updater senzr-ctl senzr_app_example
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/senzr/ DESTINATION include/senzr)
install(DIRECTORY frontend/ DESTINATION share/edge-gateway/frontend)
install(FILES database/config.json DESTINATION share/edge-gateway/database)

# Export CMake Package Configuration
install(EXPORT SenzrTargets
    FILE SenzrTargets.cmake
    NAMESPACE Senzr::
    DESTINATION lib/cmake/Senzr
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SenzrConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SenzrConfig.cmake
    INSTALL_DESTINATION lib/cmake/Senzr
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SenzrConfigVersion.cmake
    VERSION 1.1.0
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SenzrConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/SenzrConfigVersion.cmake
    DESTINATION lib/cmake/Senzr
)

# ── Summary ───────────────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "=== Edge Gateway C++ Build Configuration ===")
message(STATUS "  C++ Standard : C++20")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  OpenCV       : ${OpenCV_VERSION}")
message(STATUS "  RKNN         : ${RKNN_FOUND}")
message(STATUS "  whisper.cpp  : ${WHISPER_FOUND}")
message(STATUS "  OpenSSL      : ${OPENSSL_FOUND}")
message(STATUS "  ALSA         : ${ALSA_LIB}")
message(STATUS "============================================")
message(STATUS "")
