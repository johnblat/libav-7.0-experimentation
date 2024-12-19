// texture_ring.h
#pragma once

#include <SDL.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#define RING_SIZE 16

typedef struct
{
    SDL_Texture **textures; // Array of SDL textures
    int64_t *frame_numbers; // Array of frame numbers
    size_t pos;             // Current position in ring
    size_t capacity;        // Size of the ring
    int width;              // Width of textures
    int height;             // Height of textures
    Uint32 format;          // SDL pixel format
    bool update_oldset;
} texture_ring_t;

// Core ring operations
int ring_init(texture_ring_t *ring, SDL_Renderer *renderer, int width, int height,
              Uint32 format);
void ring_free(texture_ring_t *ring);

// Movement operations
int ring_step_forward(texture_ring_t *ring, SDL_Renderer *renderer,
                      AVFormatContext *fmt_ctx, AVCodecContext *codec_ctx,
                      int video_stream_idx, int64_t total_frames);

int ring_step_backward(texture_ring_t *ring, int64_t *frame_num);