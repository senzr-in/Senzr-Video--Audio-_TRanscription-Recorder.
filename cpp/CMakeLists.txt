cmake_minimum_required(VERSION 3.16)
project(edge_gateway_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json QUIET)
find_package(SQLite3 QUIET)
find_package(OpenSSL QUIET)

add_executable(edge-gateway-backend
    src/main.cpp
    src/app/api_server.cpp
    src/app/config_manager.cpp
    src/app/database.cpp
    src/app/logger_utils.cpp
    src/app/models.cpp
    src/session_pipeline/audio_capture.cpp
    src/session_pipeline/person_detection.cpp
    src/session_pipeline/recorder_manager.cpp
    src/session_pipeline/run_session_pipeline.cpp
    src/session_pipeline/session.cpp
    src/session_pipeline/session_manager.cpp
    src/session_pipeline/transcriber.cpp
    src/session_pipeline/uploader.cpp
)

target_include_directories(edge-gateway-backend PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(edge-gateway-backend PRIVATE nlohmann_json::nlohmann_json)
endif()

if(SQLite3_FOUND)
    target_include_directories(edge-gateway-backend PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(edge-gateway-backend PRIVATE ${SQLite3_LIBRARIES})
endif()

if(OPENSSL_FOUND)
    target_compile_definitions(edge-gateway-backend PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(edge-gateway-backend PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# Benchmark executable (C++ port of benchmark.py)
add_executable(edge-gateway-benchmark
    src/benchmark.cpp
)

target_include_directories(edge-gateway-benchmark PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(edge-gateway-benchmark PRIVATE nlohmann_json::nlohmann_json)
endif()

if(SQLite3_FOUND)
    target_include_directories(edge-gateway-benchmark PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(edge-gateway-benchmark PRIVATE ${SQLite3_LIBRARIES})
endif()

if(OPENSSL_FOUND)
    target_compile_definitions(edge-gateway-benchmark PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(edge-gateway-benchmark PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
