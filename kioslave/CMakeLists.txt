kde4_header()

include_directories( ${KDE4_INCLUDE_DIR} ${QT_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}  )

add_definitions(${GPHOTO2_INCLUDE_DIRS})

########### next target ###############

set(kio_kamera_PART_SRCS kamera.cpp )

kde4_automoc(${kio_kamera_PART_SRCS})

kde4_add_plugin(kio_kamera ${kio_kamera_PART_SRCS})

kde4_install_libtool_file( ${PLUGIN_INSTALL_DIR} kio_kamera )

target_link_libraries(kio_kamera  ${KDE4_KIO_LIBS} ${GPHOTO2_LIBRARIES}  )

install_targets(${PLUGIN_INSTALL_DIR} kio_kamera )


########### install files ###############

install_files( ${SERVICES_INSTALL_DIR} FILES camera.protocol )

kde4_footer()



#original Makefile.am contents follow:

## $Id: Makefile.am 466286 2005-10-02 08:09:51Z coolo $
## Makefile for kdebase/kioslave/kamera
#
#INCLUDES= -I$(srcdir)/../.. -I$(srcdir)/.. $(all_includes) $(GPHOTO2_INCS)
#
######## Files
#
#kde_module_LTLIBRARIES = kio_kamera.la
#
#kio_kamera_la_SOURCES = kamera.cpp
#kio_kamera_la_LIBADD  = $(LIB_KIO) -lgphoto2
#kio_kamera_la_LDFLAGS = $(all_libraries) $(GPHOTO2_LIBS) -module $(KDE_PLUGIN)
#
#noinst_HEADERS = kamera.h
#
#kde_services_DATA = camera.protocol
#
