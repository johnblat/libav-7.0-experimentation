extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "pts_frame_conversions.h"
#include "raylib.h"
#include "seek.h"
#include "state.h"
#include "texture_ring.h"
#include <raylib.h>
#include <stdio.h>

const char *APP_NAME = "Nekxtar";

// close
//
// Closes the SDL2 window and cleans up ffmpeg/libav resources
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

// print_err
//
// takes in an error number and prints the corresponding error string
void print_err_str_with_details(int errnum, const char *err_file, int err_line)
{
    if (errnum >= 0)
    {
        return;
    }
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    fprintf(stderr, "Error: %s at %s:%d\n", errbuf, err_file, err_line);
    memset(errbuf, 0, AV_ERROR_MAX_STRING_SIZE);
}

void print_err_str(int errnum)
{
    if (errnum >= 0)
    {
        return;
    }
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    fprintf(stderr, "Error: %s\n", errbuf);
    memset(errbuf, 0, AV_ERROR_MAX_STRING_SIZE);
}

void print_err_at(const char *err_file, int err_line)
{
    fprintf(stderr, "Error at %s:%d\n", err_file, err_line);
}

int log_av_err(int errnum, const char *file, int line)
{
    if (errnum < 0)
    {
        print_err_str_with_details(errnum, file, line);
    }
    return errnum;
}

const void *log_av_ptr_err(const void *ptr, const char *func_call_str, const char *file,
                           int line)
{
    if (ptr == NULL)
    {
        fprintf(stderr, "Error: %s failed to allocate. Pointer is NULL at %s:%d\n",
                func_call_str, file, line);
    }
    return ptr;
}

// MACROS
#define LOGAVERR(func_call) (log_av_err((func_call), __FILE__, __LINE__))
#define LOGAVPTRERR(ptr, func_call)                                                    \
    (log_av_ptr_err((ptr = (func_call), ptr), #func_call, __FILE__, __LINE__))
#define LOGERR() (print_err_at(__FILE__, __LINE__))
#define LOG_SDL_PTR_ERR(ptr, func_call)                                                \
    (log_av_ptr_err((ptr = (func_call), ptr), #func_call, __FILE__, __LINE__))

int init_libav(const char *filename)
{
    ic = avformat_alloc_context();
    if (LOGAVERR(avformat_open_input(&ic, filename, NULL, NULL)) < 0)
    {
        return -1;
    }
    if (LOGAVERR(avformat_find_stream_info(ic, NULL)) < 0)
    {
        return -1;
    }

    for (int i = 0; i < ic->nb_streams; i++)
    {
        if (ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index == -1)
    {
        return -1;
    }

    sws_ctx =
        sws_getContext(ic->streams[video_stream_index]->codecpar->width,
                       ic->streams[video_stream_index]->codecpar->height,
                       (AVPixelFormat)ic->streams[video_stream_index]->codecpar->format,
                       ic->streams[video_stream_index]->codecpar->width,
                       ic->streams[video_stream_index]->codecpar->height,
                       av_rgb_pixel_fmt, 0, NULL, NULL, NULL);
    if (sws_ctx == NULL)
        return -1;

    if (LOGAVPTRERR(codec_ctx, avcodec_alloc_context3(NULL)) == NULL)
    {
        return -1;
    }

    if (LOGAVERR(avcodec_parameters_to_context(
            codec_ctx, ic->streams[video_stream_index]->codecpar)) < 0)
    {
        return -1;
    }

    if (LOGAVPTRERR(codec, avcodec_find_decoder(codec_ctx->codec_id)) == NULL)
    {
        return -1;
    }

    if (LOGAVERR(avcodec_open2(codec_ctx, codec, NULL)) < 0)
    {
        return -1;
    }

    if (LOGAVPTRERR(curr_frame, av_frame_alloc()) == NULL)
    {
        return -1;
    }
    if (LOGAVPTRERR(curr_pkt, av_packet_alloc()) == NULL)
    {
        return -1;
    }
    return 0;
}

int init_raylib()
{
    InitWindow(1280, 720, APP_NAME);
    SetTargetFPS(24);
    SetWindowIcon(LoadImage("daisy.png"));
    return 0;
}

int read_until_not_eagain_frame()
{
    int read_frame_errnum = 0;
    for (;;)
    {
        read_frame_errnum = av_read_frame(ic, curr_pkt);
        if (read_frame_errnum < 0)
        {
            break;
        }
        if (curr_pkt->stream_index != video_stream_index)
        {
            av_packet_unref(curr_pkt);
            continue;
        }
        if (LOGAVERR(avcodec_send_packet(codec_ctx, curr_pkt)) < 0)
        {
            return 1;
        }
        int errnum = LOGAVERR(avcodec_receive_frame(codec_ctx, curr_frame));
        if (errnum == AVERROR(EAGAIN))
        {
            av_packet_unref(curr_pkt);
            continue;
        }
        else if (errnum == AVERROR_EOF)
        {
            av_packet_unref(curr_pkt);
            break;
        }
        else if (errnum == 0)
        {
            av_packet_unref(curr_pkt);
            break;
        }
        print_err_str(errnum);
        return 1;
    }
    if (read_frame_errnum < 0 && read_frame_errnum != AVERROR_EOF)
    {
        print_err_str(read_frame_errnum);
        return 1;
    }
    return 0;
}

int read_for_n_frames(int n)
{
    int n_video_frames_read = 0;
    int read_frame_errnum = 0;

    for (; n_video_frames_read < n;)
    {
        read_frame_errnum = av_read_frame(ic, curr_pkt);
        if (read_frame_errnum < 0)
        {
            return read_frame_errnum;
        }
        if (curr_pkt->stream_index != video_stream_index)
        {
            av_packet_unref(curr_pkt);
            continue;
        }
        if (LOGAVERR(avcodec_send_packet(codec_ctx, curr_pkt)) < 0)
        {
            return 1;
        }
        int errnum = LOGAVERR(avcodec_receive_frame(codec_ctx, curr_frame));
        if (errnum == AVERROR(EAGAIN))
        {
            av_packet_unref(curr_pkt);
            continue;
        }
        if (errnum == AVERROR_EOF)
        {
            av_packet_unref(curr_pkt);
            return errnum;
        }
        if (errnum < 0)
        {
            av_packet_unref(curr_pkt);
            print_err_str(errnum);
            return errnum;
        }
        av_packet_unref(curr_pkt);
        n_video_frames_read++;
    }

    if (read_frame_errnum < 0 && read_frame_errnum != AVERROR_EOF)
    {
        print_err_str(read_frame_errnum);
        return read_frame_errnum;
    }

    return 0;
}

int seek_backwards_one_frame(int curr_frame_num)
{
    int target_frame = curr_frame_num - 1;
    return seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt,
                         target_frame);
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

    read_until_not_eagain_frame();

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width,
                                            codec_ctx->height, 1);

    curr_frame_image.data = RL_MALLOC(numBytes);
    ImageClearBackground(&curr_frame_image, BLANK);

    int start_frame_num = 800;

    tfring = ring_init(PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    int ret = ring_fill(&tfring, start_frame_num);
    if (ret < 0)
    {
        close();
        return 1;
    }

    const float hold_time_until_playback = 0.15;
    float hold_time = 0.0;
    // SDL loop
    int quit = 0;
    while (!WindowShouldClose() && !quit)
    {

        // time_until_playback
        int input_move_forward = 0;
        int input_move_backward = 0;

        if (IsKeyDown(KEY_RIGHT))
        {
            hold_time += GetFrameTime();
            hold_time = hold_time < hold_time_until_playback ? hold_time
                                                             : hold_time_until_playback;
            input_move_forward = hold_time >= hold_time_until_playback;
        }

        if (IsKeyDown(KEY_LEFT))
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
        if (IsKeyPressed(KEY_LEFT))
            input_move_backward = 1;

        if (input_move_forward)
            ring_next(&tfring);

        if (input_move_backward)
            ring_prev(&tfring);

        ClearBackground(GRAY);
        BeginDrawing();
        {
            ring_render_curr(&tfring,
                             {0, 0, (float)tfring.width, (float)tfring.height});
            ring_render_strip(&tfring, 0, tfring.height, tfring.width,
                              tfring.height / 10);
        }
        EndDrawing();
    }

    // cleanup
    {
        close();
    }

    return 0;
}