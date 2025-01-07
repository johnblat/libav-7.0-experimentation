// texture_ring.h
#pragma once

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <raylib.h>
}
#include <stdint.h>

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
    Texture2D textures[RING_SIZE]; // Array of  textures
    int frame_numbers[RING_SIZE];  // Array of frame numbers
    uint64_t nb;                   // Size of the ring
    uint64_t cap;                  // Capacity of the ring
    uint64_t pos;                  // Current position in ring
    int width;                     // Width of textures
    int height;                    // Height of textures
    PixelFormat format;            // pixel format
} TextureFrameRing;

// Core ring operations
TextureFrameRing ring_init(PixelFormat fmt);
int ring_fill(TextureFrameRing *ring, int64_t start_frame);
Texture2D ring_get_curr(TextureFrameRing *ring);
void ring_next(TextureFrameRing *ring);
void ring_prev(TextureFrameRing *ring);
void ring_render_curr(TextureFrameRing *ring, Rectangle dst);
void ring_render_strip(TextureFrameRing *ring, int x, int y, int w, int h);