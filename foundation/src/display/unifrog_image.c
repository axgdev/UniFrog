#include <unifrog/image.h>

#include <stdlib.h>
#include <string.h>

#include <unifrog/log.h>
#include <unifrog/perf.h>
#include <unifrog/surface_alloc.h>

#define STBI_ASSERT(x) ((void)0)
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#if defined(__TINYC__)
#define STBI_NO_SIMD
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#define UNIFROG_IMAGE_FFMPEG_ENABLED \
   (UNIFROG_HCRTOS_MEDIA_NATIVE || UNIFROG_HCRTOS_MEDIA_FIRMWARE)

#if UNIFROG_IMAGE_FFMPEG_ENABLED
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
#endif

#define UNIFROG_IMAGE_MAX_PIXELS (1024u * 1536u)
#define UNIFROG_IMAGE_PIXEL_ALIGNMENT 32u
#define UNIFROG_SVG_TARGET_WIDTH 320u
#define UNIFROG_SVG_TARGET_HEIGHT 240u

static int has_ext(const char *path, const char *ext)
{
   size_t path_len;
   size_t ext_len;

   if (!path || !ext)
      return 0;
   path_len = strlen(path);
   ext_len = strlen(ext);
   if (path_len < ext_len)
      return 0;
   path += path_len - ext_len;
   for (size_t i = 0; i < ext_len; i++) {
      char a = path[i];
      char b = ext[i];

      if (a >= 'A' && a <= 'Z')
         a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z')
         b = (char)(b - 'A' + 'a');
      if (a != b)
         return 0;
   }
   return 1;
}

static int unifrog_image_store_rgba(unifrog_image *image, unsigned width,
   unsigned height, const uint8_t *rgba, unsigned stride)
{
   uint16_t *pixels;
   uint8_t *alpha;

   if (!image || !rgba || width == 0 || height == 0 ||
       (uint64_t)width * (uint64_t)height > UNIFROG_IMAGE_MAX_PIXELS)
      return -1;

   /*
    * Keep the public image representation identical to the PNG loader:
    * hardware-friendly RGB565 pixels allocated from the surface allocator plus
    * a byte-per-pixel alpha plane for compositing and page rendering.
    */
   pixels = unifrog_surface_memalign(UNIFROG_IMAGE_PIXEL_ALIGNMENT,
      (size_t)width * (size_t)height * sizeof(uint16_t));
   if (!pixels)
      return -1;

   alpha = malloc((size_t)width * (size_t)height);
   if (!alpha) {
      unifrog_surface_free(pixels);
      return -1;
   }

   for (unsigned y = 0; y < height; y++) {
      const uint8_t *row = rgba + (size_t)y * stride;

      for (unsigned x = 0; x < width; x++) {
         size_t dst = (size_t)y * (size_t)width + (size_t)x;
         const uint8_t *p = row + (size_t)x * 4u;

         pixels[dst] = UNIFROG_RGB565(p[0], p[1], p[2]);
         alpha[dst] = p[3];
      }
   }

   image->width = width;
   image->height = height;
   image->pixels = pixels;
   image->alpha = alpha;
   return 0;
}

static int unifrog_image_load_stb(const char *path, unifrog_image *image)
{
   unsigned char *rgba;
   int width;
   int height;
   int channels;
   int ret;

   if (!path || !image)
      return -1;

   /*
    * stb_image gives the reader a small no-library fallback for common still
    * formats. SVG is handled separately so XML text is not misdetected here.
    */
   if (!stbi_info(path, &width, &height, &channels))
      return -1;
   if (width <= 0 || height <= 0 ||
       (uint64_t)width * (uint64_t)height > UNIFROG_IMAGE_MAX_PIXELS)
      return -1;

   rgba = stbi_load(path, &width, &height, &channels, 4);
   if (!rgba)
      return -1;
   ret = unifrog_image_store_rgba(image, (unsigned)width, (unsigned)height,
      rgba, (unsigned)width * 4u);
   stbi_image_free(rgba);
   return ret;
}

static int unifrog_image_load_svg(const char *path, unifrog_image *image)
{
   NSVGimage *svg = NULL;
   NSVGrasterizer *rast = NULL;
   uint8_t *rgba = NULL;
   unsigned width;
   unsigned height;
   float scale = 1.0f;
   uint32_t start;
   uint32_t parsed;
   uint32_t rastered;
   int ret = -1;

   if (!path || !image)
      return -1;
   start = unifrog_perf_time_ms();

   /*
    * Match the JS2300 viewer convention: parse SVG units as CSS pixels at
    * 96 DPI, then rasterize once into RGBA before converting to RGB565.
    */
   svg = nsvgParseFromFile(path, "px", 96.0f);
   if (!svg)
      goto out;
   parsed = unifrog_perf_time_ms();

   width = (unsigned)(svg->width + 0.5f);
   height = (unsigned)(svg->height + 0.5f);
   if (width == 0 || height == 0)
      goto out;

   /*
    * NanoSVG rasterization is CPU-bound; GE cannot interpret vector paths but
    * can cheaply stretch the resulting RGB565 surface. Rasterize vectors
    * directly at their screen-fit size and leave presentation scaling/zoom to
    * GE. This avoids spending seconds producing pixels the display discards.
    */
   if (width > UNIFROG_SVG_TARGET_WIDTH ||
       height > UNIFROG_SVG_TARGET_HEIGHT) {
      float sx = (float)UNIFROG_SVG_TARGET_WIDTH / (float)width;
      float sy = (float)UNIFROG_SVG_TARGET_HEIGHT / (float)height;

      scale = sx < sy ? sx : sy;
      width = (unsigned)(svg->width * scale + 0.5f);
      height = (unsigned)(svg->height * scale + 0.5f);
   }
   if (width == 0)
      width = 1;
   if (height == 0)
      height = 1;
   rgba = malloc((size_t)width * (size_t)height * 4u);
   rast = nsvgCreateRasterizer();
   if (!rgba || !rast)
      goto out;

   nsvgRasterize(rast, svg, 0.0f, 0.0f, scale, rgba, (int)width,
      (int)height, (int)width * 4);
   rastered = unifrog_perf_time_ms();
   ret = unifrog_image_store_rgba(image, width, height, rgba, width * 4u);
   unifrog_log("unifrog svg raster ret=%d intrinsic=%ux%u output=%ux%u "
      "parse_ms=%u raster_ms=%u store_ms=%u path=%s\n", ret,
      (unsigned)(svg->width + 0.5f), (unsigned)(svg->height + 0.5f),
      width, height, (unsigned)(parsed - start), (unsigned)(rastered - parsed),
      (unsigned)(unifrog_perf_time_ms() - rastered), path);

out:
   free(rgba);
   if (rast)
      nsvgDeleteRasterizer(rast);
   if (svg)
      nsvgDelete(svg);
   return ret;
}

#if UNIFROG_IMAGE_FFMPEG_ENABLED
static int unifrog_image_load_ffmpeg(const char *path, unifrog_image *image)
{
   AVFormatContext *fmt = NULL;
   AVCodecContext *codec_ctx = NULL;
   const AVCodec *decoder = NULL;
   AVFrame *frame = NULL;
   AVPacket *packet = NULL;
   struct SwsContext *sws = NULL;
   uint8_t *rgba = NULL;
   int rgba_linesize[4];
   uint8_t *rgba_data[4];
   int stream_index;
   int got_frame = 0;
   int ret = -1;

   if (!path || !image)
      return -1;
   memset(image, 0, sizeof(*image));

   if (avformat_open_input(&fmt, path, NULL, NULL) != 0)
      goto out;
   if (avformat_find_stream_info(fmt, NULL) != 0)
      goto out;

   stream_index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1,
      &decoder, 0);
   if (stream_index < 0 || !decoder)
      goto out;

   codec_ctx = avcodec_alloc_context3(decoder);
   if (!codec_ctx)
      goto out;
   if (avcodec_parameters_to_context(codec_ctx,
       fmt->streams[stream_index]->codecpar) != 0)
      goto out;
   if (avcodec_open2(codec_ctx, decoder, NULL) != 0)
      goto out;

   packet = av_packet_alloc();
   frame = av_frame_alloc();
   if (!packet || !frame)
      goto out;

   while (!got_frame && av_read_frame(fmt, packet) >= 0) {
      int send_ret;

      if (packet->stream_index != stream_index) {
         av_packet_unref(packet);
         continue;
      }

      send_ret = avcodec_send_packet(codec_ctx, packet);
      av_packet_unref(packet);
      if (send_ret < 0 && send_ret != AVERROR(EAGAIN) &&
          send_ret != AVERROR_EOF)
         goto out;

      for (;;) {
         int recv_ret = avcodec_receive_frame(codec_ctx, frame);

         if (recv_ret == 0) {
            got_frame = 1;
            break;
         }
         if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
            break;
         goto out;
      }
   }

   if (!got_frame) {
      int flush_ret = avcodec_send_packet(codec_ctx, NULL);

      if (flush_ret < 0 && flush_ret != AVERROR(EAGAIN) &&
          flush_ret != AVERROR_EOF)
         goto out;
      for (;;) {
         int recv_ret = avcodec_receive_frame(codec_ctx, frame);

         if (recv_ret == 0) {
            got_frame = 1;
            break;
         }
         if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
            break;
         goto out;
      }
   }

   if (!got_frame)
      goto out;
   if (frame->width <= 0 || frame->height <= 0 ||
       (uint64_t)frame->width * (uint64_t)frame->height >
          UNIFROG_IMAGE_MAX_PIXELS)
      goto out;

   /*
    * Normalize every supported decoder through RGBA first. That keeps the
    * conversion path simple while still producing RGB565 storage for callers.
    */
   sws = sws_getContext(frame->width, frame->height,
      (enum AVPixelFormat)frame->format, frame->width, frame->height,
      AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
   if (!sws)
      goto out;

   rgba = malloc((size_t)frame->width * (size_t)frame->height * 4u);
   if (!rgba)
      goto out;

   rgba_data[0] = rgba;
   rgba_data[1] = NULL;
   rgba_data[2] = NULL;
   rgba_data[3] = NULL;
   rgba_linesize[0] = frame->width * 4;
   rgba_linesize[1] = 0;
   rgba_linesize[2] = 0;
   rgba_linesize[3] = 0;

   if (sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize,
       0, frame->height, rgba_data, rgba_linesize) != frame->height)
      goto out;

   if (unifrog_image_store_rgba(image, (unsigned)frame->width,
       (unsigned)frame->height, rgba, (unsigned)rgba_linesize[0]) != 0)
      goto out;

   ret = 0;

out:
   free(rgba);
   sws_freeContext(sws);
   av_frame_free(&frame);
   av_packet_free(&packet);
   avcodec_free_context(&codec_ctx);
   if (fmt)
      avformat_close_input(&fmt);
   return ret;
}
#endif

int unifrog_image_load_file(const char *path, unifrog_image *image)
{
   uint32_t start;
   const char *backend = "unsupported";
   int ret;
   int is_png;
   int is_svg;

   if (!path || !image)
      return -1;
   memset(image, 0, sizeof(*image));
   start = unifrog_perf_time_ms();
   is_png = has_ext(path, ".png");
   is_svg = has_ext(path, ".svg");

   if (is_svg) {
      ret = unifrog_image_load_svg(path, image);
      if (ret == 0) {
         backend = "nanosvg_rgb565";
         goto log_and_return;
      }
   }

   /*
    * PNG stays on the existing MMZ-backed decoder first because it allocates
    * directly in the same layout as the renderer. If it rejects a large or
    * uncommon PNG, continue through the generic backends instead of failing.
    */
   if (is_png) {
      ret = unifrog_png_load_file(path, image);
      if (ret == 0) {
         backend = "png_cpu_mmz";
         goto log_and_return;
      }
   }

   ret = unifrog_image_load_stb(path, image);
   if (ret == 0) {
      backend = "stb_rgb565";
      goto log_and_return;
   }

#if UNIFROG_IMAGE_FFMPEG_ENABLED
   ret = unifrog_image_load_ffmpeg(path, image);
   if (ret == 0) {
      backend = "ffmpeg_rgb565";
      goto log_and_return;
   }
#else
   ret = -1;
#endif

log_and_return:
   unifrog_log("unifrog image load backend=%s ret=%d ms=%u path=%s\n",
      backend, ret, (unsigned)(unifrog_perf_time_ms() - start), path);
   return ret;
}

void unifrog_image_free(unifrog_image *image)
{
   unifrog_png_free(image);
}
