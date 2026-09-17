cmake_minimum_required(VERSION 3.24)

project(OpenThermVane
    VERSION 0.1.0
    DESCRIPTION "Cross-platform thermal and fan control"
    LANGUAGES CXX
)

get_property(THERMVANE_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT THERMVANE_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 LinguistTools)

qt_standard_project_setup(REQUIRES 6.5)

if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

option(THERMVANE_ENABLE_NBFC_LINUX_BUILD "Add a target that builds the NBFC submodule on Linux" ON)
option(THERMVANE_ENABLE_NBFC_WINDOWS_BUILD "Add a target that builds the NBFC submodule on Windows" ON)

set(THERMVANE_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/Application.hpp
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

set(THERMVANE_TRANSLATIONS
    translations/thermvane_de.ts
    translations/thermvane_en.ts
)

set(THERMVANE_PROJECT_FILES
    ThermVane.qmlproject
    qtquickcontrols2.conf
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
    cmake/BuildIfwInstaller.cmake
    cmake/PackageIfw.cmake
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${THERMVANE_SOURCES}
    ${THERMVANE_QML_FILES}
    ${THERMVANE_TRANSLATIONS}
    ${THERMVANE_PROJECT_FILES}
)

qt_add_executable(ThermVane
    ${THERMVANE_SOURCES}
    ${THERMVANE_PROJECT_FILES}
)

if(WIN32)
    set_target_properties(ThermVane PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

qt_add_qml_module(ThermVane
    URI OpenThermVane
    VERSION 1.0
    QML_FILES
        ${THERMVANE_QML_FILES}
)

qt_add_translations(ThermVane
    TS_FILES
        ${THERMVANE_TRANSLATIONS}
    RESOURCE_PREFIX /i18n
)

target_include_directories(ThermVane PRIVATE src)
target_compile_definitions(ThermVane PRIVATE
    THERMVANE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(ThermVane PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
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
