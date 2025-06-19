#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#define FPS 15
#define FRAME_DELAY (1000000 / FPS) // 微秒

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <seconds>\n", argv[0]);
        return 1;
    }

    int seconds = atoi(argv[1]);
    printf("Recording video for %d seconds...\n", seconds);

    Display *display;
    Window root;
    XImage *image;
    int screen_width, screen_height;
    
    // FFmpeg相关变量
    AVFormatContext *fmt_ctx = NULL;
    AVStream *video_stream = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    struct SwsContext *sws_ctx = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;
    int ret;
    
    // 输出文件名
    const char *filename = "output.mp4";
    
    // 初始化X11
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "无法打开X显示\n");
        return 1;
    }
    
    root = DefaultRootWindow(display);
    screen_width = DisplayWidth(display, DefaultScreen(display));
    screen_height = DisplayHeight(display, DefaultScreen(display));
    
    printf("屏幕分辨率: %dx%d\n", screen_width, screen_height);
    
    // 初始化FFmpeg
    avformat_alloc_output_context2(&fmt_ctx, NULL, NULL, filename);
    if (!fmt_ctx) {
        fprintf(stderr, "无法创建输出上下文\n");
        return 1;
    }
    
    // 查找H.264编码器
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "找不到H.264编码器\n");
        return 1;
    }
    
    video_stream = avformat_new_stream(fmt_ctx, codec);
    if (!video_stream) {
        fprintf(stderr, "无法创建视频流\n");
        return 1;
    }
    
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "无法分配编码器上下文\n");
        return 1;
    }
    
    // 设置编码器参数
    codec_ctx->codec_id = AV_CODEC_ID_H264;
    codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->width = screen_width;
    codec_ctx->height = screen_height;
    codec_ctx->time_base = (AVRational){1, FPS};
    codec_ctx->framerate = (AVRational){FPS, 1};
    codec_ctx->gop_size = 10;
    codec_ctx->max_b_frames = 1;
	// 添加更多编码器配置
	codec_ctx->bit_rate = 4000000;  // 4Mbps
	codec_ctx->qmin = 10;
	codec_ctx->qmax = 51;
	codec_ctx->max_b_frames = 2;
    
    // 设置预设和调优
    if (av_opt_set(codec_ctx->priv_data, "preset", "fast", 0) < 0) {
        fprintf(stderr, "无法设置预设\n");
    }
    if (av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0) < 0) {
        fprintf(stderr, "无法设置调优\n");
    }
    
    // 打开编码器
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "无法打开编码器\n");
        return 1;
    }
    
    // 将编码器参数复制到流中
    if (avcodec_parameters_from_context(video_stream->codecpar, codec_ctx) < 0) {
        fprintf(stderr, "无法复制编解码器参数\n");
        return 1;
    }
    
    // 打开输出文件
    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_ctx->pb, filename, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "无法打开输出文件\n");
            return 1;
        }
    }
    
    // 写入文件头
    if (avformat_write_header(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "无法写入文件头\n");
        return 1;
    }
    
    // 分配帧和包
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "无法分配视频帧\n");
        return 1;
    }
    
    frame->format = codec_ctx->pix_fmt;
    frame->width = codec_ctx->width;
    frame->height = codec_ctx->height;
    
    if (av_frame_get_buffer(frame, 32) < 0) {
        fprintf(stderr, "无法分配帧数据\n");
        return 1;
    }
    
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "无法分配AVPacket\n");
        return 1;
    }
    
    printf("开始录制...\n");
    
    int64_t frame_count = 0;
    int64_t start_time = av_gettime();
    
    while (1) {
        int64_t frame_start_time = av_gettime();

        // 捕获屏幕
        image = XGetImage(display, root, 0, 0, screen_width, screen_height, AllPlanes, ZPixmap);
        if (!image) {
            fprintf(stderr, "无法获取屏幕图像\n");
            break;
        }
        printf("捕获完成\n");

        // 将XImage转换为AVFrame
        uint8_t *src_data[1] = { (uint8_t *)image->data };
        int src_linesize[1] = { image->bytes_per_line };

        if (!sws_ctx) {
            // 创建图像转换上下文
            sws_ctx = sws_getContext(
            screen_width, screen_height, 
            image->bits_per_pixel == 32 ? AV_PIX_FMT_BGRA : AV_PIX_FMT_BGR24, // 常见X11格式
            codec_ctx->width, codec_ctx->height, 
            codec_ctx->pix_fmt,
            SWS_BICUBIC, NULL, NULL, NULL);
            if (!sws_ctx) {
                fprintf(stderr, "无法创建SWS上下文\n");
                break;
            }
        }

        sws_scale(sws_ctx, src_data, src_linesize, 0, screen_height,
                  frame->data, frame->linesize);
        printf("转码完成\n");

        
        XDestroyImage(image);
        
        // 设置帧时间戳
        frame->pts = frame_count++;

        // 编码帧
        ret = avcodec_send_frame(codec_ctx, frame);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "发送帧到编码器失败: %s\n", errbuf);
            
            // 调试信息
            printf("帧信息: pts=%ld, width=%d, height=%d, format=%d\n",
                frame->pts, frame->width, frame->height, frame->format);
            printf("编码器期望: width=%d, height=%d, format=%d\n",
                codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt);
            break;
        }
        printf("帧发送完成\n");

        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                fprintf(stderr, "编码错误\n");
                break;
            }
            printf("帧接收完成\n");
            
            // 写入编码后的数据包
            av_packet_rescale_ts(pkt, codec_ctx->time_base, video_stream->time_base);
            pkt->stream_index = video_stream->index;
            
            if (av_interleaved_write_frame(fmt_ctx, pkt) < 0) {
                fprintf(stderr, "写入帧错误\n");
                break;
            }
            printf("帧写入完成, index: %d\n", frame_count);
            
            av_packet_unref(pkt);
        }
        
        // 计算帧处理时间并延迟
        int64_t frame_process_time = av_gettime() - frame_start_time;
        if (frame_process_time < FRAME_DELAY) {
            usleep(FRAME_DELAY - frame_process_time);
        }
        // 简单录制5秒后退出
        if (av_gettime() - start_time > seconds * 1000000) {
            break;
        }
    }
    
    printf("录制完成\n");
    
    // 刷新编码器
    avcodec_send_frame(codec_ctx, NULL);
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            fprintf(stderr, "编码错误\n");
            break;
        }
        
        av_packet_rescale_ts(pkt, codec_ctx->time_base, video_stream->time_base);
        pkt->stream_index = video_stream->index;
        
        if (av_interleaved_write_frame(fmt_ctx, pkt) < 0) {
            fprintf(stderr, "写入帧错误\n");
            break;
        }
        
        av_packet_unref(pkt);
    }
    
    // 写入文件尾
    av_write_trailer(fmt_ctx);
    
    // 清理资源
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);
    if (codec_ctx) avcodec_free_context(&codec_ctx);
    if (fmt_ctx && !(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&fmt_ctx->pb);
    }
    if (fmt_ctx) avformat_free_context(fmt_ctx);
    
    XCloseDisplay(display);
    
    return 0;
}