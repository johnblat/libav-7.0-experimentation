
#include "state.h"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include "texture_ring.h"
#include <raylib.h>

AVFormatContext *ic = NULL;
AVCodecContext *codec_ctx = NULL;
const AVCodec *codec = NULL;
int video_stream_index = -1;
SwsContext *sws_ctx = NULL;

AVPacket *curr_pkt = NULL;
AVFrame *curr_frame = NULL;
Image curr_frame_image = {0};

TextureFrameRing tfring;
RingSubsectionTransitionUpdateRecord
    ring_subsection_transition_update_table[TRANSITON_TABLE_SIZE];

AVPixelFormat av_rgb_pixel_fmt = AV_PIX_FMT_RGB24;
PixelFormat raylib_rgb_pixel_fmt = PIXELFORMAT_UNCOMPRESSED_R8G8B8;