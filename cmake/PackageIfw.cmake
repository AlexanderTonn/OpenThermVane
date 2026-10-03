if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "SOURCE_DIR and BINARY_DIR are required")
endif()

if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME OR CMAKE_INSTALL_CONFIG_NAME STREQUAL "")
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
endif()

if(NOT DEFINED IFW_OUTPUT_DIR OR IFW_OUTPUT_DIR STREQUAL "")
    set(IFW_OUTPUT_DIR "${BINARY_DIR}/ifw")
endif()

set(INSTALL_ROOT "${IFW_OUTPUT_DIR}/install-root")
set(PACKAGES_ROOT "${IFW_OUTPUT_DIR}/packages")
set(APP_DATA_DIR "${PACKAGES_ROOT}/org.openthermvane.app/data")
set(NBFC_DATA_DIR "${PACKAGES_ROOT}/org.openthermvane.nbfc/data/nbfc")

file(REMOVE_RECURSE "${INSTALL_ROOT}" "${PACKAGES_ROOT}")
file(MAKE_DIRECTORY "${IFW_OUTPUT_DIR}" "${APP_DATA_DIR}" "${NBFC_DATA_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --config "${CMAKE_INSTALL_CONFIG_NAME}" --prefix "${INSTALL_ROOT}"
    RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed")
endif()

find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt windeployqt.exe
    HINTS "${QT_ROOT}/bin")
if(WIN32 AND NOT WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "windeployqt not found. Set THERMVANE_QT_ROOT or add Qt bin directory to PATH.")
endif()
if(WIN32)
    execute_process(
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" "${INSTALL_ROOT}/bin/ThermVane.exe"
            --qmldir "${SOURCE_DIR}/qml"
            --compiler-runtime
        RESULT_VARIABLE deploy_result)
    if(NOT deploy_result EQUAL 0)
        message(FATAL_ERROR "windeployqt failed")
    endif()

    if(QT_ROOT)
        foreach(runtime_dll IN ITEMS libc++.dll libunwind.dll)
            if(EXISTS "${QT_ROOT}/bin/${runtime_dll}")
                file(COPY_FILE
                    "${QT_ROOT}/bin/${runtime_dll}"
                    "${INSTALL_ROOT}/bin/${runtime_dll}")
            endif()
        endforeach()
    endif()
endif()

if(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE
        NAMES macdeployqt
        HINTS "${QT_ROOT}/bin")
    if(NOT MACDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "macdeployqt not found. Set THERMVANE_QT_ROOT or add Qt macOS bin directory to PATH.")
    endif()

    set(THERMVANE_APP_BUNDLE "${INSTALL_ROOT}/ThermVane.app")
    if(NOT EXISTS "${THERMVANE_APP_BUNDLE}")
        message(FATAL_ERROR "ThermVane.app was not installed to ${THERMVANE_APP_BUNDLE}")
    endif()

    execute_process(
        COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${THERMVANE_APP_BUNDLE}"
            "-qmldir=${SOURCE_DIR}/qml"
        RESULT_VARIABLE deploy_result)
    if(NOT deploy_result EQUAL 0)
        message(FATAL_ERROR "macdeployqt failed")
    endif()

    file(REMOVE_RECURSE "${THERMVANE_APP_BUNDLE}/Contents/PlugIns/sqldrivers")
endif()

if(UNIX AND NOT APPLE)
    if(NOT QT_ROOT)
        message(FATAL_ERROR "THERMVANE_QT_ROOT must point to the Qt kit root for Linux runtime deployment.")
    endif()
    if(NOT EXISTS "${QT_ROOT}/lib")
        message(FATAL_ERROR "Qt lib directory not found at ${QT_ROOT}/lib")
    endif()

    file(MAKE_DIRECTORY
        "${INSTALL_ROOT}/lib"
        "${INSTALL_ROOT}/plugins"
        "${INSTALL_ROOT}/qml"
        "${INSTALL_ROOT}/bin")

    file(GLOB qt_runtime_libraries
        "${QT_ROOT}/lib/libQt6*.so"
        "${QT_ROOT}/lib/libQt6*.so.*")
    if(qt_runtime_libraries)
        file(COPY ${qt_runtime_libraries}
            DESTINATION "${INSTALL_ROOT}/lib"
            FOLLOW_SYMLINK_CHAIN)
    endif()

    if(EXISTS "${QT_ROOT}/plugins")
        file(COPY "${QT_ROOT}/plugins/" DESTINATION "${INSTALL_ROOT}/plugins")
    endif()
    if(EXISTS "${QT_ROOT}/qml")
        file(COPY "${QT_ROOT}/qml/" DESTINATION "${INSTALL_ROOT}/qml")
    endif()

    file(WRITE "${INSTALL_ROOT}/bin/qt.conf" "[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQml2Imports=qml\n")
endif()

file(COPY "${SOURCE_DIR}/installer/packages/" DESTINATION "${PACKAGES_ROOT}")
file(COPY "${INSTALL_ROOT}/" DESTINATION "${APP_DATA_DIR}")
if(APPLE)
    file(MAKE_DIRECTORY "${APP_DATA_DIR}/scripts/macos")
    file(COPY
        "${SOURCE_DIR}/scripts/macos/install-fan-helper.sh"
        "${SOURCE_DIR}/scripts/macos/uninstall-fan-helper.sh"
        DESTINATION "${APP_DATA_DIR}/scripts/macos")
    file(CHMOD
        "${APP_DATA_DIR}/scripts/macos/install-fan-helper.sh"
        "${APP_DATA_DIR}/scripts/macos/uninstall-fan-helper.sh"
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
endif()

if(WIN32 AND NBFC_INSTALLER)
    get_filename_component(nbfc_ext "${NBFC_INSTALLER}" EXT)
    string(TOLOWER "${nbfc_ext}" nbfc_ext)
    if(nbfc_ext STREQUAL ".msi")
        file(COPY_FILE "${NBFC_INSTALLER}" "${NBFC_DATA_DIR}/nbfc-installer.msi")
    elseif(nbfc_ext STREQUAL ".exe")
        file(COPY_FILE "${NBFC_INSTALLER}" "${NBFC_DATA_DIR}/nbfc-installer.exe")
    else()
        message(FATAL_ERROR "THERMVANE_NBFC_INSTALLER must point to a .msi or .exe")
    endif()
else()
    file(REMOVE_RECURSE "${PACKAGES_ROOT}/org.openthermvane.nbfc")
endif()

set(THERMVANE_INSTALLER_OUTPUT "${IFW_OUTPUT_DIR}/ThermVaneInstaller")
if(WIN32)
    set(THERMVANE_INSTALLER_OUTPUT "${IFW_OUTPUT_DIR}/ThermVaneInstaller.exe")
endif()

execute_process(
    COMMAND "${BINARYCREATOR}" --offline-only
        -c "${SOURCE_DIR}/installer/config/config.xml"
        -p "${PACKAGES_ROOT}"
        "${THERMVANE_INSTALLER_OUTPUT}"
    RESULT_VARIABLE ifw_result)
if(NOT ifw_result EQUAL 0)
    message(FATAL_ERROR "binarycreator failed")
endif()
