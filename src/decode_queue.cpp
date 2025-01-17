#pragma once
#include "decode_queue.h"
#include "state.h"
#include "texture_ring.h"
#include <atomic>

void decode_request_queue_init(DecodeRequestSpscQueue *queue)
{
    for (int i = 0; i < DECODE_QUEUE_SIZE; i++)
    {
        queue->records[i].start_frame = 0;
        queue->records[i].nb_frames = 0;
    }
    queue->cap = DECODE_QUEUE_SIZE;
    queue->read_head.store(0);
    queue->write_tail.store(0);
}

void decode_request_queue_push(DecodeRequestSpscQueue *queue, DecodeRequestRecord record)
{
    uint64_t write_tail = queue->write_tail.load();
    uint64_t next_write_tail = (write_tail + 1) % queue->cap;

    queue->records[write_tail] = record;
    queue->write_tail.store(next_write_tail);
    int start_idx = ring_subsection_to_index(record.tfring_subsection_udpate);
    int ret = 0;
    // for (int i = 0; i < record.nb_frames; i++)
    // {
    //     int tfring_idx = (start_idx + i) % tfring.cap;
    //     int frame_num = record.start_frame + i;
    //     tfring.frame_numbers[tfring_idx] = frame_num;
    // }
}

DecodeRequestRecord decode_request_queue_pop(DecodeRequestSpscQueue *queue)
{
    uint64_t read_head = queue->read_head.load();
    while (read_head == queue->write_tail.load())
    {
        // queue is empty
    }
    DecodeRequestRecord ret = queue->records[read_head];
    queue->read_head.store((read_head + 1) % queue->cap);
    return ret;
}

bool decode_request_queue_empty(DecodeRequestSpscQueue *queue)
{
    return queue->read_head.load() == queue->write_tail.load();
}

int64_t decode_request_queue_length(DecodeRequestSpscQueue *queue)
{
    return (queue->write_tail.load() - queue->read_head.load() + queue->cap) % queue->cap;
}