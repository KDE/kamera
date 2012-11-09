project(kamera)

find_package(KDE4 REQUIRED)
include(KDE4Defaults)
include(MacroLibrary)
include(MacroOptionalAddSubdirectory)


find_package(Gphoto2)
macro_log_feature(GPHOTO2_FOUND "gphoto2" "Required to build kamera" "http://heanet.dl.sourceforge.net/sourceforge/gphoto" TRUE "" "")

check_library_exists(gphoto2_port gp_port_info_get_name "" GPHOTO2_5)
if(GPHOTO2_5)
    message(STATUS "Found libgphoto 2.5")
else(GPHOTO2_5)
    message(STATUS "libgphoto 2.5 not found, assuming libgphoto2 2.4")
endif(GPHOTO2_5)
set(HAVE_GPHOTO2_5 ${GPHOTO2_5})

include(ConfigureChecks.cmake)



configure_file(config-kamera.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kamera.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_definitions (${QT_DEFINITIONS} ${QT_QTDBUS_DEFINITIONS} ${KDE4_DEFINITIONS})
add_definitions (-DQT_USE_FAST_CONCATENATION -DQT_USE_FAST_OPERATOR_PLUS)
include_directories (${QDBUS_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${KDE4_INCLUDES})

add_subdirectory( kcontrol ) 
add_subdirectory( kioslave ) 
add_subdirectory( doc )

install(FILES
  solid_camera.desktop
  DESTINATION ${DATA_INSTALL_DIR}/solid/actions)

macro_display_feature_log()
