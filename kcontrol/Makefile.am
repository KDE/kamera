kde_module_LTLIBRARIES = kcm_kamera.la

kcm_kamera_la_SOURCES = kamera.cpp kameradevice.cpp kameraconfigdialog.cpp

kcm_kamera_la_LDFLAGS  = $(all_libraries) -module $(KDE_PLUGIN)
kcm_kamera_la_LIBADD = $(LIB_KIO) $(GPHOTO2_LIBS)
INCLUDES= $(all_includes) $(GPHOTO2_INCS)

kcm_kamera_la_METASOURCES = AUTO

noinst_HEADERS = kamera.h kameradevice.h kameraconfigdialog.h

messages:	
	$(XGETTEXT) $(kcm_kamera_la_SOURCES) -o $(podir)/kcmkamera.pot

xdg_apps_DATA = kamera.desktop
