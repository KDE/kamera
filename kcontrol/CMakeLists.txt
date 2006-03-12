kde4_header()

include_directories( ${KDE4_INCLUDE_DIR} ${QT_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}  )


########### next target ###############

set(kcm_kamera_PART_SRCS kamera.cpp kameradevice.cpp kameraconfigdialog.cpp )

kde4_automoc(${kcm_kamera_PART_SRCS})

kde4_add_plugin(kcm_kamera ${kcm_kamera_PART_SRCS})

kde4_install_libtool_file( ${PLUGIN_INSTALL_DIR} kcm_kamera )

target_link_libraries(kcm_kamera  ${QT_AND_KDECORE_LIBS} kio )

install_targets(${LIB_INSTALL_DIR}/kde4 kcm_kamera )


########### install files ###############

install_files( ${XDG_APPS_DIR} FILES kamera.desktop )

kde4_footer()



#original Makefile.am contents follow:

#kde_module_LTLIBRARIES = kcm_kamera.la
#
#kcm_kamera_la_SOURCES = kamera.cpp kameradevice.cpp kameraconfigdialog.cpp
#
#kcm_kamera_la_LDFLAGS  = $(all_libraries) -module $(KDE_PLUGIN)
#kcm_kamera_la_LIBADD = $(LIB_KIO) $(GPHOTO2_LIBS)
#INCLUDES= $(all_includes) $(GPHOTO2_INCS)
#
#kcm_kamera_la_METASOURCES = AUTO
#
#noinst_HEADERS = kamera.h kameradevice.h kameraconfigdialog.h
#
#messages:	
#	$(XGETTEXT) $(kcm_kamera_la_SOURCES) -o $(podir)/kcmkamera.pot
#
#xdg_apps_DATA = kamera.desktop
