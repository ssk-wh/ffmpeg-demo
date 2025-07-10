cmake_minimum_required(VERSION 3.13)

project(ffmpeg-demo LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(DESKTOP_RECORDER_BIN_NAME desktop_recorder)
set(CAMERA_RECORDER_BIN_NAME camera_recorder)
set(AUDIO_RECORDER_BIN_NAME audio_recorder)
set(CODEC_DETECTOR_BIN_NAME codec_detector)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr)
endif ()
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVDEVICE REQUIRED IMPORTED_TARGET libavdevice)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)

# 摄像头内容录制
add_executable(${DESKTOP_RECORDER_BIN_NAME}
    desktop_recorder.c
    )

# 桌面内容录制
add_executable(${CAMERA_RECORDER_BIN_NAME}
    camera_recorder.c
    )

# 音频录制
add_executable(${AUDIO_RECORDER_BIN_NAME}
    audio_recorder.c
    )

# 视频编码格式检测
add_executable(${CODEC_DETECTOR_BIN_NAME}
    codec_detector.c
    )

target_link_libraries(${DESKTOP_RECORDER_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::X11
    )

target_link_libraries(${CAMERA_RECORDER_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVDEVICE
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    )

target_link_libraries(${AUDIO_RECORDER_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVDEVICE
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWRESAMPLE
    )

target_link_libraries(${CODEC_DETECTOR_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::X11
    )

install(TARGETS ${DESKTOP_RECORDER_BIN_NAME} DESTINATION bin)
install(TARGETS ${CAMERA_RECORDER_BIN_NAME} DESTINATION bin)
install(TARGETS ${AUDIO_RECORDER_BIN_NAME} DESTINATION bin)
install(TARGETS ${CODEC_DETECTOR_BIN_NAME} DESTINATION bin)
