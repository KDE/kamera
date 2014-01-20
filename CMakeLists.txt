project(kamera)

find_package(KDE4 REQUIRED)
include(CMakePushCheckState)
include(CheckSymbolExists)
include(KDE4Defaults)
include(MacroLibrary)
include(MacroOptionalAddSubdirectory)

find_package(Gphoto2)
macro_log_feature(GPHOTO2_FOUND "gphoto2" "Required to build kamera" "http://heanet.dl.sourceforge.net/sourceforge/gphoto" TRUE "" "")

cmake_push_check_state()
set(CMAKE_REQUIRED_INCLUDES "${GPHOTO2_INCLUDE_DIR}")
set(CMAKE_REQUIRED_LIBRARIES "${GPHOTO2_LIBRARIES}")
check_symbol_exists(gp_port_info_get_name "gphoto2.h" HAVE_GPHOTO2_5)
cmake_pop_check_state()

if(HAVE_GPHOTO2_5)
    message(STATUS "Detected libgphoto 2.5.0 or newer")
else()
    message(STATUS "Detected libgphoto2 2.4 or older")
endif()

include(ConfigureChecks.cmake)

configure_file(config-kamera.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kamera.h)

add_definitions (${QT_DEFINITIONS} ${KDE4_DEFINITIONS})
add_definitions (-DQT_USE_FAST_CONCATENATION -DQT_USE_FAST_OPERATOR_PLUS)
include_directories (${CMAKE_CURRENT_BINARY_DIR} ${KDE4_INCLUDES} ${GPHOTO2_INCLUDE_DIRS})

add_subdirectory( kcontrol ) 
add_subdirectory( kioslave ) 
add_subdirectory( doc )

install(FILES
  solid_camera.desktop
  DESTINATION ${DATA_INSTALL_DIR}/solid/actions)

macro_display_feature_log()
