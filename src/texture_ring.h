// texture_ring.h
#pragma once

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <raylib.h>
}
#include <stdint.h>

#define RING_SIZE 48
#define NUM_RING_SUBSECTIONS 3
#define RING_SUBSECTION_SIZE (RING_SIZE / NUM_RING_SUBSECTIONS)
#define TRANSITON_TABLE_SIZE (NUM_RING_SUBSECTIONS * 2)

typedef struct TextureFrameRing
{
    Texture2D textures[RING_SIZE]; // Array of  textures
    int frame_numbers[RING_SIZE];  // Array of frame numbers
    uint64_t nb;                   // Size of the ring
    uint64_t cap;                  // Capacity of the ring
    uint64_t pos;                  // Current position in ring
    uint64_t prev_pos;             // Previous position in ring
    int width;                     // Width of textures
    int height;                    // Height of textures
    PixelFormat format;            // pixel format
} TextureFrameRing;

typedef struct RingSubsectionTransitionUpdateRecord
{
    uint64_t ring_subsection_from;
    uint64_t ring_subsection_to;
} RingSubsectionTransitionUpdateRecord;

uint64_t ring_index_to_subsection(uint64_t index);

uint64_t ring_subsection_to_index(uint64_t subsection);
int subsection_transition_calculate_update(uint64_t from, uint64_t to);

int calculate_frame_nb_update_val(int from_subsection, int to_subsection);

TextureFrameRing ring_init(PixelFormat fmt);
int ring_fill(TextureFrameRing *ring, int64_t start_frame);
int ring_fill_subsection(TextureFrameRing *ring, int64_t start_frame,
                         uint64_t subsection);
Texture2D ring_get_curr(TextureFrameRing *ring);
void ring_next(TextureFrameRing *ring);
void ring_prev(TextureFrameRing *ring);
void ring_draw_curr(TextureFrameRing *ring, Rectangle dst);
void ring_draw_strip(TextureFrameRing *ring, int x, int y, int w, int h);