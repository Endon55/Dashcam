
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <iostream>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
}
#ifdef av_err2str
#undef av_err2str
#include <string>
av_always_inline std::string av_err2string(int errnum)
{
    char str[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err).c_str()
#endif // av_err2str

using namespace std;

static void logging(const char *fmt, ...);
static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame);
static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename);

AVDeviceInfoList *infoList;
int main(int argc, char **argv)
{
    int ret;
    char file[] = "/home/anthony/Desktop/ice_smoked.mp4";

    logging("initializing");
    AVFormatContext *pFormatContext = avformat_alloc_context();
    if (!pFormatContext)
    {
        logging("couldnt create av format context");
        return -1;
    }
    ret = avformat_open_input(&pFormatContext, file, NULL, NULL);
    if (ret < 0)
    {
        cout << "Error: " << av_err2str(ret) << endl;
        return ret;
    }

    ret = avformat_find_stream_info(pFormatContext, NULL);
    if (ret < 0)
    {
        cout << "Error: " << av_err2str(ret) << endl;
        return ret;
    }

    const AVCodec *pCodec = NULL;
    AVCodecParameters *codecParams = NULL;
    int video_stream_index = -1;

    for (int i = 0; i < pFormatContext->nb_streams; i++)
    {
        AVCodecParameters *pLocalCodecParameters = NULL;
        pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        logging("AVStream->time_base before open coded %d/%d", pFormatContext->streams[i]->time_base.num, pFormatContext->streams[i]->time_base.den);
        logging("AVStream->r_frame_rate before open coded %d/%d", pFormatContext->streams[i]->r_frame_rate.num, pFormatContext->streams[i]->r_frame_rate.den);
        logging("AVStream->start_time %" PRId64, pFormatContext->streams[i]->start_time);
        logging("AVStream->duration %" PRId64, pFormatContext->streams[i]->duration);

        const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec == NULL)
        {
            logging("Failed to find local codec");
            continue;
        }
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (video_stream_index == -1)
            {
                video_stream_index = i;
                codecParams = pLocalCodecParameters;
                pCodec = pLocalCodec;
            }
            logging("Video Codec: resolution %d x %d", pLocalCodecParameters->width, pLocalCodecParameters->height);
        }
        else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            logging("Audio Codec: %d channels, sample rate %d", pLocalCodecParameters->ch_layout.nb_channels, pLocalCodecParameters->sample_rate);
        }

        logging("\tCodec %s ID %d bit_rate %lld", pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
    }

    if (video_stream_index == -1)
    {
        logging("File %s does not contain video stream", file);
    }

    AVCodecContext *codecContext = avcodec_alloc_context3(pCodec);
    if (!codecContext)
    {
        logging("failed to allocate memory for AVCodecContext");
        return -1;
    }

    if (avcodec_parameters_to_context(codecContext, codecParams) < 0)
    {
        logging("failed to assign codec params to codec context");
        return -1;
    }

    if (avcodec_open2(codecContext, pCodec, NULL) < 0)
    {
        logging("failed to open the codec");
        return -1;
    }
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        logging("failed to allocate memory for AVFrame");
        return -1;
    }

    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        logging("failed to allocate memory for AVPacket");
        return -1;
    }

    int packets_to_process = 8;

    while (av_read_frame(pFormatContext, packet) >= 0)
    {
        if (packet->stream_index == video_stream_index)
        {
            logging("AVPackets pts: %d", packet->pts);
            ret = decode_packet(packet, codecContext, frame);
            if (ret < 0)
            {
                break;
            }
            if (--packets_to_process <= 0)
                break;
        }
        av_packet_unref(packet);
    }

    avformat_close_input(&pFormatContext);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecContext);

    return 0;
}
static int decode_packet(AVPacket *packet, AVCodecContext *codecContext, AVFrame *frame)
{
    int ret = avcodec_send_packet(codecContext, packet);
    if (ret < 0)
    {
        logging("Error while sending packet to decoder: %s", av_err2str(ret));
        return ret;
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(codecContext, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        else if (ret < 0)
        {
            logging("Error while receiving frame from decoder: %s", av_err2str(ret));
            return ret;
        }

        logging("ret %d", ret);
        if (ret >= 0)
        {
            logging("Frame %d (type=%c, format=%d) pts %d",
                    codecContext->frame_num,
                    av_get_picture_type_char(frame->pict_type),
                    frame->format,
                    frame->pts);
        }
        
        char frame_filename[1024];
        snprintf(frame_filename, sizeof(frame_filename), "%s-%ld.pgm", "frame", codecContext->frame_num);

        if (frame->format != AV_PIX_FMT_YUV420P)
        {
            logging("Warning: the generated file may not be a grayscale image, but just the R component if the video format is RGB");
        }
        save_gray_frame(frame->data[0], frame->linesize[0], frame->width, frame->height, frame_filename);
        logging("frame dim %d x %d", frame->width, frame->height);
    }
    return 0;
}

static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename, "w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

    // writing line by line
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

static void logging(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}
