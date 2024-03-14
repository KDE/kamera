cmake_minimum_required(VERSION 3.16.0)

# KDE Application Version, managed by release script
set (RELEASE_SERVICE_VERSION_MAJOR "24")
set (RELEASE_SERVICE_VERSION_MINOR "04")
set (RELEASE_SERVICE_VERSION_MICRO "70")
set(RELEASE_SERVICE_VERSION "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}.${RELEASE_SERVICE_VERSION_MICRO}")
set(PROJECT_VERSION ${RELEASE_SERVICE_VERSION})

project(kamera VERSION ${PROJECT_VERSION})

set(QT_MIN_VERSION "6.5.0")
set(KF_MIN_VERSION    "5.240.0")
find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDEClangFormat)
include(KDEGitCommitHooks)

include(FindPkgConfig)
include(FeatureSummary)
include(ECMDeprecationSettings)
include(ECMQtDeclareLoggingCategory)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    ConfigWidgets
    XmlGui
    KIO
    I18n
    Config
    KCMUtils
    )

find_package(KF6DocTools ${KF_MIN_VERSION})
set_package_properties(KF6DocTools PROPERTIES DESCRIPTION
    "Tools to generate documentation"
    TYPE OPTIONAL
)

include(CMakePushCheckState)
include(CheckSymbolExists)

# Find gphoto2 library
find_package(Gphoto2 2.5.0 REQUIRED)

set(CMAKE_CXX_FLAGS_DEBUG, "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wunused -Wextra")
remove_definitions( -DQT_NO_CAST_FROM_ASCII )
remove_definitions( -DQT_NO_CAST_FROM_BYTEARRAY)

ecm_set_disabled_deprecation_versions(QT 6.6.0
    KF 6.0.0
)


add_subdirectory( kcontrol )
add_subdirectory( kioworker )

ki18n_install(po)
if (KF6DocTools_FOUND)
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

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES *.cpp *.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)
