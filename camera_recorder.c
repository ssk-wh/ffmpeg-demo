#include <stdio.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>

int main(int argc, char *argv[]) {
    // 初始化FFmpeg
    avdevice_register_all();
    avformat_network_init();

    // 默认输出格式为MP4
    const char *default_format = "mp4";
    const char *output_format = default_format;
    const char *output_filename = "camera_output.mp4";

    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-f=", 3) == 0) {
            const char *format = argv[i] + 3;
            if (strcmp(format, "flv") == 0) {
                output_format = "flv";
                output_filename = "camera_output.flv";
            } else if (strcmp(format, "mp4") == 0) {
                output_format = "mp4";
                output_filename = "camera_output.mp4";
            } else {
                printf("Unsupported format: %s, using default mp4\n", format);
            }
        }
    }

    printf("Output format: %s\n", output_format);

    const int record_seconds = 5;

    // 1. 打开摄像头设备
    AVFormatContext* input_ctx = NULL;
    AVInputFormat* input_format = av_find_input_format("v4l2");

    AVDictionary* options = NULL;
    av_dict_set(&options, "framerate", "30", 0);
    av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "input_format", "mjpeg", 0);

    int ret = avformat_open_input(&input_ctx, "/dev/video0", input_format, &options);
    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        printf("Could not open device: %s\n", err_buf);
        return -1;
    }

    // 2. 获取流信息
    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        printf("Could not find stream info\n");
        avformat_close_input(&input_ctx);
        return -1;
    }

    // 查找视频流
    int video_stream_index = -1;
    for (unsigned i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        printf("Could not find video stream\n");
        avformat_close_input(&input_ctx);
        return -1;
    }

    // 3. 创建编码器 (H.264兼容MP4和FLV)
    AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder) {
        printf("H.264 encoder not found\n");
        avformat_close_input(&input_ctx);
        return -1;
    }

    AVCodecContext* encoder_ctx = avcodec_alloc_context3(encoder);
    encoder_ctx->width = 640;
    encoder_ctx->height = 480;
    encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_ctx->time_base = (AVRational){1, 30};
    encoder_ctx->bit_rate = 400000;
    encoder_ctx->gop_size = 10;

    // 根据输出格式调整编码参数
    if (strcmp(output_format, "flv") == 0) {
        encoder_ctx->max_b_frames = 0;  // FLV不支持B帧
    }

    av_opt_set(encoder_ctx->priv_data, "preset", "fast", 0);

    if (avcodec_open2(encoder_ctx, encoder, NULL) < 0) {
        printf("Could not open encoder\n");
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        return -1;
    }

    // 4. 创建输出文件上下文
    AVFormatContext* output_ctx = NULL;
    avformat_alloc_output_context2(&output_ctx, NULL, output_format, output_filename);
    if (!output_ctx) {
        printf("Could not create output context\n");
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        return -1;
    }

    // 5. 添加视频流到输出文件
    AVStream* out_stream = avformat_new_stream(output_ctx, encoder);
    if (!out_stream) {
        printf("Failed to create output stream\n");
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        avformat_free_context(output_ctx);
        return -1;
    }

    if (avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx) < 0) {
        printf("Failed to copy codec parameters\n");
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        avformat_free_context(output_ctx);
        return -1;
    }

    // 设置全局头部(某些格式需要)
    if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // 6. 打开输出文件
    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_ctx->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
            printf("Could not open output file %s\n", output_filename);
            avcodec_free_context(&encoder_ctx);
            avformat_close_input(&input_ctx);
            avformat_free_context(output_ctx);
            return -1;
        }
    }

    // 7. 写入文件头
    if (avformat_write_header(output_ctx, NULL) < 0) {
        printf("Error writing header\n");
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        avformat_free_context(output_ctx);
        return -1;
    }

    // 8. 准备解码和转换
    AVCodecParameters* codecpar = input_ctx->streams[video_stream_index]->codecpar;
    AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
    AVCodecContext* decoder_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoder_ctx, codecpar);

    if (avcodec_open2(decoder_ctx, decoder, NULL) < 0) {
        printf("Could not open decoder\n");
        avcodec_free_context(&decoder_ctx);
        avcodec_free_context(&encoder_ctx);
        avformat_close_input(&input_ctx);
        avformat_free_context(output_ctx);
        return -1;
    }

    struct SwsContext* sws_ctx = sws_getContext(
        decoder_ctx->width, decoder_ctx->height, decoder_ctx->pix_fmt,
        encoder_ctx->width, encoder_ctx->height, encoder_ctx->pix_fmt,
        SWS_BILINEAR, NULL, NULL, NULL);

    AVFrame* frame = av_frame_alloc();
    AVFrame* tmp_frame = av_frame_alloc();
    tmp_frame->format = encoder_ctx->pix_fmt;
    tmp_frame->width = encoder_ctx->width;
    tmp_frame->height = encoder_ctx->height;
    av_frame_get_buffer(tmp_frame, 0);

    // 9. 主录制循环
    AVPacket input_packet;
    int frame_count = 0;
    const int target_frames = record_seconds * 30;

    printf("Start recording to %s...\n", output_filename);

    while (frame_count < target_frames) {
        ret = av_read_frame(input_ctx, &input_packet);
        if (ret < 0) break;

        if (input_packet.stream_index == video_stream_index) {
            ret = avcodec_send_packet(decoder_ctx, &input_packet);
            if (ret < 0) {
                printf("Error sending packet to decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(decoder_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    printf("Error during decoding\n");
                    break;
                }

                sws_scale(sws_ctx,
                          frame->data, frame->linesize, 0, frame->height,
                          tmp_frame->data, tmp_frame->linesize);

                tmp_frame->pts = frame_count;

                AVPacket output_packet;
                av_init_packet(&output_packet);
                output_packet.data = NULL;
                output_packet.size = 0;

                ret = avcodec_send_frame(encoder_ctx, tmp_frame);
                if (ret < 0) {
                    printf("Error sending frame to encoder\n");
                    break;
                }

                while (ret >= 0) {
                    ret = avcodec_receive_packet(encoder_ctx, &output_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        printf("Error during encoding\n");
                        break;
                    }

                    output_packet.stream_index = out_stream->index;
                    av_packet_rescale_ts(&output_packet, encoder_ctx->time_base, out_stream->time_base);

                    ret = av_interleaved_write_frame(output_ctx, &output_packet);
                    if (ret < 0) {
                        printf("Error writing frame\n");
                    }

                    av_packet_unref(&output_packet);
                }

                frame_count++;
                printf("\rFrames recorded: %d/%d", frame_count, target_frames);
                fflush(stdout);
            }
        }

        av_packet_unref(&input_packet);
    }

    // 10. 冲刷编码器
    AVPacket output_packet;
    av_init_packet(&output_packet);
    output_packet.data = NULL;
    output_packet.size = 0;

    avcodec_send_frame(encoder_ctx, NULL);

    while (avcodec_receive_packet(encoder_ctx, &output_packet) >= 0) {
        output_packet.stream_index = out_stream->index;
        av_packet_rescale_ts(&output_packet, encoder_ctx->time_base, out_stream->time_base);
        av_interleaved_write_frame(output_ctx, &output_packet);
        av_packet_unref(&output_packet);
    }

    printf("\nRecording complete!\n");

    // 11. 写入文件尾
    av_write_trailer(output_ctx);

    // 12. 清理资源
    sws_freeContext(sws_ctx);
    av_frame_free(&frame);
    av_frame_free(&tmp_frame);
    avcodec_free_context(&decoder_ctx);
    avcodec_free_context(&encoder_ctx);
    avformat_close_input(&input_ctx);

    if (output_ctx && !(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_ctx->pb);
    }
    avformat_free_context(output_ctx);

    return 0;
}
