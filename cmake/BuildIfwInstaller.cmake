set(THERMVANE_IFW_OUTPUT_DIR "${CMAKE_BINARY_DIR}/ifw" CACHE PATH "QtIFW output directory")
set(THERMVANE_NBFC_INSTALLER "" CACHE FILEPATH "Optional NBFC .msi or .exe payload for the QtIFW Windows installer")
set(THERMVANE_IFW_BINARYCREATOR "binarycreator" CACHE FILEPATH "Qt Installer Framework binarycreator executable")
set(THERMVANE_QT_ROOT "" CACHE PATH "Qt kit root used by platform deployment tools")
set(THERMVANE_MACOS_DMG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/macos-dmg" CACHE PATH "macOS DMG output directory")

if(CMAKE_CONFIGURATION_TYPES)
    set(THERMVANE_IFW_INSTALL_CONFIG "$<CONFIG>")
else()
    set(THERMVANE_IFW_INSTALL_CONFIG "${CMAKE_BUILD_TYPE}")
endif()

set(THERMVANE_IFW_DEPENDS ThermVane)
if(APPLE)
    list(APPEND THERMVANE_IFW_DEPENDS ThermVaneFanHelper)
endif()

add_custom_target(package_ifw
    COMMAND "${CMAKE_COMMAND}"
        -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -D "BINARY_DIR=${CMAKE_BINARY_DIR}"
        -D "CMAKE_INSTALL_CONFIG_NAME=${THERMVANE_IFW_INSTALL_CONFIG}"
        -D "IFW_OUTPUT_DIR=${THERMVANE_IFW_OUTPUT_DIR}"
        -D "NBFC_INSTALLER=${THERMVANE_NBFC_INSTALLER}"
        -D "BINARYCREATOR=${THERMVANE_IFW_BINARYCREATOR}"
        -D "QT_ROOT=${THERMVANE_QT_ROOT}"
        -P "${CMAKE_SOURCE_DIR}/cmake/PackageIfw.cmake"
    DEPENDS ${THERMVANE_IFW_DEPENDS}
    USES_TERMINAL
    COMMENT "Build ThermVane Qt Installer Framework package")

if(APPLE)
    add_custom_target(package_macos_dmg
        COMMAND "${CMAKE_COMMAND}"
            -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -D "BINARY_DIR=${CMAKE_BINARY_DIR}"
            -D "CMAKE_INSTALL_CONFIG_NAME=${THERMVANE_IFW_INSTALL_CONFIG}"
            -D "DMG_OUTPUT_DIR=${THERMVANE_MACOS_DMG_OUTPUT_DIR}"
            -D "QT_ROOT=${THERMVANE_QT_ROOT}"
            -D "PROJECT_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageMacDmg.cmake"
        DEPENDS ThermVane ThermVaneFanHelper
        USES_TERMINAL
        COMMENT "Build ThermVane native macOS DMG package")
endif()
