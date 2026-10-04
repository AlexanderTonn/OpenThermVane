#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build/Qt_6_12_0_for_macOS_Release}"
CONFIGURATION="${CONFIGURATION:-Release}"
QT_ROOT="${QT_ROOT:-/Volumes/AlexMacSSD/Qt/6.12.0/macos}"
OUTPUT_DIR="${OUTPUT_DIR:-${BUILD_DIR}/macos-dmg}"

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CONFIGURATION}" \
    -DCMAKE_PREFIX_PATH="${QT_ROOT}" \
    -DTHERMVANE_QT_ROOT="${QT_ROOT}" \
    -DTHERMVANE_MACOS_DMG_OUTPUT_DIR="${OUTPUT_DIR}"

cmake --build "${BUILD_DIR}" --target package_macos_dmg --config "${CONFIGURATION}"

echo "ThermVane macOS DMG created at: ${OUTPUT_DIR}/ThermVaneInstaller.dmg"
