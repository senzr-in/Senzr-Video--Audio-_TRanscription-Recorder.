# Packaging & CMake Integration

## Consuming Senzr SDK

External CMake projects consume `libsenzr` with modern standard CMake targets:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_edge_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(Senzr REQUIRED)

add_executable(my_edge_app main.cpp)
target_link_libraries(my_edge_app PRIVATE Senzr::SDK)
```

## Directory Structure After Installation

```
/usr/local/
├── include/
│   └── senzr/
│       ├── ai/
│       ├── media/
│       ├── mqtt/
│       ├── device.hpp
│       ├── error.hpp
│       ├── gpio.hpp
│       ├── logging.hpp
│       ├── network.hpp
│       ├── senzr.hpp
│       ├── storage.hpp
│       ├── types.hpp
│       └── version.hpp
├── lib/
│   ├── libsenzr.a
│   └── cmake/
│       └── Senzr/
│           ├── SenzrConfig.cmake
│           ├── SenzrConfigVersion.cmake
│           └── SenzrTargets.cmake
└── bin/
    ├── senzr-ctl
    └── edge-gateway-backend
```
