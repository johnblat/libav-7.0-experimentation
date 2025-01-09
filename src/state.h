#pragma once

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include "texture_ring.h"
#include <raylib.h>

extern AVFormatContext *ic;
extern AVCodecContext *codec_ctx;
extern const AVCodec *codec;
extern int video_stream_index;
extern SwsContext *sws_ctx;
extern AVPacket *curr_pkt;
extern AVFrame *curr_frame;
extern Image curr_frame_image;

extern TextureFrameRing tfring;
extern RingSubsectionTransitionUpdateRecord
    ring_subsection_transition_update_table[TRANSITON_TABLE_SIZE];

extern AVPixelFormat av_rgb_pixel_fmt;
extern PixelFormat raylib_rgb_pixel_fmt;