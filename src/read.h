#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// Reads n frames from the current position in the video stream
int read_n_frames(AVFormatContext *ic, int stream_index, AVCodecContext *codec_ctx, int n);

