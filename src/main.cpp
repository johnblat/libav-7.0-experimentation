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
#include <raylib.h>
#include <stdio.h>
#include <threads.h>

int req_load = 0;
int req_subsection = -1;
int req_start_frame_nb = 0;
int req_exit = 0;

// FIXME(johnb):
// https://stackoverflow.com/questions/79287456/using-threading-to-load-textures-in-raylib-is-failing
int load_thread(void *arg)
{
    while (!req_exit)
    {
        if (req_load > 0)
        {
            req_load--;
            int ret = ring_fill_subsection(&tfring, req_start_frame_nb, req_subsection);
            if (ret < 0)
                fprintf(stderr, "Error loading subsection %d\n", req_subsection);
        }
        WaitTime(1 / 60);
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

    thrd_t load_thread_handle;
    ret = thrd_create(&load_thread_handle, load_thread, NULL);
    if (ret != thrd_success)
    {
        fprintf(stderr, "Error creating load thread\n");
        close();
        return 1;
    }

    // reads the first frame so that we can get information about the video
    decode_next_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt);

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
            req_load++;
            req_subsection = update_subsection;
            req_start_frame_nb = start_fnb_update;
        }

        ClearBackground(GRAY);
        BeginDrawing();
        {
            ring_draw_curr(&tfring, {0, 0, (float)tfring.width, (float)tfring.height});
            ring_draw_strip(&tfring, 0, tfring.height, tfring.width, tfring.height / 10);

            { // draw info
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
            }
        }
        EndDrawing();
    }

close: // cleanup
{
    req_exit = 1;
    thrd_join(load_thread_handle, &ret);
    close();
}

    return 0;
}