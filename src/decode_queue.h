#pragma once

#include <atomic>

typedef struct DecodeRequestRecord
{
    int64_t start_frame;
    int64_t nb_frames;
    int64_t tfring_subsection_udpate;
} DecodeRequestRecord;

#define DECODE_QUEUE_SIZE 8

typedef struct DecodeRequestSpscQueue
{
    DecodeRequestRecord records[DECODE_QUEUE_SIZE];
    int64_t cap;
    std::atomic<uint64_t> read_head;
    std::atomic<uint64_t> write_tail;
} DecodeRequestSpscQueue;

void decode_request_queue_init(DecodeRequestSpscQueue *queue);
bool decode_request_queue_empty(DecodeRequestSpscQueue *queue);
void decode_request_queue_push(DecodeRequestSpscQueue *queue, DecodeRequestRecord record);
DecodeRequestRecord decode_request_queue_pop(DecodeRequestSpscQueue *queue);
int64_t decode_request_queue_length(DecodeRequestSpscQueue *queue);
// Single-Producer Single-Consumer queue for decoding AVFrames into Raylib Images