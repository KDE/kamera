# $Id$
# Makefile for kdebase/kioslave/kamera

INCLUDES= -I$(srcdir)/../.. -I$(srcdir)/.. $(all_includes) $(GPHOTO2_INCS) $(GPIO_INCS)

####### Files

lib_LTLIBRARIES = kio_kamera.la

kio_kamera_la_SOURCES = kamera.cpp
kio_kamera_la_LIBADD  = $(LIB_KIO) -lgphoto2
kio_kamera_la_LDFLAGS = $(all_libraries) $(GPHOTO2_LIBS) -module -avoid-version

noinst_HEADERS = kamera.h

kdelnk_DATA = kamera.protocol
kdelnkdir = $(kde_servicesdir)

check_PROGRAMS = linktest
linktest_SOURCES = $(kio_kamera_la_SOURCES) linktest.cpp
linktest.cpp:
	echo "int main() {};" > linktest.cpp
linktest_LDFLAGS  = $(all_libraries) -module -avoid-version
linktest_LDADD = $(kio_kamera_la_LIBADD)
