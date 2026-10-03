#!/usr/bin/env bash
set -euo pipefail

LABEL="com.openthermvane.fanhelper"
HELPER_NAME="ThermVaneFanHelper"
SOCKET_PATH="/tmp/thermvane-fan-helper.sock"
INSTALL_PATH="/Library/PrivilegedHelperTools/${HELPER_NAME}"
PLIST_PATH="/Library/LaunchDaemons/${LABEL}.plist"
SUDO="sudo"
if [[ "${EUID}" -eq 0 ]]; then
    SUDO=""
fi

${SUDO} launchctl bootout system "${PLIST_PATH}" >/dev/null 2>&1 || true
${SUDO} rm -f "${SOCKET_PATH}"
${SUDO} rm -f "${PLIST_PATH}"
${SUDO} rm -f "${INSTALL_PATH}"

echo "Uninstalled ${LABEL}."
