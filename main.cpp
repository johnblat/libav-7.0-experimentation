extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
}
#include <stdio.h>
#include <SDL.h>

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
static const struct TextureFormatEntry {
    enum AVPixelFormat format;
    SDL_PixelFormatEnum texture_fmt;
} sdl_texture_format_map[] = {
    { AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
    { AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },
    { AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 },
    { AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 },
    { AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
    { AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
    { AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
    { AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
    { AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },
    { AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 },
    { AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
    { AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
    { AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
    { AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
    { AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
    { AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
    { AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
    { AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
    { AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
};

//
// FUNCTIONS
//

// pix_fmt_av_to_sdl
//
// Converts an AVPixelFormat to an SDL_PixelFormatEnum
// returns SDL_PIXELFORMAT_UNKNOWN if no matching format is found
SDL_PixelFormatEnum pix_fmt_av_to_sdl(enum AVPixelFormat format) {
    for (int i = 0; i < sizeof(sdl_texture_format_map) / sizeof(sdl_texture_format_map[0]); i++) {
        if (sdl_texture_format_map[i].format == format) {
            return sdl_texture_format_map[i].texture_fmt;
        }
    }
    return SDL_PIXELFORMAT_UNKNOWN;
}

// close
//
// Closes the SDL2 window and cleans up ffmpeg/libav resources
void close() {
    { // SDL2
        if (sdl_display_texture) {
            SDL_DestroyTexture(sdl_display_texture);
            sdl_display_texture = NULL;
        }
        if (sdl_renderer) {
            SDL_DestroyRenderer(sdl_renderer);
            sdl_renderer = NULL;
        }
        if (sdl_window) {
            SDL_DestroyWindow(sdl_window);
            sdl_window = NULL;
        }
        SDL_Quit();
    }
    { // ffmpeg/libav
        if (curr_frame) {
            av_frame_free(&curr_frame);
            curr_frame = NULL;
        }
        if (curr_pkt) {
            av_packet_unref(curr_pkt);
            av_packet_free(&curr_pkt);
            curr_pkt = NULL;
        }
        if (codec_ctx) {
            avcodec_close(codec_ctx);
            avcodec_free_context(&codec_ctx);
            codec_ctx = NULL;
        }
        if (ic) {
            avformat_close_input(&ic);
            ic = NULL;
        }
    }
}


// print_err
//
// takes in an error number and prints the corresponding error string
void print_err_str_with_details(int errnum, const char* err_file, int err_line) {
    if (errnum >= 0) {
        return;
    }
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    fprintf(stderr, "Error: %s at %s:%d\n", errbuf, err_file, err_line);
    memset(errbuf, 0, AV_ERROR_MAX_STRING_SIZE);
}

void print_err_str(int errnum) {
    if (errnum >= 0) {
        return;
    }
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    fprintf(stderr, "Error: %s\n", errbuf);
    memset(errbuf, 0, AV_ERROR_MAX_STRING_SIZE);
}

void print_err_at(const char *err_file, int err_line) {
    fprintf(stderr, "Error at %s:%d\n", err_file, err_line);
}


int log_av_err(int errnum, const char *file, int line){
    if(errnum < 0) {
        print_err_str_with_details(errnum, file, line);
    }
    return errnum;
}

const void *log_av_ptr_err(const void *ptr, const char *func_call_str, const char *file, int line) {
    if(ptr == NULL) {
        fprintf(stderr, "Error: %s failed to allocate. Pointer is NULL at %s:%d\n", func_call_str, file, line);
    }
    return ptr;
}



// MACROS
#define LOGAVERR(func_call) (log_av_err((func_call), __FILE__, __LINE__))
#define LOGAVPTRERR(ptr, func_call) (log_av_ptr_err((ptr = (func_call), ptr), #func_call, __FILE__, __LINE__))
#define LOGERR() (print_err_at( __FILE__, __LINE__))
#define LOG_SDL_PTR_ERR(ptr, func_call) (log_av_ptr_err((ptr = (func_call), ptr), #func_call, __FILE__, __LINE__))

int init_libav(const char *filename) {
    ic = avformat_alloc_context();
    if(LOGAVERR(avformat_open_input(&ic,filename, NULL, NULL)) < 0) {
        return -1;
    }
    if(LOGAVERR(avformat_find_stream_info(ic, NULL)) < 0) {
        return -1;
    }
    
    for(int i = 0; i < ic->nb_streams; i++) {
        if(ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    if(video_stream_index == -1) {
        return -1;
    }
    
    if(LOGAVPTRERR(codec_ctx, avcodec_alloc_context3(NULL)) == NULL) {
        return -1;
    }
    
    if(LOGAVERR(avcodec_parameters_to_context(codec_ctx, ic->streams[video_stream_index]->codecpar)) < 0) {
        return -1;
    }
    
    if(LOGAVPTRERR(codec, avcodec_find_decoder(codec_ctx->codec_id)) == NULL) {
        return -1;
    }

    if(LOGAVERR(avcodec_open2(codec_ctx, codec, NULL)) < 0) {
        return -1;
    }
    
    if(LOGAVPTRERR(curr_frame, av_frame_alloc()) == NULL) {
        return -1;
    }
    if(LOGAVPTRERR(curr_pkt, av_packet_alloc()) == NULL) {
        return -1;
    }
    return 0;
}

int init_sdl() {
    if(SDL_Init(SDL_INIT_VIDEO) < 0) {
        return -1;
    }

    if(LOG_SDL_PTR_ERR(sdl_window, 
        SDL_CreateWindow(
            "SDL2 Window", 
            SDL_WINDOWPOS_UNDEFINED, 
            SDL_WINDOWPOS_UNDEFINED, 
            sdl_display_texture_w, 
            sdl_display_texture_h, 
            SDL_WINDOW_SHOWN
        )
    ) == NULL) {
        return -1;
    }

    if(LOG_SDL_PTR_ERR(sdl_renderer, 
        SDL_CreateRenderer(
            sdl_window, 
            -1, 
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
        )
    ) == NULL) {
        return -1;
    }

    // texture 
    if(LOG_SDL_PTR_ERR(sdl_display_texture, 
        SDL_CreateTexture(
            sdl_renderer, 
            pix_fmt_av_to_sdl(codec_ctx->pix_fmt), 
            SDL_TEXTUREACCESS_STREAMING, 
            codec_ctx->width, 
            codec_ctx->height
        )
    ) == NULL) {
        return -1;
    }

    return 0;

   
}

int is_read_frame_err_ok(int errnum) {
    if(errnum < 0) {
        if(errnum == AVERROR_EOF) {
            return 1;
        }
        if(errnum == AVERROR(EAGAIN)) {
            return 1;
        }
        print_err_str(errnum);
        return 0;
    }
    return 1;
}

int read_until_not_eagain_frame() {
    int read_frame_errnum = 0;
    for(;;) {
        read_frame_errnum = av_read_frame(ic, curr_pkt);
        if(read_frame_errnum < 0) {
            break;
        }
        if(curr_pkt->stream_index != video_stream_index) {
            av_packet_unref(curr_pkt);
            continue;
        }
        if(LOGAVERR(avcodec_send_packet(codec_ctx, curr_pkt)) < 0) {
            return 1;
        }
        int errnum = LOGAVERR(avcodec_receive_frame(codec_ctx, curr_frame));
        if ( errnum == AVERROR(EAGAIN) ) {
            av_packet_unref(curr_pkt);
            continue;
        } else if ( errnum == AVERROR_EOF ) {
            av_packet_unref(curr_pkt);
            break;
        } else if ( errnum == 0 ) {
            av_packet_unref(curr_pkt);
            break;
        } 
        print_err_str(errnum);
        return 1;
    }
    if(read_frame_errnum < 0 && read_frame_errnum != AVERROR_EOF) {
        print_err_str(read_frame_errnum);
        return 1;
    }
    return 0;
}

int read_for_n_frames(int n) {
    int n_video_frames_read = 0;
    int read_frame_errnum = 0;
    for(;n_video_frames_read < n;) {
        read_frame_errnum = av_read_frame(ic, curr_pkt);
        if(read_frame_errnum < 0) {
            return read_frame_errnum;
        }
        if(curr_pkt->stream_index != video_stream_index) {
            av_packet_unref(curr_pkt);
            continue;
        }
        if(LOGAVERR(avcodec_send_packet(codec_ctx, curr_pkt)) < 0) {
            return 1;
        }
        int errnum = LOGAVERR(avcodec_receive_frame(codec_ctx, curr_frame));
        if(errnum == AVERROR(EAGAIN)) {
            av_packet_unref(curr_pkt);
            continue;
        }
        if(!is_read_frame_err_ok(errnum)) {
            print_err_str(errnum);
            return 1;
        }
        av_packet_unref(curr_pkt);
        n_video_frames_read++;
    }
    if(read_frame_errnum < 0 && read_frame_errnum != AVERROR_EOF) {
        print_err_str(read_frame_errnum);
        return 1;
    }
    return 0;
}


int64_t estimate_frame_timestamp(int target_frame) {
    AVStream *video_stream = ic->streams[video_stream_index];
    
    // If we have a duration, use it to estimate
    if (video_stream->duration != AV_NOPTS_VALUE && video_stream->nb_frames > 0) {
        return av_rescale(target_frame, video_stream->duration, video_stream->nb_frames);
    }
    
    // If we have a frame rate, use it to estimate
    if (video_stream->avg_frame_rate.num && video_stream->avg_frame_rate.den) {
        double seconds = (double)target_frame * av_q2d(av_inv_q(video_stream->avg_frame_rate));
        return av_rescale(seconds * AV_TIME_BASE, video_stream->time_base.num, video_stream->time_base.den);
    }
    
    // If all else fails, make a very rough estimate
    return av_rescale(target_frame, video_stream->duration > 0 ? video_stream->duration : AV_TIME_BASE, 250);
}

int seek_to_frame(AVFormatContext *ic, int stream_index, AVCodecContext *codec_ctx, AVFrame *frame, AVPacket *pkt, int target_frame) {
    
    int64_t target_ts = estimate_frame_timestamp(target_frame);

    int err = 0;

    if(err = avformat_seek_file(ic, stream_index, INT64_MIN, target_ts, INT64_MAX, AVSEEK_FLAG_BACKWARD), err < 0) {
        return err;
    }

    avcodec_flush_buffers(codec_ctx);

    for(;;){
        if(err = av_read_frame(ic, pkt), err < 0) {
            return err;
        }
        
        if(pkt->stream_index != stream_index) {
            av_packet_unref(pkt);
            continue;
        }

        if(err = avcodec_send_packet(codec_ctx, pkt), err < 0) {
            return err;
        }

        if(err = avcodec_receive_frame(codec_ctx, curr_frame), err < 0) {
            if(err == AVERROR(EAGAIN)) {
                av_packet_unref(pkt);
                continue;
            }

            if(err == AVERROR_EOF) {
                av_packet_unref(pkt);
                break;
            }
            
            return err;
        }

        if(curr_frame->pts >= target_ts) {
            av_packet_unref(pkt);
            break;
        }

        av_packet_unref(pkt);
        continue;  
    }
    return 0;
}

// MAIN
//
int main(int argc, char **argv) {
    if(init_libav(argv[1]) < 0) {
        close();
        return 1;
    }
    if(init_sdl() < 0) {
        close();
        return 1;
    }

    read_until_not_eagain_frame();

    // int err = read_for_n_frames(48);
    // if (err < 0) {
    //     close();
    //     return 1;
    // }

    // seek to first frame
    // if(LOGAVERR(av_seek_frame(format_ctx, video_stream_index, 0, AVSEEK_FLAG_BACKWARD)) < 0) {
    //     return 1;
    // }
    int err = seek_to_frame(ic, video_stream_index, codec_ctx, curr_frame, curr_pkt, 720);
    if (err < 0) {
        close();
        return 1;
    }


    // avcodec_flush_buffers(codec_ctx);

    // if(LOGAVERR(av_seek_frame(format_ctx, video_stream_index, 11 * 30, AVSEEK_FLAG_FRAME)) < 0) {
    //     return 1;
    // }

    // avcodec_flush_buffers(codec_ctx);

    // put current frame in sdl texture
    AVFrame *f = curr_frame;
    sdl_display_texture = SDL_CreateTexture(sdl_renderer, pix_fmt_av_to_sdl((AVPixelFormat)f->format), SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
    if(!sdl_display_texture) {
        const char *errmsg = SDL_GetError();
        fprintf(stderr, "Error: %s\n", errmsg);
        return 1;
    }

    // if( SDL_UpdateTexture(sdl_display_texture, NULL, f->data[0], f->linesize[0]) < 0 ) {
    //     const char *errmsg = SDL_GetError();
    //     fprintf(stderr, "Error: %s\n", errmsg);
    //     return 1;
    // }
    if( SDL_UpdateYUVTexture(sdl_display_texture, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2], f->linesize[2]) < 0 ) {
        const char *errmsg = SDL_GetError();
        fprintf(stderr, "Error: %s\n", errmsg);
        return 1;
    }

    // SDL loop 
    SDL_Event event;
    int quit = 0;
    while(!quit) {
        while(SDL_PollEvent(&event)) {
            if(event.type == SDL_QUIT) {
                quit = 1;
            }
        }
        // display texture
        SDL_SetRenderDrawColor(sdl_renderer, 100, 100, 100, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, sdl_display_texture, NULL, NULL);
        SDL_RenderPresent(sdl_renderer);
    }

    // cleanup
    {
        close();
    }
    
    return 0;
}