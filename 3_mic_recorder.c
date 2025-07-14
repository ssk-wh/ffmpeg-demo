#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libavdevice/avdevice.h>

#define DEFAULT_RECORD_SECONDS 5
#define SAMPLE_RATE 44100
#define CHANNEL_LAYOUT AV_CH_LAYOUT_STEREO
#define SAMPLE_FORMAT AV_SAMPLE_FMT_S16

void print_error(const char *message, int error_code) {
    char err_buf[256];
    av_strerror(error_code, err_buf, sizeof(err_buf));
    printf("%s: %s\n", message, err_buf);
}

int main(int argc, char *argv[]) {
    // 初始化FFmpeg
    avdevice_register_all();
    avformat_network_init();
    
    const char *output_filename = "audio_output.wav";
    int record_seconds = DEFAULT_RECORD_SECONDS;
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-t=", 3) == 0) {
            record_seconds = atoi(argv[i] + 3);
            if (record_seconds <= 0) {
                printf("Invalid duration, using default %d seconds\n", DEFAULT_RECORD_SECONDS);
                record_seconds = DEFAULT_RECORD_SECONDS;
            }
        } else if (strncmp(argv[i], "-o=", 3) == 0) {
            output_filename = argv[i] + 3;
        }
    }
    
    printf("Recording audio for %d seconds to %s...\n", record_seconds, output_filename);
    
    // 1. 设置音频输入设备
    AVFormatContext *input_ctx = NULL;
    AVInputFormat *input_format = av_find_input_format("alsa"); // 改用ALSA驱动
    // 备选方案:
    // - Linux: "pulse" (PulseAudio)
    // - Windows: "dshow"
    // - macOS: "avfoundation"
    
    AVDictionary *options = NULL;
    av_dict_set(&options, "sample_rate", "44100", 0);
    av_dict_set(&options, "channels", "2", 0);
    av_dict_set(&options, "format", "s16", 0); // 明确指定输入格式
    
    const char *device_name = "default"; // ALSA默认设备
    
    int ret = avformat_open_input(&input_ctx, device_name, input_format, &options);
    if (ret < 0) {
        print_error("Could not open audio device", ret);
        return -1;
    }
    
    // 2. 获取流信息
    if ((ret = avformat_find_stream_info(input_ctx, NULL)) < 0) {
        print_error("Could not find stream info", ret);
        avformat_close_input(&input_ctx);
        return -1;
    }
    
    // 查找音频流
    int audio_stream_index = -1;
    for (unsigned i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    
    if (audio_stream_index == -1) {
        printf("Could not find audio stream\n");
        avformat_close_input(&input_ctx);
        return -1;
    }
    
    AVCodecParameters *codecpar = input_ctx->streams[audio_stream_index]->codecpar;
    
    // 3. 创建输出文件上下文
    AVFormatContext *output_ctx = NULL;
    if ((ret = avformat_alloc_output_context2(&output_ctx, NULL, NULL, output_filename)) < 0) {
        print_error("Could not create output context", ret);
        avformat_close_input(&input_ctx);
        return -1;
    }
    
    // 4. 创建音频编码器(PCM)
    AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!encoder) {
        printf("PCM encoder not found\n");
        avformat_close_input(&input_ctx);
        avformat_free_context(output_ctx);
        return -1;
    }
    
    AVCodecContext *encoder_ctx = avcodec_alloc_context3(encoder);
    encoder_ctx->sample_rate = SAMPLE_RATE;
    encoder_ctx->channel_layout = CHANNEL_LAYOUT;
    encoder_ctx->channels = av_get_channel_layout_nb_channels(CHANNEL_LAYOUT);
    encoder_ctx->sample_fmt = SAMPLE_FORMAT;
    encoder_ctx->bit_rate = 64000;
    
    if ((ret = avcodec_open2(encoder_ctx, encoder, NULL)) < 0) {
        print_error("Could not open encoder", ret);
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        avformat_free_context(output_ctx);
        return -1;
    }
    
    // 5. 添加音频流到输出文件
    AVStream *out_stream = avformat_new_stream(output_ctx, encoder);
    if (!out_stream) {
        printf("Failed to create output stream\n");
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        avformat_free_context(output_ctx);
        return -1;
    }
    
    if ((ret = avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx)) < 0) {
        print_error("Failed to copy codec parameters", ret);
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        avformat_free_context(output_ctx);
        return -1;
    }
    
    // 6. 打开输出文件
    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&output_ctx->pb, output_filename, AVIO_FLAG_WRITE)) < 0) {
            print_error("Could not open output file", ret);
            avcodec_free_context(&encoder_ctx);
            avformat_close_input(&input_ctx);
            avformat_free_context(output_ctx);
            return -1;
        }
    }
    
    // 7. 写入文件头
    if ((ret = avformat_write_header(output_ctx, NULL)) < 0) {
        print_error("Error writing header", ret);
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        avformat_free_context(output_ctx);
        return -1;
    }
    
    // 8. 准备重采样器
    SwrContext *swr_ctx = NULL;
    AVCodecContext *decoder_ctx = NULL;
    
    // 获取输入音频参数
    enum AVSampleFormat in_sample_fmt = codecpar->format;
    int in_sample_rate = codecpar->sample_rate;
    uint64_t in_channel_layout = codecpar->channel_layout;
    
    // 如果输入没有指定声道布局，使用默认立体声
    if (!in_channel_layout) {
        in_channel_layout = av_get_default_channel_layout(codecpar->channels);
    }
    
    // 检查是否需要重采样
    if (in_sample_rate != SAMPLE_RATE || 
        in_sample_fmt != SAMPLE_FORMAT || 
        in_channel_layout != CHANNEL_LAYOUT) {
        
        printf("Setting up resampler...\n");
        printf("Input: rate=%d, fmt=%s, channels=%d\n", 
               in_sample_rate, av_get_sample_fmt_name(in_sample_fmt), 
               av_get_channel_layout_nb_channels(in_channel_layout));
        printf("Output: rate=%d, fmt=%s, channels=%d\n", 
               SAMPLE_RATE, av_get_sample_fmt_name(SAMPLE_FORMAT),
               av_get_channel_layout_nb_channels(CHANNEL_LAYOUT));
        
        swr_ctx = swr_alloc_set_opts(NULL,
                                    CHANNEL_LAYOUT, SAMPLE_FORMAT, SAMPLE_RATE,
                                    in_channel_layout, in_sample_fmt, in_sample_rate,
                                    0, NULL);
        if (!swr_ctx) {
            printf("Failed to allocate resampler\n");
            avformat_close_input(&input_ctx);
            avformat_free_context(output_ctx);
            return -1;
        }
        
        if ((ret = swr_init(swr_ctx)) < 0) {
            print_error("Failed to initialize resampler", ret);
            swr_free(&swr_ctx);
            avformat_close_input(&input_ctx);
            avformat_free_context(output_ctx);
            return -1;
        }
    } else {
        printf("No resampling needed\n");
    }
    
    // 9. 准备帧和包
    AVFrame *frame = av_frame_alloc();
    AVFrame *tmp_frame = av_frame_alloc();
    tmp_frame->sample_rate = SAMPLE_RATE;
    tmp_frame->channel_layout = CHANNEL_LAYOUT;
    tmp_frame->format = SAMPLE_FORMAT;
    tmp_frame->channels = av_get_channel_layout_nb_channels(CHANNEL_LAYOUT);
    
    AVPacket *input_packet = av_packet_alloc();
    AVPacket *output_packet = av_packet_alloc();
    
    // 10. 主录制循环
    int64_t start_time = av_gettime();
    int64_t end_time = start_time + (record_seconds * 1000000);
    int samples_count = 0;
    int frames_recorded = 0;
    
    while (av_gettime() < end_time) {
        ret = av_read_frame(input_ctx, input_packet);
        if (ret < 0) {
            if (ret != AVERROR(EAGAIN)) {
                print_error("Error reading audio frame", ret);
            }
            continue;
        }
        
        if (input_packet->stream_index == audio_stream_index) {
            if (swr_ctx) {
                // 需要重采样的情况
                // 直接重采样数据包
                AVFrame *input_frame = av_frame_alloc();
                input_frame->sample_rate = in_sample_rate;
                input_frame->channel_layout = in_channel_layout;
                input_frame->format = in_sample_fmt;
                input_frame->channels = av_get_channel_layout_nb_channels(in_channel_layout);
                input_frame->nb_samples = input_packet->size / 
                    (av_get_bytes_per_sample(in_sample_fmt) * 
                    av_get_channel_layout_nb_channels(in_channel_layout));
                
                ret = avcodec_fill_audio_frame(input_frame, 
                    av_get_channel_layout_nb_channels(in_channel_layout),
                    in_sample_fmt,
                    input_packet->data,
                    input_packet->size,
                    0);
                if (ret < 0) {
                    print_error("Failed to fill audio frame", ret);
                    av_frame_free(&input_frame);
                    continue;
                }
                
                // 配置输出帧
                tmp_frame->nb_samples = input_frame->nb_samples;
                tmp_frame->pts = samples_count;
                av_frame_get_buffer(tmp_frame, 0);
                
                // 重采样
                ret = swr_convert(swr_ctx, 
                                  tmp_frame->data, tmp_frame->nb_samples,
                                  (const uint8_t **)input_frame->data, input_frame->nb_samples);
                if (ret < 0) {
                    print_error("Error during resampling", ret);
                    av_frame_free(&input_frame);
                    continue;
                }
                
                // 编码
                ret = avcodec_send_frame(encoder_ctx, tmp_frame);
                if (ret < 0) {
                    print_error("Error sending frame to encoder", ret);
                    av_frame_free(&input_frame);
                    continue;
                }
                
                while (ret >= 0) {
                    ret = avcodec_receive_packet(encoder_ctx, output_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        print_error("Error during encoding", ret);
                        break;
                    }
                    
                    output_packet->stream_index = out_stream->index;
                    av_packet_rescale_ts(output_packet, encoder_ctx->time_base, out_stream->time_base);
                    
                    ret = av_interleaved_write_frame(output_ctx, output_packet);
                    if (ret < 0) {
                        print_error("Error writing frame", ret);
                    }
                    
                    av_packet_unref(output_packet);
                    frames_recorded++;
                }
                
                samples_count += tmp_frame->nb_samples;
                av_frame_unref(tmp_frame);
                av_frame_free(&input_frame);
            } else {
                // 不需要重采样，直接写入
                output_packet->stream_index = out_stream->index;
                av_packet_rescale_ts(input_packet, 
                                   input_ctx->streams[audio_stream_index]->time_base,
                                   out_stream->time_base);
                
                ret = av_interleaved_write_frame(output_ctx, input_packet);
                if (ret < 0) {
                    print_error("Error writing frame", ret);
                } else {
                    frames_recorded++;
                }
            }
        }
        
        av_packet_unref(input_packet);
    }
    
    printf("Recorded %d audio frames\n", frames_recorded);
    
    // 11. 冲刷编码器
    if (swr_ctx) {
        avcodec_send_frame(encoder_ctx, NULL);
        
        while (avcodec_receive_packet(encoder_ctx, output_packet) >= 0) {
            output_packet->stream_index = out_stream->index;
            av_packet_rescale_ts(output_packet, encoder_ctx->time_base, out_stream->time_base);
            av_interleaved_write_frame(output_ctx, output_packet);
            av_packet_unref(output_packet);
        }
    }
    
    printf("Recording complete! Saved to %s\n", output_filename);
    
    // 12. 写入文件尾
    av_write_trailer(output_ctx);
    
    // 13. 清理资源
    av_frame_free(&frame);
    av_frame_free(&tmp_frame);
    av_packet_free(&input_packet);
    av_packet_free(&output_packet);
    swr_free(&swr_ctx);
    if (decoder_ctx) avcodec_free_context(&decoder_ctx);
    avcodec_free_context(&encoder_ctx);
    avformat_close_input(&input_ctx);
    
    if (output_ctx && !(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_ctx->pb);
    }
    avformat_free_context(output_ctx);
    
    return 0;
}