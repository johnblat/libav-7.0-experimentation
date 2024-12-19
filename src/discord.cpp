// global include section
#include "src/include/SDL2/SDL_image.h"
#include "src/include/json.hpp"
#include <SDL.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <queue>
#include <random>
#include <string>
#include <thread>

extern "C"
{

// fix imports
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

// Using declarations
using json = nlohmann::json;
using namespace std;

// Structures for channel, video and their associated components
typedef struct PacketQueue
{
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

struct video
{
    string path;
    string sub;
};
struct channel
{
    int number;
    string cat;
    string logo;
    string name;
    vector<video> vids;
};

PacketQueue audioq;
int quit = 0;

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;)
    {

        if (quit)
        {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1)
        {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{

    AVPacketList *pkt1;
    pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}
void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}
int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)
{

    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;

    int len1, data_size = 0;

    for (;;)
    {
        while (audio_pkt_size > 0)
        {
            int got_frame = 0;
            len1 = pkt.size;
            if (len1 < 0)
            {
                /* if error, skip frame */
                audio_pkt_size = 0;
                break;
            }
            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            data_size = 0;
            if (got_frame)
            {
                data_size = av_samples_get_buffer_size(NULL, aCodecCtx->ch_layout.nb_channels, frame.nb_samples,
                                                       aCodecCtx->sample_fmt, 1);
                assert(data_size <= buf_size);
                memcpy(audio_buf, frame.data[0], data_size);
            }
            if (data_size <= 0)
            {
                /* No data yet, get more frames */
                continue;
            }
            /* We have data, return it and come back for more later */
            return data_size;
        }
        if (pkt.data)
            av_packet_unref(&pkt);
        if (quit)
        {
            return -1;
        }

        if (packet_queue_get(&audioq, &pkt, 1) < 0)
        {
            return -1;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }
}
void audioCallback(void *userdata, Uint8 *stream, int len)
{

    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
    int len1, audio_size;

    static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while (len > 0)
    {
        if (audio_buf_index >= audio_buf_size)
        {
            /* We have already sent all our data; get more */
            audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
            if (audio_size < 0)
            {
                /* If error, output silence */
                audio_buf_size = 1024;
                memset(audio_buf, 0, audio_buf_size);
            }
            else
            {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if (len1 > len)
            len1 = len;
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}
// Open up the channels.json file and read it
vector<channel> loadChannels()
{
    vector<channel> channels;
    ifstream chns("channels.json");

    // throw error if file Failed to be opened
    if (!chns.is_open())
    {
        cerr << "Error, Failed to open channels.json" << endl;
        return channels;
    }

    try
    {
        json channelsFile;
        chns >> channelsFile;
        // channelsFile contains the list of all channels
        // and channelFile contains the list of all
        // programs and channel metadata

        for (const auto &chnName : channelsFile)
        {
            string channelFile = chnName.get<string>() + ".chn";
            //.chn file format is .json under the hood
            ifstream chn(channelFile);

            // throw error if .chn file Failed to be opened, execution
            // will continue for other files
            if (!chn.is_open())
            {
                cerr << "Error, Failed to open " << channelFile << endl;
                continue;
            }

            try
            {
                json chnData;
                chn >> chnData;

                channel currChn;
                currChn.number = chnData.value("chnNo", 0); // default to 0
                currChn.name = chnData.value("name", "");
                currChn.logo = chnData.value("logo", "");
                currChn.cat = chnData.value("cat", "");

                // collect video and subtitle data
                for (const auto &vidData : chnData["videos"])
                {
                    video vid;
                    vid.path = vidData.value("path", "");
                    vid.sub = vidData.value("subs", "");
                    currChn.vids.push_back(vid);
                }

                // send this whole channel data to channels vector
                channels.push_back(currChn);
            }
            catch (const json::exception &e)
            {
                cerr << "Error in JSON parsing: " << chnName << endl;
                continue;
            }
        }
    }
    catch (const json::exception &e)
    {
        cerr << "Error in JSON parsing: channels.json" << endl;
    }
    return channels;
}

// Initialise a SDL_window
SDL_Window *initialiseSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        cerr << "Error initializing SDL: " << SDL_GetError() << endl;
        exit(1);
    }
    SDL_Window *window = SDL_CreateWindow("TV Application", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920, 1080,
                                          SDL_WINDOW_SHOWN);
    if (!window)
    {
        cerr << "Error creating window: " << SDL_GetError() << endl;
        SDL_Quit();
        exit(1);
    }
    return window;
}

// Choose and play a video from channel's list
void playVideo(const string &filePath, SDL_Window *window)
{
    AVFormatContext *formatCtx = nullptr;
    AVCodecContext *vCodecCtx = nullptr;
    AVCodecContext *aCodecCtx = nullptr;
    AVFrame *frame = nullptr;
    AVFrame *frameYUV = nullptr;
    AVPacket *packet = nullptr;
    struct SwsContext *swsCtx = nullptr;

    // Initialize format context
    formatCtx = avformat_alloc_context();
    if (!formatCtx)
    {
        cerr << "Failed to allocate memory for format context" << endl;
        return;
    }

    if (avformat_open_input(&formatCtx, filePath.c_str(), nullptr, nullptr) < 0)
    {
        cerr << "Failed to open file: " << filePath << endl;
        return;
    }

    if (avformat_find_stream_info(formatCtx, nullptr) < 0)
    {
        cerr << "Failed to find stream information" << endl;
        return;
    }

    // Find video stream
    int videoStream = -1;
    int audioStream = -1;
    const AVCodec *vCodec = nullptr;
    const AVCodec *aCodec = nullptr;
    for (unsigned int i = 0; i < formatCtx->nb_streams; i++)
    {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = i;
            vCodec = avcodec_find_decoder(formatCtx->streams[i]->codecpar->codec_id);
            break;
        }
    }
    for (unsigned int i = 0; i < formatCtx->nb_streams; i++)
    {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioStream = i;
            aCodec = avcodec_find_decoder(formatCtx->streams[i]->codecpar->codec_id);
            break;
        }
    }

    if (videoStream == -1 || !vCodec)
    {
        cerr << "Failed to find video stream or vCodec" << endl;
        return;
    }
    if (audioStream == -1 || !aCodec)
    {
        cerr << "Failed to find audio stream or vCodec" << endl;
        return;
    }

    // Setup vCodec context
    vCodecCtx = avcodec_alloc_context3(vCodec);
    aCodecCtx = avcodec_alloc_context3(aCodec);

    if (!vCodecCtx)
    {
        cerr << "Failed to allocate vCodec context" << endl;
        return;
    }
    if (avcodec_parameters_to_context(vCodecCtx, formatCtx->streams[videoStream]->codecpar) < 0)
    {
        cerr << "Failed to copy vCodec params" << endl;
        return;
    }
    if (avcodec_open2(vCodecCtx, vCodec, nullptr) < 0)
    {
        cerr << "Failed to open vCodec" << endl;
        return;
    }
    if (!aCodecCtx)
    {
        cerr << "Failed to allocate aCodec context" << endl;
        return;
    }
    if (avcodec_parameters_to_context(aCodecCtx, formatCtx->streams[audioStream]->codecpar) < 0)
    {
        cerr << "Failed to copy aCodec params" << endl;
        return;
    }
    if (avcodec_open2(aCodecCtx, aCodec, nullptr) < 0)
    {
        cerr << "Failed to open aCodec" << endl;
        return;
    }
    SDL_AudioSpec wanted_spec, spec;
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = aCodecCtx->ch_layout.nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audioCallback;
    wanted_spec.userdata = aCodecCtx;

    if (SDL_OpenAudio(&wanted_spec, &spec) < 0)
    {
        cerr << "Failed to setup SDL_Audio " << SDL_GetError() << endl;
    }
    // Allocate frames and packet
    frame = av_frame_alloc();
    frameYUV = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !frameYUV || !packet)
    {
        cerr << "Failed to allocate frames or packet" << endl;
        return;
    }

    // Initialize SDL renderer and texture
    SDL_Renderer *renderer = nullptr;
    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer)
    {
        cerr << "Failed to create SDL renderer " << SDL_GetError() << endl;
        return;
    }
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                             vCodecCtx->width, vCodecCtx->height);
    if (!texture)
    {
        cerr << "Failed to create SDL texture " << SDL_GetError() << endl;
        return;
    }

    // Initialize scaling context
    swsCtx = sws_getContext(vCodecCtx->width, vCodecCtx->height, vCodecCtx->pix_fmt, vCodecCtx->width,
                            vCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);

    // Allocate YUV buffer
    av_image_alloc(frameYUV->data, frameYUV->linesize, vCodecCtx->width, vCodecCtx->height, AV_PIX_FMT_YUV420P, 1);

    // Main playback loop
    SDL_Rect rect = {0, 0, vCodecCtx->width, vCodecCtx->height};
    SDL_Event event;
    bool quit = false;
    avcodec_open2(aCodecCtx, aCodec, nullptr);
    packet_queue_init(&audioq);

    while (av_read_frame(formatCtx, packet) >= 0)
    {
        if (packet->stream_index == videoStream)
        {
            if (avcodec_send_packet(vCodecCtx, packet) < 0)
            {
                break;
            }

            while (true)
            {
                int ret = avcodec_receive_frame(vCodecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    quit = true;
                    break;
                }

                sws_scale(swsCtx, frame->data, frame->linesize, 0, vCodecCtx->height, frameYUV->data,
                          frameYUV->linesize);

                SDL_UpdateTexture(texture, nullptr, frameYUV->data[0], frameYUV->linesize[0]);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, nullptr, &rect);
                SDL_RenderPresent(renderer);

                // Add some delay to control playback speed
                SDL_Delay(1000 / 30); // 30 FPS approximation
            }
        }
        else if (packet->stream_index == audioStream)
        {
            packet_queue_put(&audioq, packet);
        }
        else
        {
            av_packet_free(&packet);
        }

        // Handle SDL events
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                quit = true;
                break;
            }
        }
    }

cleanup:
    // Cleanup resources
    if (swsCtx)
        sws_freeContext(swsCtx);
    if (frameYUV)
        av_frame_free(&frameYUV);
    if (frame)
        av_frame_free(&frame);
    if (packet)
        av_packet_free(&packet);
    if (vCodecCtx)
        avcodec_free_context(&vCodecCtx);
    if (formatCtx)
        avformat_close_input(&formatCtx);
    if (texture)
        SDL_DestroyTexture(texture);
    if (renderer)
        SDL_DestroyRenderer(renderer);
}
// I'll be honest, I don't know what is going on in this whole function
// props to FFMPEG for making such an archaic library

// Start up and initialise a channel
void startChannel(const vector<channel> &channels, SDL_Window *window)
{
    if (channels.empty())
    {
        cerr << "No channels loaded" << endl;
        return;
    }

    const channel &currChannel = channels[0]; // channel 1

    // Extracting all videos into a list
    vector<string> vidPaths;
    for (const auto &vid : currChannel.vids)
    {
        vidPaths.push_back(vid.path);
    }

    // Randomise list of videos and display a blank screen
    random_device rd;
    mt19937 gen(rd());
    shuffle(vidPaths.begin(), vidPaths.end(), gen);

    // play from random list
    for (const auto &vidPath : vidPaths)
    {
        playVideo(vidPath, window);
    }
}

// main
int main(int argc, char **argv)
{