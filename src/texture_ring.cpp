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
#include "state.h"

static int internal_decode_next_frame()
{
    av_frame_unref(curr_frame);
    int err = 0;

    for (;;)
    {
        av_packet_unref(curr_pkt);

        if ((err = av_read_frame(ic, curr_pkt)) < 0)
            break;

        if (curr_pkt->stream_index != video_stream_index)
            continue;

        if ((err = avcodec_send_packet(codec_ctx, curr_pkt)) < 0)
            break;

        if ((err = avcodec_receive_frame(codec_ctx, curr_frame)) < 0)
        {
            if (err == AVERROR(EAGAIN))
                continue;
        }
        break;
    }

    return err != 0 ? err : 0;
}

TextureFrameRing ring_init(PixelFormat format)
{
    TextureFrameRing ring = {0};
    ring.cap = RING_SIZE;
    ring.format = format;

    // Initialize all textures
    for (size_t i = 0; i < RING_SIZE; i++)
    {
        ring.frame_numbers[i] = -1;
    }

    return ring;
}

// `ring_fill`
//
// Fill the ring with frames from the stream starting at `start_frame`.
// returns nb frames read on success. Negative value on error.
int ring_fill(TextureFrameRing *ring, int64_t start_frame)
{
    for (int i = 0; i < ring->cap; i++)
        if (IsTextureValid(ring->textures[i]))
            UnloadTexture(ring->textures[i]);

    ring->width = ic->streams[video_stream_index]->codecpar->width;
    ring->height = ic->streams[video_stream_index]->codecpar->height;

    int ret = seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt,
                            start_frame);

    int err = 0;
    for (ring->nb = 0; ring->nb < ring->cap; ring->nb++)
    {

        err = internal_decode_next_frame();
        if (err < 0)
            break;

        const int rgbLineSize = ring->width * 3;

        err = sws_scale(sws_ctx, (const uint8_t *const *)curr_frame->data,
                        curr_frame->linesize, 0, ring->height,
                        (uint8_t *const *)&curr_frame_image.data, &rgbLineSize);
        if (err < 0)
            break;

        curr_frame_image.width = curr_frame->width;
        curr_frame_image.height = curr_frame->height;
        curr_frame_image.mipmaps = 1;
        curr_frame_image.format = ring->format;

        ring->textures[ring->nb] = LoadTextureFromImage(curr_frame_image);
        ring->frame_numbers[ring->nb] = start_frame + ring->nb;
    }

    return err < 0 && err != AVERROR_EOF ? err : ring->nb;
}

void ring_next(TextureFrameRing *ring)
{
    ring->pos = (ring->pos + 1) % ring->nb;
}

void ring_prev(TextureFrameRing *ring)
{
    ring->pos = (ring->pos + ring->nb - 1) % ring->nb;
}

Texture2D ring_get_curr(TextureFrameRing *ring)
{
    return ring->textures[ring->pos];
}

void ring_render_curr(TextureFrameRing *ring, Rectangle dst)
{
    DrawTexture(ring->textures[ring->pos], dst.x, dst.y, WHITE);
}

void ring_render_strip(TextureFrameRing *ring, int x, int y, int w, int h)
{
    float texture_dst_height = h;
    float texture_dst_width = w / ring->nb;

    for (int i = 0; i < ring->nb; i++)
    {
        Rectangle dst = {x + i * texture_dst_width, y, texture_dst_width,
                         texture_dst_height};
        Rectangle src = {0, 0, (float)ring->width, (float)ring->height};

        DrawTexturePro(ring->textures[i], src, dst, {0, 0}, 0, WHITE);

        if (i == ring->pos)
            DrawRectangleLines(x + i * texture_dst_width, y, texture_dst_width,
                               texture_dst_height, YELLOW);
    }
}