#!/usr/bin/env bash
# ==============================================================================
# Senzr Edge Gateway - C++ Build & FOTA Release Package Generator
# ==============================================================================
set -euo pipefail

VERSION="${1:-1.0.6}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
RELEASES_DIR="${SCRIPT_DIR}/releases"
STAGE_DIR="/tmp/fota_staging_${VERSION}"
ZIP_NAME="edge-gateway-v${VERSION}.zip"
ZIP_OUTPUT="${RELEASES_DIR}/${ZIP_NAME}"

echo "======================================================"
echo "  🚀 SENZR EDGE GATEWAY BUILD & FOTA GENERATOR"
echo "  📦 Target Version : ${VERSION}"
echo "======================================================"

# 1. Compile C++ Backend Binaries
echo -e "\n[1/4] Compiling C++ Backend & SDK..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j"$(nproc)"

# 2. Stage FOTA Release Files
echo -e "\n[2/4] Staging FOTA release files..."
rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/database" "${STAGE_DIR}/models" "${STAGE_DIR}/bin" "${STAGE_DIR}/frontend" "${RELEASES_DIR}"

# Copy Frontend Web Assets
if [[ -d "${SCRIPT_DIR}/frontend" ]]; then
    cp -r "${SCRIPT_DIR}/frontend/"* "${STAGE_DIR}/frontend/"
fi

# Copy Database initial configuration
if [[ -f "${BUILD_DIR}/database/config.json" ]]; then
    cp "${BUILD_DIR}/database/config.json" "${STAGE_DIR}/database/"
elif [[ -f "${SCRIPT_DIR}/database/config.json" ]]; then
    cp "${SCRIPT_DIR}/database/config.json" "${STAGE_DIR}/database/"
else
    echo "{\"version\":\"${VERSION}\"}" > "${STAGE_DIR}/database/config.json"
fi

# Copy Compiled Binaries
for bin_name in edge-gateway-backend edge-gateway-updater senzr-ctl; do
    if [[ -f "${BUILD_DIR}/${bin_name}" ]]; then
        cp "${BUILD_DIR}/${bin_name}" "${STAGE_DIR}/bin/"
        chmod +x "${STAGE_DIR}/bin/${bin_name}"
    fi
done

# Set Version Metadata
echo -n "${VERSION}" > "${STAGE_DIR}/version.txt"
echo -n "${VERSION}" > "${STAGE_DIR}/.version"

# 3. Create Release Archive
echo -e "\n[3/4] Packaging into ${ZIP_NAME}..."
cd "${STAGE_DIR}"
rm -f "${ZIP_OUTPUT}"
if command -v zip >/dev/null 2>&1; then
    zip -q -r "${ZIP_OUTPUT}" ./*
elif command -v python3 >/dev/null 2>&1; then
    python3 -m zipfile -c "${ZIP_OUTPUT}" ./*
else
    tar -czf "${RELEASES_DIR}/edge-gateway-v${VERSION}.tar.gz" ./*
    ZIP_OUTPUT="${RELEASES_DIR}/edge-gateway-v${VERSION}.tar.gz"
fi
rm -rf "${STAGE_DIR}"

# 4. Generate Checksum & S3 Manifest
SHA256_HASH=$(sha256sum "${ZIP_OUTPUT}" | awk '{print $1}')
FILE_SIZE=$(stat -c%s "${ZIP_OUTPUT}" 2>/dev/null || stat -f%z "${ZIP_OUTPUT}")

cat <<EOF > "${RELEASES_DIR}/latest.json"
{
  "version": "${VERSION}",
  "file": "${ZIP_NAME}",
  "checksum": "${SHA256_HASH}",
  "size_bytes": ${FILE_SIZE},
  "release_notes": "SenzrOS v${VERSION} release package with direct Web Upload and Auto-Reboot support"
}
EOF

echo -e "\n======================================================"
echo "  ✅ FOTA RELEASE READY FOR DEPLOYMENT"
echo "------------------------------------------------------"
echo "  📦 Package File : ${ZIP_OUTPUT}"
echo "  📄 S3 Manifest  : ${RELEASES_DIR}/latest.json"
echo "  📊 File Size    : ${FILE_SIZE} bytes"
echo "  🔑 SHA-256 Hash : ${SHA256_HASH}"
echo "  🏷️  Version      : ${VERSION}"
echo "======================================================"
