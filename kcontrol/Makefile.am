lib_LTLIBRARIES = libkcm_kamera.la

libkcm_kamera_la_SOURCES = kamera.cpp kameradevice.cpp kameraconfigdialog.cpp

libkcm_kamera_la_LDFLAGS  = $(all_libraries) -module -avoid-version -no-undefined
libkcm_kamera_la_LIBADD = -lkdeui -lkfile $(GPHOTO2_LIBS) $(GPIO_LIBS)
INCLUDES= $(all_includes) $(GPHOTO2_INCS) $(GPIO_INCS)

libkcm_kamera_la_METASOURCES = kamera.moc kameradevice.moc kameraconfigdialog.moc

noinst_HEADERS = kamera.h kameradevice.h kameraconfigdialog.h

KDE_ICON = Kamera

messages:	
	$(XGETTEXT) $(libkcm_kamera_la_SOURCES) -o $(podir)/kcmkamera.pot

data_DATA = kamera.desktop
datadir = $(kde_appsdir)/Settings/Peripherals
