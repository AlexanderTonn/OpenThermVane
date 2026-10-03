#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build/Qt_6_12_0_for_macOS_Release}"
CONFIGURATION="${CONFIGURATION:-Release}"
QT_ROOT="${QT_ROOT:-/Volumes/AlexMacSSD/Qt/6.12.0/macos}"
IFW_ROOT="${IFW_ROOT:-/Volumes/AlexMacSSD/Qt/Tools/QtInstallerFramework/4.11}"
OUTPUT_DIR="${OUTPUT_DIR:-${BUILD_DIR}/ifw}"

BINARYCREATOR="${IFW_ROOT}/bin/binarycreator"
if [[ ! -x "${BINARYCREATOR}" ]]; then
    BINARYCREATOR="$(command -v binarycreator || true)"
fi
if [[ -z "${BINARYCREATOR}" || ! -x "${BINARYCREATOR}" ]]; then
    echo "binarycreator not found. Set IFW_ROOT or add Qt Installer Framework bin directory to PATH." >&2
    exit 1
fi

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CONFIGURATION}" \
    -DCMAKE_PREFIX_PATH="${QT_ROOT}" \
    -DTHERMVANE_IFW_BINARYCREATOR="${BINARYCREATOR}" \
    -DTHERMVANE_QT_ROOT="${QT_ROOT}" \
    -DTHERMVANE_IFW_OUTPUT_DIR="${OUTPUT_DIR}"

cmake --build "${BUILD_DIR}" --target package_ifw --config "${CONFIGURATION}"

echo "ThermVane macOS installer created at: ${OUTPUT_DIR}/ThermVaneInstaller.app"
