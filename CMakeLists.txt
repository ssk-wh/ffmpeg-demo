cmake_minimum_required(VERSION 3.13)

project(ffmpeg-demo LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(DESKTOP_RECORDER_BIN_NAME desktop_recorder)
set(CAMERA_RECORDER_BIN_NAME camera_recorder)
set(CODEC_DETECTOR_BIN_NAME codec_detector)
set(MIC_RECORDER_BIN_NAME mic_recorder)
set(SPEAKER_RECORDER_BIN_NAME speaker_recorder)
set(AUDIO_RECORDER_BIN_NAME audio_recorder)
set(LIST_DEVICES_BIN_NAME list_devices)
set(RGB_TO_YUV_BIN_NAME rgb_to_yuv)

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
    0_desktop_recorder.c
    )

# 桌面内容录制
add_executable(${CAMERA_RECORDER_BIN_NAME}
    1_camera_recorder.c
    )

# 视频编码格式检测
add_executable(${CODEC_DETECTOR_BIN_NAME}
    2_codec_detector.c
    )

# 麦克风录制
add_executable(${MIC_RECORDER_BIN_NAME}
    3_mic_recorder.c
    )

# 扬声器录制
add_executable(${SPEAKER_RECORDER_BIN_NAME}
    4_speaker_recorder.c
    )

# 音频录制
add_executable(${AUDIO_RECORDER_BIN_NAME}
    5_audio_recorder.c
    )

# 音视频设备罗列
add_executable(${LIST_DEVICES_BIN_NAME}
    6_list_devices.c
    )

# rgb 图像转 yuv420p
add_executable(${RGB_TO_YUV_BIN_NAME}
    7_rgb_to_yuv.c
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

target_link_libraries(${CODEC_DETECTOR_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::X11
    )

target_link_libraries(${MIC_RECORDER_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVDEVICE
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWRESAMPLE
    )

target_link_libraries(${SPEAKER_RECORDER_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVDEVICE
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWRESAMPLE
    )

target_link_libraries(${AUDIO_RECORDER_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVDEVICE
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWRESAMPLE
    )

target_link_libraries(${LIST_DEVICES_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVDEVICE
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    )

target_link_libraries(${RGB_TO_YUV_BIN_NAME}
    PkgConfig::AVCODEC
    PkgConfig::AVDEVICE
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    )

install(TARGETS ${DESKTOP_RECORDER_BIN_NAME} DESTINATION bin)
install(TARGETS ${CAMERA_RECORDER_BIN_NAME} DESTINATION bin)
install(TARGETS ${CODEC_DETECTOR_BIN_NAME} DESTINATION bin)
install(TARGETS ${MIC_RECORDER_BIN_NAME} DESTINATION bin)
install(TARGETS ${SPEAKER_RECORDER_BIN_NAME} DESTINATION bin)
install(TARGETS ${AUDIO_RECORDER_BIN_NAME} DESTINATION bin)
install(TARGETS ${LIST_DEVICES_BIN_NAME} DESTINATION bin)
install(TARGETS ${RGB_TO_YUV_BIN_NAME} DESTINATION bin)