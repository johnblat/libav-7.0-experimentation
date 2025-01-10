#ifndef SEEK_H
#define SEEK_H

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// Seeks to a specific frame in a video stream
int seek_to_frame(AVFormatContext *ic, int stream_index, AVCodecContext *codec_ctx,
                  AVFrame *frame, AVPacket *pkt, int target_frame);

#endif // SEEK_H
