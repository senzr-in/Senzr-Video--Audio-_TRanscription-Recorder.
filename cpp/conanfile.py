from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy
import os

class SenzrSdkConan(ConanFile):
    name = "senzr-sdk"
    version = "1.1.0"
    license = "Apache-2.0"
    author = "Senzr Edge OS Engineering"
    url = "https://github.com/daltonium/edge-gateway-framework"
    description = "Commercial-grade C++20 embedded platform SDK for Rockchip RK3588 edge appliances"
    topics = ("embedded", "rk3588", "edge-ai", "sdk", "cplusplus20")
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": True,
        "fPIC": True,
    }

    def requirements(self):
        self.requires("sqlite3/3.42.0")
        self.requires("libcurl/8.2.1")
        self.requires("openssl/3.1.2")
        self.requires("nlohmann_json/3.11.2")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["senzr"]
        self.cpp_info.set_property("cmake_file_name", "Senzr")
        self.cpp_info.set_property("cmake_target_name", "Senzr::SDK")
        self.cpp_info.set_property("pkg_config_name", "senzr")
