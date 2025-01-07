// texture_ring.cpp
#include "texture_ring.h"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "read.h"
#include "seek.h"

static int internal_decode_and_convert_frame(AVFormatContext *fmt_ctx,
                                             AVCodecContext *codec_ctx,
                                             int video_stream_idx, AVFrame *frame)
{
    AVPacket *packet = av_packet_alloc();
    int ret;

    for (;;)
    {
        ret = av_read_frame(fmt_ctx, packet);
        if (ret < 0)
        {
            av_packet_free(&packet);
            return ret;
        }
        if (packet->stream_index != video_stream_idx)
        {
            continue;
        }
        ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0)
        {
            av_packet_free(&packet);
            return ret;
        }
        ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret == AVERROR(EAGAIN))
        {
            continue;
        }
        break;
    }

    av_packet_unref(packet);
    return 0;
}

int ring_init(TextureFrameRing *ring, SDL_Renderer *renderer, int width, int height,
              Uint32 format, AVFormatContext *ic, AVCodecContext *codec_ctx,
              int video_stream_index)
{
    ring->textures = (SDL_Texture **)calloc(RING_SIZE, sizeof(SDL_Texture *));
    ring->frame_numbers = (int64_t *)calloc(RING_SIZE, sizeof(int64_t));
    ring->nb = 0;
    ring->cap = RING_SIZE;
    ring->pos = 0;
    ring->width = width;
    ring->height = height;
    ring->format = format;

    if (!ring->textures || !ring->frame_numbers)
    {
        ring_free(ring);
        return -1;
    }

    // Initialize all textures
    for (size_t i = 0; i < RING_SIZE; i++)
    {
        ring->textures[i] = SDL_CreateTexture(
            renderer, format, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!ring->textures[i])
        {
            ring_free(ring);
            return -1;
        }
        ring->frame_numbers[i] = -1;
    }

    AVFrame *frame = av_frame_alloc();
    // seek to beginning
    int ret = avformat_seek_file(ic, video_stream_index, 0, 0, 0, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        return ret;
    }
    avcodec_flush_buffers(codec_ctx);
    ret = internal_decode_and_convert_frame(ic, codec_ctx, video_stream_index, frame);

    SDL_UpdateYUVTexture(ring->textures[ring->pos], NULL, frame->data[0],
                         frame->linesize[0], frame->data[1], frame->linesize[1],
                         frame->data[2], frame->linesize[2]);
    ring->frame_numbers[ring->pos] = 0;

    return 0;
}

void ring_free(TextureFrameRing *ring)
{
    if (ring->textures)
    {
        for (size_t i = 0; i < ring->nb; i++)
        {
            if (ring->textures[i])
            {
                SDL_DestroyTexture(ring->textures[i]);
            }
        }
        free(ring->textures);
    }
    if (ring->frame_numbers)
    {
        free(ring->frame_numbers);
    }
}

int ring_step_forward(TextureFrameRing *ring, SDL_Renderer *renderer,
                      AVFormatContext *fmt_ctx, AVCodecContext *codec_ctx,
                      int video_stream_idx, int64_t nb_total_frames)
{
    // Check if we're at the end of the stream
    int64_t next_frame_num = ring->frame_numbers[ring->pos] + 1;
    if (next_frame_num >= nb_total_frames)
    {
        return -1;
    }

    // Calculate next position in ring
    size_t next_pos = (ring->pos + 1) % ring->nb;

    // Decode next frame
    AVFrame *frame = av_frame_alloc();
    int ret =
        internal_decode_and_convert_frame(fmt_ctx, codec_ctx, video_stream_idx, frame);
    if (ret < 0)
    {
        av_frame_free(&frame);
        return ret;
    }

    // Update texture with new frame
    SDL_UpdateYUVTexture(ring->textures[next_pos], NULL, frame->data[0],
                         frame->linesize[0], frame->data[1], frame->linesize[1],
                         frame->data[2], frame->linesize[2]);

    // Update frame number and position
    ring->frame_numbers[next_pos] = next_frame_num;
    ring->pos = next_pos;

    av_frame_free(&frame);
    return 0;
}

int ring_step_backward(TextureFrameRing *ring)
{
    // Check if we can move backward
    if (ring->frame_numbers[ring->pos] <= 0)
    {
        return NULL;
    }

    // Calculate previous position
    size_t prev_pos = (ring->pos == 0) ? ring->nb - 1 : ring->pos - 1;

    // If the frame at prev_pos is valid
    if (ring->frame_numbers[prev_pos] == ring->frame_numbers[ring->pos] - 1)
    {
        ring->pos = prev_pos;
        ring->update_oldset;
        return 0;
    }

    return -1;
}

int ring_update_oldest_texture(TextureFrameRing *ring, AVFormatContext *ic,
                               int stream_index, AVCodecContext *codec_ctx)
{
    // Calculate next position in ring
    size_t next_pos = (ring->pos + 1) % ring->nb;

    // Seek to the frame number of the oldest element
    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    int ret = seek_to_frame(ic, stream_index, codec_ctx, frame, pkt,
                            ring->frame_numbers[next_pos]);
    if (ret < 0)
    {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        return ret;
    }

    // Update texture with new frame
    SDL_UpdateYUVTexture(ring->textures[next_pos], NULL, frame->data[0],
                         frame->linesize[0], frame->data[1], frame->linesize[1],
                         frame->data[2], frame->linesize[2]);

    // Update frame number and position
    ring->frame_numbers[next_pos] = ring->frame_numbers[ring->pos] + 1;
    ring->pos = next_pos;

    // Read frames to get back to original position
    ret = read_n_frames(ic, stream_index, codec_ctx,
                        ring->frame_numbers[ring->pos] - ring->frame_numbers[next_pos]);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    return (ret < 0) ? ret : 0;
}

// `ring_fill`
//
// Fill the ring with frames from the stream starting at `start_frame`.
// returns nb frames read on success. Negative value on error.
int ring_fill(TextureFrameRing *ring, AVFormatContext *ic, AVCodecContext *codec_ctx,
              int video_stream_index, int64_t start_frame)
{
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    int ret = seek_to_frame(ic, video_stream_index, codec_ctx, frame, pkt, start_frame);

    int err = 0;
    int frames_read = 0;

    while (frames_read < ring->cap)
    {
        if ((err = av_read_frame(ic, pkt)) < 0)
        {
            break;
        }

        if (pkt->stream_index != video_stream_index)
        {
            av_packet_unref(pkt);
            continue;
        }

        if ((err = avcodec_send_packet(codec_ctx, pkt)) < 0)
        {
            break;
        }

        if ((err = avcodec_receive_frame(codec_ctx, frame)) < 0)
        {
            if (err == AVERROR(EAGAIN))
            {
                av_packet_unref(pkt);
                continue;
            }
            break;
        }

        ring->width = frame->width;
        ring->height = frame->height;
        ring->format = frame->format;

        SDL_UpdateYUVTexture(ring->textures[frames_read], NULL, frame->data[0],
                             frame->linesize[0], frame->data[1], frame->linesize[1],
                             frame->data[2], frame->linesize[2]);
        ring->frame_numbers[frames_read] = start_frame + frames_read;

        frames_read++;
        av_packet_unref(pkt);
        av_frame_unref(frame);
    }

    if (err < 0 && err != AVERROR_EOF)
    {
        return err;
    }

    ring->nb = frames_read;
    // ring->width = frame->width;
    // ring->height = frame->height;
    // ring->format = frame->format;

    av_packet_free(&pkt);
    av_frame_free(&frame);

    return ring->nb;
}

void ring_next(TextureFrameRing *ring)
{
    ring->pos = (ring->pos + 1) % ring->nb;
}

void ring_prev(TextureFrameRing *ring)
{
    ring->pos = (ring->pos + ring->nb - 1) % ring->nb;
}

SDL_Texture *ring_get_current_texture(TextureFrameRing *ring)
{
    return ring->textures[ring->pos];
}

SDL_Texture *ring_get_next(TextureFrameRing *ring)
{
    ring_next(ring);
    return ring_get_current_texture(ring);
}

SDL_Texture *ring_get_prev(TextureFrameRing *ring)
{
    ring_prev(ring);
    return ring_get_current_texture(ring);
}

void ring_render_as_strip(SDL_Renderer *renderer, TextureFrameRing *ring, int x, int y,
                          int w, int h)
{
    int texture_dst_height = h;
    int texture_dst_width = w / ring->nb;

    for (int i = x; i < ring->nb; i++)
    {
        SDL_Rect dst_rect = {i * texture_dst_width, y, texture_dst_width,
                             texture_dst_height};
        SDL_RenderCopy(renderer, ring->textures[i], NULL, &dst_rect);
        if (i == ring->pos)
        {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &dst_rect);
        }
    }
}