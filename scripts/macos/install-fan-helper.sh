#!/usr/bin/env bash
set -euo pipefail

LABEL="com.openthermvane.fanhelper"
HELPER_NAME="ThermVaneFanHelper"
SOCKET_PATH="/tmp/thermvane-fan-helper.sock"
INSTALL_PATH="/Library/PrivilegedHelperTools/${HELPER_NAME}"
PLIST_PATH="/Library/LaunchDaemons/${LABEL}.plist"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEFAULT_HELPER="${REPO_ROOT}/build/Qt_6_12_0_for_macOS_Debug/${HELPER_NAME}"
HELPER_SOURCE="${1:-${DEFAULT_HELPER}}"
SUDO="sudo"
if [[ "${EUID}" -eq 0 ]]; then
    SUDO=""
fi

if [[ ! -x "${HELPER_SOURCE}" ]]; then
    echo "Helper not found or not executable: ${HELPER_SOURCE}" >&2
    echo "Build first, or pass the helper path as first argument." >&2
    exit 1
fi

TMP_PLIST="$(mktemp)"
cat > "${TMP_PLIST}" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>${LABEL}</string>
    <key>ProgramArguments</key>
    <array>
        <string>${INSTALL_PATH}</string>
        <string>--server</string>
        <string>${SOCKET_PATH}</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/var/log/thermvane-fan-helper.log</string>
    <key>StandardErrorPath</key>
    <string>/var/log/thermvane-fan-helper.log</string>
</dict>
</plist>
PLIST

${SUDO} launchctl bootout system "${PLIST_PATH}" >/dev/null 2>&1 || true
${SUDO} rm -f "${SOCKET_PATH}"
${SUDO} install -o root -g wheel -m 4755 "${HELPER_SOURCE}" "${INSTALL_PATH}"
${SUDO} install -o root -g wheel -m 644 "${TMP_PLIST}" "${PLIST_PATH}"
rm -f "${TMP_PLIST}"
${SUDO} launchctl bootstrap system "${PLIST_PATH}"
${SUDO} launchctl kickstart -k "system/${LABEL}"

echo "Installed ${LABEL}."
echo "Socket: ${SOCKET_PATH}"
