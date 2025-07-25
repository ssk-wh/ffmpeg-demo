# 说明

此项目旨在演示 Linux 下使用 FFmpeg 录制桌面和摄像头视频，并进行简单的编码处理及打包为 deb 软件包。

# 项目结构
```
├── README.md
├── audio_recorder.c      # 音频录制程序
├── camera_recorder.c     # 摄像头录制程序
├── desktop_recorder.c    # 桌面录制程序
└── detector.c            # 视频文件编码格式检测程序
```
# 编译

打开终端，进入项目源码目录后
```bash
mkdir build
cd build
cmake ..
make -j8
```
编译完成后会在 build 目录中生成以下可执行文件：

desktop_recorder：运行 desktop_recorder.c 编译后的主程序，用于桌面内容录制。
camera_recorder：运行 camera_recorder.c 编译后的主程序,用于摄像头内容录制。
audio_recorder：运行 audio_recorder.c 编译后的主程序，用于音频内容录制。
detector：用于检测视频文件的编码格式。

使用 conan 编译
``` bash
conan profile detect --force
conan install . --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```


# 运行

音频录制
```bash
./audio_recorder <-t=time>
```
示例（录制音频，默认时长 5 s）：

```bash
./audio_recorder -t=5
```
输出文件将保存为当前目录下的 audio_output.wav

桌面录制
```bash
./desktop_recorder <seconds>
```
示例（录制 10 秒）：

```bash
./desktop_recorder 10
输出文件将保存为当前目录下的 desktop_output.mp4。
```
摄像头录制
```bash
./camera_recorder <-f=format>
```
示例（录制 flv 视频，默认 5 s）：

```bash
./camera_recorder -f=flv
```
输出文件将保存为当前目录下的 camera_output.flv。

编码格式检测
```bash
./detector <input_file>
```
示例：

```bash
./detector output.mp4
```

# 打包

进入源码目录，运行以下命令生成 deb 软件包：
```bash
dpkg-buildpackage -us -uc -nc -j8
```
生成的 deb 包将在源码目录的上一级目录中。

# TODO
> 计划发展为 ffmpeg 的教学项目
> 支持 windows/mac
> 支持罗列设备信息(麦克风、扬声器、摄像头、屏幕)
> 定位 flv 录制中断后视频无法播放的问题
> 优化桌面录制性能，linux 下通过 drm 直接获取帧数据，以及优化转码算法，并对比优化后的性能差异