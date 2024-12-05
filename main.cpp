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
AVFormatContext *format_ctx = NULL;
AVCodecContext *codec_ctx = NULL;
const AVCodec *codec = NULL;
int video_stream_index = -1;
AVFrame *curr_frame = NULL;
AVPacket *curr_pkt = NULL;

int64_t frame_ptss[44000] = {0};
int key_frame_indices[4400] = {0};
int key_frame_indices_len = 0;
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
        if (format_ctx) {
            avformat_close_input(&format_ctx);
            format_ctx = NULL;
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
    format_ctx = avformat_alloc_context();
    if(LOGAVERR(avformat_open_input(&format_ctx,filename, NULL, NULL)) < 0) {
        return -1;
    }
    if(LOGAVERR(avformat_find_stream_info(format_ctx, NULL)) < 0) {
        return -1;
    }
    
    for(int i = 0; i < format_ctx->nb_streams; i++) {
        if(format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
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
    
    if(LOGAVERR(avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_stream_index]->codecpar)) < 0) {
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
    while(read_frame_errnum = av_read_frame(format_ctx, curr_pkt), read_frame_errnum >= 0) {
        if(curr_pkt->stream_index != video_stream_index) {
            av_packet_unref(curr_pkt);
            continue;
        }
        if(LOGAVERR(avcodec_send_packet(codec_ctx, curr_pkt)) < 0) {
            return 1;
        }
        int errnum = LOGAVERR(avcodec_receive_frame(codec_ctx, curr_frame));
        if(!is_read_frame_err_ok(errnum)) {
            print_err_str(errnum);
            return 1;
        }
        if(errnum == AVERROR(EAGAIN)) {
            av_packet_unref(curr_pkt);
            continue;
        }
        av_packet_unref(curr_pkt);
        break;

    }
    if(read_frame_errnum < 0 && read_frame_errnum != AVERROR_EOF) {
        print_err_str(read_frame_errnum);
        return 1;
    }
}

int fill_frames_pts_array() {
    int read_frame_errnum = 0;
    int frame_index = 0;
    while(read_frame_errnum = av_read_frame(format_ctx, curr_pkt), read_frame_errnum >= 0) {
        if(curr_pkt->stream_index != video_stream_index) {
            av_packet_unref(curr_pkt);
            continue;
        }
        if(LOGAVERR(avcodec_send_packet(codec_ctx, curr_pkt)) < 0) {
            return 1;
        }
        int errnum = LOGAVERR(avcodec_receive_frame(codec_ctx, curr_frame));
        if(!is_read_frame_err_ok(errnum)) {
            print_err_str(errnum);
            return 1;
        }
        if(errnum == AVERROR(EAGAIN)) {
            av_packet_unref(curr_pkt);
            continue;
        }
        av_packet_unref(curr_pkt);
        frame_ptss[frame_index] = curr_frame->pts;
        if(curr_frame->key_frame) {
            key_frame_indices[key_frame_indices_len] = frame_index;
            key_frame_indices_len++;
        }
        frame_index++;
    }
    if(read_frame_errnum < 0 && read_frame_errnum != AVERROR_EOF) {
        print_err_str(read_frame_errnum);
        return 1;
    }

    // seek to first frame
    if(LOGAVERR(av_seek_frame(format_ctx, video_stream_index, frame_ptss[0], AVSEEK_FLAG_BACKWARD)) < 0) {
        return 1;
    }

    if(LOGAVERR(av_seek_frame(format_ctx, video_stream_index, 11 * 30, AVSEEK_FLAG_FRAME)) < 0) {
        return 1;
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

    // seek to first frame
    if(LOGAVERR(av_seek_frame(format_ctx, video_stream_index, 0, AVSEEK_FLAG_BACKWARD)) < 0) {
        return 1;
    }

    if(LOGAVERR(av_seek_frame(format_ctx, video_stream_index, 11 * 30, AVSEEK_FLAG_FRAME)) < 0) {
        return 1;
    }
    
    return 0;
}