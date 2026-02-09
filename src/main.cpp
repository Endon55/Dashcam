#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <iostream>
#include "Window.h"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
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
AppState *app_state;
SDL_Rect *rect;
int main(int argc, char **argv)
{

   unsigned int count = 10000;
   app_state = (AppState *)malloc(sizeof(AppState));
   rect = (SDL_Rect *)malloc(sizeof(SDL_Rect));
   rect->x = 0;
   rect->y = 0;

   *app_state = (AppState){
       .width = 0,
       .height = 0};

   int ret;

   logging("initializing");
   avdevice_register_all();

   AVFormatContext *formatContext = avformat_alloc_context();
   if (!formatContext)
   {
      logging("couldnt create av format context");
      return -1;
   }
   const AVInputFormat *inputFormat = av_find_input_format("v4l2");
   if (!inputFormat)
   {
      logging("coudln't load input format v4l2");
      return -1;
   }
   ret = avformat_open_input(&formatContext, "/dev/video0", inputFormat, NULL);
   if (ret < 0)
   {
      logging("Error opening av format: %s", av_err2str(ret));
      avformat_free_context(formatContext);
      return ret;
   }
   AVDeviceInfoList *devicesInfo;
   logging("device count: %d", avdevice_list_devices(formatContext, &devicesInfo));
   AVDeviceInfo *info = devicesInfo->devices[0];
   logging("device name: %c", info->device_name);
   int video_stream_index = -1;
   const AVCodec *codec = NULL;
   AVCodecParameters *codecParams = NULL;
   AVCodecContext *codecContext = avcodec_alloc_context3(codec);
   SwsContext *sws_ctx;
   AVFrame *frame = av_frame_alloc();
   AVPacket *packet = av_packet_alloc();
   AVFilterGraph *filterGraph = avfilter_graph_alloc();
   AVFrame *yuv_frame = av_frame_alloc();
   const int sink_pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};

   ret = avformat_find_stream_info(formatContext, NULL);
   if (ret < 0)
   {
      logging("Error finding stream info: %s", av_err2str(ret));
      ret = -1;
      goto cleanup;
   }

   for (int i = 0; i < formatContext->nb_streams; i++)
   {
      AVCodecParameters *pLocalCodecParameters = NULL;
      pLocalCodecParameters = formatContext->streams[i]->codecpar;
      logging("AVStream->time_base before open coded %d/%d", formatContext->streams[i]->time_base.num, formatContext->streams[i]->time_base.den);
      logging("AVStream->r_frame_rate before open coded %d/%d", formatContext->streams[i]->r_frame_rate.num, formatContext->streams[i]->r_frame_rate.den);
      logging("AVStream->start_time %" PRId64, formatContext->streams[i]->start_time);
      logging("AVStream->duration %" PRId64, formatContext->streams[i]->duration);

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
            codec = pLocalCodec;
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
      logging("Couldn't find a video stream");
      ret = -1;
      goto cleanup;
   }

   if (!codecContext)
   {
      logging("failed to allocate memory for AVCodecContext");
      ret = -1;
      goto cleanup;
   }

   if (avcodec_parameters_to_context(codecContext, codecParams) < 0)
   {
      logging("failed to assign codec params to codec context");
      ret = -1;
      goto cleanup;
   }

   if (avcodec_open2(codecContext, codec, NULL) < 0)
   {
      logging("failed to open the codec");
      ret = -1;
      goto cleanup;
   }

   app_state->width = codecContext->width;
   app_state->height = codecContext->height;
   rect->w = app_state->width;
   rect->h = app_state->height;

   if (SDL_init(app_state, 0, NULL) != SDL_APP_CONTINUE)
   {
      ret = -1;
      goto cleanup;
   }

   sws_ctx = sws_getContext(codecContext->width,
                            codecContext->height,
                            codecContext->pix_fmt,
                            codecContext->width,
                            codecContext->height,
                            AV_PIX_FMT_YUV420P,
                            SWS_BILINEAR,
                            NULL,
                            NULL,
                            NULL);
   if (!sws_ctx)
   {
      logging("failed to create sws context");
      ret = -1;
      goto cleanup;
   }
   if (!frame)
   {
      logging("failed to allocate memory for AVFrame");
      ret = -1;
      goto cleanup;
   }

   if (!packet)
   {
      logging("failed to allocate memory for AVPacket");
      ret = -1;
      goto cleanup;
   }

   if (filterGraph == NULL)
   {
      logging("failed to allocate memory for AVFilterGraph");
      ret = -1;
      goto cleanup;
   }

   // Create buffer source args with proper format
   char args[512];
   snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            codecContext->width, codecContext->height, AV_PIX_FMT_YUV420P,
            formatContext->streams[video_stream_index]->time_base.num,
            formatContext->streams[video_stream_index]->time_base.den,
            codecContext->sample_aspect_ratio.num,
            codecContext->sample_aspect_ratio.den);
   //
   AVFilterContext *source;
   AVFilterContext *sink;
   AVFilterContext *filterContext;
   
   ret = avfilter_graph_create_filter(&source, avfilter_get_by_name("buffer"), "in", args, NULL, filterGraph);
   if (ret < 0)
   {
      logging("failed to create buffer source: %s", av_err2str(ret));
      ret = -1;
      goto cleanup;
   }

   sink = avfilter_graph_alloc_filter(filterGraph, avfilter_get_by_name("buffersink"), "out");
   if (!sink)
   {
      logging("failed to allocate buffer sink");
      ret = -1;
      goto cleanup;
   }
   //ret = av_opt_set_int_list(sink, "pix_fmts", sink_pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
   if (ret < 0)
   {
      logging("failed to set buffer sink pixel format: %s", av_err2str(ret));
      ret = -1;
      goto cleanup;
   }

   ret = avfilter_init_str(sink, NULL);
   if (ret < 0)
   {
      logging("failed to initialize buffer sink: %s", av_err2str(ret));
      ret = -1;
      goto cleanup;
   }

   filterContext = avfilter_graph_alloc_filter(filterGraph, avfilter_get_by_name("hflip"), "hflip");
   if (!filterContext)
   {
      logging("failed to allocate filter context");
      ret = -1;
      goto cleanup;
   }

   ret = avfilter_init_str(filterContext, NULL);
   if (ret < 0)
   {
      logging("failed to initialize filter: %s", av_err2str(ret));
      ret = -1;
      goto cleanup;
   }

   ret = avfilter_link(source, 0, filterContext, 0);
   if (ret < 0)
   {
      logging("failed to link source to filter: %s", av_err2str(ret));
      ret = -1;
      goto cleanup;
   }

   ret = avfilter_link(filterContext, 0, sink, 0);
   if (ret < 0)
   {
      logging("failed to link filter to sink: %s", av_err2str(ret));
      ret = -1;
      goto cleanup;
   }

   ret = avfilter_graph_config(filterGraph, NULL);
   if (ret < 0)
   {
      logging("failed to configure filter graph: %s", av_err2str(ret));
      ret = -1;
      goto cleanup;
   }

   if (!yuv_frame)
   {
      logging("failed to allocate memory for YUV frame");
      ret = -1;
      goto cleanup;
   }

   yuv_frame->format = AV_PIX_FMT_YUV420P;
   yuv_frame->width = codecContext->width;
   yuv_frame->height = codecContext->height;
   if (av_frame_get_buffer(yuv_frame, 32) < 0)
   {
      logging("failed to allocate YUV frame buffer");
      ret = -1;
      goto cleanup;
   }

   while (count-- > 0)
   {

      SDL_Event ev;
      while (SDL_PollEvent(&ev))
      {
         if (SDL_event(app_state, &ev) != SDL_APP_CONTINUE)
         {
            return 0;
         }
      }

      ret = av_read_frame(formatContext, packet);
      if (ret < 0)
      {
         logging("No more frames");
         break;
      }

      if (packet->stream_index != video_stream_index)
      {
         av_packet_unref(packet);
         continue;
      }

      ret = avcodec_send_packet(codecContext, packet);

      if (ret < 0)
      {
         logging("Error while sending packet to decoder: %s", av_err2str(ret));
         // return ret;
         continue;
      }
      ret = avcodec_receive_frame(codecContext, frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      {
         av_packet_unref(packet);
         continue;
      }
      if (ret < 0)
      {
         logging("Error while receiving frame from decoder: %s", av_err2str(ret));
         av_packet_unref(packet);
         continue;
      }

      AVFrame *display_frame = frame;

      // Convert to YUV420P if needed
      if (display_frame->format != AV_PIX_FMT_YUV420P)
      {
         if (av_frame_make_writable(yuv_frame) < 0)
         {
            av_packet_unref(packet);
            continue;
         }
         sws_scale(sws_ctx,
                   display_frame->data,
                   display_frame->linesize,
                   0,
                   codecContext->height,
                   yuv_frame->data,
                   yuv_frame->linesize);
         display_frame = yuv_frame;
      }
      else
      {
         logging("already in the correct format");
      }
      // Apply filter
      AVFrame *filtered_frame = av_frame_alloc();

      ret = av_buffersrc_add_frame_flags(source, display_frame, AV_BUFFERSRC_FLAG_KEEP_REF);
      if (ret < 0)
      {
         logging("Error while feeding the filter: %s", av_err2str(ret));
         av_frame_free(&filtered_frame);
         av_packet_unref(packet);
         continue;
      }

      ret = av_buffersink_get_frame(sink, filtered_frame);
      if (ret < 0)
      {
         logging("Error while retrieving frame from filter: %s", av_err2str(ret));
         av_frame_free(&filtered_frame);
         av_packet_unref(packet);
         continue;
      }

      SDL_UpdateYUVTexture(app_state->texture, rect,
                           filtered_frame->data[0], filtered_frame->linesize[0],
                           filtered_frame->data[1], filtered_frame->linesize[1],
                           filtered_frame->data[2], filtered_frame->linesize[2]);

      av_frame_free(&filtered_frame);

      if (SDL_iterate(app_state) != SDL_APP_CONTINUE)
      {
         break;
      }

      av_packet_unref(packet);
   }

cleanup:

   if (devicesInfo != NULL)
   {
      avdevice_free_list_devices(&devicesInfo);
   }
   if (formatContext != NULL)
   {
      avformat_close_input(&formatContext);
      avformat_free_context(formatContext);
   }
   if (packet != NULL)
   {
      av_packet_free(&packet);
   }
   if (frame != NULL)
   {
      av_frame_free(&frame);
   }
   if (yuv_frame != NULL)
   {
      av_frame_free(&yuv_frame);
   }
   if (yuv_frame != NULL)
   {
      av_frame_free(&yuv_frame);
   }
   if (sws_ctx != NULL)
   {
      sws_freeContext(sws_ctx);
   }
   if (codecContext != NULL)
   {
      avcodec_free_context(&codecContext);
   }
   if (filterGraph != NULL)
   {
      avfilter_graph_free(&filterGraph);
   }

   return ret;
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
