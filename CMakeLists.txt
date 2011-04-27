
add_definitions (-DQT3_SUPPORT -DQT3_SUPPORT_WARNINGS)

find_package(KDE4 REQUIRED)
include(KDE4Defaults)
include(MacroLibrary)
include(MacroOptionalAddSubdirectory)


find_package(Gphoto2)
macro_log_feature(GPHOTO2_FOUND "gphoto2" "Required to build kamera" "http://heanet.dl.sourceforge.net/sourceforge/gphoto" TRUE "" "")


configure_file(config-kamera.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kamera.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_definitions (${QT_DEFINITIONS} ${QT_QTDBUS_DEFINITIONS} ${KDE4_DEFINITIONS})
include_directories (${QDBUS_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${KDE4_INCLUDES})

include(ConfigureChecks.cmake)

add_subdirectory( kcontrol ) 
add_subdirectory( kioslave ) 
add_subdirectory( doc )

install(FILES
  solid_camera.desktop
  DESTINATION ${DATA_INSTALL_DIR}/solid/actions)

macro_display_feature_log()
