#pragma once

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// `read_n_frames`
//
// decodes `n` valid frames and stores them in `curr_frame` and `curr_pkt`
int read_n_frames(AVFormatContext *ic, int stream_index, AVCodecContext *codec_ctx,
                  int n);

// `decode_next_frame`
//
// Reads until the next valid frame is found and decodes it in to  curr_pkt and
// curr_frame
int decode_next_frame();
