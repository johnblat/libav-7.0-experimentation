#pragma once

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "state.h"
#include <raylib.h>

const char *APP_NAME = "Nekxtar";

int init_libav(const char *filename)
{
    int ret = 0;

    ic = avformat_alloc_context();

    ret = avformat_open_input(&ic, filename, NULL, NULL);
    if (ret < 0)
        return ret;

    ret = avformat_find_stream_info(ic, NULL);
    if (ret < 0)
        return ret;

    video_stream_index = -1;
    for (int i = 0; i < ic->nb_streams; i++)
        if (ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = i;
            break;
        }
    if (video_stream_index == -1)
        return -1;

    sws_ctx = sws_getContext(ic->streams[video_stream_index]->codecpar->width,
                             ic->streams[video_stream_index]->codecpar->height,
                             (AVPixelFormat)ic->streams[video_stream_index]->codecpar->format,
                             ic->streams[video_stream_index]->codecpar->width,
                             ic->streams[video_stream_index]->codecpar->height,
                             av_rgb_pixel_fmt, 0, NULL, NULL, NULL);
    if (!sws_ctx)
        return -1;

    codec_ctx = avcodec_alloc_context3(NULL);
    if (!codec_ctx)
        return -1;

    ret = avcodec_parameters_to_context(codec_ctx, ic->streams[video_stream_index]->codecpar);
    if (ret < 0)
        return ret;

    codec = avcodec_find_decoder(codec_ctx->codec_id);
    if (!codec)
        return -1;

    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0)
        return ret;

    curr_frame = av_frame_alloc();
    if (!curr_frame)
        return -1;

    curr_pkt = av_packet_alloc();
    if (!curr_pkt)
        return -1;

    return 0;
}

int init_raylib()
{
    InitWindow(1280, 720, APP_NAME);
    SetTargetFPS(24);
    SetWindowIcon(LoadImage("daisy.png"));
    return 0;
}

void close()
{
    { // raylib
    }
    { // ffmpeg/libav
        if (curr_frame)
        {
            av_frame_free(&curr_frame);
            curr_frame = NULL;
        }
        if (curr_pkt)
        {
            av_packet_unref(curr_pkt);
            av_packet_free(&curr_pkt);
            curr_pkt = NULL;
        }
        if (codec_ctx)
        {
            // TODO: fixme
            // avcodec_close(codec_ctx);
            // avcodec_free_context(&codec_ctx);
            // codec_ctx = NULL;
        }
        if (ic)
        {
            avformat_close_input(&ic);
            ic = NULL;
        }
    }
}
