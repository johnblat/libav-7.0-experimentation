extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "pts_frame_conversions.h"
#include "raylib.h"
#include "read.h"
#include "seek.h"
#include "state.h"
#include "texture_ring.h"
#include <raylib.h>
#include <stdio.h>
#include <threads.h>

const char *APP_NAME = "Nekxtar";

void close()
{
    { // raylib
    }
    { // ffmpeg/libav
        if (curr_frame)
        {
            av_frame_free(&curr_frame);
            curr_frame = NULL;
        }
        if (curr_pkt)
        {
            av_packet_unref(curr_pkt);
            av_packet_free(&curr_pkt);
            curr_pkt = NULL;
        }
        if (codec_ctx)
        {
            // TODO: fixme
            // avcodec_close(codec_ctx);
            // avcodec_free_context(&codec_ctx);
            // codec_ctx = NULL;
        }
        if (ic)
        {
            avformat_close_input(&ic);
            ic = NULL;
        }
    }
}

int init_libav(const char *filename)
{
    int ret = 0;

    ic = avformat_alloc_context();

    ret = avformat_open_input(&ic, filename, NULL, NULL);
    if (ret < 0)
        return ret;

    ret = avformat_find_stream_info(ic, NULL);
    if (ret < 0)
        return ret;

    video_stream_index = -1;
    for (int i = 0; i < ic->nb_streams; i++)
        if (ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = i;
            break;
        }
    if (video_stream_index == -1)
        return -1;

    sws_ctx =
        sws_getContext(ic->streams[video_stream_index]->codecpar->width,
                       ic->streams[video_stream_index]->codecpar->height,
                       (AVPixelFormat)ic->streams[video_stream_index]->codecpar->format,
                       ic->streams[video_stream_index]->codecpar->width,
                       ic->streams[video_stream_index]->codecpar->height,
                       av_rgb_pixel_fmt, 0, NULL, NULL, NULL);
    if (!sws_ctx)
        return -1;

    codec_ctx = avcodec_alloc_context3(NULL);
    if (!codec_ctx)
        return -1;

    ret = avcodec_parameters_to_context(codec_ctx,
                                        ic->streams[video_stream_index]->codecpar);
    if (ret < 0)
        return ret;

    codec = avcodec_find_decoder(codec_ctx->codec_id);
    if (!codec)
        return -1;

    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0)
        return ret;

    curr_frame = av_frame_alloc();
    if (!curr_frame)
        return -1;

    curr_pkt = av_packet_alloc();
    if (!curr_pkt)
        return -1;

    return 0;
}

int init_raylib()
{
    InitWindow(1280, 720, APP_NAME);
    SetTargetFPS(24);
    SetWindowIcon(LoadImage("daisy.png"));
    return 0;
}

// MAIN
//
int main(int argc, char **argv)
{
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

    // reads the first frame so that we can get information about the video
    decode_next_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt);

    int nb_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width,
                                            codec_ctx->height, 1);

    curr_frame_image.data = RL_MALLOC(nb_bytes);
    ImageClearBackground(&curr_frame_image, BLANK);

    int start_frame_num = 1400;

    tfring = ring_init(PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    int ret = ring_fill(&tfring, start_frame_num);
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
        int subsection_update_val = -1;
        int input_move_forward = 0;
        int input_move_backward = 0;

        if (IsKeyDown(KEY_RIGHT))
        {
            hold_time += GetFrameTime();
            hold_time = hold_time < hold_time_until_playback ? hold_time
                                                             : hold_time_until_playback;
            input_move_forward = hold_time >= hold_time_until_playback;
        }
        else if (IsKeyDown(KEY_LEFT))
        {
            hold_time += GetFrameTime();
            hold_time = hold_time < hold_time_until_playback ? hold_time
                                                             : hold_time_until_playback;
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
            subsection_update_val = subsection_transition_calculate_update(
                prev_subsection, curr_subsection);
        }

        if (subsection_update_val >= 0 && subsection_update_val < NUM_RING_SUBSECTIONS)
        {
            // FIXME(johnb): somewhere in here is a bug that will make stepping
            // backwards from the beginning of the stream to the start where the loading
            // wrap logic gets screwed up and it just loads in frames 0 thru 15 again.
            int start_fnb_update =
                calculate_frame_nb_update_val(ring_index_to_subsection(tfring.prev_pos),
                                              ring_index_to_subsection(tfring.pos));
            ring_fill_subsection(&tfring, start_fnb_update, subsection_update_val);
        }

        ClearBackground(GRAY);
        BeginDrawing();
        {
            ring_draw_curr(&tfring, {0, 0, (float)tfring.width, (float)tfring.height});
            ring_draw_strip(&tfring, 0, tfring.height, tfring.width,
                            tfring.height / 10);

            { // draw info
                int y = 20;
                int font_size = 20;
                int y_add = font_size + 5;

                DrawText(TextFormat("pos: %d", tfring.pos), tfring.width, y, font_size,
                         WHITE);
                y += y_add;
                DrawText(TextFormat("prev_pos: %d", tfring.prev_pos), tfring.width, y,
                         font_size, WHITE);
                y += y_add;
                DrawText(
                    TextFormat("subsection: %d", ring_index_to_subsection(tfring.pos)),
                    tfring.width, y, font_size, WHITE);
                y += y_add;

                DrawText(TextFormat("frame_nb: %d", tfring.frame_numbers[tfring.pos]),
                         tfring.width, y, font_size, WHITE);
                y += y_add;
                DrawText(TextFormat("prev frame_nb: %d",
                                    tfring.frame_numbers[tfring.prev_pos]),
                         tfring.width, y, font_size, WHITE);
                y += y_add;
                DrawText(TextFormat("total frames: %d",
                                    ic->streams[video_stream_index]->nb_frames),
                         tfring.width, y, font_size, WHITE);
            }
        }
        EndDrawing();
    }

close: // cleanup
{
    close();
}

    return 0;
}