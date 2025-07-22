#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
    #define VIDEODEVICE "dshow"
    #define AUDIODEVICE "dshow"
#elif defined(__APPLE__)
    #define VIDEODEVICE "avfoundation"
    #define AUDIODEVICE "avfoundation"
#else
    #define VIDEODEVICE "x11grab"
    #define VIDEO_ALTERNATE "v4l2"
    #define AUDIODEVICE "pulse"
#endif

// 通用设备枚举函数
void list_devices(const char *device, const char *device_type) {
    AVDeviceInfoList *device_list = NULL;
    AVFormatContext *format_ctx = avformat_alloc_context();
    if (!format_ctx) {
        fprintf(stderr, "分配格式上下文失败\n");
        return;
    }
    
    const AVInputFormat *input_fmt = av_find_input_format(device);
    if (!input_fmt) {
        fprintf(stderr, "找不到输入格式: %s\n", device);
        avformat_free_context(format_ctx);
        return;
    }
    
    AVDictionary *options = NULL;
    int ret = avdevice_list_input_sources(input_fmt, NULL, options, &device_list);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "枚举设备失败: %s (%s)\n", device, errbuf);
        avformat_free_context(format_ctx);
        return;
    }
    
    printf("\n=== %s 设备 (%s) ===\n", device_type, device);
    
    if (device_list->nb_devices == 0) {
        printf("未找到设备\n");
    } else {
        for (int i = 0; i < device_list->nb_devices; i++) {
            AVDeviceInfo *dev = device_list->devices[i];
            printf("[设备 %d]: %s\n", i, dev->device_name);
            if (dev->device_description) {
                printf("  描述: %s\n", dev->device_description);
            }
            if (strlen(dev->device_name) > 0) {
                printf("  使用示例: ffmpeg -f %s -i \"%s\" output\n", device, dev->device_name);
            }
        }
    }
    
    avdevice_free_list_devices(&device_list);
    avformat_free_context(format_ctx);
}

// 平台特定的设备枚举
void list_platform_devices() {
    printf("\n==== 当前平台: ");
    
#ifdef _WIN32
    printf("Windows ====\n");
    list_devices("gdigrab", "屏幕捕获");
    list_devices("dshow", "摄像头");
    list_devices("dshow", "音频设备");
    
#elif defined(__APPLE__)
    printf("macOS ====\n");
    list_devices("avfoundation", "屏幕/摄像头/音频设备");
    
#else
    printf("Linux ====\n");
    // list_devices("x11grab", "屏幕捕获");
    list_devices("v4l2", "摄像头");
    list_devices("pulse", "音频设备");
    
    // 尝试ALSA作为备选
    printf("\n=== 尝试ALSA音频设备 ===\n");
    list_devices("alsa", "音频设备");
#endif
}

int main() {
    // 初始化FFmpeg设备注册
    avdevice_register_all();
    
    printf("===== FFmpeg 多媒体设备枚举器 =====\n");
    printf("兼容FFmpeg 4.x及以上版本\n\n");
    
    // 显示平台特定的设备信息
    list_platform_devices();
    
    printf("\n===== 枚举完成 =====\n");
    
    return 0;
}