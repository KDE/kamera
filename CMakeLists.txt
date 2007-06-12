
add_definitions (-DQT3_SUPPORT -DQT3_SUPPORT_WARNINGS)

configure_file(config-kamera.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kamera.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory( kcontrol ) 
add_subdirectory( kioslave ) 
add_subdirectory( pics ) 

