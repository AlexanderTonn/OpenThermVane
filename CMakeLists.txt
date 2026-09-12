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
    src/hardware/linux/LinuxHardwareBackend.cpp
    src/hardware/linux/LinuxHardwareBackend.hpp
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

target_link_libraries(ThermVane PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

install(TARGETS ThermVane
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
