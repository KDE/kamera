
########### next target ###############

add_definitions(-DTRANSLATION_DOMAIN=\"kio5_kamera\")

set(kio_kamera_PART_SRCS kamera.cpp )

add_library(kio_kamera MODULE ${kio_kamera_PART_SRCS})

include_directories(${GPHOTO2_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})

target_link_libraries(kio_kamera
    Qt::Core
    KF5::KIOCore
    KF5::I18n
    KF5::ConfigCore
    ${GPHOTO2_LIBRARIES}
    )

install(TARGETS kio_kamera  DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf5/kio )
