add_definitions(-DTRANSLATION_DOMAIN=\"kio5_kamera\")

add_library(kio_kamera MODULE kamera.cpp )

target_include_directories(kio_kamera PRIVATE
    ${GPHOTO2_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
)

ecm_qt_declare_logging_category(kio_kamera
    HEADER kio_kamera_log.h
    IDENTIFIER KIO_KAMERA_LOG
    CATEGORY_NAME kf.kio.workers.camera
    DESCRIPTION "Kamera KIO worker"
    EXPORT KAMERA
)

target_link_libraries(kio_kamera
    Qt${QT_MAJOR_VERSION}::Core
    KF5::KIOCore
    KF5::I18n
    KF5::ConfigCore
    ${GPHOTO2_LIBRARIES}
    )

install(TARGETS kio_kamera  DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf${QT_MAJOR_VERSION}/kio )
