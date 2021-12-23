cmake_minimum_required(VERSION 3.16.0)
project(kamera)

# KDE Application Version, managed by release script
set (RELEASE_SERVICE_VERSION_MAJOR "22")
set (RELEASE_SERVICE_VERSION_MINOR "03")
set (RELEASE_SERVICE_VERSION_MICRO "70")
set(RELEASE_SERVICE_VERSION "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}.${RELEASE_SERVICE_VERSION_MICRO}")
set(PROJECT_VERSION ${RELEASE_SERVICE_VERSION})

set(KF5_MIN_VERSION    "5.85.0")
find_package(ECM ${KF5_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings)
include(FindPkgConfig)
include(FeatureSummary)


find_package(Qt5 5.15 REQUIRED COMPONENTS Core)
find_package(KF5 ${KF5_MIN_VERSION} REQUIRED COMPONENTS
    ConfigWidgets
    XmlGui
    KIO
    I18n
    Config
    )

find_package(KF5DocTools ${KF5_MIN_VERSION})
set_package_properties(KF5DocTools PROPERTIES DESCRIPTION
    "Tools to generate documentation"
    TYPE OPTIONAL
)

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

set(CMAKE_CXX_FLAGS_DEBUG, "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wunused -Wextra")
remove_definitions( -DQT_NO_CAST_FROM_ASCII )
remove_definitions( -DQT_NO_CAST_FROM_BYTEARRAY)
add_definitions(
    -DQT_DISABLE_DEPRECATED_BEFORE=0x050F00
    -DQT_DEPRECATED_WARNINGS_SINCE=0x060000
    -DKF_DISABLE_DEPRECATED_BEFORE_AND_AT=0x055800
    -DKF_DEPRECATED_WARNINGS_SINCE=0x060000
)

add_subdirectory( kcontrol )
add_subdirectory( kioslave )

ki18n_install(po)
if (KF5DocTools_FOUND)
    kdoctools_install(po)
    add_subdirectory(doc)
endif()


install(FILES
    solid_camera.desktop
    DESTINATION ${DATA_INSTALL_DIR}/solid/actions)

install(FILES
    org.kde.kamera.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR})

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
