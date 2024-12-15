#include "read.h"

int read_n_frames(AVFormatContext *ic, int stream_index, AVCodecContext *codec_ctx, int n) {
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int err = 0;
    int frames_read = 0;

    while (frames_read < n) {
        if ((err = av_read_frame(ic, pkt)) < 0) {
            break;
        }

        if (pkt->stream_index != stream_index) {
            av_packet_unref(pkt);
            continue;
        }

        if ((err = avcodec_send_packet(codec_ctx, pkt)) < 0) {
            break;
        }

        if ((err = avcodec_receive_frame(codec_ctx, frame)) < 0) {
            if (err == AVERROR(EAGAIN)) {
                av_packet_unref(pkt);
                continue;
            }
            break;
        }

        frames_read++;
        av_packet_unref(pkt);
        av_frame_unref(frame);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);

    if (err < 0 && err != AVERROR_EOF) {
        return err;
    }

    return frames_read;
}
