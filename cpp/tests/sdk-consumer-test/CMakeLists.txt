cmake_minimum_required(VERSION 3.20)
project(sdk_consumer_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Discover the Senzr SDK installed package
find_package(Senzr REQUIRED)

add_executable(consumer_app
    main.cpp
)

target_link_libraries(consumer_app PRIVATE
    Senzr::SDK
)
