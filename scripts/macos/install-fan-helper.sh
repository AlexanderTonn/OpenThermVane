#!/usr/bin/env bash
set -euo pipefail

LABEL="com.openthermvane.fanhelper"
HELPER_NAME="ThermVaneFanHelper"
SOCKET_PATH="/tmp/thermvane-fan-helper.sock"
INSTALL_PATH="/Library/PrivilegedHelperTools/${HELPER_NAME}"
PLIST_PATH="/Library/LaunchDaemons/${LABEL}.plist"
AUTHORIZED_UID_FILE="/Library/Application Support/OpenThermVane/fan-helper.uid"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEFAULT_HELPER="${REPO_ROOT}/build/Qt_6_12_0_for_macOS_Debug/${HELPER_NAME}"
HELPER_SOURCE="${1:-${DEFAULT_HELPER}}"
SUDO="sudo"
if [[ "${EUID}" -eq 0 ]]; then
    SUDO=""
fi

resolve_uid_for_user()
{
    local user_name="${1:-}"
    if [[ -z "${user_name}" || "${user_name}" == "root" || "${user_name}" == "loginwindow" ]]; then
        return 0
    fi

    /usr/bin/id -u "${user_name}" 2>/dev/null || true
}

RUNNING_USER_UID=""
if [[ -n "${THERMVANE_AUTHORIZED_UID:-}" ]]; then
    RUNNING_USER_UID="${THERMVANE_AUTHORIZED_UID}"
elif [[ -n "${SUDO_UID:-}" && "${SUDO_UID}" != "0" ]]; then
    RUNNING_USER_UID="${SUDO_UID}"
elif [[ "${EUID}" -ne 0 ]]; then
    RUNNING_USER_UID="$(/usr/bin/id -u 2>/dev/null || true)"
else
    CONSOLE_USER="$(/usr/sbin/scutil <<< 'show State:/Users/ConsoleUser' 2>/dev/null \
        | /usr/bin/awk -F': *' '$1 == "Name" { print $2; exit }' || true)"
    RUNNING_USER_UID="$(resolve_uid_for_user "${CONSOLE_USER}")"

    if [[ -z "${RUNNING_USER_UID}" || "${RUNNING_USER_UID}" == "0" ]]; then
        # The active console session is also visible through `who` on macOS.
        CONSOLE_USER="$(/usr/bin/who 2>/dev/null \
            | /usr/bin/awk '$2 == "console" { print $1; exit }' || true)"
        RUNNING_USER_UID="$(resolve_uid_for_user "${CONSOLE_USER}")"
    fi

    if [[ -z "${RUNNING_USER_UID}" || "${RUNNING_USER_UID}" == "0" ]]; then
        # Some Installer.app contexts expose the active GUI account through
        # /dev/console even when scutil is unavailable to package scripts.
        CONSOLE_USER="$(/usr/bin/stat -f '%Su' /dev/console 2>/dev/null || true)"
        RUNNING_USER_UID="$(resolve_uid_for_user "${CONSOLE_USER}")"
    fi

    if [[ -z "${RUNNING_USER_UID}" || "${RUNNING_USER_UID}" == "0" ]]; then
        # Installer.app can hide the console-user state from a root package
        # script. Select a normal local account as a safe fallback; system
        # accounts and the root account are excluded.
        RUNNING_USER_UID="$(/usr/bin/dscl . -list /Users UniqueID 2>/dev/null \
            | /usr/bin/awk '$2 >= 500 && $2 < 1000 && $1 !~ /^_/ && $1 != "root" { print $2; exit }' || true)"
    fi
fi

if [[ -z "${RUNNING_USER_UID}" || ! "${RUNNING_USER_UID}" =~ ^[0-9]+$ || "${RUNNING_USER_UID}" == "0" ]]; then
    echo "Could not determine a non-root authorized macOS user UID." >&2
    echo "Set THERMVANE_AUTHORIZED_UID when installing non-interactively." >&2
    exit 1
fi
echo "Using authorized macOS user UID: ${RUNNING_USER_UID}"

if [[ ! -x "${HELPER_SOURCE}" ]]; then
    echo "Helper not found or not executable: ${HELPER_SOURCE}" >&2
    echo "Build first, or pass the helper path as first argument." >&2
    exit 1
fi

TMP_PLIST="$(mktemp)"
TMP_UID_FILE="$(mktemp)"
trap 'rm -f "${TMP_PLIST}" "${TMP_UID_FILE}"' EXIT
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
        <string>--uid</string>
        <string>${RUNNING_USER_UID}</string>
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

printf '%s\n' "${RUNNING_USER_UID}" > "${TMP_UID_FILE}"

${SUDO} launchctl bootout system "${PLIST_PATH}" >/dev/null 2>&1 || true
${SUDO} rm -f "${SOCKET_PATH}"
${SUDO} install -o root -g wheel -m 4755 "${HELPER_SOURCE}" "${INSTALL_PATH}"
${SUDO} install -o root -g wheel -m 644 "${TMP_PLIST}" "${PLIST_PATH}"
${SUDO} install -d -o root -g wheel -m 755 "$(dirname "${AUTHORIZED_UID_FILE}")"
${SUDO} install -o root -g wheel -m 644 "${TMP_UID_FILE}" "${AUTHORIZED_UID_FILE}"
${SUDO} touch /var/log/thermvane-fan-helper.log
${SUDO} chown root:wheel /var/log/thermvane-fan-helper.log
${SUDO} chmod 600 /var/log/thermvane-fan-helper.log
rm -f "${TMP_PLIST}" "${TMP_UID_FILE}"
${SUDO} launchctl bootstrap system "${PLIST_PATH}"
if ! ${SUDO} launchctl kickstart -k "system/${LABEL}"; then
    # kickstart can return a transient non-zero status while launchd is
    # registering the daemon. Treat it as successful only when the service is
    # actually loaded; otherwise keep the failure visible to the installer.
    if ! ${SUDO} launchctl print "system/${LABEL}" >/dev/null 2>&1; then
        echo "launchd did not load ${LABEL}." >&2
        exit 1
    fi
fi

echo "Installed ${LABEL}."
echo "Socket: ${SOCKET_PATH}"
