SUMMARY = "Senzr Edge OS Embedded Platform SDK"
DESCRIPTION = "Commercial-grade C++20 embedded platform SDK for Rockchip RK3588 edge appliances"
HOMEPAGE = "https://github.com/daltonium/edge-gateway-framework"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3b83ef96387f1465e798573296d19501"

SRC_URI = "git://github.com/daltonium/edge-gateway-framework.git;protocol=https;branch=main"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git/cpp"

inherit cmake pkgconfig

DEPENDS = "sqlite3 curl openssl opencv nlohmann-json"
RDEPENDS:${PN} = "libsqlite3 libcurl libssl libcrypto"

EXTRA_OECMAKE = "\
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DSENZR_PLATFORM=rockchip \
"

FILES:${PN} = "\
    ${libdir}/libsenzr.so.* \
    ${bindir}/edge-gateway-backend \
    ${bindir}/edge-gateway-updater \
    ${bindir}/senzr-ctl \
    ${datadir}/edge-gateway/* \
"

FILES:${PN}-dev = "\
    ${includedir}/senzr/* \
    ${libdir}/libsenzr.so \
    ${libdir}/pkgconfig/senzr.pc \
    ${libdir}/cmake/Senzr/* \
"
