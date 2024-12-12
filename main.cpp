extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <SDL.h>
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

#define CACHE_SIZE 16
// FrameBufferCache
typedef struct AVFrameCircularBufferCache
{
    AVFrame *frames[CACHE_SIZE];
    int start_index;
    int size;
};

AVFrameCircularBufferCache av_frame_circular_buffer_cache_init()
{
    AVFrameCircularBufferCache cache;
    cache.start_index = 0;
    cache.size = 0;
    for (int i = 0; i < CACHE_SIZE; i++)
    {
        cache.frames[i] = av_frame_alloc();
    }
    return cache;
}

void av_frame_circular_buffer_cache_free(AVFrameCircularBufferCache *cache)
{
    for (int i = 0; i < CACHE_SIZE; i++)
    {
        av_frame_free(&cache->frames[i]);
    }
    cache->start_index = 0;
    cache->size = 0;
}

void av_frame_circular_buffer_cache_push(AVFrameCircularBufferCache *cache,
                                         AVFrame *frame)
{
    if (cache->size >= CACHE_SIZE)
    {
        av_frame_free(&cache->frames[cache->start_index]);
        cache->start_index = (cache->start_index + 1) % CACHE_SIZE;
        cache->size--;
    }
    cache->frames[(cache->start_index + cache->size) % CACHE_SIZE] = frame;
    cache->size++;
}

// SDLTexture stuff
int sdl_texture_array_init(SDL_Texture **arr, int size, Uint32 pix_fmt, int w, int h)
{
    arr = (SDL_Texture **)malloc(size * sizeof(SDL_Texture *));
    if (!arr)
    {
        return 1;
    }

    for (int i = 0; i < size; i++)
    {
        arr[i] =
            SDL_CreateTexture(sdl_renderer, pix_fmt, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!arr[i])
        {
            return 1;
        }
    }

    return 0;
}

void sdl_texture_array_render_horizontal_strip(SDL_Texture **arr, int size, int elm_w,
                                               int elm_h, int x, int y)
{
    int total_w = elm_w * size;
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

        // arr[i] = SDL_CreateTexture(sdl_renderer,
        // pix_fmt_av_to_sdl((AVPixelFormat)avframe->format),
        //                            SDL_TEXTUREACCESS_STREAMING, avframe->width,
        //                            avframe->height);
        // if (!arr[i])
        // {
        //     const char *errmsg = SDL_GetError();
        //     fprintf(stderr, "Error: %s\n", errmsg);
        //     return 1;
        // }

        // if (SDL_UpdateYUVTexture(arr[i], NULL, avframe->data[0],
        // avframe->linesize[0], avframe->data[1],
        //                          avframe->linesize[1], avframe->data[2],
        //                          avframe->linesize[2]) < 0)
        // {
        //     const char *errmsg = SDL_GetError();
        //     fprintf(stderr, "Error: %s\n", errmsg);
        //     return 1;
        // }
        // use sws_scale to convert the frame to the texture of the desired scale

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

//
// FUNCTIONS
//

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
                                         sdl_display_texture_h, SDL_WINDOW_SHOWN)) ==
        NULL)
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

int is_read_frame_err_ok(int errnum)
{
    if (errnum < 0)
    {
        if (errnum == AVERROR_EOF)
        {
            return 1;
        }
        if (errnum == AVERROR(EAGAIN))
        {
            return 1;
        }
        print_err_str(errnum);
        return 0;
    }
    return 1;
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

int readloop()
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
        if (errnum < 0)
        {
            av_packet_unref(curr_pkt);
            print_err_str(errnum);
            return errnum;
        }
        av_packet_unref(curr_pkt);
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

int64_t estimate_frame_timestamp(int framenum)
{
    AVStream *video_stream = ic->streams[video_stream_index];

    // If we have a duration, use it to estimate
    if (video_stream->duration != AV_NOPTS_VALUE && video_stream->nb_frames > 0)
    {
        return av_rescale(framenum, video_stream->duration, video_stream->nb_frames);
    }

    // If we have a frame rate, use it to estimate
    if (video_stream->avg_frame_rate.num && video_stream->avg_frame_rate.den)
    {
        double seconds =
            (double)framenum * av_q2d(av_inv_q(video_stream->avg_frame_rate));
        return av_rescale(seconds * AV_TIME_BASE, video_stream->time_base.num,
                          video_stream->time_base.den);
    }

    // If all else fails, make a very rough estimate
    return av_rescale(
        framenum, video_stream->duration > 0 ? video_stream->duration : AV_TIME_BASE,
        250);
}

int timestamp_to_framenum(int64_t timestamp)
{
    AVStream *video_stream = ic->streams[video_stream_index];

    // If we have a duration, use it to estimate
    if (video_stream->duration != AV_NOPTS_VALUE && video_stream->nb_frames > 0)
    {
        return av_rescale(timestamp, video_stream->nb_frames, video_stream->duration);
    }

    // If we have a frame rate, use it to estimate
    if (video_stream->avg_frame_rate.num && video_stream->avg_frame_rate.den)
    {
        double seconds =
            (double)timestamp * av_q2d(av_inv_q(video_stream->avg_frame_rate));
        return av_rescale(seconds * AV_TIME_BASE, video_stream->time_base.num,
                          video_stream->time_base.den);
    }

    // If all else fails, make a very rough estimate
    return av_rescale(timestamp, 250,
                      video_stream->duration > 0 ? video_stream->duration
                                                 : AV_TIME_BASE);
}

int seek_to_frame(AVFormatContext *ic, int stream_index, AVCodecContext *codec_ctx,
                  AVFrame *frame, AVPacket *pkt, int target_frame)
{
    int64_t target_ts = estimate_frame_timestamp(target_frame);

    int err = 0;

    if (err = avformat_seek_file(ic, stream_index, INT64_MIN, target_ts, INT64_MAX,
                                 AVSEEK_FLAG_BACKWARD),
        err < 0)
    {
        return err;
    }

    avcodec_flush_buffers(codec_ctx);

    for (;;)
    {
        if (err = av_read_frame(ic, pkt), err < 0)
        {
            return err;
        }

        if (pkt->stream_index != stream_index)
        {
            av_packet_unref(pkt);
            continue;
        }

        if (err = avcodec_send_packet(codec_ctx, pkt), err < 0)
        {
            return err;
        }

        if (err = avcodec_receive_frame(codec_ctx, curr_frame), err < 0)
        {
            if (err == AVERROR(EAGAIN))
            {
                av_packet_unref(pkt);
                continue;
            }

            if (err == AVERROR_EOF)
            {
                av_packet_unref(pkt);
                break;
            }

            return err;
        }

        if (curr_frame->pts >= target_ts)
        {
            av_packet_unref(pkt);
            break;
        }

        av_packet_unref(pkt);
        continue;
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

    int err =
        seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt, 200);
    if (err < 0)
    {
        close();
        return 1;
    }

    // make the sdltexture arr
    int sdl_texture_arr_size = 32;

    SDL_Texture **sdl_texture_arr =
        (SDL_Texture **)malloc(sdl_texture_arr_size * sizeof(SDL_Texture *));
    for (int i = 0; i < sdl_texture_arr_size; i++)
    {
        sdl_texture_arr[i] = NULL;
    }

    int strip_elm_w = 50;
    int strip_elm_h = 50;
    read_n_frames_into_sdl_texture_arr(sdl_texture_arr, sdl_texture_arr_size,
                                       strip_elm_w, strip_elm_h);

    err = seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt, 200);
    if (err < 0)
    {
        close();
        return 1;
    }

    // put current frame in sdl texture
    AVFrame *f = curr_frame;
    sdl_display_texture =
        SDL_CreateTexture(sdl_renderer, pix_fmt_av_to_sdl((AVPixelFormat)f->format),
                          SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
    if (!sdl_display_texture)
    {
        const char *errmsg = SDL_GetError();
        fprintf(stderr, "Error: %s\n", errmsg);
        return 1;
    }

    if (SDL_UpdateYUVTexture(sdl_display_texture, NULL, f->data[0], f->linesize[0],
                             f->data[1], f->linesize[1], f->data[2],
                             f->linesize[2]) < 0)
    {
        const char *errmsg = SDL_GetError();
        fprintf(stderr, "Error: %s\n", errmsg);
        return 1;
    }

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
            if (read_for_n_frames(1) < 0)
            {
                close();
                return 1;
            }
            if (SDL_UpdateYUVTexture(sdl_display_texture, NULL, curr_frame->data[0],
                                     curr_frame->linesize[0], curr_frame->data[1],
                                     curr_frame->linesize[1], curr_frame->data[2],
                                     curr_frame->linesize[2]) < 0)
            {
                const char *errmsg = SDL_GetError();
                fprintf(stderr, "Error: %s\n", errmsg);
                return 1;
            }
        }
        if (input_move_backward)
        {
            input_move_backward = 0;
            int curr_framenum = timestamp_to_framenum(curr_frame->pts);
            if (seek_backwards_one_frame(curr_framenum) < 0)
            {
                close();
                return 1;
            }
            if (SDL_UpdateYUVTexture(sdl_display_texture, NULL, curr_frame->data[0],
                                     curr_frame->linesize[0], curr_frame->data[1],
                                     curr_frame->linesize[1], curr_frame->data[2],
                                     curr_frame->linesize[2]) < 0)
            {
                const char *errmsg = SDL_GetError();
                fprintf(stderr, "Error: %s\n", errmsg);
                return 1;
            }
        }
        // display texture
        SDL_SetRenderDrawColor(sdl_renderer, 100, 100, 100, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, sdl_display_texture, NULL, NULL);
        sdl_texture_array_render_horizontal_strip(sdl_texture_arr, sdl_texture_arr_size,
                                                  strip_elm_w, strip_elm_h, 0, 0);
        SDL_RenderPresent(sdl_renderer);

        // render
    }

    // cleanup
    {
        close();
    }

    return 0;
}