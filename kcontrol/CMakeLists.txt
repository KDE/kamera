
########### next target ###############

add_definitions(-DTRANSLATION_DOMAIN=\"kcmkamera\")

set(kcm_kamera_PART_SRCS kamera.cpp kameradevice.cpp kameraconfigdialog.cpp )

kcoreaddons_add_plugin(kcm_kamera SOURCES ${kcm_kamera_PART_SRCS} INSTALL_NAMESPACE "plasma/kcms/systemsettings_qwidgets")

include_directories(${GPHOTO2_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})

ecm_qt_declare_logging_category(kcm_kamera
    HEADER kcm_kamera_log.h
    IDENTIFIER KAMERA_KCONTROL
    CATEGORY_NAME kamera.kcm
    DESCRIPTION "Kamera KCM"
    EXPORT KAMERA
)

target_link_libraries(kcm_kamera
    KF5::ConfigWidgets
    KF5::XmlGui
    KF5::KIOCore
    KF5::I18n
    KF5::ConfigCore
    ${GPHOTO2_LIBRARIES}
    )
set_target_properties(kcm_kamera PROPERTIES OUTPUT_NAME kamera)

install( FILES kamera.desktop  DESTINATION  ${KDE_INSTALL_APPDIR} )

