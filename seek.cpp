#include "seek.h"
#include "pts_frame_conversions.h"

// Helper function to estimate frame timestamp
// static int64_t estimate_frame_timestamp(AVFormatContext *ic, int stream_index, int
// frame_num) {
//     AVStream *st = ic->streams[stream_index];
//     double frame_rate = av_q2d(st->r_frame_rate);
//     double time_base = av_q2d(st->time_base);
//     double time_in_seconds = frame_num / frame_rate;
//     return (int64_t)(time_in_seconds / time_base);
// }

int seek_to_frame(AVFormatContext *ic, int stream_index, AVCodecContext *codec_ctx,
                  AVFrame *frame, AVPacket *pkt, int target_frame)
{
    int64_t target_ts = estimate_frame_timestamp(ic, stream_index, target_frame);

    int err = 0;

    if (err = avformat_seek_file(ic, stream_index, INT64_MIN, target_ts, INT64_MAX,
                                 AVSEEK_FLAG_BACKWARD),
        err < 0)
    {
        return err;
    }

    avcodec_flush_buffers(codec_ctx);

    for (;;)
    {
        if (err = av_read_frame(ic, pkt), err < 0)
        {
            return err;
        }

        if (pkt->stream_index != stream_index)
        {
            av_packet_unref(pkt);
            continue;
        }

        if (err = avcodec_send_packet(codec_ctx, pkt), err < 0)
        {
            return err;
        }

        if (err = avcodec_receive_frame(codec_ctx, frame), err < 0)
        {
            if (err == AVERROR(EAGAIN))
            {
                av_packet_unref(pkt);
                continue;
            }

            if (err == AVERROR_EOF)
            {
                av_packet_unref(pkt);
                break;
            }

            return err;
        }

        if (frame->pts >= target_ts)
        {
            av_packet_unref(pkt);
            break;
        }

        av_packet_unref(pkt);
        continue;
    }
    return 0;
}
