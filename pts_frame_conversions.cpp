extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <stdint.h>

int64_t estimate_frame_timestamp(AVFormatContext *ic, int stream_index, int framenum)
{
    AVStream *video_stream = ic->streams[stream_index];

    // If we have a duration, use it to estimate
    if (video_stream->duration != AV_NOPTS_VALUE && video_stream->nb_frames > 0)
    {
        return av_rescale(framenum, video_stream->duration, video_stream->nb_frames);
    }

    // If we have a frame rate, use it to estimate
    if (video_stream->avg_frame_rate.num && video_stream->avg_frame_rate.den)
    {
        double seconds =
            (double)framenum * av_q2d(av_inv_q(video_stream->avg_frame_rate));
        return av_rescale(seconds * AV_TIME_BASE, video_stream->time_base.num,
                          video_stream->time_base.den);
    }

    // If all else fails, make a very rough estimate
    return av_rescale(
        framenum, video_stream->duration > 0 ? video_stream->duration : AV_TIME_BASE,
        250);
}

int timestamp_to_framenum(AVFormatContext *ic, int stream_index, int timestamp)
{
    AVStream *video_stream = ic->streams[stream_index];

    // If we have a duration, use it to estimate
    if (video_stream->duration != AV_NOPTS_VALUE && video_stream->nb_frames > 0)
    {
        return av_rescale(timestamp, video_stream->nb_frames, video_stream->duration);
    }

    // If we have a frame rate, use it to estimate
    if (video_stream->avg_frame_rate.num && video_stream->avg_frame_rate.den)
    {
        double seconds =
            (double)timestamp * av_q2d(av_inv_q(video_stream->avg_frame_rate));
        return av_rescale(seconds * AV_TIME_BASE, video_stream->time_base.num,
                          video_stream->time_base.den);
    }

    // If all else fails, make a very rough estimate
    return av_rescale(timestamp, 250,
                      video_stream->duration > 0 ? video_stream->duration
                                                 : AV_TIME_BASE);
}