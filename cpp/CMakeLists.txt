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
)

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

# ── Senzr OS SDK Library (libsenzr) ───────────────────────────────────────────

set(SENZR_SDK_SOURCES
    src/sdk/device.cpp
    src/sdk/fota.cpp
    src/app/updater.cpp
    src/app/gpio_manager.cpp
    src/app/connectivity_manager.cpp
    src/app/ntp_client.cpp
    src/app/database.cpp
)

add_library(senzr STATIC
    ${SENZR_SDK_SOURCES}
)

target_include_directories(senzr PUBLIC
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(senzr PUBLIC
    SQLite3::SQLite3
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
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
    senzr
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
    senzr
)

# Alias for Senzr OS naming convention
add_custom_target(senzr-fota-daemon ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink edge-gateway-updater senzr-fota-daemon
    DEPENDS edge-gateway-updater
)

# ── SDK Example Application ───────────────────────────────────────────────────

add_executable(senzr_app_example
    examples/senzr_app_example.cpp
)
target_include_directories(senzr_app_example PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(senzr_app_example PRIVATE
    senzr
)

# ── Tests ─────────────────────────────────────────────────────────────────────

enable_testing()

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

# ── Install ───────────────────────────────────────────────────────────────────

install(TARGETS edge-gateway-backend edge-gateway-updater senzr senzr-ctl senzr_app_example
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/senzr/ DESTINATION include/senzr)
install(DIRECTORY frontend/ DESTINATION share/edge-gateway/frontend)
install(FILES database/config.json DESTINATION share/edge-gateway/database)

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
