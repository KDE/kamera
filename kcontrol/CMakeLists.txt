add_definitions(-DTRANSLATION_DOMAIN=\"kcmkamera\")

kcoreaddons_add_plugin(kcm_kamera
    SOURCES
        kamera.cpp
        kamera.h
        kameradevice.cpp
        kameradevice.h
        kameraconfigdialog.cpp
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
    KF6::ConfigWidgets
    KF6::XmlGui
    KF6::KIOCore
    KF6::I18n
    KF6::ConfigCore
    KF6::KCMUtils
    ${GPHOTO2_LIBRARIES}
    )
