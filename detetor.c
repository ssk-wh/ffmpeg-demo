#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <stdio.h>

void print_codec_info(enum AVCodecID codec_id) {
    switch (codec_id) {
        case AV_CODEC_ID_H264:
            printf("视频编码格式: H.264\n");
            break;
        case AV_CODEC_ID_H265:
            printf("视频编码格式: H.265 (HEVC)\n");
            break;
        case AV_CODEC_ID_VP9:
            printf("视频编码格式: VP9\n");
            break;
        default:
            printf("视频编码格式: 未知 (%d)\n", codec_id);
            break;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("用法: %s <输入文件>\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    AVFormatContext *fmt_ctx = NULL;
    AVCodec *codec = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVStream *stream = NULL;
    int ret;

    // 打开输入文件
    if ((ret = avformat_open_input(&fmt_ctx, input_file, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "无法打开输入文件\n");
        return ret;
    }

    // 获取流信息
    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "无法获取流信息\n");
        return ret;
    }

    // 查找视频流
    int video_stream_index = -1;
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        av_log(NULL, AV_LOG_ERROR, "未找到视频流\n");
        return -1;
    }

    stream = fmt_ctx->streams[video_stream_index];

    // 打印视频流的编解码器信息
    print_codec_info(stream->codecpar->codec_id);

    // 查找解码器
    codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "未找到解码器\n");
        return -1;
    }

    // 分配解码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        av_log(NULL, AV_LOG_ERROR, "无法分配解码器上下文\n");
        return -1;
    }

    // 将流参数复制到解码器上下文
    if ((ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "无法复制解码器参数\n");
        return ret;
    }

    // 打开解码器
    if ((ret = avcodec_open2(codec_ctx, codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "无法打开解码器\n");
        return ret;
    }

    // 释放资源
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    return 0;
}