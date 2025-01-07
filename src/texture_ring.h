// texture_ring.h
#pragma once

#include <SDL.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#define RING_SIZE 16

// typedef struct TextureFrameRow
// {
//     SDL_Texture *texture;
//     int64_t frame_number;
// } TextureFrameNbRow;

// typedef struct TextureFramePropertiesRow
// {
//     int texture_frame_nb_index;
//     int width;
//     int height;
//     Uint32 format;
// } TextureFramePropertiesRow;

// typedef struct TextureFrameRingPositionRow
// {
//     int texture_frame_nb_index;
//     int64_t pos;
// } TextureFrameRingPositionRow;

// const int N_TEXTURE_FRAME_NB_ROWS = 16;
// TextureFrameNbRow texture_frame_nb_rows[N_TEXTURE_FRAME_NB_ROWS];

typedef struct TextureFrameRing
{
    SDL_Texture **textures; // Array of SDL textures
    int64_t *frame_numbers; // Array of frame numbers
    size_t nb;              // Size of the ring
    size_t cap;             // Capacity of the ring
    size_t pos;             // Current position in ring
    int width;              // Width of textures
    int height;             // Height of textures
    Uint32 format;          // SDL pixel format
    bool update_oldset;
} TextureFrameRing;

// Core ring operations
int ring_init(TextureFrameRing *ring, SDL_Renderer *renderer, int width, int height,
              Uint32 format, AVFormatContext *ic, AVCodecContext *codec_ctx,
              int video_stream_index);
void ring_free(TextureFrameRing *ring);

// Movement operations
int ring_step_forward(TextureFrameRing *ring, SDL_Renderer *renderer,
                      AVFormatContext *fmt_ctx, AVCodecContext *codec_ctx,
                      int video_stream_idx, int64_t total_frames);

int ring_step_backward(TextureFrameRing *ring, int64_t *frame_num);

int ring_fill(TextureFrameRing *ring, AVFormatContext *fmt_ctx,
              AVCodecContext *codec_ctx, int video_stream_idx, int64_t start_frame);

SDL_Texture *ring_get_current_texture(TextureFrameRing *ring);
SDL_Texture *ring_get_next(TextureFrameRing *ring);
SDL_Texture *ring_get_prev(TextureFrameRing *ring);
void ring_next(TextureFrameRing *ring);
void ring_prev(TextureFrameRing *ring);
void ring_render_current(SDL_Renderer *renderer, TextureFrameRing *ring, SDL_Rect dst);
void ring_render_as_strip(SDL_Renderer *renderer, TextureFrameRing *ring, int x, int y,
                          int w, int h);