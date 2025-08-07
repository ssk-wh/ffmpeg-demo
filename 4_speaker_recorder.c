/*
 *  speaker_record.c
 *
 *  gcc speaker_record.c -o speaker_record \
 *      `pkg-config --cflags --libs libavformat libavcodec libavutil libswresample libavdevice`
 *
 *  ./speaker_record -t=10 -o=speaker.wav
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <signal.h>
 #include <time.h>
 #include <unistd.h>
 
 #include <libavcodec/avcodec.h>
 #include <libavformat/avformat.h>
 #include <libavdevice/avdevice.h>
 #include <libavutil/opt.h>
 #include <libavutil/timestamp.h>
 #include <libswresample/swresample.h>
 
 #define DEFAULT_DURATION 10
 #define SAMPLE_RATE      44100
 #define CHANNEL_LAYOUT   AV_CH_LAYOUT_STEREO
 #define SAMPLE_FMT       AV_SAMPLE_FMT_S16
 
 static volatile int g_running = 1;
 static void sigint_handler(int sig) { g_running = 0; }
 
 static void log_err(const char *tag, int ret)
 {
     char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
     av_strerror(ret, buf, sizeof(buf));
     fprintf(stderr, "[%s] ERROR: %s: %s\n", __func__, tag, buf);
 }
 
 static char *get_default_monitor(void)
 {
     FILE *fp = popen("pactl list short sources | awk '/\\.monitor/ {print $2; exit}'", "r");
     if (!fp) return NULL;
     char buf[256] = {0};
     if (fgets(buf, sizeof(buf), fp))
         buf[strcspn(buf, "\r\n")] = '\0';
     pclose(fp);
     return strdup(buf[0] ? buf : "default");
 }
 
 static void print_time(const char *prefix, int64_t us)
 {
     int s = (int)(us / AV_TIME_BASE);
     fprintf(stderr, "[%s] %s %02d:%02d\n",
             __func__, prefix, s / 60, s % 60);
 }
 
 int main(int argc, char *argv[])
 {
     /* ---------- 参数解析 ---------- */
     int seconds = DEFAULT_DURATION;
     const char *outfile = "speaker.wav";
     for (int i = 1; i < argc; ++i) {
         if (!strncmp(argv[i], "-t=", 3))
             seconds = atoi(argv[i] + 3);
         else if (!strncmp(argv[i], "-o=", 3))
             outfile = argv[i] + 3;
     }
     if (seconds <= 0) seconds = DEFAULT_DURATION;
 
     signal(SIGINT, sigint_handler);
 
     fprintf(stderr, "[INFO] 输出文件: %s\n", outfile);
     fprintf(stderr, "[INFO] 录制时长: %d 秒\n", seconds);
 
     /* ---------- 初始化 ---------- */
     avdevice_register_all();
     avformat_network_init();
 
     /* ---------- 打开输入 ---------- */
     AVInputFormat *ifmt = av_find_input_format("pulse");
     char *idev_free = get_default_monitor();
     const char *idev = idev_free ? idev_free : "default";
     fprintf(stderr, "[INFO] 使用 PulseAudio 源: %s\n", idev);
 
     AVDictionary *fmt_opts = NULL;
     av_dict_set(&fmt_opts, "sample_rate", "44100", 0);
     av_dict_set(&fmt_opts, "channels", "2", 0);
 
     AVFormatContext *ifmt_ctx = NULL;
     int ret = avformat_open_input(&ifmt_ctx, idev, ifmt, &fmt_opts);
     if (ret < 0) { log_err("avformat_open_input", ret); return 1; }
 
     ret = avformat_find_stream_info(ifmt_ctx, NULL);
     if (ret < 0) { log_err("avformat_find_stream_info", ret); return 1; }
 
     int audio_idx = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
     if (audio_idx < 0) { fprintf(stderr, "no audio stream\n"); return 1; }
 
     /* ---------- 准备输出 ---------- */
     AVFormatContext *ofmt_ctx = NULL;
     ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, outfile);
     if (ret < 0) { log_err("avformat_alloc_output_context2", ret); return 1; }
 
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
 
     /* ---------- 重采样 ---------- */
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
         fprintf(stderr, "[INFO] 启用重采样\n");
     }
 
     /* ---------- 主循环 ---------- */
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
     int64_t last_report = 0;
 
     fprintf(stderr, "[INFO] 开始录制，按 Ctrl+C 可提前结束\n");
 
     while (g_running && av_gettime() < end) {
         ret = av_read_frame(ifmt_ctx, pkt);
         if (ret < 0) {
             if (ret != AVERROR(EAGAIN)) break;
             continue;
         }
         if (pkt->stream_index != audio_idx) { av_packet_unref(pkt); continue; }
 
         int64_t now = av_gettime();
         if (now - last_report >= AV_TIME_BASE) {
             int64_t done   = now - start;
             int64_t remain = end - now;
             fprintf(stderr, "[PROGRESS] 已录 %lld s, 剩余 %lld s\n",
                     (long long)(done / AV_TIME_BASE),
                     (long long)(remain / AV_TIME_BASE));
             last_report = now;
         }
 
         if (swr) {
             in_frame->nb_samples = pkt->size /
                 (av_get_bytes_per_sample(par->format) * par->channels);
             in_frame->format      = par->format;
             in_frame->channel_layout = par->channel_layout ? par->channel_layout
                                                              : av_get_default_channel_layout(par->channels);
             avcodec_fill_audio_frame(in_frame, par->channels, par->format,
                                      pkt->data, pkt->size, 1);
 
             int out_samples = swr_convert(swr,
                                           out_frame->data, out_frame->nb_samples,
                                           (const uint8_t **)in_frame->data, in_frame->nb_samples);
             if (out_samples < 0) { log_err("swr_convert", out_samples); continue; }
             out_frame->nb_samples = out_samples;
         } else {
             pkt->stream_index = ost->index;
             av_packet_rescale_ts(pkt,
                                  ifmt_ctx->streams[audio_idx]->time_base,
                                  ost->time_base);
             av_interleaved_write_frame(ofmt_ctx, pkt);
             av_packet_unref(pkt);
             continue;
         }
 
         out_frame->pts = av_rescale_q(now - start, AV_TIME_BASE_Q, enc_ctx->time_base);
         ret = avcodec_send_frame(enc_ctx, out_frame);
         if (ret < 0) { log_err("avcodec_send_frame", ret); continue; }
 
         while ((ret = avcodec_receive_packet(enc_ctx, pkt)) >= 0) {
             pkt->stream_index = ost->index;
             av_packet_rescale_ts(pkt, enc_ctx->time_base, ost->time_base);
             av_interleaved_write_frame(ofmt_ctx, pkt);
             av_packet_unref(pkt);
         }
     }
 
     /* ---------- flush ---------- */
     avcodec_send_frame(enc_ctx, NULL);
     while (avcodec_receive_packet(enc_ctx, pkt) >= 0) {
         pkt->stream_index = ost->index;
         av_packet_rescale_ts(pkt, enc_ctx->time_base, ost->time_base);
         av_interleaved_write_frame(ofmt_ctx, pkt);
         av_packet_unref(pkt);
     }
 
     /* ---------- 收尾 ---------- */
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
 
     fprintf(stderr, "[INFO] 录制结束，文件保存至: %s\n", outfile);
     free(idev_free);
     return 0;
 }