

include_directories(${GPHOTO2_INCLUDE_DIRS})


########### next target ###############

set(kio_kamera_PART_SRCS kamera.cpp )

kde4_automoc(${kio_kamera_PART_SRCS})

kde4_add_plugin(kio_kamera ${kio_kamera_PART_SRCS})


target_link_libraries(kio_kamera  ${KDE4_KIO_LIBS} ${GPHOTO2_LIBRARIES}  )

install(TARGETS kio_kamera  DESTINATION ${PLUGIN_INSTALL_DIR} )


########### install files ###############

install( FILES camera.protocol  DESTINATION  ${SERVICES_INSTALL_DIR} )
