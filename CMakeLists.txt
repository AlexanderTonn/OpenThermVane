cmake_minimum_required(VERSION 3.24)

# Xcode updates can remove the SDK directory stored in an existing CMake
# cache (for example, MacOSX26.5.sdk after upgrading to Xcode 27). Resolve
# that path before project() initializes the Darwin platform, otherwise CMake
# can emit compiler flags that still reference the deleted SDK.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    # The Qt 6.12 macOS package used by the project is built for macOS 14.4.
    # Without an explicit deployment target, newer Xcode SDKs make the app
    # require the SDK's OS version (for example macOS 27), causing older
    # systems to terminate it immediately when launched from Finder/Dock.
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Minimum macOS version" FORCE)
    endif()

    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE THERMVANE_CURRENT_MACOS_SDK
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(THERMVANE_CURRENT_MACOS_SDK AND EXISTS "${THERMVANE_CURRENT_MACOS_SDK}")
        if(CMAKE_OSX_SYSROOT AND NOT EXISTS "${CMAKE_OSX_SYSROOT}")
            message(STATUS "Replacing unavailable macOS SDK '${CMAKE_OSX_SYSROOT}' with '${THERMVANE_CURRENT_MACOS_SDK}'")
            set(CMAKE_OSX_SYSROOT "${THERMVANE_CURRENT_MACOS_SDK}" CACHE PATH "macOS SDK to use" FORCE)
        endif()

        # Framework discovery results are cached too. Drop paths that point
        # into a removed SDK so FindOpenGL/find_library can rediscover them.
        foreach(THERMVANE_FRAMEWORK_CACHE IN ITEMS
            COREFOUNDATION_FRAMEWORK IOKIT_FRAMEWORK
            OPENGL_GLU_INCLUDE_DIR OPENGL_INCLUDE_DIR
            OPENGL_gl_LIBRARY OPENGL_glu_LIBRARY
        )
            if(DEFINED ${THERMVANE_FRAMEWORK_CACHE} AND NOT EXISTS "${${THERMVANE_FRAMEWORK_CACHE}}")
                unset(${THERMVANE_FRAMEWORK_CACHE} CACHE)
            endif()
        endforeach()
        unset(FIND_PACKAGE_MESSAGE_DETAILS_OpenGL CACHE)
    endif()
endif()

project(OpenThermVane
    VERSION 1.1.3
    DESCRIPTION "Cross-platform thermal and fan control"
    LANGUAGES CXX
)

get_property(THERMVANE_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT THERMVANE_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
if(NOT THERMVANE_MULTI_CONFIG)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets)

qt_standard_project_setup(REQUIRES 6.5)

if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

option(THERMVANE_ENABLE_NBFC_LINUX_BUILD "Add a target that builds the NBFC submodule on Linux" ON)
option(THERMVANE_ENABLE_NBFC_WINDOWS_BUILD "Add a target that builds the NBFC submodule on Windows" ON)
option(THERMVANE_ALLOW_UNLEASED_DIRECT_SMC
    "Allow macOS manual SMC writes without the installed lease-backed helper (development only)"
    OFF)

set(THERMVANE_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/Application.hpp
    src/app/LoggingManager.cpp
    src/app/LoggingManager.hpp
    src/app/LanguageManager.cpp
    src/app/LanguageManager.hpp
    src/core/FanController.cpp
    src/core/FanController.hpp
    src/core/FanCurve.cpp
    src/core/FanCurve.hpp
    src/core/FanManager.cpp
    src/core/FanManager.hpp
    src/core/SensorManager.cpp
    src/core/SensorManager.hpp
    src/hardware/HardwareTypes.hpp
    src/hardware/IHardwareBackend.hpp
    src/hardware/mock/MockHardwareBackend.cpp
    src/hardware/mock/MockHardwareBackend.hpp
    src/hardware/macos/MacHardwareBackend.cpp
    src/hardware/macos/MacHardwareBackend.hpp
    src/hardware/windows/WindowsHardwareBackend.cpp
    src/hardware/windows/WindowsHardwareBackend.hpp
    src/models/FanCurveModel.cpp
    src/models/FanCurveModel.hpp
    src/models/FanModel.cpp
    src/models/FanModel.hpp
    src/models/SensorModel.cpp
    src/models/SensorModel.hpp
    src/service/FanService.cpp
    src/service/FanService.hpp
    src/service/ServiceProtocol.cpp
    src/service/ServiceProtocol.hpp
)

if(LINUX)
    list(APPEND THERMVANE_SOURCES
        src/hardware/linux/LinuxHardwareBackend.cpp
        src/hardware/linux/LinuxHardwareBackend.hpp
    )
endif()

set(THERMVANE_QML_FILES
    qml/Main.qml
    qml/pages/DashboardPage.qml
    qml/pages/FanPage.qml
    qml/pages/SensorPage.qml
    qml/pages/SettingsPage.qml
    qml/controls/FanCard.qml
    qml/controls/FanCurveEditor.qml
    qml/controls/PercentageSlider.qml
    qml/controls/SensorCard.qml
)

set(THERMVANE_PROJECT_FILES
    ThermVane.qmlproject
    qtquickcontrols2.conf
    assets/icons/thermvane-icon.png
    assets/icons/import.svg
    assets/icons/export.svg
    i18n/de.json
    i18n/en.json
    README.md
    docs/architecture.md
    docs/packaging.md
    installer/config/config.xml
    installer/packages/org.openthermvane.app/meta/package.xml
    installer/packages/org.openthermvane.app/meta/installscript.qs
    installer/packages/org.openthermvane.nbfc/meta/package.xml
    installer/packages/org.openthermvane.nbfc/meta/installscript.qs
    scripts/build-ifw-installer.ps1
    scripts/build-nbfc-linux.sh
    scripts/package-macos-dmg.sh
    cmake/BuildIfwInstaller.cmake
    cmake/PackageIfw.cmake
    cmake/PackageMacDmg.cmake
    assets/thermvane.desktop
)

set(THERMVANE_PLATFORM_ASSETS)
if(APPLE)
    # Embed native macOS icon in bundle Resources and reference it from
    # generated Info.plist. Without this, Finder shows the generic app icon.
    set_source_files_properties(
        assets/icons/thermvane.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND THERMVANE_PLATFORM_ASSETS assets/icons/thermvane.icns)
elseif(WIN32)
    # Embed native .ico in the executable. Windows Start-menu shortcuts then
    # inherit the same icon instead of falling back to the generic one.
    list(APPEND THERMVANE_PLATFORM_ASSETS assets/icons/thermvane.rc)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${THERMVANE_SOURCES}
    ${THERMVANE_QML_FILES}
    ${THERMVANE_PROJECT_FILES}
    ${THERMVANE_PLATFORM_ASSETS}
)

qt_add_executable(ThermVane
    ${THERMVANE_SOURCES}
    ${THERMVANE_PROJECT_FILES}
    ${THERMVANE_PLATFORM_ASSETS}
)

if(WIN32)
    set_target_properties(ThermVane PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(APPLE)
    set_target_properties(ThermVane PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ThermVane"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.openthermvane.ThermVane"
        MACOSX_BUNDLE_ICON_FILE "thermvane.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()
if(UNIX AND NOT APPLE)
    set_target_properties(ThermVane PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    install(FILES
        assets/thermvane.desktop
        DESTINATION share/applications)
    install(FILES
        assets/icons/thermvane-icon.png
        DESTINATION share/icons/hicolor/512x512/apps
        RENAME thermvane.png)
endif()

qt_add_qml_module(ThermVane
    URI OpenThermVane
    VERSION 1.0
    QML_FILES
        ${THERMVANE_QML_FILES}
)

qt_add_resources(ThermVane "ThermVaneAppAssets"
    PREFIX "/"
    FILES
        assets/icons/thermvane-icon.png
        assets/icons/import.svg
        assets/icons/export.svg
        i18n/de.json
        i18n/en.json
)

target_include_directories(ThermVane PRIVATE src)
target_compile_definitions(ThermVane PRIVATE
    THERMVANE_VERSION="${PROJECT_VERSION}"
)
if(THERMVANE_ALLOW_UNLEASED_DIRECT_SMC)
    target_compile_definitions(ThermVane PRIVATE THERMVANE_ALLOW_UNLEASED_DIRECT_SMC=1)
endif()

target_link_libraries(ThermVane PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)

if(APPLE)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(ThermVane PRIVATE
        ${IOKIT_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
    )

    add_executable(ThermVaneFanHelper
        src/helper/FanSpeedHelper.cpp
    )
    target_link_libraries(ThermVaneFanHelper PRIVATE
        ${IOKIT_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
    )
endif()

install(TARGETS ThermVane
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

if(APPLE)
    install(TARGETS ThermVaneFanHelper
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
endif()

if(THERMVANE_ENABLE_NBFC_LINUX_BUILD AND LINUX)
    add_custom_target(nbfc_linux_build
        COMMAND "${CMAKE_COMMAND}" -E env bash "${CMAKE_SOURCE_DIR}/scripts/build-nbfc-linux.sh"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
        COMMENT "Build NBFC from the external/nbfc submodule for Linux")
endif()

if(THERMVANE_ENABLE_NBFC_WINDOWS_BUILD AND WIN32)
    add_custom_target(nbfc_windows_build
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/external/nbfc/build.ps1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/external/nbfc"
        USES_TERMINAL
        COMMENT "Build NBFC from the external/nbfc submodule for Windows")
endif()

include(cmake/BuildIfwInstaller.cmake)
