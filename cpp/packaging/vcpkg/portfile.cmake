# vcpkg portfile for senzr-sdk
# TODO: Update REF and SHA512 when publishing release tag

vcpkg_check_linkage(ONLY_STATIC_LIBRARY) # Or support dynamic via options

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO daltonium/edge-gateway-framework
    REF v1.1.0
    SHA512 00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 # TODO: compute hash
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/cpp"
    OPTIONS
        -DSENZR_BUILD_TESTS=OFF
        -DSENZR_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME Senzr CONFIG_PATH lib/cmake/Senzr)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
