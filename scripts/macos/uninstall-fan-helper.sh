#!/usr/bin/env bash
set -euo pipefail

LABEL="com.openthermvane.fanhelper"
HELPER_NAME="ThermVaneFanHelper"
SOCKET_PATH="/tmp/thermvane-fan-helper.sock"
INSTALL_PATH="/Library/PrivilegedHelperTools/${HELPER_NAME}"
PLIST_PATH="/Library/LaunchDaemons/${LABEL}.plist"
AUTHORIZED_UID_FILE="/Library/Application Support/OpenThermVane/fan-helper.uid"
SUPPORT_DIR="/Library/Application Support/OpenThermVane"
SUDO="sudo"
if [[ "${EUID}" -eq 0 ]]; then
    SUDO=""
fi

${SUDO} launchctl bootout system "${PLIST_PATH}" >/dev/null 2>&1 || true
${SUDO} rm -f "${SOCKET_PATH}"
${SUDO} rm -f "${PLIST_PATH}"
${SUDO} rm -f "${INSTALL_PATH}"
${SUDO} rm -f "${AUTHORIZED_UID_FILE}"
${SUDO} rm -f "${SUPPORT_DIR}/install-fan-helper.sh" "${SUPPORT_DIR}/uninstall-fan-helper.sh"
${SUDO} rmdir "${SUPPORT_DIR}" >/dev/null 2>&1 || true

echo "Uninstalled ${LABEL}."
