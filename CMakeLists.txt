cmake_minimum_required(VERSION 3.16.0)
project(kamera)

# KDE Application Version, managed by release script
set (RELEASE_SERVICE_VERSION_MAJOR "21")
set (RELEASE_SERVICE_VERSION_MINOR "11")
set (RELEASE_SERVICE_VERSION_MICRO "70")
set(RELEASE_SERVICE_VERSION "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}.${RELEASE_SERVICE_VERSION_MICRO}")
set(PROJECT_VERSION ${RELEASE_SERVICE_VERSION})

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings)
include(FindPkgConfig)
include(FeatureSummary)

set(KF5_MIN_VERSION    "5.85.0")

find_package(Qt5 5.15 REQUIRED COMPONENTS Core)
find_package(KF5 ${KF5_MIN_VERSION} REQUIRED COMPONENTS
    ConfigWidgets
    XmlGui
    KIO
    I18n
    Config
    DocTools)

include(CMakePushCheckState)
include(CheckSymbolExists)

# Find gphoto2 library
find_package(Gphoto2 REQUIRED)
# Check version >= 2.5.0
cmake_push_check_state()
set(CMAKE_REQUIRED_INCLUDES "${GPHOTO2_INCLUDE_DIRS}")
set(CMAKE_REQUIRED_LIBRARIES "${GPHOTO2_LIBRARIES}")
check_symbol_exists(gp_port_info_get_name "gphoto2.h" HAVE_GPHOTO2_5)
cmake_pop_check_state()

if(HAVE_GPHOTO2_5)
    message(STATUS "Detected libgphoto2 2.5.0 or newer")
else()
    message(STATUS "Detected libgphoto2 2.4 or older")
endif()

include(ConfigureChecks.cmake)

configure_file(config-kamera.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kamera.h)

set(CMAKE_CXX_FLAGS_DEBUG, "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wunused -Wextra -DDEBUG_KAMERA_KIO -DDEBUG_KAMERA_KCONTROL")

add_subdirectory( kcontrol )
add_subdirectory( kioslave )
add_subdirectory( doc )

install(FILES
    solid_camera.desktop
    DESTINATION ${DATA_INSTALL_DIR}/solid/actions)

install(FILES
    org.kde.kamera.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR})

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
