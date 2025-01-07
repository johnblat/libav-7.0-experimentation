extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "pts_frame_conversions.h"
#include "seek.h"
#include "texture_ring.h"
#include <SDL.h>

// #include <raylib.h>
#include <stdio.h>

//
// GLOBAL VARS
//
//
// 1. ffmpeg/libav
//
AVFormatContext *ic = NULL;
AVCodecContext *codec_ctx = NULL;
const AVCodec *codec = NULL;
int video_stream_index = -1;
AVFrame *curr_frame = NULL;
AVPacket *curr_pkt = NULL;

//
// 2. SDL2
//
SDL_Window *sdl_window = NULL;
SDL_Renderer *sdl_renderer = NULL;
SDL_Texture *sdl_display_texture = NULL;
int sdl_display_texture_w = 1024;
int sdl_display_texture_h = 768;

//
// 2. Raylib
// NOTE: This will replace SDL2 in this codebase, so need to replace the SDL vars with
// raylib equivelants
//

//
// 3. texture pixel format map
//
static const struct TextureFormatEntry
{
    enum AVPixelFormat format;
    SDL_PixelFormatEnum texture_fmt;
} sdl_texture_format_map[] = {
    {AV_PIX_FMT_RGB8, SDL_PIXELFORMAT_RGB332},
    {AV_PIX_FMT_RGB444, SDL_PIXELFORMAT_RGB444},
    {AV_PIX_FMT_RGB555, SDL_PIXELFORMAT_RGB555},
    {AV_PIX_FMT_BGR555, SDL_PIXELFORMAT_BGR555},
    {AV_PIX_FMT_RGB565, SDL_PIXELFORMAT_RGB565},
    {AV_PIX_FMT_BGR565, SDL_PIXELFORMAT_BGR565},
    {AV_PIX_FMT_RGB24, SDL_PIXELFORMAT_RGB24},
    {AV_PIX_FMT_BGR24, SDL_PIXELFORMAT_BGR24},
    {AV_PIX_FMT_0RGB32, SDL_PIXELFORMAT_RGB888},
    {AV_PIX_FMT_0BGR32, SDL_PIXELFORMAT_BGR888},
    {AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888},
    {AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888},
    {AV_PIX_FMT_RGB32, SDL_PIXELFORMAT_ARGB8888},
    {AV_PIX_FMT_RGB32_1, SDL_PIXELFORMAT_RGBA8888},
    {AV_PIX_FMT_BGR32, SDL_PIXELFORMAT_ABGR8888},
    {AV_PIX_FMT_BGR32_1, SDL_PIXELFORMAT_BGRA8888},
    {AV_PIX_FMT_YUV420P, SDL_PIXELFORMAT_IYUV},
    {AV_PIX_FMT_YUYV422, SDL_PIXELFORMAT_YUY2},
    {AV_PIX_FMT_UYVY422, SDL_PIXELFORMAT_UYVY},
};

// pix_fmt_av_to_sdl
//
// Converts an AVPixelFormat to an SDL_PixelFormatEnum
// returns SDL_PIXELFORMAT_UNKNOWN if no matching format is found
SDL_PixelFormatEnum pix_fmt_av_to_sdl(enum AVPixelFormat format)
{
    for (int i = 0;
         i < sizeof(sdl_texture_format_map) / sizeof(sdl_texture_format_map[0]); i++)
    {
        if (sdl_texture_format_map[i].format == format)
        {
            return sdl_texture_format_map[i].texture_fmt;
        }
    }
    return SDL_PIXELFORMAT_UNKNOWN;
}

void sdl_texture_array_render_horizontal_strip(SDL_Texture **arr, int size, int elm_w,
                                               int elm_h, int x, int y)
{
    for (int i = 0; i < size; i++)
    {
        int xi = (i * elm_w) + x;
        SDL_Rect dst_rect = {xi, y, elm_w, elm_h};
        SDL_RenderCopy(sdl_renderer, arr[i], NULL, &dst_rect);
    }
}

int read_n_frames_into_sdl_texture_arr(SDL_Texture **arr, int size, int dst_w,
                                       int dst_h)
{
    AVFrame *avframe = av_frame_alloc();
    if (!avframe)
    {
        return 1;
    }

    for (int i = 0; i < size; i++)
    {
        if (arr[i] != NULL)
        {
            SDL_DestroyTexture(arr[i]);
            arr[i] = NULL;
        }
    }

    int err = 0;
    int i = 0;
    for (; i < size;)
    {

        err = av_read_frame(ic, curr_pkt);
        if (err < 0)
        {
            return err;
        }
        if (curr_pkt->stream_index != video_stream_index)
        {
            av_packet_unref(curr_pkt);
            continue;
        }
        err = avcodec_send_packet(codec_ctx, curr_pkt);
        if (err < 0)
        {
            return err;
        }
        err = avcodec_receive_frame(codec_ctx, avframe);
        if (err == AVERROR(EAGAIN))
        {
            av_packet_unref(curr_pkt);
            continue;
        }

        if (err < 0)
        {
            return err;
        }

        SwsContext *sws_ctx = sws_getContext(
            avframe->width, avframe->height, (AVPixelFormat)avframe->format, dst_w,
            dst_h, (AVPixelFormat)avframe->format, SWS_BILINEAR, NULL, NULL, NULL);
        if (!sws_ctx)
        {
            return 1;
        }

        AVFrame *dst_frame = av_frame_alloc();
        if (!dst_frame)
        {
            sws_freeContext(sws_ctx);
            return 1;
        }

        dst_frame->format = (AVPixelFormat)avframe->format;
        dst_frame->width = dst_w;
        dst_frame->height = dst_h;

        if (av_image_alloc(dst_frame->data, dst_frame->linesize, dst_w, dst_h,
                           (AVPixelFormat)avframe->format, 1) < 1)
        {
            av_frame_free(&dst_frame);
            sws_freeContext(sws_ctx);
            return 1;
        }

        sws_scale(sws_ctx, avframe->data, avframe->linesize, 0, avframe->height,
                  dst_frame->data, dst_frame->linesize);

        arr[i] = SDL_CreateTexture(sdl_renderer,
                                   pix_fmt_av_to_sdl((AVPixelFormat)avframe->format),
                                   SDL_TEXTUREACCESS_STREAMING, dst_w, dst_h);

        if (!arr[i])
        {
            av_freep(&dst_frame->data[0]);
            av_frame_free(&dst_frame);
            sws_freeContext(sws_ctx);
            return 1;
        }

        SDL_UpdateYUVTexture(arr[i], NULL, dst_frame->data[0], dst_frame->linesize[0],
                             dst_frame->data[1], dst_frame->linesize[1],
                             dst_frame->data[2], dst_frame->linesize[2]);

        sws_freeContext(sws_ctx);
        i++;
        av_packet_unref(curr_pkt);
    }

    return 0;
}

// close
//
// Closes the SDL2 window and cleans up ffmpeg/libav resources
void close()
{
    { // SDL2
        if (sdl_display_texture)
        {
            SDL_DestroyTexture(sdl_display_texture);
            sdl_display_texture = NULL;
        }
        if (sdl_renderer)
        {
            SDL_DestroyRenderer(sdl_renderer);
            sdl_renderer = NULL;
        }
        if (sdl_window)
        {
            SDL_DestroyWindow(sdl_window);
            sdl_window = NULL;
        }
        SDL_Quit();
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
            avcodec_close(codec_ctx);
            avcodec_free_context(&codec_ctx);
            codec_ctx = NULL;
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

int init_sdl()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        return -1;
    }

    if (LOG_SDL_PTR_ERR(sdl_window,
                        SDL_CreateWindow("SDL2 Window", SDL_WINDOWPOS_UNDEFINED,
                                         SDL_WINDOWPOS_UNDEFINED, sdl_display_texture_w,
                                         sdl_display_texture_h + 40,
                                         SDL_WINDOW_SHOWN)) == NULL)
    {
        return -1;
    }

    if (LOG_SDL_PTR_ERR(sdl_renderer,
                        SDL_CreateRenderer(sdl_window, -1,
                                           SDL_RENDERER_ACCELERATED |
                                               SDL_RENDERER_PRESENTVSYNC)) == NULL)
    {
        return -1;
    }

    // texture
    if (LOG_SDL_PTR_ERR(sdl_display_texture,
                        SDL_CreateTexture(sdl_renderer,
                                          pix_fmt_av_to_sdl(codec_ctx->pix_fmt),
                                          SDL_TEXTUREACCESS_STREAMING, codec_ctx->width,
                                          codec_ctx->height)) == NULL)
    {
        return -1;
    }

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
    if (init_sdl() < 0)
    {
        close();
        return 1;
    }

    read_until_not_eagain_frame();

    int start_frame_num = 800;

    int err = seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt,
                            start_frame_num);

    if (err < 0)
    {
        close();
        return 1;
    }

    TextureFrameRing tfring;
    ring_init(&tfring, sdl_renderer, curr_frame->width, curr_frame->height,
              pix_fmt_av_to_sdl((AVPixelFormat)curr_frame->format), ic, codec_ctx,
              video_stream_index);
    int ret = ring_fill(&tfring, ic, codec_ctx, video_stream_index, start_frame_num);
    if (ret < 0)
    {
        close();
        return 1;
    }

    // make the sdltexture arr
    // int sdl_texture_arr_size = 32;

    // SDL_Texture **sdl_texture_arr =
    //     (SDL_Texture **)malloc(sdl_texture_arr_size * sizeof(SDL_Texture *));
    // for (int i = 0; i < sdl_texture_arr_size; i++)
    // {
    //     sdl_texture_arr[i] = NULL;
    // }

    // int strip_elm_w = 50;
    // int strip_elm_h = 50;
    // read_n_frames_into_sdl_texture_arr(sdl_texture_arr, sdl_texture_arr_size,
    //                                    strip_elm_w, strip_elm_h);

    // err = seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt,
    //                     start_frame_num);
    // if (err < 0)
    // {
    //     close();
    //     return 1;
    // }

    // put current frame in sdl texture
    // AVFrame *f = curr_frame;
    // sdl_display_texture =
    //     SDL_CreateTexture(sdl_renderer, pix_fmt_av_to_sdl((AVPixelFormat)f->format),
    //                       SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
    // if (!sdl_display_texture)
    // {
    //     const char *errmsg = SDL_GetError();
    //     fprintf(stderr, "Error: %s\n", errmsg);
    //     return 1;
    // }

    // if (SDL_UpdateYUVTexture(sdl_display_texture, NULL, f->data[0], f->linesize[0],
    //                          f->data[1], f->linesize[1], f->data[2],
    //                          f->linesize[2]) < 0)
    // {
    //     const char *errmsg = SDL_GetError();
    //     fprintf(stderr, "Error: %s\n", errmsg);
    //     return 1;
    // }

    int input_move_forward = 0;
    int input_move_backward = 0;

    // SDL loop
    SDL_Event event;
    int quit = 0;
    while (!quit)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                quit = 1;
            }
            if (event.type == SDL_KEYUP)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_RIGHT:
                    input_move_forward = 1;
                    break;
                case SDLK_LEFT:
                    input_move_backward = 1;
                    break;
                case SDLK_q:
                    quit = 1;
                    break;
                }
            }
        }
        if (input_move_forward)
        {
            input_move_forward = 0;
            // input_move_forward = 0;
            // if (read_for_n_frames(1) < 0)
            // {
            //     close();
            //     return 1;
            // }
            // if (SDL_UpdateYUVTexture(sdl_display_texture, NULL, curr_frame->data[0],
            //                          curr_frame->linesize[0], curr_frame->data[1],
            //                          curr_frame->linesize[1], curr_frame->data[2],
            //                          curr_frame->linesize[2]) < 0)
            // {
            //     const char *errmsg = SDL_GetError();
            //     fprintf(stderr, "Error: %s\n", errmsg);
            //     return 1;
            // }
            ring_next(&tfring);
        }
        if (input_move_backward)
        {
            input_move_backward = 0;
            // input_move_backward = 0;
            // int curr_framenum =
            //     timestamp_to_framenum(ic, video_stream_index, curr_frame->pts);
            // if (seek_backwards_one_frame(curr_framenum) < 0)
            // {
            //     close();
            //     return 1;
            // }
            // if (SDL_UpdateYUVTexture(sdl_display_texture, NULL, curr_frame->data[0],
            //                          curr_frame->linesize[0], curr_frame->data[1],
            //                          curr_frame->linesize[1], curr_frame->data[2],
            //                          curr_frame->linesize[2]) < 0)
            // {
            //     const char *errmsg = SDL_GetError();
            //     fprintf(stderr, "Error: %s\n", errmsg);
            //     return 1;
            // }
            ring_prev(&tfring);
        }
        // display texture
        SDL_SetRenderDrawColor(sdl_renderer, 100, 100, 100, 255);
        SDL_RenderClear(sdl_renderer);
        // SDL_RenderCopy(sdl_renderer, sdl_display_texture, NULL, NULL);
        SDL_Rect dst_rect = {0, 0, tfring.width, tfring.height};
        SDL_RenderCopy(sdl_renderer, tfring.textures[tfring.pos], NULL, &dst_rect);
        // sdl_texture_array_render_horizontal_strip(sdl_texture_arr,
        // sdl_texture_arr_size,
        //   strip_elm_w, strip_elm_h, 0, 0);
        ring_render_as_strip(sdl_renderer, &tfring, 0, tfring.height, tfring.width,
                             tfring.height / 10);
        SDL_RenderPresent(sdl_renderer);

        // render
    }

    // cleanup
    {
        close();
    }

    return 0;
}