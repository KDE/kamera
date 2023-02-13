add_definitions(-DTRANSLATION_DOMAIN=\"kcmkamera\")

kcoreaddons_add_plugin(kcm_kamera
    SOURCES
        kamera.cpp
        kameradevice.cpp
        kameraconfigdialog.cpp
        kameradevice.h
        kameraconfigdialog.h
    INSTALL_NAMESPACE "plasma/kcms/systemsettings_qwidgets"
)

kcmutils_generate_desktop_file(kcm_kamera)

target_include_directories(kcm_kamera PRIVATE
    ${GPHOTO2_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
)

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
