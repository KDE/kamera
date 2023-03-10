cmake_minimum_required(VERSION 3.16.0)

# KDE Application Version, managed by release script
set (RELEASE_SERVICE_VERSION_MAJOR "23")
set (RELEASE_SERVICE_VERSION_MINOR "07")
set (RELEASE_SERVICE_VERSION_MICRO "70")
set(RELEASE_SERVICE_VERSION "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}.${RELEASE_SERVICE_VERSION_MICRO}")
set(PROJECT_VERSION ${RELEASE_SERVICE_VERSION})

project(kamera VERSION ${PROJECT_VERSION})

set(QT_MIN_VERSION "5.15.2")
set(KF5_MIN_VERSION    "5.96.0")
find_package(ECM ${KF5_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

include(FindPkgConfig)
include(FeatureSummary)
include(ECMDeprecationSettings)
if (QT_MAJOR_VERSION STREQUAL "6")
    set(QT_REQUIRED_VERSION "6.4.0")
    set(KF_MIN_VERSION "5.240.0")
    set(KF_MAJOR_VERSION "6")
else()
    set(KF_MAJOR_VERSION "5")
endif()
include(ECMQtDeclareLoggingCategory)

find_package(Qt${QT_MAJOR_VERSION} ${QT_MIN_VERSION} REQUIRED COMPONENTS Core)
if (QT_MAJOR_VERSION STREQUAL "6")
    find_package(Qt6Core5Compat REQUIRED ${QT_MIN_VERSION})
endif()

find_package(KF${KF_MAJOR_VERSION} ${KF5_MIN_VERSION} REQUIRED COMPONENTS
    ConfigWidgets
    XmlGui
    KIO
    I18n
    Config
    KCMUtils
    )

find_package(KF${KF_MAJOR_VERSION}DocTools ${KF5_MIN_VERSION})
set_package_properties(KF${KF_MAJOR_VERSION}DocTools PROPERTIES DESCRIPTION
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

ecm_set_disabled_deprecation_versions(QT 5.15.2
    KF 5.101.0
)


add_subdirectory( kcontrol )
add_subdirectory( kioworker )

ki18n_install(po)
if (KF5DocTools_FOUND)
    kdoctools_install(po)
    add_subdirectory(doc)
endif()


install(FILES
    solid_camera.desktop
    DESTINATION ${KDE_INSTALL_DATADIR}/solid/actions)

install(FILES
    org.kde.kamera.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR})

ecm_qt_install_logging_categories(
    EXPORT KAMERA
    FILE kamera.categories
    DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR}
)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
