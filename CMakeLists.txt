cmake_minimum_required(VERSION 3.13)

project(ffmpeg-demo LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(RECORDER_BIN_NAME recorder)
set(DETECTOR_BIN_NAME detector)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr)
endif ()
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)

add_executable(${RECORDER_BIN_NAME}
    recorder.c
    )
add_executable(${DETECTOR_BIN_NAME}
    detector.c
    )

target_link_libraries(${RECORDER_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::X11
    )

target_link_libraries(${DETECTOR_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::X11
    )

install(TARGETS ${RECORDER_BIN_NAME} DESTINATION bin)
install(TARGETS ${DETECTOR_BIN_NAME} DESTINATION bin)