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

//
// Helper functions
//

int internal_modulo(int a, int b)
{
    return (a % b + b) % b;
}

int internal_avframe_to_image(AVFrame *frame, Image *image)
{
    const int rgbLineSize = frame->width * 3;

    int ret = 0;

    ret = sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0,
                    frame->height, (uint8_t *const *)&image->data, &rgbLineSize);
    if (ret < 0)
        return ret;

    image->width = frame->width;
    image->height = frame->height;
    image->mipmaps = 1;
    image->format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;

    return ret;
}

int internal_decode_into_curr_image(int width, int height, PixelFormat format)
{
    int ret = 0;
    ret = decode_next_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt);
    if (ret < 0)
        return ret;
    ret = internal_avframe_to_image(curr_frame, &curr_frame_image);
    return ret;
}

//
// Ring Procedures
//

// `ring_init`
//
// Initialize a new ring with the given pixel format.
TextureFrameRing ring_init(PixelFormat format)
{
    TextureFrameRing ring = {0};
    ring.cap = RING_SIZE;
    ring.format = format;

    for (size_t i = 0; i < RING_SIZE; i++)
        ring.frame_numbers[i] = -1;

    int curr_pos = 1;
    int prev_pos = 0;
    for (int i = 0; i < TRANSITON_TABLE_SIZE / 2; i++)
    {
        ring_subsection_transition_update_table[i].ring_subsection_from = prev_pos;
        ring_subsection_transition_update_table[i].ring_subsection_to = curr_pos;
        prev_pos = curr_pos;
        curr_pos = (curr_pos + 1) % NUM_RING_SUBSECTIONS;
    }
    curr_pos = 2;
    prev_pos = 0;
    for (int i = TRANSITON_TABLE_SIZE / 2; i < TRANSITON_TABLE_SIZE; i++)
    {
        ring_subsection_transition_update_table[i].ring_subsection_from = prev_pos;
        ring_subsection_transition_update_table[i].ring_subsection_to = curr_pos;
        prev_pos = curr_pos;
        curr_pos = internal_modulo((curr_pos - 1), NUM_RING_SUBSECTIONS);
    }
    return ring;
}

// `ring_fill`
//
// Fill the ring with frames from the stream starting at `start_frame`.
// returns nb frames read on success. Negative value on error.
int ring_fill(TextureFrameRing *ring, int64_t start_frame)
{
    if (start_frame < 0 || start_frame > ic->streams[video_stream_index]->nb_frames)
        start_frame = 0;

    for (int i = 0; i < ring->cap; i++)
        if (IsTextureValid(ring->textures[i]))
            UnloadTexture(ring->textures[i]);

    ring->width = ic->streams[video_stream_index]->codecpar->width;
    ring->height = ic->streams[video_stream_index]->codecpar->height;

    int ret = seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt,
                            start_frame);

    for (ring->nb = 0; ring->nb < ring->cap; ring->nb++)
    {
        ret = internal_decode_into_curr_image(ring->width, ring->height, ring->format);
        if (ret == AVERROR_EOF)
        { // reached the end of stream so start over
            ret = avformat_seek_file(ic, video_stream_index, INT64_MIN, 0, INT64_MAX,
                                     AVSEEK_FLAG_BACKWARD);
            if (ret < 0)
                return ret;

            avcodec_flush_buffers(codec_ctx);

            ret = internal_decode_into_curr_image(ring->width, ring->height,
                                                  ring->format);
            if (ret < 0)
                break;
        }

        if (ret < 0)
            break;
        ring->textures[ring->nb] = LoadTextureFromImage(curr_frame_image);
        ring->frame_numbers[ring->nb] = start_frame + ring->nb;
    }

    return ret < 0 && ret != AVERROR_EOF ? ret : ring->nb;
}

void ring_next(TextureFrameRing *ring)
{
    ring->prev_pos = ring->pos;
    ring->pos = internal_modulo((ring->pos + 1), ring->nb);
}

void ring_prev(TextureFrameRing *ring)
{
    ring->prev_pos = ring->pos;
    ring->pos = internal_modulo((ring->pos - 1), ring->nb);
}

void ring_draw_curr(TextureFrameRing *ring, Rectangle dst)
{
    DrawTexture(ring->textures[ring->pos], dst.x, dst.y, WHITE);
}

void ring_draw_strip(TextureFrameRing *ring, int x, int y, int w, int h)
{
    float texture_dst_height = h;
    float texture_dst_width = w / ring->nb;

    for (int i = 0; i < ring->nb; i++)
    {
        Rectangle dst = {x + i * texture_dst_width, (float)y, texture_dst_width,
                         texture_dst_height};
        Rectangle src = {0, 0, (float)ring->width, (float)ring->height};

        DrawTexturePro(ring->textures[i], src, dst, {0, 0}, 0, WHITE);

        if (i == ring->pos)
            DrawRectangleLines(x + i * texture_dst_width, y, texture_dst_width,
                               texture_dst_height, YELLOW);
    }
}

int ring_fill_subsection(TextureFrameRing *ring, int64_t start_frame,
                         uint64_t subsection)
{
    if (start_frame < 0 || start_frame > ic->streams[video_stream_index]->nb_frames)
        start_frame = 0;

    int start_idx = ring_subsection_to_index(subsection);

    int ret = seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt,
                            start_frame);
    if (ret < 0)
        return ret;

    for (int i = 0; i < RING_SUBSECTION_SIZE; i++)
    {
        ret = internal_decode_into_curr_image(ring->width, ring->height, ring->format);
        if (ret == AVERROR_EOF)
        { // reached the end of stream so start over
            ret = avformat_seek_file(ic, video_stream_index, INT64_MIN, 0, INT64_MAX,
                                     AVSEEK_FLAG_BACKWARD);
            if (ret < 0)
                return ret;

            avcodec_flush_buffers(codec_ctx);

            ret = internal_decode_into_curr_image(ring->width, ring->height,
                                                  ring->format);
            if (ret < 0)
                break;
        }
        if (ret < 0)
            break;
        int idx = start_idx + i;
        UnloadTexture(ring->textures[idx]);
        ring->textures[idx] = LoadTextureFromImage(curr_frame_image);
        ring->frame_numbers[idx] = start_frame + i;
    }

    return ret;
}

int calculate_frame_nb_update_val(int from_subsection, int to_subsection)
{
    int subsection_update =
        subsection_transition_calculate_update(from_subsection, to_subsection);
    if (subsection_update == -1)
        return -1;
    int diff =
        (to_subsection - from_subsection + NUM_RING_SUBSECTIONS) % NUM_RING_SUBSECTIONS;
    int dir = 0;
    if (diff == 1)
        dir = 1;
    else if (diff == NUM_RING_SUBSECTIONS - 1)
        dir = -1;
    else
        return -1;
    int load_start_idx = ring_subsection_to_index(subsection_update);
    int to_subsection_start_idx = ring_subsection_to_index(to_subsection);
    uint64_t start_fnb_load =
        tfring.frame_numbers[to_subsection_start_idx] + (dir * RING_SUBSECTION_SIZE);
    return start_fnb_load;
}

int subsection_transition_calculate_update(uint64_t from, uint64_t to)
{
    const int no_update = -1;
    int update_subsection = (NUM_RING_SUBSECTIONS - from - to) % NUM_RING_SUBSECTIONS;

    // make sure its an actual transition
    for (int i = 0; i < TRANSITON_TABLE_SIZE; i++)
        if (ring_subsection_transition_update_table[i].ring_subsection_from == from &&
            ring_subsection_transition_update_table[i].ring_subsection_to == to)
            return update_subsection;

    return no_update;
}

uint64_t ring_subsection_to_index(uint64_t subsection)
{
    return subsection * RING_SUBSECTION_SIZE;
}

uint64_t ring_index_to_subsection(uint64_t index)
{
    uint64_t ret = index / RING_SUBSECTION_SIZE;
    return ret;
}

int load_request = -1;
uint64_t frame_start_load_request = 0;

void ring_load_request_thread()
{
    for (;;)
    {
        if (load_request >= 0 && load_request < RING_SUBSECTION_SIZE)
        {
            ring_fill_subsection(&tfring, frame_start_load_request, load_request);
            load_request = -1;
        }
        WaitTime(1 / 30);
    }
}