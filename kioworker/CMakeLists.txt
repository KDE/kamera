add_definitions(-DTRANSLATION_DOMAIN=\"kio6_kamera\")

add_library(kio_kamera MODULE kamera.cpp kameralist.cpp)

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
    Qt6::Core
    KF6::KIOCore
    KF6::I18n
    KF6::ConfigCore
    ${GPHOTO2_LIBRARIES}
    )

install(TARGETS kio_kamera  DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/kio )
