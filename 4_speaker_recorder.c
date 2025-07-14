/*
 *  speaker_record.c
 *  gcc speaker_record.c -o speaker_record \
 *      `pkg-config --cflags --libs libavformat libavcodec libavutil libswresample libavdevice`
 *
 *  运行示例：
 *      ./speaker_record -t=10 -o=speaker.wav
 *
 *  Linux + PulseAudio 会录系统声卡输出（monitor 源）。
 *  Windows 请把 input_format 换成 dshow，设备名换成 "立体声混音"。
 *  macOS  请把 input_format 换成 avfoundation，设备名换成 BlackHole/Soundflower 的 loopback。
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <signal.h>
 #include <libavcodec/avcodec.h>
 #include <libavformat/avformat.h>
 #include <libavdevice/avdevice.h>
 #include <libavutil/opt.h>
 #include <libavutil/timestamp.h>
 #include <libswresample/swresample.h>
 
 #define DEFAULT_DURATION 5   /* 默认录 5 秒 */
 #define SAMPLE_RATE      44100
 #define CHANNEL_LAYOUT   AV_CH_LAYOUT_STEREO
 #define SAMPLE_FMT       AV_SAMPLE_FMT_S16
 
 static volatile int g_running = 1;
 static void sigint_handler(int sig) { g_running = 0; }
 
 static void log_err(const char *tag, int ret)
 {
     char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
     av_strerror(ret, buf, sizeof(buf));
     fprintf(stderr, "%s: %s\n", tag, buf);
 }
 
 int main(int argc, char *argv[])
 {
     /* ------------- 参数解析 ------------- */
     int seconds = DEFAULT_DURATION;
     const char *outfile = "speaker.wav";
     for (int i = 1; i < argc; ++i) {
         if (!strncmp(argv[i], "-t=", 3))      seconds   = atoi(argv[i] + 3);
         else if (!strncmp(argv[i], "-o=", 3)) outfile   = argv[i] + 3;
     }
     if (seconds <= 0) seconds = DEFAULT_DURATION;
 
     signal(SIGINT, sigint_handler);
 
     /* ------------- 初始化 ------------- */
     avdevice_register_all();
     avformat_network_init();
 
     /* ------------- 1. 打开 loopback 输入 ------------- */
     AVInputFormat  *ifmt  = av_find_input_format("pulse");        /* Linux PulseAudio */
     const char     *idev  = "histen_sink.monitor";                            /* monitor 源 */
     /* Windows:  ifmt=av_find_input_format("dshow"); idev="立体声混音"; */
     /* macOS:    ifmt=av_find_input_format("avfoundation"); idev="BlackHole 16ch"; */
 
     AVDictionary *fmt_opts = NULL;
     av_dict_set(&fmt_opts, "sample_rate", "44100", 0);
     av_dict_set(&fmt_opts, "channels",    "2",     0);
 
     AVFormatContext *ifmt_ctx = NULL;
     int ret = avformat_open_input(&ifmt_ctx, idev, ifmt, &fmt_opts);
     if (ret < 0) { log_err("avformat_open_input", ret); return 1; }
 
     ret = avformat_find_stream_info(ifmt_ctx, NULL);
     if (ret < 0) { log_err("avformat_find_stream_info", ret); return 1; }
 
     int audio_idx = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
     if (audio_idx < 0) { fprintf(stderr, "no audio stream\n"); return 1; }
 
     /* ------------- 2. 准备输出 ------------- */
     AVFormatContext *ofmt_ctx = NULL;
     ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, outfile);
     if (ret < 0) { log_err("avformat_alloc_output_context2", ret); return 1; }
 
     /* 输出编码器：PCM_S16LE */
     const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
     if (!enc) { fprintf(stderr, "PCM encoder not found\n"); return 1; }
     AVCodecContext *enc_ctx = avcodec_alloc_context3(enc);
     enc_ctx->sample_rate    = SAMPLE_RATE;
     enc_ctx->channel_layout = CHANNEL_LAYOUT;
     enc_ctx->channels       = av_get_channel_layout_nb_channels(CHANNEL_LAYOUT);
     enc_ctx->sample_fmt     = SAMPLE_FMT;
     ret = avcodec_open2(enc_ctx, enc, NULL);
     if (ret < 0) { log_err("avcodec_open2", ret); return 1; }
 
     AVStream *ost = avformat_new_stream(ofmt_ctx, enc);
     avcodec_parameters_from_context(ost->codecpar, enc_ctx);
 
     if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
         ret = avio_open(&ofmt_ctx->pb, outfile, AVIO_FLAG_WRITE);
         if (ret < 0) { log_err("avio_open", ret); return 1; }
     }
     ret = avformat_write_header(ofmt_ctx, NULL);
     if (ret < 0) { log_err("avformat_write_header", ret); return 1; }
 
     /* ------------- 3. 重采样（如有必要） ------------- */
     SwrContext *swr = NULL;
     AVCodecParameters *par = ifmt_ctx->streams[audio_idx]->codecpar;
     if (par->sample_rate    != SAMPLE_RATE ||
         par->channel_layout != CHANNEL_LAYOUT ||
         par->format         != SAMPLE_FMT)
     {
         swr = swr_alloc_set_opts(NULL,
                                  CHANNEL_LAYOUT, SAMPLE_FMT, SAMPLE_RATE,
                                  par->channel_layout ? par->channel_layout
                                                      : av_get_default_channel_layout(par->channels),
                                  par->format, par->sample_rate,
                                  0, NULL);
         swr_init(swr);
         printf("resampler enabled\n");
     }
 
     /* ------------- 4. 主循环 ------------- */
     AVPacket *pkt  = av_packet_alloc();
     AVFrame  *in_frame  = av_frame_alloc();
     AVFrame  *out_frame = av_frame_alloc();
     out_frame->format         = SAMPLE_FMT;
     out_frame->channel_layout = CHANNEL_LAYOUT;
     out_frame->sample_rate    = SAMPLE_RATE;
     out_frame->nb_samples     = enc_ctx->frame_size ? enc_ctx->frame_size : 1024;
 
     av_frame_get_buffer(out_frame, 0);
 
     int64_t start = av_gettime();
     int64_t end   = start + seconds * AV_TIME_BASE;
 
     printf("Recording %s for %d seconds … Ctrl+C to stop early\n", outfile, seconds);
 
     while (g_running && av_gettime() < end) {
         ret = av_read_frame(ifmt_ctx, pkt);
         if (ret < 0) {
             if (ret != AVERROR(EAGAIN)) break;
             continue;
         }
         if (pkt->stream_index != audio_idx) { av_packet_unref(pkt); continue; }
 
         /* 解码 -> 重采样 -> 编码 -> 写入 */
         /* PCM 输入时其实可以直接拷贝，但为了通用性仍走重采样路径 */
         if (swr) {
             /* 把 packet 转成 frame（PCM 可直接填充） */
             in_frame->nb_samples = pkt->size /
                 (av_get_bytes_per_sample(par->format) * par->channels);
             in_frame->format      = par->format;
             in_frame->channel_layout = par->channel_layout ? par->channel_layout
                                                              : av_get_default_channel_layout(par->channels);
             avcodec_fill_audio_frame(in_frame, par->channels, par->format,
                                      pkt->data, pkt->size, 1);
 
             /* 重采样 */
             int out_samples = swr_convert(swr,
                                           out_frame->data, out_frame->nb_samples,
                                           (const uint8_t **)in_frame->data, in_frame->nb_samples);
             if (out_samples < 0) { log_err("swr_convert", out_samples); continue; }
             out_frame->nb_samples = out_samples;
         } else {
             /* 无需重采样，直接把 packet 写入 */
             pkt->stream_index = ost->index;
             av_packet_rescale_ts(pkt, ifmt_ctx->streams[audio_idx]->time_base, ost->time_base);
             av_interleaved_write_frame(ofmt_ctx, pkt);
             av_packet_unref(pkt);
             continue;
         }
 
         /* 编码 */
         out_frame->pts = av_rescale_q(av_gettime() - start, AV_TIME_BASE_Q, enc_ctx->time_base);
         ret = avcodec_send_frame(enc_ctx, out_frame);
         if (ret < 0) { log_err("avcodec_send_frame", ret); continue; }
 
         while (ret >= 0) {
             ret = avcodec_receive_packet(enc_ctx, pkt);
             if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
             if (ret < 0) { log_err("avcodec_receive_packet", ret); break; }
             pkt->stream_index = ost->index;
             av_packet_rescale_ts(pkt, enc_ctx->time_base, ost->time_base);
             av_interleaved_write_frame(ofmt_ctx, pkt);
             av_packet_unref(pkt);
         }
     }
 
     /* ------------- 5. 冲刷 ------------- */
     avcodec_send_frame(enc_ctx, NULL);
     while (avcodec_receive_packet(enc_ctx, pkt) >= 0) {
         pkt->stream_index = ost->index;
         av_packet_rescale_ts(pkt, enc_ctx->time_base, ost->time_base);
         av_interleaved_write_frame(ofmt_ctx, pkt);
         av_packet_unref(pkt);
     }
 
     /* ------------- 6. 收尾 ------------- */
     av_write_trailer(ofmt_ctx);
 
     if (swr) swr_free(&swr);
     av_frame_free(&in_frame);
     av_frame_free(&out_frame);
     av_packet_free(&pkt);
     avcodec_free_context(&enc_ctx);
     avformat_close_input(&ifmt_ctx);
 
     if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
         avio_closep(&ofmt_ctx->pb);
     avformat_free_context(ofmt_ctx);
 
     printf("Finished.\n");
     return 0;
 }