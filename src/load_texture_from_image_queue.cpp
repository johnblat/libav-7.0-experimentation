#include "load_texture_from_image_queue.h"

extern "C"
{
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "read.h"
#include "state.h"

void load_texture_from_img_queue_init(LoadTextureFromImgSpscQueue *queue, int64_t img_w,
                                      int64_t img_h)
{
    for (int i = 0; i < LOAD_QUEUE_SIZE; i++)
    {
        queue->records[i].frame_num = 0;
        queue->records[i].tfring_idx = 0;
    }
    queue->cap = LOAD_QUEUE_SIZE;
    queue->read_head.store(0);
    queue->write_tail.store(0);
    int nb_bytes_per_image = av_image_get_buffer_size(AV_PIX_FMT_RGB24, img_w, img_h, 1);
    int nb_bytes_total = nb_bytes_per_image * LOAD_QUEUE_SIZE;
    queue->image_data = RL_MALLOC(nb_bytes_total);
    for (int i = 0; i < LOAD_QUEUE_SIZE; i++)
        queue->records[i].image.data =
            (unsigned char *)queue->image_data + i * nb_bytes_per_image;
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

int internal_decode_into_image(int width, int height, Image *image, PixelFormat format)
{
    int ret = 0;
    ret = decode_next_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt);
    if (ret < 0)
        return ret;
    ret = internal_avframe_to_image(curr_frame, image);
    return ret;
}

int load_image_from_texture_queue_push(LoadTextureFromImgSpscQueue *queue, int64_t frame_num,
                                       int64_t tfring_idx)
{
    uint64_t write_tail = queue->write_tail.load();
    uint64_t next_write_tail = (write_tail + 1) % queue->cap;
    queue->records[write_tail].frame_num = frame_num;
    queue->records[write_tail].tfring_idx = tfring_idx;
    int ret = internal_decode_into_image(tfring.width, tfring.height,
                                         &queue->records[write_tail].image,
                                         PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    if (ret < 0)
        fprintf(stderr, "Failed to load image from texture queue. frame num: %d\n", frame_num);
    queue->records[write_tail].is_keyframe = curr_frame->key_frame;
    queue->write_tail.store(next_write_tail);
    return ret;
}

LoadTextureFromImgRecord load_image_from_texture_queue_pop(LoadTextureFromImgSpscQueue *queue)
{
    uint64_t read_head = queue->read_head.load();
    while (read_head == queue->write_tail.load())
    {
        // queue is empty
    }
    LoadTextureFromImgRecord ret = queue->records[read_head];
    queue->read_head.store((read_head + 1) % queue->cap);
    return ret;
}

bool load_image_from_texture_queue_empty(LoadTextureFromImgSpscQueue *queue)
{
    return queue->read_head.load() == queue->write_tail.load();
}

int64_t load_image_from_texture_queue_length(LoadTextureFromImgSpscQueue *queue)
{
    return (queue->write_tail.load() - queue->read_head.load() + queue->cap) % queue->cap;
}