#include "seek.h"
#include "pts_frame_conversions.h"
#include "read.h"

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
                  AVFrame *frame, AVPacket *pkt, int target_frame_nb)
{
    int64_t target_ts = estimate_frame_timestamp(ic, stream_index, target_frame_nb);

    int ret = 0;

    ret = avformat_seek_file(ic, stream_index, INT64_MIN, target_ts, INT64_MAX,
                             AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codec_ctx);
    if (ret < 0 || target_ts == 0)
        return ret;

    int64_t frame_ts_before_target_ts =
        estimate_frame_timestamp(ic, stream_index, target_frame_nb - 1);

    for (;;)
    {
        ret = decode_next_frame(ic, stream_index, codec_ctx, frame, pkt);
        if (ret < 0)
            break;
        if (frame->pts >= frame_ts_before_target_ts)
            break;
    }

    return ret;
}
