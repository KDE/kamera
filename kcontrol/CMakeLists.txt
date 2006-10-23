add_definitions(${GPHOTO2_INCLUDE_DIRS})

########### next target ###############

set(kcm_kamera_PART_SRCS kamera.cpp kameradevice.cpp kameraconfigdialog.cpp )

kde4_automoc(${kcm_kamera_PART_SRCS})

kde4_add_plugin(kcm_kamera ${kcm_kamera_PART_SRCS})

kde4_install_libtool_file( ${PLUGIN_INSTALL_DIR} kcm_kamera )

target_link_libraries(kcm_kamera  ${KDE4_KIO_LIBS} ${GPHOTO2_LIBRARIES})

install(TARGETS kcm_kamera  DESTINATION ${PLUGIN_INSTALL_DIR} )


########### install files ###############

install( FILES kamera.desktop  DESTINATION  ${SERVICES_INSTALL_DIR} )

