#!/usr/bin/env bash
# ==============================================================================
# Edge Gateway - Orange Pi 5 Max Flashable Image Builder
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT_DIR="${PROJECT_ROOT}/deploy/output"
WORK_DIR="${PROJECT_ROOT}/deploy/work"
IMG_NAME="edge-gateway-orangepi5max.img"

echo "=== Edge Gateway Image Builder for Orange Pi 5 Max ==="

mkdir -p "${OUTPUT_DIR}" "${WORK_DIR}"

# 1. Base Image selection
# Orange Pi 5 Max Ubuntu 22.04 LTS Server official rockchip kernel image
BASE_IMG_URL="https://github.com/Joshua-Riek/ubuntu-rockchip/releases/download/v2.4.0/ubuntu-22.04-preinstalled-server-arm64-orangepi-5-max.img.xz"
BASE_IMG_XZ="${WORK_DIR}/base_image.img.xz"
TARGET_IMG="${OUTPUT_DIR}/${IMG_NAME}"

if [[ ! -f "${TARGET_IMG}" ]]; then
    if [[ ! -f "${BASE_IMG_XZ}" ]] || [[ $(stat -c%s "${BASE_IMG_XZ}" 2>/dev/null || stat -f%z "${BASE_IMG_XZ}") -lt 10000000 ]]; then
        rm -f "${BASE_IMG_XZ}"
        echo "[1/5] Downloading Orange Pi 5 Max Ubuntu 22.04 base image (~916 MB)..."
        curl -fL --progress-bar -o "${BASE_IMG_XZ}" "${BASE_IMG_URL}" || {
            echo "Direct download failed. Please place your Orange Pi 5 Max image at: ${TARGET_IMG}"
            exit 1
        }
    fi
    echo "[2/5] Decompressing base image..."
    xz -dk -f "${BASE_IMG_XZ}"
    # Find extracted image file in work dir
    EXTRACTED_IMG=$(find "${WORK_DIR}" -maxdepth 1 -name "*.img" -not -name "${IMG_NAME}" | head -n 1)
    if [[ -n "${EXTRACTED_IMG}" && -f "${EXTRACTED_IMG}" ]]; then
        mv "${EXTRACTED_IMG}" "${TARGET_IMG}"
    elif [[ -f "${WORK_DIR}/base_image.img" ]]; then
        mv "${WORK_DIR}/base_image.img" "${TARGET_IMG}"
    fi
else
    echo "[1/5] Using existing base image: ${TARGET_IMG}"
fi

# 2. Mount partitions
echo "[3/5] Setting up loop device..."
MOUNT_DIR="${WORK_DIR}/rootfs"
mkdir -p "${MOUNT_DIR}"

LOOP_DEV=$(losetup -Pf --show "${TARGET_IMG}")
echo "Attached to ${LOOP_DEV}"

cleanup() {
    echo "Cleaning up mounts..."
    sync
    umount -R "${MOUNT_DIR}" 2>/dev/null || true
    losetup -d "${LOOP_DEV}" 2>/dev/null || true
}
trap cleanup EXIT

# In RK3588 images, root partition is typically the last partition (p2 or p1)
ROOT_PART="${LOOP_DEV}p2"
if [[ ! -b "${ROOT_PART}" ]]; then
    ROOT_PART="${LOOP_DEV}p1"
fi

echo "Mounting rootfs from ${ROOT_PART}..."
mount "${ROOT_PART}" "${MOUNT_DIR}"

# 3. Inject Edge Gateway files
echo "[4/5] Injecting Edge Gateway binaries, services, and frontend..."

INSTALL_ROOT="${MOUNT_DIR}/opt/edge-gateway"
mkdir -p "${INSTALL_ROOT}/bin" \
         "${INSTALL_ROOT}/database" \
         "${INSTALL_ROOT}/models" \
         "${INSTALL_ROOT}/logs" \
         "${INSTALL_ROOT}/scripts"

# Copy Compiled Binaries
for bin_name in edge-gateway-backend edge-gateway-updater senzr-ctl; do
    if [[ -f "${PROJECT_ROOT}/cpp/build/${bin_name}" ]]; then
        echo "  -> Copying ${bin_name}..."
        cp "${PROJECT_ROOT}/cpp/build/${bin_name}" "${INSTALL_ROOT}/bin/"
        chmod +x "${INSTALL_ROOT}/bin/${bin_name}"
    elif [[ -f "${PROJECT_ROOT}/build/${bin_name}" ]]; then
        echo "  -> Copying ${bin_name} from build..."
        cp "${PROJECT_ROOT}/build/${bin_name}" "${INSTALL_ROOT}/bin/"
        chmod +x "${INSTALL_ROOT}/bin/${bin_name}"
    fi
done

# Setup symlinks in /usr/local/bin
mkdir -p "${MOUNT_DIR}/usr/local/bin"
if [[ -f "${INSTALL_ROOT}/bin/senzr-ctl" ]]; then
    ln -sf "/opt/edge-gateway/bin/senzr-ctl" "${MOUNT_DIR}/usr/local/bin/senzr-ctl"
fi
if [[ -f "${INSTALL_ROOT}/bin/edge-gateway-updater" ]]; then
    ln -sf "/opt/edge-gateway/bin/edge-gateway-updater" "${MOUNT_DIR}/usr/local/bin/edge-gateway-updater"
fi

# Copy Database config
if [[ -f "${PROJECT_ROOT}/cpp/build/database/config.json" ]]; then
    cp "${PROJECT_ROOT}/cpp/build/database/config.json" "${INSTALL_ROOT}/database/"
elif [[ -f "${PROJECT_ROOT}/current/database/config.json" ]]; then
    cp "${PROJECT_ROOT}/current/database/config.json" "${INSTALL_ROOT}/database/"
fi

# Copy Models
if [[ -d "${PROJECT_ROOT}/current/models" ]]; then
    cp -r "${PROJECT_ROOT}/current/models/"* "${INSTALL_ROOT}/models/"
fi

# Copy Top scripts
if [[ -d "${PROJECT_ROOT}/scripts" ]]; then
    cp "${PROJECT_ROOT}/scripts/"* "${INSTALL_ROOT}/scripts/"
    chmod +x "${INSTALL_ROOT}/scripts/"*.sh 2>/dev/null || true
fi

# Copy Updater Configuration for FOTA
if [[ -f "${SCRIPT_DIR}/updater_config.env" ]]; then
    echo "  -> Copying updater_config.env..."
    cp "${SCRIPT_DIR}/updater_config.env" "${INSTALL_ROOT}/updater_config.env"
    mkdir -p "${MOUNT_DIR}/etc/senzr"
    cp "${SCRIPT_DIR}/updater_config.env" "${MOUNT_DIR}/etc/senzr/fota.env"
fi

# Create runtime directories for FOTA
mkdir -p "${INSTALL_ROOT}/downloads" \
         "${INSTALL_ROOT}/backup" \
         "${INSTALL_ROOT}/current"

# Copy Systemd services
cp "${SCRIPT_DIR}/systemd/edge-gateway.service" "${MOUNT_DIR}/etc/systemd/system/"
cp "${SCRIPT_DIR}/systemd/edge-gateway-updater.service" "${MOUNT_DIR}/etc/systemd/system/"

# Copy Network configs
mkdir -p "${MOUNT_DIR}/etc/hostapd" "${MOUNT_DIR}/etc/dnsmasq.d" "${MOUNT_DIR}/etc/nginx/sites-available"
if [[ -f "${SCRIPT_DIR}/network/hostapd.conf" ]]; then
    cp "${SCRIPT_DIR}/network/hostapd.conf" "${MOUNT_DIR}/etc/hostapd/hostapd.conf"
fi
if [[ -f "${SCRIPT_DIR}/network/dnsmasq.conf" ]]; then
    cp "${SCRIPT_DIR}/network/dnsmasq.conf" "${MOUNT_DIR}/etc/dnsmasq.d/edge-gateway.conf"
fi
if [[ -f "${SCRIPT_DIR}/network/nginx-edge-gateway.conf" ]]; then
    cp "${SCRIPT_DIR}/network/nginx-edge-gateway.conf" "${MOUNT_DIR}/etc/nginx/sites-available/default"
fi

# Enable systemd services
mkdir -p "${MOUNT_DIR}/etc/systemd/system/multi-user.target.wants"
ln -sf "/etc/systemd/system/edge-gateway.service" "${MOUNT_DIR}/etc/systemd/system/multi-user.target.wants/edge-gateway.service"
ln -sf "/etc/systemd/system/edge-gateway-updater.service" "${MOUNT_DIR}/etc/systemd/system/multi-user.target.wants/edge-gateway-updater.service"

# Configure passwordless sudo and polkit for headless operation
mkdir -p "${MOUNT_DIR}/etc/sudoers.d"
echo "orangepi ALL=(ALL) NOPASSWD: ALL" > "${MOUNT_DIR}/etc/sudoers.d/010_orangepi-nopasswd"
chmod 0440 "${MOUNT_DIR}/etc/sudoers.d/010_orangepi-nopasswd"

mkdir -p "${MOUNT_DIR}/etc/polkit-1/rules.d"
cat << 'EOF' > "${MOUNT_DIR}/etc/polkit-1/rules.d/50-senzr-systemd.rules"
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.systemd1.manage-units") == 0) {
        return polkit.Result.YES;
    }
});
EOF
chmod 0644 "${MOUNT_DIR}/etc/polkit-1/rules.d/50-senzr-systemd.rules"

# Version tag (Initial Base Firmware Version)
echo -n "1.0.6" > "${INSTALL_ROOT}/.version"
echo -n "1.0.6" > "${INSTALL_ROOT}/version.txt"

echo "[5/5] Finalizing image..."
sync

echo ""
echo "================================================================="
echo " SUCCESS! Image is ready at:"
echo "   ${TARGET_IMG}"
echo ""
echo " Windows path:"
echo "   F:\\PI5MAX\\deploy\\output\\${IMG_NAME}"
echo "================================================================="
