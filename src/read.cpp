#include "read.h"
#include "state.h"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

int read_n_frames(AVFormatContext *ic, int stream_index, AVCodecContext *codec_ctx,
                  int n)
{
    int err = 0;
    for (int i = 0; i < n; i++)
    {
        err = decode_next_frame(ic, stream_index, codec_ctx, curr_frame, curr_pkt);
        if (err < 0)
            break;
    }

    return err;
}

int decode_next_frame(AVFormatContext *ic, int stream_index, AVCodecContext *codec_ctx,
                      AVFrame *frame, AVPacket *pkt)
{
    int ret = 0;

    for (;;)
    {
        av_packet_unref(pkt);

        ret = av_read_frame(ic, pkt);
        if (ret < 0)
            break;

        if (pkt->stream_index != video_stream_index)
            continue;

        ret = avcodec_send_packet(codec_ctx, pkt);
        if (ret < 0)
            break;

        ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret == AVERROR(EAGAIN))
            continue;
        break;
    }

    return ret;
}