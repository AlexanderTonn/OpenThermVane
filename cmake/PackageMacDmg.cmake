if(NOT APPLE)
    message(FATAL_ERROR "PackageMacDmg.cmake is only supported on macOS")
endif()

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "SOURCE_DIR and BINARY_DIR are required")
endif()

if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME OR CMAKE_INSTALL_CONFIG_NAME STREQUAL "")
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
endif()

if(NOT DEFINED DMG_OUTPUT_DIR OR DMG_OUTPUT_DIR STREQUAL "")
    set(DMG_OUTPUT_DIR "${BINARY_DIR}/macos-dmg")
endif()

if(NOT DEFINED PROJECT_VERSION OR PROJECT_VERSION STREQUAL "")
    set(PROJECT_VERSION "0.1.0")
endif()

set(INSTALL_ROOT "${DMG_OUTPUT_DIR}/install-root")
set(PKG_PAYLOAD_ROOT "${DMG_OUTPUT_DIR}/pkg-payload")
set(PKG_SCRIPTS_DIR "${DMG_OUTPUT_DIR}/pkg-scripts")
set(DMG_ROOT "${DMG_OUTPUT_DIR}/dmg-root")
set(COMPONENT_PKG "${DMG_OUTPUT_DIR}/ThermVaneComponent.pkg")
set(PRODUCT_PKG "${DMG_ROOT}/ThermVane.pkg")
set(DMG_PATH "${DMG_OUTPUT_DIR}/ThermVaneInstaller.dmg")

file(REMOVE_RECURSE
    "${INSTALL_ROOT}"
    "${PKG_PAYLOAD_ROOT}"
    "${PKG_SCRIPTS_DIR}"
    "${DMG_ROOT}"
    "${COMPONENT_PKG}"
    "${DMG_PATH}")
file(MAKE_DIRECTORY
    "${DMG_OUTPUT_DIR}"
    "${PKG_PAYLOAD_ROOT}/Applications"
    "${PKG_SCRIPTS_DIR}"
    "${DMG_ROOT}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --config "${CMAKE_INSTALL_CONFIG_NAME}" --prefix "${INSTALL_ROOT}"
    RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed")
endif()

find_program(MACDEPLOYQT_EXECUTABLE
    NAMES macdeployqt
    HINTS "${QT_ROOT}/bin")
if(NOT MACDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "macdeployqt not found. Set THERMVANE_QT_ROOT or add Qt macOS bin directory to PATH.")
endif()

set(THERMVANE_APP_BUNDLE "${INSTALL_ROOT}/ThermVane.app")
set(THERMVANE_HELPER "${INSTALL_ROOT}/bin/ThermVaneFanHelper")
if(NOT EXISTS "${THERMVANE_APP_BUNDLE}")
    message(FATAL_ERROR "ThermVane.app was not installed to ${THERMVANE_APP_BUNDLE}")
endif()
if(NOT EXISTS "${THERMVANE_HELPER}")
    message(FATAL_ERROR "ThermVaneFanHelper was not installed to ${THERMVANE_HELPER}")
endif()

execute_process(
    COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${THERMVANE_APP_BUNDLE}"
        "-qmldir=${SOURCE_DIR}/qml"
    RESULT_VARIABLE deploy_result)
if(NOT deploy_result EQUAL 0)
    message(FATAL_ERROR "macdeployqt failed")
endif()
file(REMOVE_RECURSE "${THERMVANE_APP_BUNDLE}/Contents/PlugIns/sqldrivers")

file(COPY "${THERMVANE_APP_BUNDLE}" DESTINATION "${PKG_PAYLOAD_ROOT}/Applications")
file(COPY_FILE "${THERMVANE_HELPER}" "${PKG_SCRIPTS_DIR}/ThermVaneFanHelper")
file(COPY_FILE "${SOURCE_DIR}/scripts/macos/install-fan-helper.sh" "${PKG_SCRIPTS_DIR}/install-fan-helper.sh")
file(COPY_FILE "${SOURCE_DIR}/scripts/macos/uninstall-fan-helper.sh" "${PKG_SCRIPTS_DIR}/uninstall-fan-helper.sh")

file(WRITE "${PKG_SCRIPTS_DIR}/postinstall" [[#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
/bin/bash "${SCRIPT_DIR}/install-fan-helper.sh" "${SCRIPT_DIR}/ThermVaneFanHelper"
exit 0
]])

file(CHMOD
    "${PKG_SCRIPTS_DIR}/ThermVaneFanHelper"
    "${PKG_SCRIPTS_DIR}/install-fan-helper.sh"
    "${PKG_SCRIPTS_DIR}/uninstall-fan-helper.sh"
    "${PKG_SCRIPTS_DIR}/postinstall"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)

foreach(clean_path IN ITEMS "${PKG_PAYLOAD_ROOT}" "${PKG_SCRIPTS_DIR}" "${DMG_ROOT}")
    execute_process(
        COMMAND /usr/bin/find "${clean_path}" -name "._*" -type f -delete
        RESULT_VARIABLE clean_result)
    if(NOT clean_result EQUAL 0)
        message(FATAL_ERROR "Failed to remove AppleDouble metadata files from ${clean_path}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env COPYFILE_DISABLE=1 pkgbuild
        --root "${PKG_PAYLOAD_ROOT}"
        --scripts "${PKG_SCRIPTS_DIR}"
        --identifier "org.openthermvane.ThermVane"
        --version "${PROJECT_VERSION}"
        --install-location "/"
        "${COMPONENT_PKG}"
    RESULT_VARIABLE pkgbuild_result)
if(NOT pkgbuild_result EQUAL 0)
    message(FATAL_ERROR "pkgbuild failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env COPYFILE_DISABLE=1 productbuild
        --package "${COMPONENT_PKG}"
        "${PRODUCT_PKG}"
    RESULT_VARIABLE productbuild_result)
if(NOT productbuild_result EQUAL 0)
    message(FATAL_ERROR "productbuild failed")
endif()

file(WRITE "${DMG_ROOT}/README.txt" "Open ThermVane.pkg to install ThermVane.app into /Applications and install the fan helper.\n")

execute_process(
    COMMAND /usr/bin/find "${DMG_ROOT}" -name "._*" -type f -delete
    RESULT_VARIABLE dmg_clean_result)
if(NOT dmg_clean_result EQUAL 0)
    message(FATAL_ERROR "Failed to remove AppleDouble metadata files from ${DMG_ROOT}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env COPYFILE_DISABLE=1 hdiutil create
        -volname "ThermVane ${PROJECT_VERSION}"
        -srcfolder "${DMG_ROOT}"
        -ov
        -format UDZO
        "${DMG_PATH}"
    RESULT_VARIABLE hdiutil_result)
if(NOT hdiutil_result EQUAL 0)
    message(FATAL_ERROR "hdiutil create failed")
endif()

message(STATUS "ThermVane macOS DMG created at: ${DMG_PATH}")
