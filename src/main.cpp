extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "init.h"
#include "pts_frame_conversions.h"
#include "raylib.h"
#include "read.h"
#include "seek.h"
#include "state.h"
#include "texture_ring.h"
#include <intrin.h>
#include <raylib.h>
#include <stdio.h>
#include <threads.h>

int req_exit = 0;

int decode_thread(void *arg)
{
    while (!req_exit)
    {
        bool no_requests = decode_request_queue_empty(&decode_queue);
        if (no_requests)
        {
            WaitTime(1 / 60);
            continue;
        }

        DecodeRequestRecord record = decode_request_queue_pop(&decode_queue);

        int start_idx = ring_subsection_to_index(record.tfring_subsection_udpate);
        int ret = 0;
        for (int i = 0; i < record.nb_frames; i++)
        {
            int tfring_idx = (start_idx + i) % tfring.cap;
            int frame_num = record.start_frame + i;
            // uint64_t max_frame_num = ring_max_frame_number(&tfring);
            // if (frame_num < max_frame_num) // breakpoint here using windows debugger
            //     __debugbreak();
            tfring.frame_numbers[tfring_idx] = frame_num;
        }

        seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt,
                      record.start_frame);
        for (int i = 0; i < record.nb_frames; i++)
        {
            int tfring_idx = (start_idx + i) % tfring.cap;
            int frame_num = record.start_frame + i;
            ret = load_image_from_texture_queue_push(&load_queue, frame_num, tfring_idx);
            if (ret < 0)
                break;
        }
    }
    return 0;
}

// MAIN
//
int main(int argc, char **argv)
{
    int ret = 0;
    if (init_libav(argv[1]) < 0)
    {
        close();
        return 1;
    }
    if (init_raylib() < 0)
    {
        close();
        return 1;
    }

    thrd_t decode_thrd_handle;
    ret = thrd_create(&decode_thrd_handle, decode_thread, NULL);
    if (ret != thrd_success)
    {
        fprintf(stderr, "Error creating decode thread\n");
        close();
        return 1;
    }

    // reads the first frame so that we can get information about the video
    decode_next_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt);

    load_texture_from_img_queue_init(&load_queue, codec_ctx->width, codec_ctx->height);
    decode_request_queue_init(&decode_queue);

    int nb_bytes =
        av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);

    curr_frame_image.data = RL_MALLOC(nb_bytes);
    ImageClearBackground(&curr_frame_image, BLANK);

    int start_frame_num = 800;

    tfring = ring_init(PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    ret = ring_fill(&tfring, start_frame_num);
    if (ret < 0)
    {
        close();
        return 1;
    }
    tfring.pos = 16;
    tfring.prev_pos = 16;

    const float hold_time_until_playback = 0.15;
    float hold_time = 0.0;
    int quit = 0;

    while (!WindowShouldClose() && !quit)
    {
        int update_subsection = -1;
        int input_move_forward = 0;
        int input_move_backward = 0;

        if (IsKeyDown(KEY_RIGHT))
        {
            hold_time += GetFrameTime();
            hold_time =
                hold_time < hold_time_until_playback ? hold_time : hold_time_until_playback;
            input_move_forward = hold_time >= hold_time_until_playback;
        }
        else if (IsKeyDown(KEY_LEFT))
        {
            hold_time += GetFrameTime();
            hold_time =
                hold_time < hold_time_until_playback ? hold_time : hold_time_until_playback;
            input_move_backward = hold_time >= hold_time_until_playback;
        }

        if (IsKeyUp(KEY_RIGHT) && IsKeyUp(KEY_LEFT))
        {
            hold_time = 0.0;
            input_move_backward = 0;
            input_move_forward = 0;
        }

        if (IsKeyPressed(KEY_RIGHT))
            input_move_forward = 1;
        else if (IsKeyPressed(KEY_LEFT))
            input_move_backward = 1;

        if (input_move_forward)
            ring_next(&tfring);
        else if (input_move_backward)
            ring_prev(&tfring);

        if (input_move_forward || input_move_backward)
        {
            uint64_t prev_subsection = ring_index_to_subsection(tfring.prev_pos);
            uint64_t curr_subsection = ring_index_to_subsection(tfring.pos);
            update_subsection =
                calculate_subsection_to_update(prev_subsection, curr_subsection);
        }

        if (update_subsection >= 0 && update_subsection < NB_SUBSECTIONS)
        {
            // FIXME(johnb): somewhere in here is a bug that will make stepping
            // backwards from the beginning of the stream to the start where the loading
            // wrap logic gets screwed up and it just loads in frames 0 thru 15 again.
            int start_fnb_update =
                calculate_frame_nb_to_update(ring_index_to_subsection(tfring.prev_pos),
                                             ring_index_to_subsection(tfring.pos));

            DecodeRequestRecord decode_req_record = {start_fnb_update, RING_SUBSECTION_SIZE,
                                                     update_subsection};
            uint64_t max_frame_num = ring_max_frame_number(&tfring);
            if (start_fnb_update < max_frame_num) // breakpoint here using windows debugger
                __debugbreak();
            decode_request_queue_push(&decode_queue, decode_req_record);
        }

        bool has_requests = !load_image_from_texture_queue_empty(&load_queue);
        if (has_requests)
        {
            LoadTextureFromImgRecord record = load_image_from_texture_queue_pop(&load_queue);
            int idx = record.tfring_idx;
            UnloadTexture(tfring.textures[idx]);
            tfring.textures[idx] = LoadTextureFromImage(record.image);
            tfring.frame_numbers[idx] = record.frame_num;
            tfring.is_keyframe[idx] = record.is_keyframe;
        }

        ClearBackground(GRAY);
        BeginDrawing();
        {
            ring_draw_curr(&tfring, {0, 0, (float)tfring.width, (float)tfring.height});
            ring_draw_strip(&tfring, 0, tfring.height, tfring.width, tfring.height / 10);

            { // draw some info
                int x = tfring.width + 5;
                int y = 20;
                int font_size = 20;
                int y_add = font_size + 5;

                DrawText(TextFormat("pos: %d", tfring.pos), x, y, font_size, YELLOW);
                y += y_add;
                DrawText(TextFormat("prev_pos: %d", tfring.prev_pos), x, y, font_size, YELLOW);
                y += y_add;
                DrawText(TextFormat("subsection: %d", ring_index_to_subsection(tfring.pos)), x,
                         y, font_size, YELLOW);
                y += y_add;

                DrawText(TextFormat("frame_nb: %d", tfring.frame_numbers[tfring.pos]), x, y,
                         font_size, YELLOW);
                y += y_add;
                DrawText(TextFormat("prev frame_nb: %d", tfring.frame_numbers[tfring.prev_pos]),
                         x, y, font_size, YELLOW);
                y += y_add;
                DrawText(
                    TextFormat("total frames: %d", ic->streams[video_stream_index]->nb_frames),
                    x, y, font_size, YELLOW);

                // Draw a 6 row * 8 column grid of the rectangle lines representing the frames
                // and inside the rectangle, write the number. This should represent the tfring
                // make sure the text fully fits inside the rectangle
                y += y_add * 2;
                for (int i = 0; i < tfring.cap; i++)
                {
                    int x = tfring.width + 10;
                    int cols = 8;
                    int rows = 6;
                    int grid_w = 60 * cols;
                    int grid_h = 40 * rows;
                    Color color = i == tfring.pos ? YELLOW : GOLD;
                    int row = i / cols;
                    int col = i % cols;
                    int rect_w = grid_w / cols;
                    int rect_h = grid_h / rows;
                    int rect_x = x + col * rect_w;
                    if (col == 0)
                        y += rect_h;
                    Rectangle rect = {rect_x, y, rect_w, rect_h};
                    DrawRectangleLinesEx(rect, 1, color);
                    DrawText(TextFormat("%d", tfring.frame_numbers[i]), 2 + rect_x, y,
                             font_size, color);
                }
                y += y_add * 2;

                // print out length of each queue
                int64_t decode_queue_length = decode_request_queue_length(&decode_queue);
                int64_t load_queue_length = load_image_from_texture_queue_length(&load_queue);
                DrawText(TextFormat("decode queue length: %d", decode_queue_length), x, y,
                         font_size, YELLOW);
                y += y_add;
                DrawText(TextFormat("load queue length: %d", load_queue_length), x, y,
                         font_size, YELLOW);
            }
        }
        EndDrawing();
    }

close: // cleanup
{
    req_exit = 1;
    thrd_join(decode_thrd_handle, &ret);
    close();
}

    return 0;
}