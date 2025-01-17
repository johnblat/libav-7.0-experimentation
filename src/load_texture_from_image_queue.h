#pragma once

#include "raylib.h"
#include "texture_ring.h"
#include <atomic>

#define LOAD_QUEUE_SIZE RING_CAP

typedef struct LoadTextureFromImgRecord
{
    Image image;
    int64_t frame_num;
    int64_t tfring_idx;
    bool is_keyframe;
} LoadTextureFromImgRecord;

typedef struct LoadTextureFromImgSpscQueue
{
    LoadTextureFromImgRecord records[LOAD_QUEUE_SIZE];
    void *image_data;
    int64_t cap;
    std::atomic<uint64_t> read_head;
    std::atomic<uint64_t> write_tail;
} LoadTextureFromImgSpscQueue;

void load_texture_from_img_queue_init(LoadTextureFromImgSpscQueue *queue, int64_t img_w,
                                      int64_t img_h);
bool load_image_from_texture_queue_empty(LoadTextureFromImgSpscQueue *queue);
int load_image_from_texture_queue_push(LoadTextureFromImgSpscQueue *queue, int64_t frame_num,
                                       int64_t tfring_idx);
LoadTextureFromImgRecord load_image_from_texture_queue_pop(LoadTextureFromImgSpscQueue *queue);
int64_t load_image_from_texture_queue_length(LoadTextureFromImgSpscQueue *queue);