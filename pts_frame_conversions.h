#pragma once

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <stdint.h>

int64_t estimate_frame_timestamp(AVFormatContext *ic, int stream_index, int framenum);
int timestamp_to_framenum(AVFormatContext *ic, int stream_index, int timestamp);
