#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define OUTPUT_FILE "output.wav"  // 改为WAV格式
#define DURATION_SEC 8
#define SILENCE_START_SEC 5
#define SAMPLE_RATE 44100
#define CHANNELS 2
#define FRAME_SIZE 1024

typedef enum {
    MIC_ONLY,
    SPEAKER_ONLY,
    MIXED
} AudioSourceType;

void print_error(const char *msg, int err) {
    char errbuf[128];
    av_strerror(err, errbuf, sizeof(errbuf));
    fprintf(stderr, "%s: %s\n", msg, errbuf);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source>\n", argv[0]);
        fprintf(stderr, "Sources: mic, speaker\n");
        return 1;
    }

    AudioSourceType source_type;
    if (strcmp(argv[1], "mic") == 0) source_type = MIC_ONLY;
    else if (strcmp(argv[1], "speaker") == 0) source_type = SPEAKER_ONLY;
    else {
        fprintf(stderr, "Invalid source. Use mic or speaker\n");
        return 1;
    }

    avdevice_register_all();
    avformat_network_init();

    AVFormatContext *input_ctx = NULL;
    AVFormatContext *output_ctx = NULL;
    AVDictionary *options = NULL;
    int ret;
    const char *device_name = NULL;
    const AVInputFormat *input_fmt = NULL;

    // 设置设备选项
    av_dict_set(&options, "sample_rate", "44100", 0);
    av_dict_set(&options, "channels", "2", 0);
    av_dict_set(&options, "buffer_size", "1024", 0);

#ifdef _WIN32
    input_fmt = av_find_input_format("dshow");
    device_name = (source_type == MIC_ONLY) ? "audio=Microphone" : "audio=Stereo Mix";
#elif __APPLE__
    input_fmt = av_find_input_format("avfoundation");
    device_name = (source_type == MIC_ONLY) ? ":0" : ":1";
#else
    input_fmt = av_find_input_format("pulse");
    device_name = "default";
#endif

    printf("尝试打开设备: %s\n", device_name);
    
    // 打开输入设备
    if ((ret = avformat_open_input(&input_ctx, device_name, input_fmt, &options)) < 0) {
        print_error("无法打开音频设备", ret);
        goto cleanup;
    }

    // 打印输入格式信息
    av_dump_format(input_ctx, 0, device_name, 0);

    // 检查输入流参数
    if (input_ctx->nb_streams < 1) {
        fprintf(stderr, "没有找到音频流\n");
        goto cleanup;
    }

    AVCodecParameters *codecpar = input_ctx->streams[0]->codecpar;
    
    // 修正输入格式和声道布局
    enum AVSampleFormat sample_fmt = (enum AVSampleFormat)codecpar->format;
    if (sample_fmt == AV_SAMPLE_FMT_NONE) {
        sample_fmt = AV_SAMPLE_FMT_S16;
        fprintf(stderr, "警告：输入格式未设置，尝试使用AV_SAMPLE_FMT_S16\n");
    }
    
    uint64_t channel_layout = codecpar->channel_layout;
    if (channel_layout == 0 && codecpar->channels > 0) {
        channel_layout = av_get_default_channel_layout(codecpar->channels);
        if (channel_layout == 0) {
            channel_layout = AV_CH_LAYOUT_STEREO;
            fprintf(stderr, "警告：声道布局未设置，尝试使用AV_CH_LAYOUT_STEREO\n");
        }
    }
    
    printf("输入音频格式: %s, 采样率: %d, 声道: %d\n",
           av_get_sample_fmt_name(sample_fmt),
           codecpar->sample_rate,
           codecpar->channels);

    // 创建输出上下文 - 使用WAV格式
    if ((ret = avformat_alloc_output_context2(&output_ctx, NULL, "wav", OUTPUT_FILE)) < 0) {
        print_error("无法创建输出文件", ret);
        goto cleanup;
    }

    // 配置编码器 - 使用PCM编码器
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!codec) {
        fprintf(stderr, "找不到PCM编码器\n");
        goto cleanup;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "无法分配编码器上下文\n");
        goto cleanup;
    }
    
    codec_ctx->sample_rate = SAMPLE_RATE;
    codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    codec_ctx->channels = CHANNELS;
    codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;  // 16位有符号整型
    codec_ctx->bit_rate = SAMPLE_RATE * CHANNELS * 16; // 16位深度
    codec_ctx->time_base = (AVRational){1, SAMPLE_RATE};

    if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if ((ret = avcodec_open2(codec_ctx, codec, NULL)) < 0) {
        print_error("无法打开编码器", ret);
        goto cleanup;
    }

    // 添加输出流
    AVStream *out_stream = avformat_new_stream(output_ctx, NULL);
    if (!out_stream) {
        fprintf(stderr, "无法创建输出流\n");
        goto cleanup;
    }
    if ((ret = avcodec_parameters_from_context(out_stream->codecpar, codec_ctx)) < 0) {
        print_error("无法复制编码器参数", ret);
        goto cleanup;
    }

    // 打开输出文件
    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&output_ctx->pb, OUTPUT_FILE, AVIO_FLAG_WRITE)) < 0) {
            print_error("无法打开输出文件", ret);
            goto cleanup;
        }
    }

    // 写入文件头
    if ((ret = avformat_write_header(output_ctx, NULL)) < 0) {
        print_error("写入文件头失败", ret);
        goto cleanup;
    }

    // 初始化重采样 - 输出格式改为S16
    SwrContext *swr_ctx = swr_alloc();
    if (!swr_ctx) {
        fprintf(stderr, "无法分配重采样上下文\n");
        goto cleanup;
    }
    
    av_opt_set_int(swr_ctx, "in_sample_rate", codecpar->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", SAMPLE_RATE, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", sample_fmt, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);  // 改为16位有符号整型
    av_opt_set_int(swr_ctx, "in_channel_count", codecpar->channels, 0);
    av_opt_set_int(swr_ctx, "out_channel_count", CHANNELS, 0);
    av_opt_set_channel_layout(swr_ctx, "in_channel_layout", channel_layout, 0);
    av_opt_set_channel_layout(swr_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    
    if ((ret = swr_init(swr_ctx)) < 0) {
        print_error("无法初始化重采样", ret);
        goto cleanup;
    }

    // 初始化帧和包
    AVPacket *pkt = av_packet_alloc();
    AVFrame *in_frame = av_frame_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!pkt || !in_frame || !frame) {
        fprintf(stderr, "无法分配帧或包\n");
        goto cleanup;
    }

    // 使用基于时间的循环控制
    int64_t start_time = av_gettime();
    int64_t duration_us = DURATION_SEC * 1000000;
    int64_t silence_start_us = SILENCE_START_SEC * 1000000;
    int64_t samples_written = 0;
    int64_t total_samples = DURATION_SEC * SAMPLE_RATE;

    printf("开始录制 %d 秒音频...\n", DURATION_SEC);
    printf("将在 %.1f 秒后插入静音\n", (float)SILENCE_START_SEC);

    while (av_gettime() - start_time < duration_us) {
        int64_t elapsed_us = av_gettime() - start_time;
        
        // 准备输出帧
        frame->format = AV_SAMPLE_FMT_S16;  // 改为16位有符号整型
        frame->channel_layout = AV_CH_LAYOUT_STEREO;
        frame->channels = CHANNELS;
        frame->sample_rate = SAMPLE_RATE;
        frame->nb_samples = FRAME_SIZE;
        
        // 分配输出帧内存
        if ((ret = av_frame_get_buffer(frame, 0)) < 0) {
            print_error("无法分配音频帧", ret);
            break;
        }
        
        // 设置正确的PTS
        frame->pts = av_rescale_q(elapsed_us, (AVRational){1, 1000000}, 
                                 (AVRational){1, SAMPLE_RATE});
        
        // 生成静音或捕获音频
        if (elapsed_us >= silence_start_us) {
            // 设置静音 - 使用S16格式
            av_samples_set_silence(frame->data, 0, frame->nb_samples, 
                                   frame->channels, AV_SAMPLE_FMT_S16);
        } else {
            // 读取音频帧
            if ((ret = av_read_frame(input_ctx, pkt)) < 0) {
                if (ret == AVERROR(EAGAIN)) {
                    av_packet_unref(pkt);
                    continue;
                }
                print_error("读取错误", ret);
                break;
            }
            
            // 跳过非音频流
            if (pkt->stream_index != 0) {
                av_packet_unref(pkt);
                continue;
            }

            // 配置输入帧
            in_frame->format = sample_fmt;
            in_frame->channel_layout = channel_layout;
            in_frame->channels = codecpar->channels;
            in_frame->sample_rate = codecpar->sample_rate;
            
            // 计算样本数
            int bytes_per_sample = av_get_bytes_per_sample(sample_fmt);
            in_frame->nb_samples = pkt->size / (bytes_per_sample * in_frame->channels);
            
            // 分配输入帧内存
            if ((ret = av_frame_get_buffer(in_frame, 0)) < 0) {
                print_error("无法分配输入帧缓冲区", ret);
                av_packet_unref(pkt);
                break;
            }
            
            // 计算实际需要的字节数
            int required_bytes = in_frame->nb_samples * bytes_per_sample * in_frame->channels;
            int copy_bytes = pkt->size < required_bytes ? pkt->size : required_bytes;
            
            // 复制数据
            memcpy(in_frame->data[0], pkt->data, copy_bytes);
            av_packet_unref(pkt);
            
            // 重采样 - 输出格式改为S16
            int actual_samples = swr_convert(swr_ctx, 
                                  frame->data, frame->nb_samples,
                                  (const uint8_t**)in_frame->data, in_frame->nb_samples);
            if (actual_samples < 0) {
                print_error("重采样错误", actual_samples);
                break;
            }
            
            // 更新实际样本数
            if (actual_samples > 0 && actual_samples < frame->nb_samples) {
                frame->nb_samples = actual_samples;
            }
            
            // 释放输入帧
            av_frame_unref(in_frame);
        }

        // 编码帧
        if ((ret = avcodec_send_frame(codec_ctx, frame)) < 0) {
            print_error("发送帧错误", ret);
            break;
        }

        // 接收并写入编码数据
        while (1) {
            ret = avcodec_receive_packet(codec_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                print_error("编码错误", ret);
                goto cleanup;
            }

            pkt->stream_index = out_stream->index;
            // 正确转换时间戳
            av_packet_rescale_ts(pkt, codec_ctx->time_base, out_stream->time_base);
            if ((ret = av_interleaved_write_frame(output_ctx, pkt)) < 0) {
                print_error("写入错误", ret);
                av_packet_unref(pkt);
                goto cleanup;
            }
            av_packet_unref(pkt);
        }

        samples_written += frame->nb_samples;
        
        // 计算实际时间与预期时间的差异
        int64_t expected_time = (samples_written * 1000000) / SAMPLE_RATE;
        int64_t actual_time = elapsed_us;
        int64_t time_diff = expected_time - actual_time;
        
        // 如果落后太多，跳过一些处理
        if (time_diff > 10000) { // 超过10ms
            usleep(time_diff);
        }
        
        printf("\r进度: %.1f%%", (float)elapsed_us * 100.0f / duration_us);
        fflush(stdout);
        
        // 释放输出帧
        av_frame_unref(frame);
    }

    // 刷新编码器
    avcodec_send_frame(codec_ctx, NULL);
    while (1) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR_EOF) break;
        if (ret < 0) {
            if (ret != AVERROR_EOF) print_error("刷新错误", ret);
            break;
        }
        pkt->stream_index = out_stream->index;
        av_packet_rescale_ts(pkt, codec_ctx->time_base, out_stream->time_base);
        if ((ret = av_interleaved_write_frame(output_ctx, pkt)) < 0) {
            print_error("写入包错误", ret);
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(output_ctx);
    printf("\n录制完成，文件已保存至: %s\n", OUTPUT_FILE);

cleanup:
    av_dict_free(&options);
    av_packet_free(&pkt);
    if (in_frame) av_frame_free(&in_frame);
    if (frame) av_frame_free(&frame);
    if (swr_ctx) swr_free(&swr_ctx);
    if (codec_ctx) avcodec_free_context(&codec_ctx);
    if (output_ctx) {
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&output_ctx->pb);
        avformat_free_context(output_ctx);
    }
    if (input_ctx) avformat_close_input(&input_ctx);
    return 0;
}