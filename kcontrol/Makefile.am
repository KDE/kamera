kde_module_LTLIBRARIES = kcm_kamera.la

kcm_kamera_la_SOURCES = kamera.cpp kameradevice.cpp kameraconfigdialog.cpp

kcm_kamera_la_LDFLAGS  = $(all_libraries) -module -avoid-version -no-undefined
kcm_kamera_la_LIBADD = $(LIB_KDEUI) $(LIB_KIO) $(GPHOTO2_LIBS)
INCLUDES= $(all_includes) $(GPHOTO2_INCS)

kcm_kamera_la_METASOURCES = kamera.moc kameradevice.moc kameraconfigdialog.moc

noinst_HEADERS = kamera.h kameradevice.h kameraconfigdialog.h

messages:	
	$(XGETTEXT) $(kcm_kamera_la_SOURCES) -o $(podir)/kcmkamera.pot

data_DATA = kamera.desktop
datadir = $(kde_appsdir)/Settings/Peripherals
