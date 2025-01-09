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
        err = decode_next_frame();
        if (err < 0)
            break;
    }

    return err;
}

int decode_next_frame()
{
    av_frame_unref(curr_frame);
    int ret = 0;

    for (;;)
    {
        av_packet_unref(curr_pkt);

        if ((ret = av_read_frame(ic, curr_pkt)) < 0)
            break;

        if (curr_pkt->stream_index != video_stream_index)
            continue;

        if ((ret = avcodec_send_packet(codec_ctx, curr_pkt)) < 0)
            break;

        if ((ret = avcodec_receive_frame(codec_ctx, curr_frame)) < 0)
        {
            if (ret == AVERROR(EAGAIN))
                continue;
        }
        break;
    }

    return ret;
}