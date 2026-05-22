#include <unifrog/media.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <kernel/fb.h>
#include <kernel/delay.h>
#include <kernel/module.h>
#include <hcuapi/avsync.h>
#include <hcuapi/dis.h>
#include <hcuapi/iocbase.h>
#include <hcuapi/codec_id.h>
#include <hcuapi/viddec.h>
#include <vendor/ffplayer.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>

#include <unifrog/abi.h>
#include <unifrog/audio.h>
#include <unifrog/diag.h>
#include <unifrog/hcrtos_media_compat.h>
#include <unifrog/input.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>
#include <unifrog/runtime.h>
#include <unifrog/text.h>

#define printf unifrog_log

/*
 * The SF2000 LCD is 320x240, but HCRTOS routes decoded video through the HD
 * video plane. The confirmed full-screen mode is a 1920x1080 display rect;
 * using 320x240 leaves decoded video in a small top-left rectangle.
 */
#define VIDEO_SOURCE_W 1920
#define VIDEO_SOURCE_H 1080
#define VIDEO_OUTPUT_W 1920
#define VIDEO_OUTPUT_H 1080
#define MEDIA_MAX_VIDEO_W 1920
#define MEDIA_MAX_VIDEO_H 1080
#define VIDEO_EXIT_HOLD_POLLS 4u
#define VIDEO_MONITOR_POLLS 30u
#define VIDEO_STALL_LIMIT 8u
#define VIDEO_LOG_AUTO_FLUSH_BYTES (64u * 1024u)
#define MEDIA_AUDIO_VOLUME 75u
#define MEDIA_WAV_CHUNK_FRAMES 512u
#define MEDIA_FFMPEG_CHUNK_FRAMES 512u
#define MEDIA_AUDIO_KSHM_SIZE 0x000a0000u
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MEDIA_SEEK_STEP_MS 10000

#ifndef UNIFROG_ENABLE_HCPLAYER
#define UNIFROG_ENABLE_HCPLAYER 0
#endif

struct playback_preset {
   const char *name;
   HCPlayerSyncType sync_type;
   bool quick_mode;
   int qm_drop_thresh;
   int audio_flush_thres;
   bool buffering_enable;
};

static const struct playback_preset playback_presets[] = {
   { "audio loose", HCPLAYER_AUDIO_MASTER, false, 3, 0, false },
   { "stc sync", HCPLAYER_SYNC_STC, false, 1, 0, false },
   { "freerun", HCPLAYER_FREERUN, false, 1, 0, false },
   { "audio quick", HCPLAYER_AUDIO_MASTER, true, 1, 0, false },
   { "video master", HCPLAYER_VIDEO_MASTER, false, 1, 0, false },
   { "stc buffered", HCPLAYER_SYNC_STC, false, 1, 0, true },
   { "audio buffered", HCPLAYER_AUDIO_MASTER, false, 3, 0, true },
};

struct media_auddec {
   int fd;
   int stream;
   AVRational time_base;
   uint32_t packets;
   int freerun;
   int aac_adts;
   unsigned aac_profile;
   unsigned aac_sample_rate_index;
   unsigned aac_channels;
};

struct media_auddec_variant {
   const char *label;
   int force_rate;
   uint32_t snd_devs;
   int enable_audsink;
   int full_stream_fields;
   int audio_flush_thres;
};

struct media_raw_auddec_variant {
   const char *label;
   uint32_t snd_devs;
   int enable_audsink;
   int audio_flush_thres;
   int kshm_size;
};

static int media_has_suffix(const char *path, const char *const *suffixes,
   unsigned suffix_count)
{
   if (!path)
      return 0;
   for (unsigned i = 0; i < suffix_count; i++) {
      if (unifrog_text_ends_with_ci(path, suffixes[i]))
         return 1;
   }
   return 0;
}

static int media_is_audio_path(const char *path)
{
   static const char *const suffixes[] = {
      ".mp3", ".wav", ".flac", ".ogg", ".opus", ".aac", ".m4a",
      ".wma", ".ra", ".rm", ".rmvb",
   };

   return media_has_suffix(path, suffixes,
      sizeof(suffixes) / sizeof(suffixes[0]));
}

static int media_is_image_path(const char *path)
{
   static const char *const suffixes[] = {
      ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp",
   };

   return media_has_suffix(path, suffixes,
      sizeof(suffixes) / sizeof(suffixes[0]));
}

static int media_is_wav_path(const char *path)
{
   static const char *const suffixes[] = { ".wav" };

   return media_has_suffix(path, suffixes,
      sizeof(suffixes) / sizeof(suffixes[0]));
}

static int media_is_mp3_path(const char *path)
{
   static const char *const suffixes[] = { ".mp3" };

   return media_has_suffix(path, suffixes,
      sizeof(suffixes) / sizeof(suffixes[0]));
}

static int media_is_aac_path(const char *path)
{
   static const char *const suffixes[] = { ".aac", ".adts" };

   return media_has_suffix(path, suffixes,
      sizeof(suffixes) / sizeof(suffixes[0]));
}

static int media_is_flac_path(const char *path)
{
   static const char *const suffixes[] = { ".flac" };

   return media_has_suffix(path, suffixes,
      sizeof(suffixes) / sizeof(suffixes[0]));
}

static int media_is_ogg_path(const char *path)
{
   static const char *const suffixes[] = { ".ogg", ".opus" };

   return media_has_suffix(path, suffixes,
      sizeof(suffixes) / sizeof(suffixes[0]));
}

static uint16_t media_read_le16(const uint8_t *p)
{
   return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t media_read_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t media_read_be32(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int dis_fd = -1;
static int fb_fd = -1;
static unsigned media_video_debug_packets;
static int media_caps_logged;
static unsigned media_sd_read_depth;

extern unsigned long _padec_start;
extern unsigned long _padec_end;
extern unsigned long _pvdec_start;
extern unsigned long _pvdec_end;
extern unsigned long _deca_audio_stream_struct_start;
extern unsigned long _deca_audio_stream_struct_end;

static void media_init_drivers_once(void);
static int media_auddec_open(AVFormatContext *fmt, int stream_index,
   int sync_mode, struct media_auddec *auddec);
static const char *media_avcodec_name(enum AVCodecID codec_id);
static int media_auddec_send_packet(struct media_auddec *auddec,
   const AVPacket *packet);
static void media_auddec_finish(struct media_auddec *auddec,
   unsigned timeout_ms);
static void media_auddec_close(struct media_auddec *auddec);

static void media_sd_read_begin(const char *tag, const char *path)
{
   if (media_sd_read_depth++ == 0) {
      printf("unifrog media sd_read begin tag=%s path=%s pending=%lu\n",
         tag ? tag : "", path ? path : "",
         (unsigned long)unifrog_log_pending());
      unifrog_diag_memory_snapshot(tag ? tag : "media.sd_read_begin");
      (void)unifrog_log_flush();
      unifrog_log_set_disk_suspended(1);
      printf("unifrog media sd_read disk_suspended=1 tag=%s path=%s\n",
         tag ? tag : "", path ? path : "");
   } else {
      printf("unifrog media sd_read nested depth=%u tag=%s path=%s\n",
         media_sd_read_depth, tag ? tag : "", path ? path : "");
   }
}

static void media_sd_read_end(const char *tag, const char *path)
{
   if (media_sd_read_depth == 0)
      return;
   media_sd_read_depth--;
   if (media_sd_read_depth == 0) {
      printf("unifrog media sd_read end tag=%s path=%s pending=%lu\n",
         tag ? tag : "", path ? path : "",
         (unsigned long)unifrog_log_pending());
      unifrog_diag_memory_snapshot(tag ? tag : "media.sd_read_end");
      unifrog_log_set_disk_suspended(0);
   } else {
      printf("unifrog media sd_read nested_end depth=%u tag=%s path=%s\n",
         media_sd_read_depth, tag ? tag : "", path ? path : "");
   }
}

static void media_sd_read_recover_stale(const char *tag)
{
   if (media_sd_read_depth == 0)
      return;
   printf("unifrog media sd_read recover_stale tag=%s depth=%u pending=%lu\n",
      tag ? tag : "", media_sd_read_depth,
      (unsigned long)unifrog_log_pending());
   media_sd_read_depth = 0;
   unifrog_log_set_disk_suspended(0);
}

static void media_log_file_probe(const char *path, const char *tag)
{
   FILE *file;
   struct stat st;
   uint8_t head[32];
   size_t got = 0;

   if (!path)
      return;
   memset(&st, 0, sizeof(st));
   printf("unifrog media probe begin tag=%s path=%s\n",
      tag ? tag : "", path);
   (void)unifrog_log_flush();
   if (stat(path, &st) != 0) {
      printf("unifrog media probe tag=%s path=%s stat=-1 errno=%d\n",
         tag ? tag : "", path, errno);
      return;
   }
   printf("unifrog media probe stat tag=%s path=%s size=%ld\n",
      tag ? tag : "", path, (long)st.st_size);
   (void)unifrog_log_flush();
   errno = 0;
   file = fopen(path, "rb");
   printf("unifrog media probe open tag=%s path=%s ok=%d errno=%d\n",
      tag ? tag : "", path, file ? 1 : 0, errno);
   (void)unifrog_log_flush();
   if (file) {
      errno = 0;
      got = fread(head, 1, sizeof(head), file);
      printf("unifrog media probe read tag=%s path=%s got=%lu errno=%d ferror=%d\n",
         tag ? tag : "", path, (unsigned long)got, errno, ferror(file));
      fclose(file);
      (void)unifrog_log_flush();
   }
   printf("unifrog media probe tag=%s path=%s size=%ld head_len=%lu "
          "head=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
      tag ? tag : "", path, (long)st.st_size, (unsigned long)got,
      got > 0 ? head[0] : 0, got > 1 ? head[1] : 0,
      got > 2 ? head[2] : 0, got > 3 ? head[3] : 0,
      got > 4 ? head[4] : 0, got > 5 ? head[5] : 0,
      got > 6 ? head[6] : 0, got > 7 ? head[7] : 0,
      got > 8 ? head[8] : 0, got > 9 ? head[9] : 0,
      got > 10 ? head[10] : 0, got > 11 ? head[11] : 0,
      got > 12 ? head[12] : 0, got > 13 ? head[13] : 0,
      got > 14 ? head[14] : 0, got > 15 ? head[15] : 0);
}

static void media_log_format_streams(AVFormatContext *fmt, const char *path,
   const char *tag)
{
   if (!fmt)
      return;
   printf("unifrog media format tag=%s path=%s streams=%lu duration=%lld bitrate=%lld\n",
      tag ? tag : "", path ? path : "", (unsigned long)fmt->nb_streams,
      (long long)fmt->duration, (long long)fmt->bit_rate);
   (void)unifrog_log_flush();
   for (unsigned i = 0; i < fmt->nb_streams; i++) {
      AVStream *stream = fmt->streams[i];
      AVCodecParameters *par = stream ? stream->codecpar : NULL;

      if (!stream || !par) {
         printf("unifrog media stream tag=%s idx=%u missing stream=%d par=%d\n",
            tag ? tag : "", i, stream ? 1 : 0, par ? 1 : 0);
         (void)unifrog_log_flush();
         continue;
      }
      printf("unifrog media stream tag=%s idx=%u type=%d codec=%d/%s %dx%d rate=%d ch=%d bits=%d extra=%d tb=%d/%d\n",
         tag ? tag : "", i, par->codec_type, par->codec_id,
         media_avcodec_name(par->codec_id), par->width, par->height,
         par->sample_rate, par->channels, par->bits_per_coded_sample,
         par->extradata_size, stream->time_base.num, stream->time_base.den);
      (void)unifrog_log_flush();
   }
}

static int media_find_stream_type(AVFormatContext *fmt,
   enum AVMediaType codec_type)
{
   if (!fmt)
      return -1;
   for (unsigned i = 0; i < fmt->nb_streams; i++) {
      AVStream *stream = fmt->streams[i];
      AVCodecParameters *par = stream ? stream->codecpar : NULL;

      if (par && par->codec_type == codec_type)
         return (int)i;
   }
   return -1;
}

static void media_log_ffmpeg_caps_once(void)
{
   if (media_caps_logged)
      return;
   media_caps_logged = 1;
   printf("unifrog media ffmpeg caps source=upstream-4.4 math=softfloat/fixed demuxers=avi,h264,m4v,matroska,mov,mpegps,mpegts,mpegvideo,mp3,wav,flac,ogg,aac,ape codecs_linked=mp3,aac_fixed,pcm,flac,vorbis,opus,wma,h264,mpeg4,vp8\n");
}

static void open_display(void)
{
   if (dis_fd < 0) {
      dis_fd = open("/dev/dis", O_RDWR);
      printf("unifrog media dis open %s\n", dis_fd >= 0 ? "ok" : "failed");
   }
   if (fb_fd < 0) {
      fb_fd = open("/dev/fb0", O_RDWR);
      printf("unifrog media fb open %s\n", fb_fd >= 0 ? "ok" : "failed");
   }
}

static int set_video_layer_visible(int visible, int src_w, int src_h,
   int dst_w, int dst_h)
{
   struct dis_layer_blend_order order;
   struct dis_zoom zoom;
   int order_ret;
   int zoom_ret;

   open_display();
   if (dis_fd < 0)
      return -1;

   memset(&order, 0, sizeof(order));
   order.distype = DIS_TYPE_HD;
   if (visible) {
      order.main_layer = 3;
      order.auxp_layer = 2;
      order.gmas_layer = 1;
      order.gmaf_layer = 0;
   } else {
      order.main_layer = 0;
      order.auxp_layer = 1;
      order.gmas_layer = 2;
      order.gmaf_layer = 3;
   }
   errno = 0;
   order_ret = ioctl(dis_fd, DIS_SET_LAYER_ORDER, &order);
   int order_errno = errno;

   memset(&zoom, 0, sizeof(zoom));
   zoom.distype = DIS_TYPE_HD;
   zoom.layer = DIS_LAYER_MAIN;
   zoom.src_area.x = 0;
   zoom.src_area.y = 0;
   zoom.src_area.w = (uint16_t)(src_w > 0 ? src_w : VIDEO_SOURCE_W);
   zoom.src_area.h = (uint16_t)(src_h > 0 ? src_h : VIDEO_SOURCE_H);
   zoom.dst_area.x = 0;
   zoom.dst_area.y = 0;
   zoom.dst_area.w = (uint16_t)(dst_w > 0 ? dst_w : VIDEO_OUTPUT_W);
   zoom.dst_area.h = (uint16_t)(dst_h > 0 ? dst_h : VIDEO_OUTPUT_H);
   errno = 0;
   zoom_ret = ioctl(dis_fd, DIS_SET_ZOOM, &zoom);
   int zoom_errno = errno;

   printf("unifrog media layer visible=%d src=%ux%u dst=%ux%u order_ret=%d order_errno=%d zoom_ret=%d zoom_errno=%d\n",
      visible, zoom.src_area.w, zoom.src_area.h,
      zoom.dst_area.w, zoom.dst_area.h, order_ret, order_errno, zoom_ret,
      zoom_errno);
   return order_ret == 0 && zoom_ret == 0 ? 0 : -1;
}

static int set_player_display_rect(void *player, int src_w, int src_h,
   int dst_w, int dst_h)
{
   struct vdec_dis_rect rect;
   int ret;

   if (!player)
      return -1;
   memset(&rect, 0, sizeof(rect));
   rect.src_rect.x = 0;
   rect.src_rect.y = 0;
   rect.src_rect.w = (uint16_t)(src_w > 0 ? src_w : VIDEO_SOURCE_W);
   rect.src_rect.h = (uint16_t)(src_h > 0 ? src_h : VIDEO_SOURCE_H);
   rect.dst_rect.x = 0;
   rect.dst_rect.y = 0;
   rect.dst_rect.w = (uint16_t)(dst_w > 0 ? dst_w : VIDEO_OUTPUT_W);
   rect.dst_rect.h = (uint16_t)(dst_h > 0 ? dst_h : VIDEO_OUTPUT_H);
   ret = hcplayer_set_display_rect(player, &rect);
   printf("unifrog media player rect src=%ux%u dst=%ux%u ret=%d\n",
      rect.src_rect.w, rect.src_rect.h, rect.dst_rect.w, rect.dst_rect.h,
      ret);
   return ret;
}

static void media_set_aspect_mode(dis_tv_mode_e ratio, dis_mode_e mode)
{
   dis_aspect_mode_t aspect;
   int ret;

   open_display();
   if (dis_fd < 0)
      return;
   memset(&aspect, 0, sizeof(aspect));
   aspect.distype = DIS_TYPE_HD;
   aspect.tv_mode = ratio;
   aspect.dis_mode = mode;
   ret = ioctl(dis_fd, DIS_SET_ASPECT_MODE, &aspect);
   printf("unifrog media aspect ratio=%d mode=%d ret=%d\n",
      ratio, mode, ret);
}

static void close_display(void)
{
   if (fb_fd >= 0) {
      (void)ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
      close(fb_fd);
      fb_fd = -1;
   }
   if (dis_fd >= 0) {
      media_set_aspect_mode(DIS_TV_AUTO, DIS_PILLBOX);
      (void)set_video_layer_visible(0, 0, 0, 0, 0);
      close(dis_fd);
      dis_fd = -1;
   }
}

static int media_exit_down(void)
{
   uint32_t buttons;

   unifrog_input_save_previous();
   unifrog_input_poll_with_wireless_divisor(2);
   buttons = unifrog_input_menu_buttons();
   return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B)) ||
      ((buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT)) &&
       (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START)));
}

static int media_wav_read_pcm_sample(FILE *file, unsigned bits,
   int32_t *sample)
{
   uint8_t bytes[4];

   if (!file || !sample)
      return -1;
   if (bits == 8u) {
      if (fread(bytes, 1, 1, file) != 1)
         return -1;
      *sample = ((int)bytes[0] - 128) << 8;
      return 0;
   }
   if (bits == 16u) {
      if (fread(bytes, 1, 2, file) != 2)
         return -1;
      *sample = (int16_t)media_read_le16(bytes);
      return 0;
   }
   if (bits == 24u) {
      int32_t value;

      if (fread(bytes, 1, 3, file) != 3)
         return -1;
      value = (int32_t)((uint32_t)bytes[0] |
         ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16));
      if (value & 0x00800000)
         value |= (int32_t)0xff000000u;
      *sample = value >> 8;
      return 0;
   }
   if (bits == 32u) {
      if (fread(bytes, 1, 4, file) != 4)
         return -1;
      *sample = (int32_t)media_read_le32(bytes) >> 16;
      return 0;
   }
   return -1;
}

static int16_t media_wav_clip_sample(int32_t sample)
{
   if (sample > 32767)
      return 32767;
   if (sample < -32768)
      return -32768;
   return (int16_t)sample;
}

static const char *media_sample_format_name(enum AVSampleFormat fmt)
{
   const char *name = av_get_sample_fmt_name(fmt);

   return name ? name : "?";
}

static void media_ffmpeg_register_once(void)
{
   static int registered;

   if (registered)
      return;
   registered = 1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
   av_register_all();
   avcodec_register_all();
#pragma GCC diagnostic pop
   printf("unifrog media ffmpeg registered\n");
   media_log_ffmpeg_caps_once();
}

static int32_t media_ffmpeg_read_sample(const uint8_t *p,
   enum AVSampleFormat packed_fmt)
{
   if (!p)
      return 0;
   switch (packed_fmt) {
   case AV_SAMPLE_FMT_U8:
      return ((int32_t)p[0] - 128) << 8;
   case AV_SAMPLE_FMT_S16:
      return (int16_t)media_read_le16(p);
   case AV_SAMPLE_FMT_S32:
      return ((int32_t)media_read_le32(p)) >> 16;
   case AV_SAMPLE_FMT_FLT: {
      float value;

      memcpy(&value, p, sizeof(value));
      if (value > 1.0f)
         value = 1.0f;
      else if (value < -1.0f)
         value = -1.0f;
      return (int32_t)(value * 32767.0f);
   }
   default:
      return 0;
   }
}

static int media_ffmpeg_frame_to_mono_s16(const AVFrame *frame,
   int16_t *out, unsigned out_capacity)
{
   enum AVSampleFormat src_fmt;
   enum AVSampleFormat packed_fmt;
   int planar;
   int bytes;
   int channels;
   unsigned frames;

   if (!frame || !out || out_capacity == 0)
      return -1;
   src_fmt = (enum AVSampleFormat)frame->format;
   packed_fmt = av_get_packed_sample_fmt(src_fmt);
   planar = av_sample_fmt_is_planar(src_fmt);
   bytes = av_get_bytes_per_sample(packed_fmt);
   channels = frame->channels;
   if (channels <= 0 || frame->nb_samples <= 0 || bytes <= 0)
      return -1;
   if (packed_fmt != AV_SAMPLE_FMT_U8 &&
       packed_fmt != AV_SAMPLE_FMT_S16 &&
       packed_fmt != AV_SAMPLE_FMT_S32 &&
       packed_fmt != AV_SAMPLE_FMT_FLT)
      return -1;

   frames = (unsigned)frame->nb_samples;
   if (frames > out_capacity)
      frames = out_capacity;
   for (unsigned i = 0; i < frames; i++) {
      int64_t mix = 0;
      unsigned mix_channels = (unsigned)channels;

      if (mix_channels > 2u)
         mix_channels = 2u;
      for (unsigned ch = 0; ch < mix_channels; ch++) {
         const uint8_t *sample;

         if (planar)
            sample = frame->extended_data[ch] + i * (unsigned)bytes;
         else
            sample = frame->extended_data[0] +
               (i * (unsigned)channels + ch) * (unsigned)bytes;
         mix += media_ffmpeg_read_sample(sample, packed_fmt);
      }
      if (mix_channels > 1u)
         mix /= (int)mix_channels;
      out[i] = media_wav_clip_sample((int32_t)mix);
   }
   return (int)frames;
}

static int media_ffmpeg_open_audio(const char *path, AVFormatContext **fmt_out,
   AVCodecContext **codec_out, int *stream_out, AVCodec **decoder_out)
{
   AVFormatContext *fmt = NULL;
   AVCodecContext *codec_ctx = NULL;
   AVCodec *decoder = NULL;
   int stream;
   int ret;
   int sd_read_active = 0;

   media_ffmpeg_register_once();
   media_sd_read_begin("ffmpeg_audio_open", path);
   sd_read_active = 1;
   printf("unifrog media ffmpeg open_input begin path=%s\n",
      path ? path : "");
   ret = avformat_open_input(&fmt, path, NULL, NULL);
   printf("unifrog media ffmpeg open_input done ret=%d fmt=0x%08lx path=%s\n",
      ret, (unsigned long)(uintptr_t)fmt, path ? path : "");
   if (ret < 0) {
      printf("unifrog media ffmpeg open_input failed ret=%d path=%s\n",
         ret, path);
      media_log_file_probe(path, "ffmpeg_open_failed");
      goto fail;
   }
   printf("unifrog media ffmpeg stream_info begin path=%s\n",
      path ? path : "");
   ret = avformat_find_stream_info(fmt, NULL);
   printf("unifrog media ffmpeg stream_info done ret=%d streams=%u path=%s\n",
      ret, fmt ? fmt->nb_streams : 0, path ? path : "");
   if (ret < 0) {
      printf("unifrog media ffmpeg stream_info failed ret=%d path=%s\n",
         ret, path);
      media_log_file_probe(path, "ffmpeg_info_failed");
      media_log_format_streams(fmt, path, "ffmpeg_partial");
      goto fail;
   }
   media_log_format_streams(fmt, path, "ffmpeg_open");
   stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
   if (stream < 0) {
      printf("unifrog media ffmpeg audio_stream missing ret=%d streams=%u path=%s\n",
         stream, fmt->nb_streams, path);
      goto fail;
   }
   decoder = avcodec_find_decoder(fmt->streams[stream]->codecpar->codec_id);
   printf("unifrog media ffmpeg audio stream=%d codec=%d codec_name=%s decoder=%s tag=0x%lx rate=%d ch=%d extra=%d path=%s\n",
      stream, fmt->streams[stream]->codecpar->codec_id,
      media_avcodec_name(fmt->streams[stream]->codecpar->codec_id),
      decoder && decoder->name ? decoder->name : "missing",
      (unsigned long)fmt->streams[stream]->codecpar->codec_tag,
      fmt->streams[stream]->codecpar->sample_rate,
      fmt->streams[stream]->codecpar->channels,
      fmt->streams[stream]->codecpar->extradata_size, path);
   if (!decoder) {
      printf("unifrog media ffmpeg decoder missing codec=%d codec_name=%s path=%s\n",
         fmt->streams[stream]->codecpar->codec_id,
         media_avcodec_name(fmt->streams[stream]->codecpar->codec_id), path);
      goto fail;
   }
   codec_ctx = avcodec_alloc_context3(decoder);
   if (!codec_ctx) {
      printf("unifrog media ffmpeg codec_alloc failed path=%s\n", path);
      goto fail;
   }
   ret = avcodec_parameters_to_context(codec_ctx,
      fmt->streams[stream]->codecpar);
   if (ret < 0) {
      printf("unifrog media ffmpeg parameters failed ret=%d path=%s\n",
         ret, path);
      goto fail;
   }
   codec_ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;
   codec_ctx->request_channel_layout = AV_CH_LAYOUT_MONO;
   ret = avcodec_open2(codec_ctx, decoder, NULL);
   if (ret < 0) {
      printf("unifrog media ffmpeg codec_open failed ret=%d codec=%s path=%s\n",
         ret, decoder->name ? decoder->name : "?", path);
      goto fail;
   }

   *fmt_out = fmt;
   *codec_out = codec_ctx;
   *stream_out = stream;
   if (decoder_out)
      *decoder_out = decoder;
   sd_read_active = 0;
   return 0;

fail:
   if (codec_ctx)
      avcodec_free_context(&codec_ctx);
   if (fmt)
      avformat_close_input(&fmt);
   if (sd_read_active)
      media_sd_read_end("ffmpeg_audio_open_fail", path);
   return -1;
}

static int media_ffmpeg_write_frame(struct unifrog_audio *audio,
   const AVFrame *frame, int16_t *pcm, unsigned pcm_capacity,
   uint32_t *played)
{
   unsigned offset = 0;

   while (offset < (unsigned)frame->nb_samples) {
      unsigned chunk = (unsigned)frame->nb_samples - offset;
      int got;
      AVFrame chunk_frame = *frame;

      if (chunk > pcm_capacity)
         chunk = pcm_capacity;
      if (offset) {
         int channels = frame->channels;
         int bytes = av_get_bytes_per_sample(
            av_get_packed_sample_fmt((enum AVSampleFormat)frame->format));
         int planar = av_sample_fmt_is_planar(
            (enum AVSampleFormat)frame->format);

         if (channels <= 0)
            channels = frame->channels;
         if (channels <= 0 || bytes <= 0)
            return -1;
         if (planar) {
            for (int ch = 0; ch < channels; ch++)
               chunk_frame.extended_data[ch] =
                  frame->extended_data[ch] + offset * (unsigned)bytes;
         } else {
            chunk_frame.extended_data[0] = frame->extended_data[0] +
               offset * (unsigned)channels * (unsigned)bytes;
         }
      }
      chunk_frame.nb_samples = (int)chunk;
      got = media_ffmpeg_frame_to_mono_s16(&chunk_frame, pcm, pcm_capacity);
      if (got <= 0)
         return -1;
      (void)unifrog_audio_write(audio, pcm, (unsigned)got);
      if (played)
         *played += (uint32_t)got;
      offset += (unsigned)got;
      if (media_exit_down())
         return 1;
   }
   return 0;
}

static int media_play_ffmpeg_audio(const char *path)
{
   struct unifrog_audio audio;
   AVFormatContext *fmt = NULL;
   AVCodecContext *codec_ctx = NULL;
   AVCodec *decoder = NULL;
   AVPacket *packet = NULL;
   AVFrame *frame = NULL;
   int16_t *pcm = NULL;
   int stream = -1;
   uint32_t played = 0;
   int saw_frame = 0;
   int ret = -1;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   if (media_ffmpeg_open_audio(path, &fmt, &codec_ctx, &stream,
       &decoder) != 0)
      goto out;
   if (codec_ctx->sample_rate < 8000 || codec_ctx->sample_rate > 48000) {
      printf("unifrog media ffmpeg unsupported_rate rate=%d path=%s\n",
         codec_ctx->sample_rate, path);
      goto out;
   }

   packet = av_packet_alloc();
   frame = av_frame_alloc();
   pcm = malloc(sizeof(*pcm) * MEDIA_FFMPEG_CHUNK_FRAMES);
   if (!packet || !frame || !pcm) {
      printf("unifrog media ffmpeg alloc failed packet=%p frame=%p pcm=%p path=%s\n",
         (void *)packet, (void *)frame, (void *)pcm, path);
      goto out;
   }
   if (unifrog_audio_open(&audio, (unsigned)codec_ctx->sample_rate, 1,
       512, 8) != 0) {
      printf("unifrog media ffmpeg audio_open failed rate=%d path=%s\n",
         codec_ctx->sample_rate, path);
      goto out;
   }
   (void)unifrog_audio_set_volume(&audio, MEDIA_AUDIO_VOLUME);
   (void)unifrog_audio_set_mute(&audio, 1);
   (void)unifrog_audio_start(&audio);
   (void)unifrog_audio_set_output_enabled(&audio, 1);
   unifrog_audio_debug_dump(&audio, "ffmpeg_after_start");
   printf("unifrog media ffmpeg audio start codec=%s stream=%d rate=%d ch=%d fmt=%s path=%s\n",
      decoder && decoder->name ? decoder->name : "?",
      stream, codec_ctx->sample_rate, codec_ctx->channels,
      media_sample_format_name(codec_ctx->sample_fmt), path);

   while (!media_exit_down()) {
      int read_ret = av_read_frame(fmt, packet);

      if (read_ret < 0)
         break;
      if (packet->stream_index == stream) {
         int send_ret = avcodec_send_packet(codec_ctx, packet);

         if (send_ret < 0 && send_ret != AVERROR(EAGAIN)) {
            printf("unifrog media ffmpeg send failed ret=%d path=%s\n",
               send_ret, path);
            av_packet_unref(packet);
            break;
         }
         for (;;) {
            int recv_ret = avcodec_receive_frame(codec_ctx, frame);
            int write_ret;

            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
               break;
            if (recv_ret < 0) {
               printf("unifrog media ffmpeg receive failed ret=%d path=%s\n",
                  recv_ret, path);
               break;
            }
            if (!saw_frame) {
               printf("unifrog media ffmpeg frame rate=%d ch=%d samples=%d fmt=%s path=%s\n",
                  frame->sample_rate, frame->channels,
                  frame->nb_samples,
                  media_sample_format_name((enum AVSampleFormat)frame->format),
                  path);
               saw_frame = 1;
            }
            write_ret = media_ffmpeg_write_frame(&audio, frame, pcm,
               MEDIA_FFMPEG_CHUNK_FRAMES, &played);
            if (write_ret < 0) {
               printf("unifrog media ffmpeg unsupported frame fmt=%s ch=%d samples=%d path=%s\n",
                  media_sample_format_name((enum AVSampleFormat)frame->format),
                  frame->channels, frame->nb_samples, path);
               av_packet_unref(packet);
               goto out;
            }
            if (write_ret > 0) {
               av_packet_unref(packet);
               ret = 0;
               goto out;
            }
         }
      }
      av_packet_unref(packet);
   }

   (void)avcodec_send_packet(codec_ctx, NULL);
   for (;;) {
      int recv_ret = avcodec_receive_frame(codec_ctx, frame);
      int write_ret;

      if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
         break;
      if (recv_ret < 0)
         break;
      write_ret = media_ffmpeg_write_frame(&audio, frame, pcm,
         MEDIA_FFMPEG_CHUNK_FRAMES, &played);
      if (write_ret != 0)
         break;
   }
   ret = played ? 0 : -1;

out:
   printf("unifrog media ffmpeg audio end ret=%d frames=%lu path=%s\n",
      ret, (unsigned long)played, path ? path : "");
   if (audio.fd >= 0)
      unifrog_audio_close(&audio);
   free(pcm);
   if (frame)
      av_frame_free(&frame);
   if (packet)
      av_packet_free(&packet);
   if (codec_ctx)
      avcodec_free_context(&codec_ctx);
   if (fmt)
      avformat_close_input(&fmt);
   media_sd_read_end("ffmpeg_audio_close", path);
   return ret;
}

static int media_play_native_audio_compressed(const char *path)
{
   AVFormatContext *fmt = NULL;
   AVPacket *packet = NULL;
   struct media_auddec auddec;
   int stream = -1;
   int ret = -1;
   unsigned finish_timeout_ms = 600000u;
   uint32_t loop_polls = 0;
   uint32_t start_ms = unifrog_perf_time_ms();
   int sd_read_active = 0;

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   media_ffmpeg_register_once();
   media_sd_read_begin("auddec_open", path);
   sd_read_active = 1;
   printf("unifrog media auddec open_input begin path=%s\n",
      path ? path : "");
   int open_ret = avformat_open_input(&fmt, path, NULL, NULL);
   printf("unifrog media auddec open_input done ret=%d fmt=0x%08lx path=%s\n",
      open_ret, (unsigned long)(uintptr_t)fmt, path ? path : "");
   printf("unifrog media auddec stream_info begin path=%s\n",
      path ? path : "");
   int info_ret = open_ret == 0 ? avformat_find_stream_info(fmt, NULL) : 0;
   printf("unifrog media auddec stream_info done ret=%d streams=%u path=%s\n",
      info_ret, fmt ? fmt->nb_streams : 0, path ? path : "");

   if (open_ret < 0 || info_ret < 0) {
      printf("unifrog media auddec open_input failed open=%d info=%d path=%s\n",
         open_ret, info_ret, path);
      media_log_file_probe(path, "auddec_open_failed");
      media_log_format_streams(fmt, path, "auddec_partial");
      goto out;
   }
   media_log_format_streams(fmt, path, "auddec_open");
   stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
   if (stream < 0) {
      printf("unifrog media auddec audio_stream missing ret=%d path=%s\n",
         stream, path);
      goto out;
   }
   if (fmt->duration > 0) {
      int64_t duration_ms = fmt->duration / (AV_TIME_BASE / 1000);

      if (duration_ms > 0 && duration_ms < 3600000)
         finish_timeout_ms = (unsigned)duration_ms + 5000u;
   }
   media_init_drivers_once();
   if (media_auddec_open(fmt, stream, AVSYNC_TYPE_UPDATESTC, &auddec) != 0 &&
       media_auddec_open(fmt, stream, AVSYNC_TYPE_FREERUN, &auddec) != 0)
      goto out;
   packet = av_packet_alloc();
   if (!packet)
      goto out;
   printf("unifrog media auddec play stream=%d path=%s\n", stream, path);
   while (!media_exit_down()) {
      int read_ret = av_read_frame(fmt, packet);

      if (read_ret < 0)
         break;
      if (packet->stream_index == stream &&
          media_auddec_send_packet(&auddec, packet) != 0) {
         printf("unifrog media auddec write failed packets=%lu path=%s\n",
            (unsigned long)auddec.packets, path);
         av_packet_unref(packet);
         break;
      }
      av_packet_unref(packet);
      if ((++loop_polls % 240u) == 0) {
         struct audio_decore_status status;

         memset(&status, 0, sizeof(status));
         if (ioctl(auddec.fd, AUDDEC_GET_STATUS, &status) == 0)
            printf("unifrog media auddec monitor packets=%lu decoded=%lu rate=%lu ch=%u bits=%u ms=%lu\n",
               (unsigned long)auddec.packets,
               (unsigned long)status.frames_decoded,
               (unsigned long)status.sample_rate,
               status.channels, status.bits_per_sample,
               (unsigned long)(unifrog_perf_time_ms() - start_ms));
      }
   }
   ret = auddec.packets ? 0 : -1;

out:
   printf("unifrog media auddec end ret=%d packets=%lu ms=%lu path=%s\n",
      ret, (unsigned long)auddec.packets,
      (unsigned long)(unifrog_perf_time_ms() - start_ms), path ? path : "");
   if (ret == 0)
      media_auddec_finish(&auddec, finish_timeout_ms);
   media_auddec_close(&auddec);
   if (packet)
      av_packet_free(&packet);
   if (fmt)
      avformat_close_input(&fmt);
   if (sd_read_active)
      media_sd_read_end("auddec_close", path);
   return ret;
}

static int media_hc_codec_from_av(enum AVCodecID codec_id)
{
   switch (codec_id) {
   case AV_CODEC_ID_MPEG1VIDEO:
      return HC_AVCODEC_ID_MPEG1VIDEO;
   case AV_CODEC_ID_MPEG2VIDEO:
      return HC_AVCODEC_ID_MPEG2VIDEO;
   case AV_CODEC_ID_H263:
      return HC_AVCODEC_ID_H263;
   case AV_CODEC_ID_H264:
      return HC_AVCODEC_ID_H264;
   case AV_CODEC_ID_MJPEG:
   case AV_CODEC_ID_MJPEGB:
      return HC_AVCODEC_ID_MJPEG;
   case AV_CODEC_ID_MPEG4:
      return HC_AVCODEC_ID_MPEG4;
   case AV_CODEC_ID_VC1:
      return HC_AVCODEC_ID_VC1;
   case AV_CODEC_ID_WMV3:
      return HC_AVCODEC_ID_WMV3;
   case AV_CODEC_ID_VP8:
      return HC_AVCODEC_ID_VP8;
   default:
      return 0;
   }
}

static int media_hc_audio_codec_from_av(enum AVCodecID codec_id)
{
   switch (codec_id) {
   case AV_CODEC_ID_PCM_S16LE:
      return HC_AVCODEC_ID_PCM_S16LE;
   case AV_CODEC_ID_PCM_S16BE:
      return HC_AVCODEC_ID_PCM_S16BE;
   case AV_CODEC_ID_PCM_U16LE:
      return HC_AVCODEC_ID_PCM_U16LE;
   case AV_CODEC_ID_PCM_U16BE:
      return HC_AVCODEC_ID_PCM_U16BE;
   case AV_CODEC_ID_PCM_S8:
      return HC_AVCODEC_ID_PCM_S8;
   case AV_CODEC_ID_PCM_U8:
      return HC_AVCODEC_ID_PCM_U8;
   case AV_CODEC_ID_PCM_MULAW:
      return HC_AVCODEC_ID_PCM_MULAW;
   case AV_CODEC_ID_PCM_ALAW:
      return HC_AVCODEC_ID_PCM_ALAW;
   case AV_CODEC_ID_PCM_S32LE:
      return HC_AVCODEC_ID_PCM_S32LE;
   case AV_CODEC_ID_PCM_S32BE:
      return HC_AVCODEC_ID_PCM_S32BE;
   case AV_CODEC_ID_PCM_S24LE:
      return HC_AVCODEC_ID_PCM_S24LE;
   case AV_CODEC_ID_PCM_S24BE:
      return HC_AVCODEC_ID_PCM_S24BE;
   case AV_CODEC_ID_ADPCM_IMA_WAV:
      return HC_AVCODEC_ID_ADPCM_IMA_WAV;
   case AV_CODEC_ID_ADPCM_MS:
      return HC_AVCODEC_ID_ADPCM_MS;
   case AV_CODEC_ID_ADPCM_IMA_QT:
      return HC_AVCODEC_ID_ADPCM_IMA_QT;
   case AV_CODEC_ID_ADPCM_IMA_DK3:
      return HC_AVCODEC_ID_ADPCM_IMA_DK3;
   case AV_CODEC_ID_ADPCM_IMA_DK4:
      return HC_AVCODEC_ID_ADPCM_IMA_DK4;
   case AV_CODEC_ID_ADPCM_IMA_WS:
      return HC_AVCODEC_ID_ADPCM_IMA_WS;
   case AV_CODEC_ID_ADPCM_IMA_SMJPEG:
      return HC_AVCODEC_ID_ADPCM_IMA_SMJPEG;
   case AV_CODEC_ID_MP1:
      return HC_AVCODEC_ID_MP1;
   case AV_CODEC_ID_MP2:
      return HC_AVCODEC_ID_MP2;
   case AV_CODEC_ID_MP3:
      return HC_AVCODEC_ID_MP3;
   case AV_CODEC_ID_AAC:
      return HC_AVCODEC_ID_AAC;
   case AV_CODEC_ID_AAC_LATM:
      return HC_AVCODEC_ID_AAC_LATM;
   case AV_CODEC_ID_VORBIS:
      return HC_AVCODEC_ID_VORBIS;
   case AV_CODEC_ID_FLAC:
      return HC_AVCODEC_ID_FLAC;
   case AV_CODEC_ID_WMAV1:
      return HC_AVCODEC_ID_WMAV1;
   case AV_CODEC_ID_WMAV2:
      return HC_AVCODEC_ID_WMAV2;
   case AV_CODEC_ID_WMAPRO:
      return HC_AVCODEC_ID_WMAPRO;
   case AV_CODEC_ID_OPUS:
      return HC_AVCODEC_ID_OPUS;
   case AV_CODEC_ID_RA_144:
      return HC_AVCODEC_ID_RA_144;
   case AV_CODEC_ID_RA_288:
      return HC_AVCODEC_ID_RA_288;
   default:
      return 0;
   }
}

static int32_t media_packet_pts_ms(const AVPacket *packet,
   AVRational time_base)
{
   int64_t pts;

   if (!packet)
      return -1;
   pts = packet->pts;
   if (pts == AV_NOPTS_VALUE)
      pts = packet->dts;
   if (pts == AV_NOPTS_VALUE)
      return -1;
   pts = av_rescale_q(pts, time_base, (AVRational){ 1, 1000 });
   if (pts > INT32_MAX)
      return INT32_MAX;
   if (pts < INT32_MIN)
      return INT32_MIN;
   return (int32_t)pts;
}

static int32_t media_packet_duration_ms(const AVPacket *packet,
   AVRational time_base)
{
   int64_t dur;

   if (!packet || packet->duration <= 0)
      return 0;
   dur = av_rescale_q(packet->duration, time_base, (AVRational){ 1, 1000 });
   if (dur > INT32_MAX)
      return INT32_MAX;
   return (int32_t)dur;
}

static int media_aac_sample_rate_index(unsigned sample_rate)
{
   static const unsigned rates[] = {
      96000, 88200, 64000, 48000, 44100, 32000,
      24000, 22050, 16000, 12000, 11025, 8000, 7350,
   };

   for (unsigned i = 0; i < ARRAY_SIZE(rates); i++) {
      if (rates[i] == sample_rate)
         return (int)i;
   }
   return -1;
}

static void media_aac_parse_asc(const AVCodecParameters *par,
   unsigned *profile, unsigned *sample_rate_index, unsigned *channels)
{
   unsigned object_type = 2;
   unsigned sr_index = 4;
   unsigned channel_config = 2;

   if (par) {
      int idx = media_aac_sample_rate_index((unsigned)par->sample_rate);

      if (idx >= 0)
         sr_index = (unsigned)idx;
      if (par->channels > 0 && par->channels < 8)
         channel_config = (unsigned)par->channels;
      if (par->extradata && par->extradata_size >= 2) {
         const uint8_t *d = par->extradata;

         object_type = (unsigned)(d[0] >> 3);
         sr_index = (unsigned)(((d[0] & 0x07) << 1) | (d[1] >> 7));
         channel_config = (unsigned)((d[1] >> 3) & 0x0f);
         if (object_type == 0 || object_type > 4)
            object_type = 2;
         if (sr_index > 12)
            sr_index = 4;
         if (channel_config == 0 || channel_config > 7)
            channel_config = par->channels > 0 && par->channels < 8 ?
               (unsigned)par->channels : 2u;
      }
   }

   *profile = object_type > 0 ? object_type - 1u : 1u;
   *sample_rate_index = sr_index;
   *channels = channel_config;
}

static void media_aac_make_adts(uint8_t header[7], unsigned frame_size,
   unsigned profile, unsigned sample_rate_index, unsigned channels)
{
   unsigned full_size = frame_size + 7u;

   if (profile > 3)
      profile = 1;
   if (sample_rate_index > 12)
      sample_rate_index = 4;
   if (channels == 0 || channels > 7)
      channels = 2;

   header[0] = 0xff;
   header[1] = 0xf1;
   header[2] = (uint8_t)(((profile & 0x03) << 6) |
      ((sample_rate_index & 0x0f) << 2) | ((channels >> 2) & 0x01));
   header[3] = (uint8_t)(((channels & 0x03) << 6) |
      ((full_size >> 11) & 0x03));
   header[4] = (uint8_t)((full_size >> 3) & 0xff);
   header[5] = (uint8_t)(((full_size & 0x07) << 5) | 0x1f);
   header[6] = 0xfc;
}

static int media_write_all(int fd, const void *data, size_t size)
{
   const uint8_t *p = (const uint8_t *)data;
   size_t written = 0;

   while (written < size) {
      ssize_t ret = write(fd, p + written, size - written);

      if (ret < 0) {
         if (errno == EINTR)
            continue;
         return -1;
      }
      if (ret == 0)
         return -1;
      written += (size_t)ret;
   }
   return 0;
}

static const char *media_avcodec_name(enum AVCodecID codec_id)
{
   const AVCodecDescriptor *desc = avcodec_descriptor_get(codec_id);

   return desc && desc->name ? desc->name : "?";
}

static int media_h264_first_nal_type(const uint8_t *data, size_t size)
{
   size_t i = 0;

   if (!data || size < 5)
      return -1;
   while (i + 4 < size && data[i] == 0)
      i++;
   if (i < 2 || i >= size || data[i] != 1)
      return -1;
   if (i + 1 >= size)
      return -1;
   return data[i + 1] & 0x1f;
}

static int media_video_send_packet(int fd, const AVPacket *packet,
   AVRational time_base, int freerun, int h264)
{
   static const uint8_t h264_aud[] = { 0x00, 0x00, 0x00, 0x01, 0x09, 0xf0 };
   AvPktHd header;
   unsigned packet_index;
   int add_aud;

   if (fd < 0 || !packet || !packet->data || packet->size <= 0)
      return -1;
   packet_index = media_video_debug_packets++;
   add_aud = h264 && media_h264_first_nal_type(packet->data,
      (size_t)packet->size) != 9;
   memset(&header, 0, sizeof(header));
   header.pts = freerun ? -1 : media_packet_pts_ms(packet, time_base);
   header.dur = freerun ? 0 : media_packet_duration_ms(packet, time_base);
   header.size = (uint32_t)packet->size + (add_aud ? sizeof(h264_aud) : 0u);
   header.flag = AV_PACKET_ES_DATA;
   if (packet_index < 8u) {
      const uint8_t *d = packet->data;

      printf("unifrog media native video packet idx=%u size=%d send=%lu pts=%ld dur=%ld aud=%d nal=%d bytes=%02x %02x %02x %02x %02x %02x %02x %02x hdr=%u\n",
         packet_index, packet->size, (unsigned long)header.size,
         (long)header.pts, (long)header.dur, add_aud,
         media_h264_first_nal_type(packet->data, (size_t)packet->size),
         packet->size > 0 ? d[0] : 0, packet->size > 1 ? d[1] : 0,
         packet->size > 2 ? d[2] : 0, packet->size > 3 ? d[3] : 0,
         packet->size > 4 ? d[4] : 0, packet->size > 5 ? d[5] : 0,
         packet->size > 6 ? d[6] : 0, packet->size > 7 ? d[7] : 0,
         (unsigned)sizeof(header));
   }
   if (media_write_all(fd, &header, sizeof(header)) != 0 ||
       (add_aud && media_write_all(fd, h264_aud, sizeof(h264_aud)) != 0) ||
       media_write_all(fd, packet->data, (size_t)packet->size) != 0) {
      printf("unifrog media native video packet_write_failed idx=%u errno=%d size=%d\n",
         packet_index, errno, packet->size);
      return -1;
   }
   if (packet_index < 8u) {
      struct vdec_decore_status status;

      memset(&status, 0, sizeof(status));
      if (ioctl(fd, VIDDEC_GET_STATUS, &status) == 0)
         printf("unifrog media native video status_after_packet idx=%u decoded=%lu displayed=%lu underrun=%lu used=%lu/%lu\n",
            packet_index,
            (unsigned long)status.frames_decoded,
            (unsigned long)status.frames_displayed,
            (unsigned long)status.under_run_cnt,
            (unsigned long)status.buffer_used,
            (unsigned long)status.buffer_size);
   }
   return 0;
}

static void media_video_send_eos(int fd)
{
   AvPktHd header;

   if (fd < 0)
      return;
   memset(&header, 0, sizeof(header));
   header.pts = -1;
   header.flag = AV_PACKET_EOS;
   (void)media_write_all(fd, &header, sizeof(header));
}

static int media_send_extra_packet(int fd, const uint8_t *data, int size)
{
   AvPktHd header;

   if (fd < 0 || !data || size <= 0)
      return 0;
   memset(&header, 0, sizeof(header));
   header.pts = 0;
   header.size = (uint32_t)size;
   header.flag = AV_PACKET_EXTRA_DATA;
   if (media_write_all(fd, &header, sizeof(header)) != 0 ||
       media_write_all(fd, data, (size_t)size) != 0)
      return -1;
   return 0;
}

static int media_write_extra_before_init(int fd, const char *tag,
   const uint8_t *data, int size)
{
   int ret;

   if (fd < 0 || !data || size <= 0)
      return 0;
   printf("unifrog media extra_write begin tag=%s fd=%d size=%d\n",
      tag ? tag : "", fd, size);
   ret = media_send_extra_packet(fd, data, size);
   printf("unifrog media extra_write done tag=%s fd=%d size=%d ret=%d errno=%d\n",
      tag ? tag : "", fd, size, ret, errno);
   return ret;
}

static int media_auddec_send_packet(struct media_auddec *auddec,
   const AVPacket *packet)
{
   AvPktHd header;
   uint8_t adts[7];
   uint32_t payload_size;

   if (!auddec || auddec->fd < 0 || !packet || !packet->data ||
       packet->size <= 0)
      return -1;
   payload_size = (uint32_t)packet->size;
   if (auddec->aac_adts)
      payload_size += (uint32_t)sizeof(adts);
   memset(&header, 0, sizeof(header));
   header.pts = auddec->freerun ? -1 :
      media_packet_pts_ms(packet, auddec->time_base);
   header.dur = auddec->freerun ? 0 :
      media_packet_duration_ms(packet, auddec->time_base);
   header.size = payload_size;
   header.flag = AV_PACKET_ES_DATA;
   if (media_write_all(auddec->fd, &header, sizeof(header)) != 0)
      return -1;
   if (auddec->aac_adts) {
      media_aac_make_adts(adts, (unsigned)packet->size,
         auddec->aac_profile, auddec->aac_sample_rate_index,
         auddec->aac_channels);
      if (media_write_all(auddec->fd, adts, sizeof(adts)) != 0)
         return -1;
   }
   if (media_write_all(auddec->fd, packet->data, (size_t)packet->size) != 0)
      return -1;
   auddec->packets++;
   return 0;
}

static void media_auddec_send_eos(struct media_auddec *auddec)
{
   AvPktHd header;

   if (!auddec || auddec->fd < 0)
      return;
   memset(&header, 0, sizeof(header));
   header.pts = -1;
   header.flag = AV_PACKET_EOS;
   (void)media_write_all(auddec->fd, &header, sizeof(header));
}

static void media_auddec_close(struct media_auddec *auddec)
{
   if (!auddec || auddec->fd < 0)
      return;
   close(auddec->fd);
   auddec->fd = -1;
   unifrog_audio_set_system_output_enabled(0);
}

static void media_auddec_finish(struct media_auddec *auddec,
   unsigned timeout_ms)
{
   uint32_t start_ms;
   int eos = 0;

   if (!auddec || auddec->fd < 0)
      return;
   media_auddec_send_eos(auddec);
   start_ms = unifrog_perf_time_ms();
   while (!eos && !media_exit_down()) {
      if (ioctl(auddec->fd, AUDDEC_CHECK_EOS, &eos) != 0)
         break;
      if (eos)
         break;
      if (timeout_ms &&
          unifrog_perf_time_ms() - start_ms >= timeout_ms)
         break;
      usleep(20 * 1000);
   }
   printf("unifrog media auddec finish eos=%d packets=%lu wait_ms=%lu timeout=%u\n",
      eos, (unsigned long)auddec->packets,
      (unsigned long)(unifrog_perf_time_ms() - start_ms), timeout_ms);
}

static int media_auddec_send_raw(struct media_auddec *auddec,
   const uint8_t *data, size_t size, int32_t pts, int32_t dur)
{
   AvPktHd header;
   uint32_t packet_index;

   if (!auddec || auddec->fd < 0 || !data || size == 0 ||
       size > 0x3fffffffu)
      return -1;
   packet_index = auddec->packets;
   memset(&header, 0, sizeof(header));
   header.pts = auddec->freerun ? -1 : pts;
   header.dur = auddec->freerun ? 0 : dur;
   header.size = (uint32_t)size;
   header.flag = AV_PACKET_ES_DATA;
   if (media_write_all(auddec->fd, &header, sizeof(header)) != 0 ||
       media_write_all(auddec->fd, data, size) != 0)
      return -1;
   auddec->packets++;
   if (packet_index < 4u) {
      struct audio_decore_status status;

      memset(&status, 0, sizeof(status));
      if (ioctl(auddec->fd, AUDDEC_GET_STATUS, &status) == 0)
         printf("unifrog media raw auddec packet idx=%lu size=%lu pts=%ld dur=%ld bytes=%02x %02x %02x %02x decoded=%lu rate=%lu ch=%u bits=%u first=%u/%u\n",
            (unsigned long)packet_index, (unsigned long)size,
            (long)header.pts, (long)header.dur,
            size > 0 ? data[0] : 0, size > 1 ? data[1] : 0,
            size > 2 ? data[2] : 0, size > 3 ? data[3] : 0,
            (unsigned long)status.frames_decoded,
            (unsigned long)status.sample_rate, status.channels,
            status.bits_per_sample, status.first_header_got,
            status.first_header_parsed);
   }
   return 0;
}

static int media_auddec_open_raw(const char *label, uint32_t codec_id,
   unsigned sample_rate, unsigned channels, unsigned bits,
   const uint8_t *extradata, unsigned extradata_size, int sync_mode,
   struct media_auddec *auddec)
{
   struct audio_config cfg;
   struct audio_config base_cfg;
   static const struct media_raw_auddec_variant variants[] = {
      { "raw_minimal", AUDDEV_DEFAULT, 0, 0, 0 },
      { "raw_minimal_i2so", AUDDEV_I2SO, 0, 0, 0 },
      { "raw_sink_i2so", AUDDEV_I2SO, 1, 200, 0x200000 },
      { "raw_sink_default", AUDDEV_DEFAULT, 1, 200, 0x200000 },
   };
   int fd;
   audio_channel_select_t channel = AUDIO_MONO_LEFT;
   uint8_t volume = MEDIA_AUDIO_VOLUME;

   if (!auddec || !codec_id)
      return -1;
   media_init_drivers_once();
   memset(auddec, 0, sizeof(*auddec));
   auddec->fd = -1;
   auddec->stream = 0;
   auddec->time_base = (AVRational){ 1, 1000 };
   auddec->freerun = sync_mode == 0;
   memset(&base_cfg, 0, sizeof(base_cfg));
   base_cfg.codec_id = codec_id;
   base_cfg.sync_mode = (uint8_t)sync_mode;
   base_cfg.bits_per_coded_sample = bits ? (uint8_t)bits : 16u;
   base_cfg.channels = channels ? (uint8_t)channels : 2u;
   base_cfg.sample_rate = sample_rate ? sample_rate : 44100u;
   if (extradata && extradata_size > 0) {
      base_cfg.extradata_size = extradata_size;
      if (extradata_size <= sizeof(base_cfg.extra_data)) {
         base_cfg.extradata_mode = 0;
         memcpy(base_cfg.extra_data, extradata, extradata_size);
      } else {
         base_cfg.extradata_mode = 1;
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(variants); i++) {
      const struct media_raw_auddec_variant *variant = &variants[i];
      int extra_ret = 0;
      int init_ret;
      int init_errno;
      int start_ret;
      int start_errno;

      cfg = base_cfg;
      cfg.snd_devs = variant->snd_devs;
      cfg.enable_audsink = variant->enable_audsink;
      cfg.audio_flush_thres = variant->audio_flush_thres;
      cfg.kshm_size = variant->kshm_size;
      fd = open("/dev/auddec", O_RDWR);
      if (fd < 0) {
         printf("unifrog media raw auddec open failed label=%s try=%s errno=%d codec=%lu\n",
            label ? label : "?", variant->label, errno,
            (unsigned long)codec_id);
         continue;
      }
      if (cfg.extradata_mode == 1)
         extra_ret = media_send_extra_packet(fd, extradata,
            (int)extradata_size);
      errno = 0;
      init_ret = extra_ret == 0 ? ioctl(fd, AUDDEC_INIT, &cfg) : -1;
      init_errno = errno;
      errno = 0;
      start_ret = init_ret == 0 ? ioctl(fd, AUDDEC_START, 0) : -1;
      start_errno = errno;
      printf("unifrog media raw auddec init label=%s try=%s fd=%d extra=%d init=%d init_errno=%d start=%d start_errno=%d codec=%lu rate=%u ch=%u bits=%u x=%u mode=%u sync=%d snd=0x%lx audsink=%d kshm=%d\n",
         label ? label : "?", variant->label, fd, extra_ret, init_ret,
         init_errno, start_ret, start_errno, (unsigned long)codec_id,
         cfg.sample_rate, cfg.channels, cfg.bits_per_coded_sample,
         cfg.extradata_size, cfg.extradata_mode, sync_mode,
         (unsigned long)cfg.snd_devs, cfg.enable_audsink, cfg.kshm_size);
      if (extra_ret == 0 && init_ret == 0 && start_ret == 0) {
         (void)ioctl(fd, AUDIO_CHANNEL_SELECT, &channel);
         (void)ioctl(fd, AUDIO_SET_VOLUME, &volume);
         unifrog_audio_set_system_output_enabled(1);
         auddec->fd = fd;
         return 0;
      }
      close(fd);
   }
   return -1;
}

static int media_auddec_open(AVFormatContext *fmt, int stream_index,
   int sync_mode, struct media_auddec *auddec)
{
   AVStream *stream;
   AVCodecParameters *par;
   struct audio_config base_cfg;
   struct audio_config cfg;
   static const struct media_auddec_variant variants[] = {
      { "hcplayer_i2so", 0, AUDDEV_I2SO, 1, 1, 0 },
      { "hcplayer_default", 0, AUDDEV_DEFAULT, 1, 1, 0 },
      { "minimal", 0, AUDDEV_DEFAULT, 0, 0, 0 },
      { "minimal_i2so", 0, AUDDEV_I2SO, 0, 0, 0 },
      { "minimal_48k", 48000, AUDDEV_DEFAULT, 0, 0, 0 },
      { "sink_i2so", 0, AUDDEV_I2SO, 1, 0, 200 },
      { "stream_full", 0, AUDDEV_DEFAULT, 1, 1, 200 },
   };
   int hc_codec;
   audio_channel_select_t channel = AUDIO_MONO_LEFT;
   uint8_t volume = MEDIA_AUDIO_VOLUME;
   unsigned bits;

   if (!auddec)
      return -1;
   auddec->fd = -1;
   auddec->stream = -1;
   auddec->time_base = (AVRational){ 1, 1000 };
   auddec->packets = 0;
   auddec->freerun = sync_mode == 0;
   auddec->aac_adts = 0;
   auddec->aac_profile = 1;
   auddec->aac_sample_rate_index = 4;
   auddec->aac_channels = 2;
   if (!fmt || stream_index < 0 || stream_index >= (int)fmt->nb_streams)
      return -1;
   stream = fmt->streams[stream_index];
   par = stream->codecpar;
   hc_codec = media_hc_audio_codec_from_av(par->codec_id);
   if (!hc_codec) {
      printf("unifrog media auddec unsupported codec=%d stream=%d\n",
         par->codec_id, stream_index);
      return -1;
   }

   bits = par->bits_per_coded_sample > 0 ?
      (unsigned)par->bits_per_coded_sample : 16u;
   memset(&base_cfg, 0, sizeof(base_cfg));
   base_cfg.codec_id = (uint32_t)hc_codec;
   base_cfg.sync_mode = (uint8_t)sync_mode;
   base_cfg.bits_per_coded_sample = bits > 255u ? 16u : (uint8_t)bits;
   base_cfg.channels = par->channels > 0 ? (uint8_t)par->channels : 2u;
   base_cfg.sample_rate = par->sample_rate > 0 ?
      (uint32_t)par->sample_rate : 44100u;
   if (par->codec_id == AV_CODEC_ID_AAC) {
      /*
       * The HCRTOS AAC decoder accepts ADTS elementary stream packets. MP4/M4A
       * streams carry raw AAC frames plus AudioSpecificConfig extradata; passing
       * that ASC directly makes AUDDEC_INIT reject the stream on SF2000.
       */
      auddec->aac_adts = 1;
      media_aac_parse_asc(par, &auddec->aac_profile,
         &auddec->aac_sample_rate_index, &auddec->aac_channels);
   } else if (par->extradata && par->extradata_size > 0) {
      base_cfg.extradata_size = (uint32_t)par->extradata_size;
      if (par->extradata_size <= (int)sizeof(cfg.extra_data)) {
         base_cfg.extradata_mode = 0;
         memcpy(base_cfg.extra_data, par->extradata,
            (size_t)par->extradata_size);
      } else {
         base_cfg.extradata_mode = 1;
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(variants); i++) {
      const struct media_auddec_variant *variant = &variants[i];
      int fd;
      int extra_ret = 0;
      int init_ret;
      int init_errno;
      int start_ret;
      int start_errno;

      cfg = base_cfg;
      cfg.snd_devs = variant->snd_devs;
      cfg.enable_audsink = variant->enable_audsink;
      cfg.audio_flush_thres = variant->audio_flush_thres;
      cfg.buffering_start = 200;
      cfg.buffering_end = 1000;
      cfg.kshm_size = MEDIA_AUDIO_KSHM_SIZE;
      if (variant->force_rate)
         cfg.sample_rate = (uint32_t)variant->force_rate;
      if (variant->full_stream_fields) {
         cfg.codec_tag = par->codec_tag;
         cfg.bit_rate = par->bit_rate > 0 ? (uint32_t)par->bit_rate : 0u;
         cfg.block_align = par->block_align > 0 ?
            (uint32_t)par->block_align : 0u;
         cfg.channel_layout = par->channel_layout;
      }

      fd = open("/dev/auddec", O_RDWR);
      if (fd < 0) {
         printf("unifrog media auddec open failed errno=%d stream=%d codec=%d try=%s\n",
            errno, stream_index, par->codec_id, variant->label);
         continue;
      }
      if (cfg.extradata_mode == 1)
         extra_ret = media_write_extra_before_init(fd, "auddec",
            par->extradata,
            par->extradata_size);
      errno = 0;
      init_ret = extra_ret == 0 ? ioctl(fd, AUDDEC_INIT, &cfg) : -1;
      init_errno = errno;
      errno = 0;
      start_ret = init_ret == 0 ? ioctl(fd, AUDDEC_START, 0) : -1;
      start_errno = errno;
      printf("unifrog media auddec init_try label=%s fd=%d extra=%d init=%d init_errno=%d start=%d start_errno=%d stream=%d codec=%u av=%d name=%s tag=0x%lx rate=%u ch=%u bits=%u block=%u extra_size=%u mode=%u sync=%u snd=0x%lx audsink=%d flush=%d adts=%d prof=%u sridx=%u\n",
         variant->label, fd, extra_ret, init_ret, init_errno, start_ret,
         start_errno, stream_index, cfg.codec_id, par->codec_id,
         media_avcodec_name(par->codec_id), (unsigned long)par->codec_tag,
         cfg.sample_rate, cfg.channels, cfg.bits_per_coded_sample,
         cfg.block_align, cfg.extradata_size, cfg.extradata_mode,
         cfg.sync_mode, (unsigned long)cfg.snd_devs, cfg.enable_audsink,
         cfg.audio_flush_thres, auddec->aac_adts, auddec->aac_profile,
         auddec->aac_sample_rate_index);
      if (extra_ret == 0 && init_ret == 0 && start_ret == 0) {
         (void)ioctl(fd, AUDIO_CHANNEL_SELECT, &channel);
         (void)ioctl(fd, AUDIO_SET_VOLUME, &volume);
         unifrog_audio_set_system_output_enabled(1);
         auddec->fd = fd;
         auddec->stream = stream_index;
         auddec->time_base = stream->time_base;
         auddec->freerun = cfg.sync_mode == 0;
         printf("unifrog media auddec open ok label=%s fd=%d stream=%d freerun=%d\n",
            variant->label, fd, stream_index, auddec->freerun);
         return 0;
      }
      close(fd);
   }
   printf("unifrog media auddec open failed stream=%d codec=%d name=%s rate=%d ch=%d extra=%d\n",
      stream_index, par->codec_id, media_avcodec_name(par->codec_id),
      par->sample_rate, par->channels, par->extradata_size);
   return -1;
}

static unsigned media_video_frame_rate_milli(const AVStream *stream)
{
   AVRational rate = { 0, 1 };
   int64_t value;

   if (!stream)
      return 25000;
   if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0)
      rate = stream->avg_frame_rate;
   else if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0)
      rate = stream->r_frame_rate;
   if (rate.num <= 0 || rate.den <= 0)
      return 25000;
   value = ((int64_t)rate.num * 1000) / rate.den;
   if (value < 1000 || value > 120000)
      return 25000;
   return (unsigned)value;
}

static int media_h264_extradata_annexb(const uint8_t *src, int src_size,
   uint8_t *dst, size_t dst_size, size_t *out_size)
{
   const uint8_t start_code[] = { 0x00, 0x00, 0x00, 0x01 };
   const uint8_t *p;
   const uint8_t *end;
   size_t used = 0;
   unsigned sps_count;
   unsigned pps_count;

   if (out_size)
      *out_size = 0;
   if (!src || src_size <= 0 || !dst || !out_size)
      return -1;
   if (src_size >= 4 && src[0] == 0 && src[1] == 0 &&
       ((src[2] == 1) || (src[2] == 0 && src[3] == 1))) {
      if ((size_t)src_size > dst_size)
         return -1;
      memcpy(dst, src, (size_t)src_size);
      *out_size = (size_t)src_size;
      return 0;
   }
   if (src_size < 7 || src[0] != 1)
      return -1;
   p = src + 5;
   end = src + src_size;
   sps_count = *p++ & 0x1f;
   for (unsigned i = 0; i < sps_count; i++) {
      unsigned len;

      if (p + 2 > end)
         return -1;
      len = ((unsigned)p[0] << 8) | p[1];
      p += 2;
      if (p + len > end || used + sizeof(start_code) + len > dst_size)
         return -1;
      memcpy(dst + used, start_code, sizeof(start_code));
      used += sizeof(start_code);
      memcpy(dst + used, p, len);
      used += len;
      p += len;
   }
   if (p >= end)
      return -1;
   pps_count = *p++;
   for (unsigned i = 0; i < pps_count; i++) {
      unsigned len;

      if (p + 2 > end)
         return -1;
      len = ((unsigned)p[0] << 8) | p[1];
      p += 2;
      if (p + len > end || used + sizeof(start_code) + len > dst_size)
         return -1;
      memcpy(dst + used, start_code, sizeof(start_code));
      used += sizeof(start_code);
      memcpy(dst + used, p, len);
      used += len;
      p += len;
   }
   if (!used)
      return -1;
   *out_size = used;
   return 0;
}

static int media_video_open_decoder(AVFormatContext *fmt, int stream_index,
   int disable_audio)
{
   AVStream *stream;
   AVCodecParameters *par;
   struct video_config cfg;
   struct vdec_dis_rect rect;
   int fd;
   int hc_codec;
   int init_ret;
   int init_errno;
   int start_ret;
   int start_errno;
   int rect_ret;
   int extra_ret = 0;
   int write_extra = 0;

   if (!fmt || stream_index < 0 || stream_index >= (int)fmt->nb_streams)
      return -1;
   (void)disable_audio;
   stream = fmt->streams[stream_index];
   par = stream->codecpar;
   hc_codec = media_hc_codec_from_av(par->codec_id);
   if (!hc_codec) {
      printf("unifrog media native video unsupported codec=%d path=?\n",
         par->codec_id);
      return -1;
   }
   if (par->width > MEDIA_MAX_VIDEO_W || par->height > MEDIA_MAX_VIDEO_H) {
      printf("unifrog media native video unsupported_size %dx%d max=%dx%d codec=%d\n",
         par->width, par->height, MEDIA_MAX_VIDEO_W, MEDIA_MAX_VIDEO_H,
         par->codec_id);
      return -1;
   }

   memset(&cfg, 0, sizeof(cfg));
   cfg.codec_id = (uint32_t)hc_codec;
   cfg.sync_mode = 0;
   cfg.decode_mode = VDEC_WORK_MODE_KSHM;
   cfg.pic_width = par->width > 0 ? par->width : VIDEO_SOURCE_W;
   cfg.pic_height = par->height > 0 ? par->height : VIDEO_SOURCE_H;
   cfg.pixel_aspect_x = 1;
   cfg.pixel_aspect_y = 1;
   cfg.preview = 0;
   cfg.frame_rate = par->codec_id == AV_CODEC_ID_H264 ? 60000u :
      media_video_frame_rate_milli(stream);
   cfg.src_area.x = 0;
   cfg.src_area.y = 0;
   cfg.src_area.w = (uint16_t)(cfg.pic_width > 0 ? cfg.pic_width : VIDEO_SOURCE_W);
   cfg.src_area.h = (uint16_t)(cfg.pic_height > 0 ? cfg.pic_height : VIDEO_SOURCE_H);
   cfg.dst_area.x = 0;
   cfg.dst_area.y = 0;
   cfg.dst_area.w = VIDEO_OUTPUT_W;
   cfg.dst_area.h = VIDEO_OUTPUT_H;
   cfg.quick_mode = 3;
   cfg.kshm_size = 0x800000;
   if (par->codec_id == AV_CODEC_ID_H264) {
      if (par->extradata && par->extradata_size > 0 &&
          par->extradata_size <= (int)sizeof(cfg.extra_data)) {
         cfg.extradata_size = par->extradata_size;
         cfg.extradata_mode = 0;
         memcpy(cfg.extra_data, par->extradata, (size_t)par->extradata_size);
      } else if (par->extradata && par->extradata_size > 0) {
         cfg.extradata_size = par->extradata_size;
         cfg.extradata_mode = 1;
         cfg.extradata = par->extradata;
         write_extra = 1;
      }
   } else if (par->extradata && par->extradata_size > 0) {
      cfg.extradata_size = par->extradata_size;
      if (par->extradata_size <= (int)sizeof(cfg.extra_data)) {
         cfg.extradata_mode = 0;
         memcpy(cfg.extra_data, par->extradata, (size_t)par->extradata_size);
      } else {
         cfg.extradata_mode = 1;
         cfg.extradata = par->extradata;
         write_extra = 1;
      }
   }

   media_set_aspect_mode(DIS_TV_16_9, DIS_PILLBOX);
   fd = open("/dev/viddec", O_RDWR);
   if (fd < 0) {
      printf("unifrog media native video open viddec failed errno=%d\n", errno);
      return -1;
   }
   if (write_extra)
      extra_ret = media_write_extra_before_init(fd, "viddec",
         par->extradata, par->extradata_size);
   errno = 0;
   init_ret = extra_ret == 0 ? ioctl(fd, VIDDEC_INIT, &cfg) : -1;
   init_errno = errno;
   errno = 0;
   start_ret = init_ret == 0 ? ioctl(fd, VIDDEC_START, 0) : -1;
   start_errno = errno;
   if (start_ret == 0) {
      int mosaic = 2;

      (void)ioctl(fd, VIDDEC_SET_SHOW_MASAIC_ON_ERR, mosaic);
   }
   memset(&rect, 0, sizeof(rect));
   rect.src_rect = cfg.src_area;
   rect.dst_rect = cfg.dst_area;
   rect_ret = init_ret == 0 ? ioctl(fd, VIDDEC_SET_DISPLAY_RECT, &rect) : -1;
   printf("unifrog media native video open fd=%d ret extra=%d init=%d init_errno=%d start=%d start_errno=%d rect=%d codec=%u av=%d tag=0x%lx %dx%d fps_milli=%u kshm=%lu extra=%d mode=%d\n",
      fd, extra_ret, init_ret, init_errno, start_ret, start_errno, rect_ret,
      cfg.codec_id, par->codec_id, (unsigned long)par->codec_tag,
      cfg.pic_width, cfg.pic_height, cfg.frame_rate,
      (unsigned long)cfg.kshm_size, cfg.extradata_size, cfg.extradata_mode);
   if (init_ret != 0 || start_ret != 0) {
      close(fd);
      return -1;
   }
   {
      struct vdec_decore_status status;

      memset(&status, 0, sizeof(status));
      if (ioctl(fd, VIDDEC_GET_STATUS, &status) == 0)
         printf("unifrog media native video status_after_open decoded=%lu displayed=%lu underrun=%lu used=%lu/%lu\n",
            (unsigned long)status.frames_decoded,
            (unsigned long)status.frames_displayed,
            (unsigned long)status.under_run_cnt,
            (unsigned long)status.buffer_used,
            (unsigned long)status.buffer_size);
   }
   (void)set_video_layer_visible(1, cfg.pic_width, cfg.pic_height,
      VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
   return fd;
}

static int media_video_bsf_init(AVStream *stream, AVBSFContext **bsf_out)
{
   const char *name = NULL;
   const AVBitStreamFilter *filter;
   AVBSFContext *bsf = NULL;
   int ret;

   if (!stream || !bsf_out)
      return -1;
   *bsf_out = NULL;
   if (stream->codecpar->codec_id == AV_CODEC_ID_H264)
      name = "h264_mp4toannexb";
   if (!name)
      return 0;
   filter = av_bsf_get_by_name(name);
   if (!filter) {
      printf("unifrog media native bsf missing name=%s\n", name);
      return 0;
   }
   ret = av_bsf_alloc(filter, &bsf);
   if (ret < 0 || !bsf)
      return -1;
   ret = avcodec_parameters_copy(bsf->par_in, stream->codecpar);
   if (ret >= 0) {
      bsf->time_base_in = stream->time_base;
      ret = av_bsf_init(bsf);
   }
   if (ret < 0) {
      printf("unifrog media native bsf init failed name=%s ret=%d\n",
         name, ret);
      av_bsf_free(&bsf);
      return -1;
   }
   printf("unifrog media native bsf enabled name=%s\n", name);
   *bsf_out = bsf;
   return 0;
}

static int media_video_send_filtered(int fd, AVBSFContext *bsf,
   AVPacket *packet, AVRational time_base, int freerun, int h264,
   uint32_t *video_packets)
{
   int ret;

   if (!bsf) {
      ret = media_video_send_packet(fd, packet, time_base, freerun, h264);
      if (ret == 0 && video_packets)
         (*video_packets)++;
      return ret;
   }
   ret = av_bsf_send_packet(bsf, packet);
   if (ret < 0)
      return ret;
   for (;;) {
      ret = av_bsf_receive_packet(bsf, packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
         return 0;
      if (ret < 0)
         return ret;
      ret = media_video_send_packet(fd, packet, time_base, freerun, h264);
      av_packet_unref(packet);
      if (ret != 0)
         return -1;
      if (video_packets)
         (*video_packets)++;
   }
}

static int media_play_native_video(const char *path,
   const struct unifrog_media_video_options *options)
{
   AVFormatContext *fmt = NULL;
   AVCodecContext *audio_ctx = NULL;
   AVCodec *audio_decoder = NULL;
   AVPacket *packet = NULL;
   AVFrame *frame = NULL;
   AVBSFContext *video_bsf = NULL;
   struct unifrog_audio audio;
   struct media_auddec auddec;
   int16_t *pcm = NULL;
   int video_stream = -1;
   int audio_stream = -1;
   int video_fd = -1;
   int ret = -1;
   int audio_enabled = 0;
   uint32_t video_packets = 0;
   uint32_t audio_frames = 0;
   uint32_t loop_polls = 0;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned long frames_decoded = 0;
   unsigned long frames_displayed = 0;
   int disable_audio = options && options->disable_audio;
   int sd_read_active = 0;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   media_ffmpeg_register_once();
   media_sd_read_begin("native_video_open", path);
   sd_read_active = 1;
   printf("unifrog media native open_input begin path=%s\n",
      path ? path : "");
   int open_ret = avformat_open_input(&fmt, path, NULL, NULL);
   printf("unifrog media native open_input done ret=%d fmt=0x%08lx path=%s\n",
      open_ret, (unsigned long)(uintptr_t)fmt, path ? path : "");
   printf("unifrog media native stream_info begin path=%s\n",
      path ? path : "");
   int info_ret = open_ret == 0 ? avformat_find_stream_info(fmt, NULL) : 0;
   printf("unifrog media native stream_info done ret=%d streams=%u path=%s\n",
      info_ret, fmt ? fmt->nb_streams : 0, path ? path : "");

   if (open_ret < 0 || info_ret < 0) {
      printf("unifrog media native open failed open=%d info=%d path=%s\n",
         open_ret, info_ret, path);
      media_log_file_probe(path, "native_video_open_failed");
      media_log_format_streams(fmt, path, "native_video_partial");
      goto out;
   }
   media_log_format_streams(fmt, path, "native_video_open");
   video_stream = media_find_stream_type(fmt, AVMEDIA_TYPE_VIDEO);
   if (!disable_audio)
      audio_stream = media_find_stream_type(fmt, AVMEDIA_TYPE_AUDIO);
   printf("unifrog media native streams selected video=%d audio=%d disable_audio=%d path=%s\n",
      video_stream, audio_stream, disable_audio, path ? path : "");
   (void)unifrog_log_flush();
   if (video_stream < 0) {
      printf("unifrog media native no video audio=%d path=%s\n",
         audio_stream, path);
      if (!disable_audio && audio_stream >= 0)
         ret = media_play_native_audio_compressed(path);
      goto out;
   }
   printf("unifrog media native init_drivers begin\n");
   (void)unifrog_log_flush();
   media_init_drivers_once();
   printf("unifrog media native init_drivers done\n");
   (void)unifrog_log_flush();
   if (!disable_audio && audio_stream >= 0) {
      printf("unifrog media native auddec_open begin stream=%d\n",
         audio_stream);
      (void)unifrog_log_flush();
      (void)media_auddec_open(fmt, audio_stream, 2, &auddec);
      printf("unifrog media native auddec_open done fd=%d\n", auddec.fd);
      (void)unifrog_log_flush();
   }
   media_video_debug_packets = 0;
   printf("unifrog media native video_open begin stream=%d audio_fd=%d\n",
      video_stream, auddec.fd);
   (void)unifrog_log_flush();
   video_fd = media_video_open_decoder(fmt, video_stream, disable_audio ||
      auddec.fd < 0);
   printf("unifrog media native video_open done fd=%d\n", video_fd);
   (void)unifrog_log_flush();
   if (video_fd < 0)
      goto out;
   printf("unifrog media native bsf begin stream=%d\n", video_stream);
   (void)unifrog_log_flush();
   (void)media_video_bsf_init(fmt->streams[video_stream], &video_bsf);
   printf("unifrog media native bsf done enabled=%d\n", video_bsf ? 1 : 0);
   (void)unifrog_log_flush();
   if (audio_stream >= 0 && auddec.fd < 0 &&
       fmt->streams[audio_stream] && fmt->streams[audio_stream]->codecpar)
      audio_decoder = avcodec_find_decoder(
         fmt->streams[audio_stream]->codecpar->codec_id);
   if (audio_stream >= 0 && auddec.fd < 0 && audio_decoder) {
      audio_ctx = avcodec_alloc_context3(audio_decoder);
      if (audio_ctx &&
          avcodec_parameters_to_context(audio_ctx,
             fmt->streams[audio_stream]->codecpar) == 0) {
         audio_ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;
         audio_ctx->request_channel_layout = AV_CH_LAYOUT_MONO;
         if (avcodec_open2(audio_ctx, audio_decoder, NULL) == 0 &&
             audio_ctx->sample_rate >= 8000 &&
             audio_ctx->sample_rate <= 48000 &&
             unifrog_audio_open(&audio, (unsigned)audio_ctx->sample_rate, 1,
                512, 8) == 0) {
            pcm = malloc(sizeof(*pcm) * MEDIA_FFMPEG_CHUNK_FRAMES);
            if (pcm) {
               (void)unifrog_audio_set_volume(&audio, MEDIA_AUDIO_VOLUME);
               (void)unifrog_audio_set_mute(&audio, 1);
               (void)unifrog_audio_start(&audio);
               (void)unifrog_audio_set_output_enabled(&audio, 1);
               audio_enabled = 1;
            }
         }
      }
   }
   packet = av_packet_alloc();
   frame = av_frame_alloc();
   if (!packet || !frame)
      goto out;
   printf("unifrog media native play video=%d audio=%d audio_enabled=%d auddec=%d path=%s\n",
      video_stream, audio_stream, audio_enabled, auddec.fd >= 0, path);
   if (fb_fd >= 0)
      (void)ioctl(fb_fd, FBIOBLANK, FB_BLANK_NORMAL);

   while (!media_exit_down()) {
      int read_ret = av_read_frame(fmt, packet);

      if (read_ret < 0)
         break;
      if (packet->stream_index == video_stream) {
         int write_ret = media_video_send_filtered(video_fd, video_bsf,
            packet, fmt->streams[video_stream]->time_base,
            disable_audio || auddec.fd < 0,
            fmt->streams[video_stream]->codecpar->codec_id == AV_CODEC_ID_H264,
            &video_packets);

         if (write_ret < 0) {
            printf("unifrog media native video write failed ret=%d packets=%lu path=%s\n",
               write_ret, (unsigned long)video_packets, path);
            av_packet_unref(packet);
            break;
         }
      } else if (auddec.fd >= 0 && packet->stream_index == audio_stream) {
         if (media_auddec_send_packet(&auddec, packet) != 0)
            printf("unifrog media native auddec write failed packets=%lu path=%s\n",
               (unsigned long)auddec.packets, path);
      } else if (audio_enabled && packet->stream_index == audio_stream) {
         int send_ret = avcodec_send_packet(audio_ctx, packet);

         if (send_ret >= 0 || send_ret == AVERROR(EAGAIN)) {
            for (;;) {
               int recv_ret = avcodec_receive_frame(audio_ctx, frame);
               int write_ret;

               if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
                  break;
               if (recv_ret < 0)
                  break;
               write_ret = media_ffmpeg_write_frame(&audio, frame, pcm,
                  MEDIA_FFMPEG_CHUNK_FRAMES, &audio_frames);
               if (write_ret != 0)
                  break;
            }
         }
      }
      av_packet_unref(packet);
      if ((++loop_polls % 180u) == 0) {
         struct vdec_decore_status status;

         memset(&status, 0, sizeof(status));
         if (ioctl(video_fd, VIDDEC_GET_STATUS, &status) == 0)
            printf("unifrog media native monitor packets=%lu audio=%lu decoded=%lu displayed=%lu underrun=%lu used=%lu/%lu ms=%lu\n",
               (unsigned long)video_packets,
               (unsigned long)(audio_frames + auddec.packets),
               (unsigned long)status.frames_decoded,
               (unsigned long)status.frames_displayed,
               (unsigned long)status.under_run_cnt,
               (unsigned long)status.buffer_used,
               (unsigned long)status.buffer_size,
               (unsigned long)(unifrog_perf_time_ms() - start_ms));
      }
   }
   media_video_send_eos(video_fd);
   if (video_fd >= 0) {
      struct vdec_decore_status status;

      memset(&status, 0, sizeof(status));
      if (ioctl(video_fd, VIDDEC_GET_STATUS, &status) == 0) {
         frames_decoded = (unsigned long)status.frames_decoded;
         frames_displayed = (unsigned long)status.frames_displayed;
         printf("unifrog media native final_status decoded=%lu displayed=%lu underrun=%lu used=%lu/%lu packets=%lu\n",
            frames_decoded, frames_displayed,
            (unsigned long)status.under_run_cnt,
            (unsigned long)status.buffer_used,
            (unsigned long)status.buffer_size,
            (unsigned long)video_packets);
      }
   }
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, 1000);
   ret = video_packets && (frames_decoded || frames_displayed) ? 0 : -1;

out:
   printf("unifrog media native end ret=%d video_packets=%lu audio_frames=%lu ms=%lu path=%s\n",
      ret, (unsigned long)video_packets,
      (unsigned long)(audio_frames + auddec.packets),
      (unsigned long)(unifrog_perf_time_ms() - start_ms), path ? path : "");
   if (audio.fd >= 0)
      unifrog_audio_close(&audio);
   media_auddec_close(&auddec);
   if (video_fd >= 0) {
      struct vdec_rls_param rls;

      memset(&rls, 0, sizeof(rls));
      rls.closevp = 0;
      rls.fillblack = 0;
      (void)ioctl(video_fd, VIDDEC_RLS, (unsigned long)&rls);
      close(video_fd);
   }
   close_display();
   free(pcm);
   if (video_bsf)
      av_bsf_free(&video_bsf);
   if (frame)
      av_frame_free(&frame);
   if (packet)
      av_packet_free(&packet);
   if (audio_ctx)
      avcodec_free_context(&audio_ctx);
   if (fmt)
      avformat_close_input(&fmt);
   if (sd_read_active)
      media_sd_read_end("native_video_close", path);
   return ret;
}

static int media_wav_ms_adpcm_nibble(int nibble, int16_t sample1,
   int16_t sample2, int *delta, int coeff1, int coeff2)
{
   static const int adapt_table[16] = {
      230, 230, 230, 230, 307, 409, 512, 614,
      768, 614, 512, 409, 307, 230, 230, 230,
   };
   int signed_nibble = nibble & 0x08 ? nibble - 0x10 : nibble;
   int decoded = (((int)sample1 * coeff1 + (int)sample2 * coeff2) / 256) +
      signed_nibble * *delta;

   if (decoded > 32767)
      decoded = 32767;
   else if (decoded < -32768)
      decoded = -32768;
   *delta = (*delta * adapt_table[nibble & 0x0f]) / 256;
   if (*delta < 16)
      *delta = 16;
   return decoded;
}

static unsigned media_mp3_bitrate_kbps(unsigned version, unsigned layer,
   unsigned index)
{
   static const unsigned v1_l3[16] = {
      0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0,
   };
   static const unsigned v2_l3[16] = {
      0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0,
   };

   (void)layer;
   return version == 3u ? v1_l3[index & 15u] : v2_l3[index & 15u];
}

static unsigned media_mp3_sample_rate(unsigned version, unsigned index)
{
   static const unsigned base[4] = { 44100, 48000, 32000, 0 };
   unsigned rate = base[index & 3u];

   if (version == 2u)
      rate /= 2u;
   else if (version == 0u)
      rate /= 4u;
   return rate;
}

static int media_parse_mp3_header(const uint8_t h[4], unsigned *frame_size,
   unsigned *sample_rate, unsigned *channels, unsigned *duration_ms)
{
   unsigned version;
   unsigned layer;
   unsigned bitrate_index;
   unsigned rate_index;
   unsigned padding;
   unsigned bitrate;
   unsigned rate;

   if (!h || h[0] != 0xff || (h[1] & 0xe0) != 0xe0)
      return -1;
   version = (h[1] >> 3) & 0x03;
   layer = (h[1] >> 1) & 0x03;
   bitrate_index = (h[2] >> 4) & 0x0f;
   rate_index = (h[2] >> 2) & 0x03;
   padding = (h[2] >> 1) & 0x01;
   if (version == 1u || layer != 1u || bitrate_index == 0u ||
       bitrate_index == 15u || rate_index == 3u)
      return -1;
   bitrate = media_mp3_bitrate_kbps(version, layer, bitrate_index);
   rate = media_mp3_sample_rate(version, rate_index);
   if (!bitrate || !rate)
      return -1;
   *frame_size = ((version == 3u ? 144000u : 72000u) * bitrate) / rate +
      padding;
   *sample_rate = rate;
   *channels = ((h[3] >> 6) & 0x03) == 3u ? 1u : 2u;
   *duration_ms = version == 3u ? 26u : 13u;
   return *frame_size >= 4u ? 0 : -1;
}

static long media_skip_id3v2(FILE *file)
{
   uint8_t h[10];
   uint32_t size;

   if (!file)
      return 0;
   if (fread(h, 1, sizeof(h), file) != sizeof(h)) {
      (void)fseek(file, 0, SEEK_SET);
      return 0;
   }
   if (memcmp(h, "ID3", 3) != 0) {
      (void)fseek(file, 0, SEEK_SET);
      return 0;
   }
   size = ((uint32_t)(h[6] & 0x7f) << 21) |
      ((uint32_t)(h[7] & 0x7f) << 14) |
      ((uint32_t)(h[8] & 0x7f) << 7) | (uint32_t)(h[9] & 0x7f);
   (void)fseek(file, (long)size, SEEK_CUR);
   return 10L + (long)size;
}

static int media_play_mp3_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_auddec auddec;
   uint8_t *frame = NULL;
   uint8_t h[4];
   unsigned frame_size = 0;
   unsigned rate = 44100;
   unsigned channels = 2;
   unsigned dur = 26;
   unsigned packets = 0;
   int32_t pts = 0;
   int ret = -1;

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   (void)media_skip_id3v2(file);
   while (fread(h, 1, sizeof(h), file) == sizeof(h)) {
      if (media_parse_mp3_header(h, &frame_size, &rate, &channels, &dur) == 0)
         break;
      (void)fseek(file, -3, SEEK_CUR);
   }
   if (!frame_size)
      goto out;
   if (media_auddec_open_raw("mp3", HC_AVCODEC_ID_MP3, rate, channels, 16,
      NULL, 0, 0, &auddec) != 0)
      goto out;
   frame = malloc(frame_size > 4096u ? frame_size : 4096u);
   if (!frame)
      goto out;
   for (;;) {
      unsigned next_size;
      unsigned next_rate;
      unsigned next_channels;
      unsigned next_dur;

      if (frame_size > 65536u)
         break;
      memcpy(frame, h, sizeof(h));
      if (fread(frame + 4, 1, frame_size - 4u, file) != frame_size - 4u)
         break;
      if (media_auddec_send_raw(&auddec, frame, frame_size, pts,
         (int32_t)dur) != 0)
         break;
      packets++;
      pts += (int32_t)dur;
      if (media_exit_down())
         break;
      if (fread(h, 1, sizeof(h), file) != sizeof(h))
         break;
      if (media_parse_mp3_header(h, &next_size, &next_rate, &next_channels,
         &next_dur) != 0)
         break;
      if (next_size > frame_size) {
         uint8_t *new_frame = realloc(frame, next_size);

         if (!new_frame)
            break;
         frame = new_frame;
      }
      frame_size = next_size;
      dur = next_dur;
   }
   ret = packets ? 0 : -1;

out:
   printf("unifrog media mp3 native end ret=%d packets=%u path=%s\n",
      ret, packets, path ? path : "");
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, 1000);
   media_auddec_close(&auddec);
   free(frame);
   if (file)
      fclose(file);
   return ret;
}

static int media_parse_adts_header(const uint8_t h[7], unsigned *frame_size,
   unsigned *sample_rate, unsigned *channels, unsigned *duration_ms)
{
   static const unsigned rates[16] = {
      96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
      16000, 12000, 11025, 8000, 7350, 0, 0, 0,
   };
   unsigned sr_index;

   if (!h || h[0] != 0xff || (h[1] & 0xf0) != 0xf0)
      return -1;
   sr_index = (h[2] >> 2) & 0x0f;
   *frame_size = ((unsigned)(h[3] & 0x03) << 11) |
      ((unsigned)h[4] << 3) | ((unsigned)h[5] >> 5);
   *sample_rate = rates[sr_index];
   *channels = ((unsigned)(h[2] & 0x01) << 2) | ((unsigned)h[3] >> 6);
   if (!*channels)
      *channels = 2;
   *duration_ms = *sample_rate ? (1024u * 1000u) / *sample_rate : 23u;
   return *frame_size >= 7u && *sample_rate ? 0 : -1;
}

static int media_play_aac_adts_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_auddec auddec;
   uint8_t *frame = NULL;
   uint8_t h[7];
   unsigned frame_size = 0;
   unsigned rate = 44100;
   unsigned channels = 2;
   unsigned dur = 23;
   unsigned packets = 0;
   int32_t pts = 0;
   int ret = -1;

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   if (fread(h, 1, sizeof(h), file) != sizeof(h) ||
       media_parse_adts_header(h, &frame_size, &rate, &channels, &dur) != 0)
      goto out;
   if (media_auddec_open_raw("aac_adts", HC_AVCODEC_ID_AAC, rate, channels,
      16, NULL, 0, 0, &auddec) != 0)
      goto out;
   frame = malloc(frame_size > 4096u ? frame_size : 4096u);
   if (!frame)
      goto out;
   for (;;) {
      unsigned next_size;
      unsigned next_rate;
      unsigned next_channels;
      unsigned next_dur;

      if (frame_size > 65536u)
         break;
      memcpy(frame, h, sizeof(h));
      if (fread(frame + 7, 1, frame_size - 7u, file) != frame_size - 7u)
         break;
      if (media_auddec_send_raw(&auddec, frame, frame_size, pts,
         (int32_t)dur) != 0)
         break;
      packets++;
      pts += (int32_t)dur;
      if (media_exit_down())
         break;
      if (fread(h, 1, sizeof(h), file) != sizeof(h))
         break;
      if (media_parse_adts_header(h, &next_size, &next_rate, &next_channels,
         &next_dur) != 0)
         break;
      if (next_size > frame_size) {
         uint8_t *new_frame = realloc(frame, next_size);

         if (!new_frame)
            break;
         frame = new_frame;
      }
      frame_size = next_size;
      dur = next_dur;
   }
   ret = packets ? 0 : -1;

out:
   printf("unifrog media aac native end ret=%d packets=%u path=%s\n",
      ret, packets, path ? path : "");
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, 1000);
   media_auddec_close(&auddec);
   free(frame);
   if (file)
      fclose(file);
   return ret;
}

static int media_play_flac_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_auddec auddec;
   uint8_t magic[4];
   uint8_t block_header[4];
   uint8_t streaminfo[34];
   uint8_t buf[4096];
   unsigned rate = 44100;
   unsigned channels = 2;
   unsigned packets = 0;
   long audio_pos = -1;
   int have_streaminfo = 0;
   int ret = -1;

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   if (fread(magic, 1, sizeof(magic), file) != sizeof(magic) ||
       memcmp(magic, "fLaC", 4) != 0)
      goto out;
   while (fread(block_header, 1, sizeof(block_header), file) ==
      sizeof(block_header)) {
      int last = block_header[0] & 0x80;
      unsigned type = block_header[0] & 0x7f;
      uint32_t size = ((uint32_t)block_header[1] << 16) |
         ((uint32_t)block_header[2] << 8) | block_header[3];

      if (type == 0u && size == sizeof(streaminfo)) {
         if (fread(streaminfo, 1, sizeof(streaminfo), file) !=
             sizeof(streaminfo))
            goto out;
         have_streaminfo = 1;
      } else if (fseek(file, (long)size, SEEK_CUR) != 0) {
         goto out;
      }
      if (last) {
         audio_pos = ftell(file);
         break;
      }
   }
   if (!have_streaminfo || audio_pos < 0)
      goto out;
   {
      uint32_t packed = media_read_be32(streaminfo + 10);

      rate = packed >> 12;
      channels = ((packed >> 9) & 0x07) + 1u;
      if (!rate)
         rate = 44100;
   }
   if (media_auddec_open_raw("flac", HC_AVCODEC_ID_FLAC, rate, channels, 16,
      streaminfo, sizeof(streaminfo), 0, &auddec) != 0)
      goto out;
   if (fseek(file, audio_pos, SEEK_SET) != 0)
      goto out;
   while (!media_exit_down()) {
      size_t got = fread(buf, 1, sizeof(buf), file);

      if (!got)
         break;
      if (media_auddec_send_raw(&auddec, buf, got, -1, 0) != 0)
         break;
      packets++;
   }
   ret = packets ? 0 : -1;

out:
   printf("unifrog media flac native end ret=%d packets=%u rate=%u ch=%u audio_pos=%ld streaminfo=%d path=%s\n",
      ret, packets, rate, channels, audio_pos, have_streaminfo,
      path ? path : "");
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, 1000);
   media_auddec_close(&auddec);
   if (file)
      fclose(file);
   return ret;
}

struct media_ogg_state {
   struct media_auddec auddec;
   uint8_t *packet;
   size_t packet_size;
   size_t packet_cap;
   uint8_t *headers[3];
   size_t header_sizes[3];
   unsigned header_count;
   unsigned codec_id;
   unsigned sample_rate;
   unsigned channels;
   unsigned packets;
   int opened;
   int open_failed;
};

static int media_ogg_append(struct media_ogg_state *st, const uint8_t *data,
   size_t size)
{
   if (!st || (!data && size))
      return -1;
   if (st->packet_size + size > st->packet_cap) {
      size_t cap = st->packet_cap ? st->packet_cap * 2u : 4096u;
      uint8_t *new_packet;

      while (cap < st->packet_size + size)
         cap *= 2u;
      new_packet = realloc(st->packet, cap);
      if (!new_packet)
         return -1;
      st->packet = new_packet;
      st->packet_cap = cap;
   }
   memcpy(st->packet + st->packet_size, data, size);
   st->packet_size += size;
   return 0;
}

static int media_xiph_lace(uint8_t *out, size_t *pos, size_t out_size,
   size_t value)
{
   while (value >= 255u) {
      if (*pos >= out_size)
         return -1;
      out[(*pos)++] = 255u;
      value -= 255u;
   }
   if (*pos >= out_size)
      return -1;
   out[(*pos)++] = (uint8_t)value;
   return 0;
}

static uint8_t *media_make_vorbis_extradata(const uint8_t *a, size_t as,
   const uint8_t *b, size_t bs, const uint8_t *c, size_t cs, size_t *out_size)
{
   size_t lace = 1u + (as / 255u + 1u) + (bs / 255u + 1u);
   size_t total = lace + as + bs + cs;
   uint8_t *out = malloc(total);
   size_t pos = 0;

   if (!out)
      return NULL;
   out[pos++] = 2u;
   if (media_xiph_lace(out, &pos, total, as) != 0 ||
       media_xiph_lace(out, &pos, total, bs) != 0) {
      free(out);
      return NULL;
   }
   memcpy(out + pos, a, as);
   pos += as;
   memcpy(out + pos, b, bs);
   pos += bs;
   memcpy(out + pos, c, cs);
   pos += cs;
   *out_size = pos;
   return out;
}

static int media_ogg_open_decoder(struct media_ogg_state *st)
{
   uint8_t *extra = NULL;
   size_t extra_size = 0;
   int ret;

   if (!st || st->opened || !st->codec_id)
      return st && st->opened ? 0 : -1;
   if (st->codec_id == HC_AVCODEC_ID_OPUS) {
      extra = st->headers[0];
      extra_size = st->header_sizes[0];
   } else if (st->codec_id == HC_AVCODEC_ID_VORBIS && st->header_count >= 3u) {
      extra = media_make_vorbis_extradata(st->headers[0],
         st->header_sizes[0], st->headers[1], st->header_sizes[1],
         st->headers[2], st->header_sizes[2], &extra_size);
      if (!extra)
         return -1;
   } else {
      return -1;
   }
   ret = media_auddec_open_raw(st->codec_id == HC_AVCODEC_ID_OPUS ?
      "ogg_opus" : "ogg_vorbis", st->codec_id, st->sample_rate,
      st->channels, 16, extra, (unsigned)extra_size, 0, &st->auddec);
   if (st->codec_id == HC_AVCODEC_ID_VORBIS)
      free(extra);
   st->opened = ret == 0;
   return ret;
}

static int media_ogg_packet(struct media_ogg_state *st)
{
   uint8_t *copy;

   if (!st || !st->packet || !st->packet_size)
      return 0;
   if (st->header_count == 0u && st->packet_size >= 19u &&
       memcmp(st->packet, "OpusHead", 8) == 0) {
      st->codec_id = HC_AVCODEC_ID_OPUS;
      st->channels = st->packet[9] ? st->packet[9] : 2u;
      st->sample_rate = media_read_le32(st->packet + 12);
      if (!st->sample_rate)
         st->sample_rate = 48000u;
   } else if (st->header_count == 0u && st->packet_size >= 30u &&
       st->packet[0] == 1u && memcmp(st->packet + 1, "vorbis", 6) == 0) {
      st->codec_id = HC_AVCODEC_ID_VORBIS;
      st->channels = st->packet[11] ? st->packet[11] : 2u;
      st->sample_rate = media_read_le32(st->packet + 12);
      if (!st->sample_rate)
         st->sample_rate = 44100u;
   }
   if (!st->codec_id)
      goto done;
   if (st->codec_id == HC_AVCODEC_ID_OPUS &&
       (st->header_count < 2u &&
        ((st->packet_size >= 8u && memcmp(st->packet, "OpusHead", 8) == 0) ||
         (st->packet_size >= 8u && memcmp(st->packet, "OpusTags", 8) == 0)))) {
      copy = malloc(st->packet_size);
      if (!copy)
         return -1;
      memcpy(copy, st->packet, st->packet_size);
      st->headers[st->header_count] = copy;
      st->header_sizes[st->header_count++] = st->packet_size;
      goto done;
   }
   if (st->codec_id == HC_AVCODEC_ID_VORBIS && st->header_count < 3u &&
       st->packet_size >= 7u && memcmp(st->packet + 1, "vorbis", 6) == 0) {
      copy = malloc(st->packet_size);
      if (!copy)
         return -1;
      memcpy(copy, st->packet, st->packet_size);
      st->headers[st->header_count] = copy;
      st->header_sizes[st->header_count++] = st->packet_size;
      goto done;
   }
   if (st->open_failed)
      goto done;
   if (!st->opened && media_ogg_open_decoder(st) != 0) {
      st->open_failed = 1;
      goto done;
   }
   if (st->opened &&
       media_auddec_send_raw(&st->auddec, st->packet, st->packet_size,
          -1, 0) == 0)
      st->packets++;

done:
   st->packet_size = 0;
   return 0;
}

static int media_play_ogg_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_ogg_state st;
   uint8_t page[27 + 255];
   int ret = -1;

   memset(&st, 0, sizeof(st));
   st.auddec.fd = -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   while (!media_exit_down()) {
      unsigned segments;
      unsigned body_size = 0;

      if (fread(page, 1, 27, file) != 27)
         break;
      if (memcmp(page, "OggS", 4) != 0)
         break;
      segments = page[26];
      if (segments > 255u || fread(page + 27, 1, segments, file) != segments)
         break;
      for (unsigned i = 0; i < segments; i++)
         body_size += page[27 + i];
      for (unsigned i = 0; i < segments; i++) {
         unsigned seg = page[27 + i];
         uint8_t tmp[255];

         if (seg && fread(tmp, 1, seg, file) != seg)
            goto out;
         if (media_ogg_append(&st, tmp, seg) != 0)
            goto out;
         if (seg < 255u && media_ogg_packet(&st) != 0)
            goto out;
      }
      (void)body_size;
   }
   ret = st.packets ? 0 : -1;

out:
   printf("unifrog media ogg native end ret=%d codec=%lu packets=%u headers=%u rate=%u ch=%u path=%s\n",
      ret, (unsigned long)st.codec_id, st.packets, st.header_count,
      st.sample_rate, st.channels, path ? path : "");
   if (st.auddec.fd >= 0)
      media_auddec_finish(&st.auddec, 1000);
   media_auddec_close(&st.auddec);
   for (unsigned i = 0; i < ARRAY_SIZE(st.headers); i++)
      free(st.headers[i]);
   free(st.packet);
   if (file)
      fclose(file);
   return ret;
}

static int media_wav_decode_ms_adpcm_block(const uint8_t *block,
   unsigned block_size, unsigned channels, const int coeffs[][2],
   unsigned coeff_count, int16_t *out, unsigned out_frames)
{
   int predictor[2];
   int delta[2];
   int16_t sample1[2];
   int16_t sample2[2];
   unsigned pos;
   unsigned frame = 0;

   if (!block || !out || (channels != 1u && channels != 2u) ||
       coeff_count == 0)
      return -1;
   if (channels == 1u) {
      if (block_size < 7u)
         return -1;
      predictor[0] = block[0] < coeff_count ? block[0] : 0;
      delta[0] = (int16_t)media_read_le16(block + 1);
      sample1[0] = (int16_t)media_read_le16(block + 3);
      sample2[0] = (int16_t)media_read_le16(block + 5);
      pos = 7u;
      if (out_frames > 0)
         out[frame++] = sample2[0];
      if (frame < out_frames)
         out[frame++] = sample1[0];
      while (pos < block_size && frame < out_frames) {
         uint8_t byte = block[pos++];
         int decoded;

         decoded = media_wav_ms_adpcm_nibble(byte >> 4, sample1[0],
            sample2[0], &delta[0], coeffs[predictor[0]][0],
            coeffs[predictor[0]][1]);
         sample2[0] = sample1[0];
         sample1[0] = (int16_t)decoded;
         out[frame++] = sample1[0];
         if (frame >= out_frames)
            break;
         decoded = media_wav_ms_adpcm_nibble(byte, sample1[0],
            sample2[0], &delta[0], coeffs[predictor[0]][0],
            coeffs[predictor[0]][1]);
         sample2[0] = sample1[0];
         sample1[0] = (int16_t)decoded;
         out[frame++] = sample1[0];
      }
      return (int)frame;
   }

   if (block_size < 14u)
      return -1;
   predictor[0] = block[0] < coeff_count ? block[0] : 0;
   predictor[1] = block[1] < coeff_count ? block[1] : 0;
   delta[0] = (int16_t)media_read_le16(block + 2);
   delta[1] = (int16_t)media_read_le16(block + 4);
   sample1[0] = (int16_t)media_read_le16(block + 6);
   sample1[1] = (int16_t)media_read_le16(block + 8);
   sample2[0] = (int16_t)media_read_le16(block + 10);
   sample2[1] = (int16_t)media_read_le16(block + 12);
   pos = 14u;
   if (out_frames > 0)
      out[frame++] = media_wav_clip_sample((sample2[0] + sample2[1]) >> 1);
   if (frame < out_frames)
      out[frame++] = media_wav_clip_sample((sample1[0] + sample1[1]) >> 1);
   while (pos < block_size && frame < out_frames) {
      uint8_t byte = block[pos++];
      int decoded_l = media_wav_ms_adpcm_nibble(byte >> 4, sample1[0],
         sample2[0], &delta[0], coeffs[predictor[0]][0],
         coeffs[predictor[0]][1]);
      int decoded_r = media_wav_ms_adpcm_nibble(byte, sample1[1],
         sample2[1], &delta[1], coeffs[predictor[1]][0],
         coeffs[predictor[1]][1]);

      sample2[0] = sample1[0];
      sample1[0] = (int16_t)decoded_l;
      sample2[1] = sample1[1];
      sample1[1] = (int16_t)decoded_r;
      out[frame++] = media_wav_clip_sample((decoded_l + decoded_r) >> 1);
   }
   return (int)frame;
}

static int media_wav_pcm_codec(unsigned format, unsigned bits,
   uint32_t *codec_id)
{
   if (!codec_id || (format != 1u && format != 65534u))
      return -1;
   switch (bits) {
   case 8:
      *codec_id = HC_AVCODEC_ID_PCM_U8;
      return 0;
   case 16:
      *codec_id = HC_AVCODEC_ID_PCM_S16LE;
      return 0;
   case 24:
      *codec_id = HC_AVCODEC_ID_PCM_S24LE;
      return 0;
   case 32:
      *codec_id = HC_AVCODEC_ID_PCM_S32LE;
      return 0;
   default:
      return -1;
   }
}

static int media_play_wav_auddec(const char *path)
{
   FILE *file = NULL;
   struct media_auddec auddec;
   uint8_t header[12];
   uint8_t chunk[8];
   uint8_t *buf = NULL;
   unsigned channels = 0;
   unsigned rate = 0;
   unsigned bits = 0;
   unsigned format = 0;
   uint32_t data_size = 0;
   long data_pos = -1;
   uint32_t codec_id = 0;
   uint32_t sent = 0;
   int ret = -1;

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
       memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
      goto out;
   while (fread(chunk, 1, sizeof(chunk), file) == sizeof(chunk)) {
      uint32_t size = media_read_le32(chunk + 4);
      long pos = ftell(file);
      long next = pos + (long)((size + 1u) & ~1u);

      if (memcmp(chunk, "fmt ", 4) == 0) {
         uint8_t fmt[40];
         size_t want = size < sizeof(fmt) ? size : sizeof(fmt);

         if (fread(fmt, 1, want, file) != want)
            goto out;
         if (want >= 16u) {
            format = media_read_le16(fmt);
            channels = media_read_le16(fmt + 2);
            rate = media_read_le32(fmt + 4);
            bits = media_read_le16(fmt + 14);
         }
      } else if (memcmp(chunk, "data", 4) == 0) {
         data_pos = pos;
         data_size = size;
      }
      if (fseek(file, next, SEEK_SET) != 0)
         break;
      if (format && data_pos >= 0)
         break;
   }
   if ((channels != 1u && channels != 2u) || rate < 8000u ||
       rate > 48000u || data_pos < 0 ||
       media_wav_pcm_codec(format, bits, &codec_id) != 0) {
      printf("unifrog media wav auddec unsupported path=%s format=%u ch=%u rate=%u bits=%u data=%lu\n",
         path, format, channels, rate, bits, (unsigned long)data_size);
      goto out;
   }
   printf("unifrog media wav auddec start path=%s codec=%lu format=%u ch=%u rate=%u bits=%u data=%lu\n",
      path, (unsigned long)codec_id, format, channels, rate, bits,
      (unsigned long)data_size);
   if (media_auddec_open_raw("wav_pcm", codec_id, rate, channels, bits,
      NULL, 0, 0, &auddec) != 0)
      goto out;
   if (fseek(file, data_pos, SEEK_SET) != 0)
      goto out;
   buf = malloc(4096);
   if (!buf)
      goto out;
   while (sent < data_size && !media_exit_down()) {
      size_t want = data_size - sent;
      size_t got;

      if (want > 4096u)
         want = 4096u;
      got = fread(buf, 1, want, file);
      if (!got)
         break;
      if (media_auddec_send_raw(&auddec, buf, got, -1, 0) != 0)
         break;
      sent += (uint32_t)got;
   }
   ret = sent ? 0 : -1;

out:
   printf("unifrog media wav auddec end ret=%d sent=%lu/%lu path=%s\n",
      ret, (unsigned long)sent, (unsigned long)data_size, path ? path : "");
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, 1000);
   media_auddec_close(&auddec);
   free(buf);
   if (file)
      fclose(file);
   return ret;
}

static int media_play_wav_pcm(const char *path)
{
   struct unifrog_audio audio;
   FILE *file = NULL;
   uint8_t header[12];
   uint8_t chunk[8];
   unsigned channels = 0;
   unsigned rate = 0;
   unsigned bits = 0;
   unsigned format = 0;
   unsigned block_align = 0;
   unsigned samples_per_block = 0;
   unsigned coeff_count = 0;
   int coeffs[7][2] = {
      { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 },
      { 240, 0 }, { 460, -208 }, { 392, -232 },
   };
   uint32_t data_size = 0;
   long data_pos = -1;
   uint32_t frames;
   uint32_t played = 0;
   int16_t pcm[MEDIA_WAV_CHUNK_FRAMES];
   int ret = -1;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog media wav open failed path=%s\n", path);
      return -1;
   }
   if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
       memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
      goto out;
   while (fread(chunk, 1, sizeof(chunk), file) == sizeof(chunk)) {
      uint32_t size = media_read_le32(chunk + 4);
      long pos = ftell(file);
      long next = pos + (long)((size + 1u) & ~1u);

      if (memcmp(chunk, "fmt ", 4) == 0) {
         uint8_t fmt[40];
         size_t want = size < sizeof(fmt) ? size : sizeof(fmt);

         if (fread(fmt, 1, want, file) != want)
            goto out;
         if (want >= 16u) {
            format = media_read_le16(fmt);
            channels = media_read_le16(fmt + 2);
            rate = media_read_le32(fmt + 4);
            block_align = media_read_le16(fmt + 12);
            bits = media_read_le16(fmt + 14);
            if (format == 2u && want >= 22u) {
               samples_per_block = media_read_le16(fmt + 18);
               coeff_count = media_read_le16(fmt + 20);
               if (coeff_count > ARRAY_SIZE(coeffs))
                  coeff_count = ARRAY_SIZE(coeffs);
               if (want >= 22u + coeff_count * 4u) {
                  for (unsigned c = 0; c < coeff_count; c++) {
                     coeffs[c][0] =
                        (int16_t)media_read_le16(fmt + 22u + c * 4u);
                     coeffs[c][1] =
                        (int16_t)media_read_le16(fmt + 24u + c * 4u);
                  }
               } else {
                  coeff_count = ARRAY_SIZE(coeffs);
               }
            }
         }
      } else if (memcmp(chunk, "data", 4) == 0) {
         data_pos = pos;
         data_size = size;
      }
      if (fseek(file, next, SEEK_SET) != 0)
         break;
      if (format && data_pos >= 0)
         break;
   }
   printf("unifrog media wav probe path=%s format=%u ch=%u rate=%u bits=%u block=%u spb=%u coeffs=%u data=%lu\n",
      path, format, channels, rate, bits, block_align, samples_per_block,
      coeff_count,
      (unsigned long)data_size);
   if ((channels != 1u && channels != 2u) ||
       rate < 8000u || rate > 48000u || data_pos < 0) {
      printf("unifrog media wav unsupported path=%s format=%u ch=%u rate=%u bits=%u\n",
         path, format, channels, rate, bits);
      goto out;
   }
   if ((format == 1u || format == 65534u) &&
       (bits == 8u || bits == 16u || bits == 24u || bits == 32u) &&
       data_size >= channels * ((bits + 7u) / 8u)) {
      frames = data_size / (channels * ((bits + 7u) / 8u));
   } else if (format == 2u && block_align >= (channels == 2u ? 14u : 7u) &&
       samples_per_block >= 2u && coeff_count > 0u &&
       data_size >= block_align) {
      frames = (data_size / block_align) * samples_per_block;
   } else {
      printf("unifrog media wav unsupported path=%s format=%u ch=%u rate=%u bits=%u\n",
         path, format, channels, rate, bits);
      goto out;
   }
   if (fseek(file, data_pos, SEEK_SET) != 0)
      goto out;
   if (unifrog_audio_open(&audio, rate, 1, 512, 8) != 0) {
      printf("unifrog media wav audio_open failed rate=%u path=%s\n",
         rate, path);
      goto out;
   }
   (void)unifrog_audio_set_volume(&audio, MEDIA_AUDIO_VOLUME);
   (void)unifrog_audio_set_mute(&audio, 0);
   (void)unifrog_audio_start(&audio);
   (void)unifrog_audio_set_output_enabled(&audio, 1);
   unifrog_audio_debug_dump(&audio, "wav_after_start");
   if (format == 2u) {
      uint8_t *block = malloc(block_align);

      if (!block)
         goto out;
      while (played < frames &&
          fread(block, 1, block_align, file) == block_align) {
         int got = media_wav_decode_ms_adpcm_block(block, block_align,
            channels, coeffs, coeff_count, pcm,
            frames - played > MEDIA_WAV_CHUNK_FRAMES ?
            MEDIA_WAV_CHUNK_FRAMES : frames - played);

         if (got <= 0)
            break;
         (void)unifrog_audio_write(&audio, pcm, (unsigned)got);
         played += (uint32_t)got;
         if (media_exit_down())
            break;
      }
      free(block);
   } else while (played < frames) {
      unsigned chunk_frames = frames - played;
      unsigned got = 0;

      if (chunk_frames > MEDIA_WAV_CHUNK_FRAMES)
         chunk_frames = MEDIA_WAV_CHUNK_FRAMES;
      while (got < chunk_frames) {
         int32_t left;
         int32_t right;

         if (media_wav_read_pcm_sample(file, bits, &left) != 0)
            break;
         right = left;
         if (channels == 2u &&
             media_wav_read_pcm_sample(file, bits, &right) != 0)
            break;
         pcm[got++] = media_wav_clip_sample((left + right) >> 1);
      }
      if (!got)
         break;
      (void)unifrog_audio_write(&audio, pcm, got);
      played += got;
      if (media_exit_down())
         break;
   }
   printf("unifrog media wav played path=%s frames=%lu/%lu rate=%u\n",
      path, (unsigned long)played, (unsigned long)frames, rate);
   ret = played ? 0 : -1;

out:
   if (audio.fd >= 0)
      unifrog_audio_close(&audio);
   if (file)
      fclose(file);
   return ret;
}

static int media_init_module_logged(const char *name)
{
   int ret = module_init(name);

   printf("unifrog media module_init name=%s ret=%d\n", name, ret);
   return ret;
}

static void media_init_drivers_once(void)
{
   static int initialized;
   int ret = 0;
   static const char *const modules[] = {
      /*
       * HCRTOS media drivers are module-registered. Initializing only the raw
       * driver entry points skips dependencies such as DSC and audio platform
       * modules, which can leave decoders open but unable to allocate buffers.
       */
      "dsc",
      "llav_vdec",
      "vidsink",
      "viddec",
      "apll_dai",
      "avsync",
      "cjc8990_dai",
      "cjc8988_dai",
      "cs4344_dai",
      "pwm_dac_dai",
      "i2s_dai",
      "i2si_platform",
      "i2so_platform",
      "spo_dai",
      "spo_platform",
      "spin_platform",
      "hc16xx_link",
      "auddec",
      "audsink",
   };

   if (initialized)
      return;

   for (size_t i = 0; i < ARRAY_SIZE(modules); i++) {
      int module_ret = media_init_module_logged(modules[i]);

      if (module_ret != 0 && ret == 0)
         ret = module_ret;
   }
   printf("unifrog media module_init done ret=%d modules=%lu padec_bytes=%lu pvdec_bytes=%lu deca_bytes=%lu\n",
      ret, (unsigned long)ARRAY_SIZE(modules),
      (unsigned long)((uintptr_t)&_padec_end - (uintptr_t)&_padec_start),
      (unsigned long)((uintptr_t)&_pvdec_end - (uintptr_t)&_pvdec_start),
      (unsigned long)((uintptr_t)&_deca_audio_stream_struct_end -
         (uintptr_t)&_deca_audio_stream_struct_start));
   (void)unifrog_log_flush();
   initialized = 1;
}

static int media_play_direct_audio(const char *path)
{
   int ret = -1;

   if (media_is_wav_path(path)) {
      ret = media_play_wav_pcm(path);
      if (ret != 0) {
         printf("unifrog media direct wav fallback auddec path=%s\n", path);
         ret = media_play_native_audio_compressed(path);
      }
   } else if (media_is_mp3_path(path)) {
      ret = media_play_mp3_auddec(path);
      if (ret != 0) {
         printf("unifrog media direct mp3 fallback container path=%s\n",
            path);
         ret = media_play_native_audio_compressed(path);
      }
   } else if (media_is_aac_path(path)) {
      ret = media_play_aac_adts_auddec(path);
      if (ret != 0) {
         printf("unifrog media direct aac fallback container path=%s\n",
            path);
         ret = media_play_native_audio_compressed(path);
      }
   } else if (media_is_flac_path(path)) {
      ret = media_play_flac_auddec(path);
      if (ret != 0) {
         printf("unifrog media direct flac fallback container path=%s\n",
            path);
         ret = media_play_native_audio_compressed(path);
      }
   } else if (media_is_ogg_path(path)) {
      ret = media_play_ogg_auddec(path);
      if (ret != 0) {
         printf("unifrog media direct ogg fallback container path=%s\n",
            path);
         ret = media_play_native_audio_compressed(path);
      }
   } else {
      ret = media_play_native_audio_compressed(path);
   }
   if (ret != 0) {
      printf("unifrog media direct fallback ffmpeg path=%s ret=%d\n",
         path ? path : "", ret);
      ret = media_play_ffmpeg_audio(path);
   }
   printf("unifrog media direct audio end ret=%d path=%s\n", ret,
      path ? path : "");
   return ret;
}

int unifrog_media_play_video_ex(const char *path,
   const struct unifrog_media_video_options *options)
{
#if !UNIFROG_ENABLE_HCPLAYER
   int audio_only = media_is_audio_path(path);
   int image_file = media_is_image_path(path);
   int force_native = options && options->force_native;
   int ret;
   size_t old_log_auto_flush;

   if (!path || !path[0])
      return -1;
   media_sd_read_recover_stale("play_start");
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(VIDEO_LOG_AUTO_FLUSH_BYTES);
   printf("unifrog media start stack=native path=%s audio_only=%d image=%d force_native=%d\n",
      path, audio_only, image_file, force_native);
   (void)unifrog_log_flush();
   if (audio_only) {
      if (media_is_wav_path(path)) {
         ret = media_play_wav_pcm(path);
         if (ret != 0) {
            printf("unifrog media wav fallback auddec path=%s\n", path);
            ret = media_play_native_audio_compressed(path);
         }
      } else {
         ret = media_play_native_audio_compressed(path);
         if (ret != 0) {
            printf("unifrog media auddec fallback ffmpeg audio path=%s\n",
               path);
            ret = media_play_ffmpeg_audio(path);
         }
      }
   } else if (image_file) {
      printf("unifrog media native image unsupported_needs_hcplayer path=%s\n",
         path);
      ret = -1;
   } else {
      ret = media_play_native_video(path, options);
   }
   if (ret != 0 && !force_native) {
      printf("unifrog media native fallback_unavailable ret=%d path=%s\n",
         ret, path);
      (void)unifrog_log_flush();
   }
   printf("unifrog media end stack=native ret=%d path=%s\n", ret, path);
   unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
   (void)unifrog_log_flush();
   return ret;
#else
   HCPlayerInitArgs init_args;
   HCPlayerAudioInfo audio_info;
   HCPlayerVideoInfo video_info;
   const struct playback_preset *preset = &playback_presets[0];
   void *player = NULL;
   unsigned exit_hold = 0;
   unsigned monitor_polls = 0;
   unsigned stall_count = 0;
   int audio_output_enabled = 0;
   int audio_only = media_is_audio_path(path);
   int image_file = media_is_image_path(path);
   int force_no_audio = options && options->disable_audio;
   int force_audio = options && options->force_audio;
   int direct_audio_fallback = 0;
   int audio_stream_count = -1;
   int video_stream_count = -1;
   int64_t last_pos = -1;
   size_t old_log_auto_flush;
   int ret = -1;

   if (!path || !path[0])
      return -1;
   media_sd_read_recover_stale("play_start");
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(VIDEO_LOG_AUTO_FLUSH_BYTES);
   printf("unifrog media start path=%s audio_only=%d image=%d\n",
      path, audio_only, image_file);
   (void)unifrog_log_flush();
   media_log_file_probe(path, "play_start");
   media_log_ffmpeg_caps_once();
   if (options && options->preset >= 0 &&
      (unsigned)options->preset < sizeof(playback_presets) / sizeof(playback_presets[0]))
      preset = &playback_presets[options->preset];
   if (audio_only && options && options->force_native) {
      printf("unifrog media audio route=direct reason=explicit_native "
             "force_native=1 force_audio=%d path=%s\n",
         force_audio, path ? path : "");
      ret = media_play_direct_audio(path);
      unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
      (void)unifrog_log_flush();
      return ret;
   }
   if (!audio_only && !image_file && options && options->force_native) {
      printf("unifrog media video route=native reason=explicit_native "
             "force_native=1 disable_audio=%d path=%s\n",
         force_no_audio, path);
      ret = media_play_native_video(path, options);
      printf("unifrog media video route=native ret=%d path=%s\n", ret, path);
      unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
      (void)unifrog_log_flush();
      return ret;
   }
   printf("unifrog media route=hcplayer reason=%s audio_only=%d image=%d "
          "force_audio=%d disable_audio=%d path=%s\n",
      options && options->force_hcplayer ? "requested" : "image",
      audio_only, image_file, force_audio, force_no_audio, path ? path : "");
   media_init_drivers_once();

   memset(&init_args, 0, sizeof(init_args));
   /*
    * HCRTOS media playback is most reliable when hcplayer owns the URI open.
    * The callback reader is kept for future diagnostics, but earlier device
    * testing showed the callback path can break the video/audio handoff.
    */
   init_args.uri = (char *)path;
   init_args.sync_type = preset->sync_type;
   init_args.quick_mode = preset->quick_mode;
   init_args.qm_drop_thresh = preset->qm_drop_thresh;
   init_args.audio_flush_thres = preset->audio_flush_thres;
   init_args.buffering_enable = preset->buffering_enable;
   init_args.buffering_start = 200;
   init_args.buffering_end = 1000;
   init_args.disable_audio = force_no_audio ? true : false;
   init_args.disable_video = audio_only ? true : false;
   init_args.snd_devs = force_no_audio ? 0 : AUDDEV_I2SO;
   init_args.enable_audsink = force_no_audio ? false : true;
   init_args.msg_id = 0;
   init_args.preview_enable = true;
   init_args.src_area.x = 0;
   init_args.src_area.y = 0;
   init_args.src_area.w = VIDEO_SOURCE_W;
   init_args.src_area.h = VIDEO_SOURCE_H;
   init_args.dst_area.x = 0;
   init_args.dst_area.y = 0;
   init_args.dst_area.w = VIDEO_OUTPUT_W;
   init_args.dst_area.h = VIDEO_OUTPUT_H;
   if (image_file) {
      init_args.img_dis_mode = IMG_DIS_SCALE;
      init_args.img_dis_hold_time = 10 * 60 * 1000;
      init_args.gif_dis_interval = 100;
      init_args.img_alpha_mode = ALPHA_BLEND_UNIFORM;
   }

   printf("unifrog media opts source=%s preset=%s sync=%d quick=%d drop=%d "
          "audio_flush=%d buffering=%d cache=%u audio_only=%d image=%d no_audio=%d force_audio=%d\n",
      init_args.uri ? "uri" : "callback",
      preset->name, init_args.sync_type, init_args.quick_mode ? 1 : 0,
      init_args.qm_drop_thresh, init_args.audio_flush_thres,
      init_args.buffering_enable ? 1 : 0,
      (unsigned)MEDIA_AUDIO_KSHM_SIZE, audio_only, image_file,
      force_no_audio, force_audio);
   printf("unifrog media init display preview=%d src=%dx%d dst=%dx%d disable_video=%d disable_audio=%d snd=0x%lx\n",
      init_args.preview_enable ? 1 : 0, init_args.src_area.w,
      init_args.src_area.h, init_args.dst_area.w, init_args.dst_area.h,
      init_args.disable_video ? 1 : 0, init_args.disable_audio ? 1 : 0,
      (unsigned long)init_args.snd_devs);
   (void)unifrog_log_flush();
   printf("unifrog media hcplayer_init begin\n");
   (void)unifrog_log_flush();
   hcplayer_init(LOG_INFO);
   printf("unifrog media hcplayer_init done\n");
   (void)unifrog_log_flush();
   unifrog_audio_set_system_output_enabled(0);
   (void)unifrog_audio_set_system_volume(MEDIA_AUDIO_VOLUME);
   (void)unifrog_audio_set_system_mute(1);
   unifrog_audio_debug_dump(NULL, "media_before_create");
   printf("unifrog media hcplayer_create begin\n");
   (void)unifrog_log_flush();
   player = hcplayer_create(&init_args);
   printf("unifrog media hcplayer_create done player=0x%08lx\n",
      (unsigned long)(uintptr_t)player);
   (void)unifrog_log_flush();
   if (!player) {
      printf("unifrog media create failed path=%s\n", path);
      goto out;
   }
   printf("unifrog media audio config snd_devs=0x%lx audsink=%d\n",
      (unsigned long)init_args.snd_devs, init_args.enable_audsink ? 1 : 0);
   if (!force_no_audio) {
      audio_stream_count = hcplayer_get_audio_streams_count(player);
      printf("unifrog media audio streams count=%d\n", audio_stream_count);
   }
   if (!audio_only) {
      video_stream_count = hcplayer_get_video_streams_count(player);
      printf("unifrog media video streams count=%d\n", video_stream_count);
   }
   if (audio_only && !force_no_audio && audio_stream_count <= 0) {
      printf("unifrog media hcplayer audio unavailable fallback direct count=%d force_audio=%d path=%s\n",
         audio_stream_count, force_audio, path);
      direct_audio_fallback = 1;
      goto out;
   }

   if (!force_no_audio &&
       hcplayer_get_nth_audio_stream_info(player, 0, &audio_info) == 0) {
      audio_output_enabled = 1;
      (void)hcplayer_set_audio_output_dev(player, AUDDEV_I2SO);
      printf("unifrog media stream audio codec=0x%x rate=%d ch=%d\n",
         audio_info.codec_id, audio_info.sample_rate, audio_info.channels);
   } else if (!force_no_audio) {
      printf("unifrog media stream audio unavailable\n");
   }

   memset(&video_info, 0, sizeof(video_info));
   if (!audio_only &&
       hcplayer_get_nth_video_stream_info(player, 0, &video_info) == 0) {
      printf("unifrog media stream video codec=0x%x %dx%d fps=%d\n",
         video_info.codec_id, video_info.width, video_info.height,
         (int)video_info.frame_rate);
      (void)set_video_layer_visible(1, video_info.width, video_info.height,
         VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
      (void)set_player_display_rect(player, video_info.width,
         video_info.height,
         VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
   } else if (!audio_only) {
      printf("unifrog media stream info unavailable\n");
      (void)set_video_layer_visible(1, VIDEO_SOURCE_W, VIDEO_SOURCE_H,
         VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
      (void)set_player_display_rect(player, VIDEO_SOURCE_W, VIDEO_SOURCE_H,
         VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
   } else {
      printf("unifrog media stream video disabled audio_only=1\n");
   }

   if (!audio_only && fb_fd >= 0) {
      int blank_ret = ioctl(fb_fd, FBIOBLANK, FB_BLANK_NORMAL);

      printf("unifrog media fb blank ret=%d errno=%d\n", blank_ret, errno);
   }
   unifrog_audio_debug_dump(NULL, "media_before_play");
   printf("unifrog media hcplayer_play begin\n");
   (void)unifrog_log_flush();
   hcplayer_play(player);
   printf("unifrog media hcplayer_play done\n");
   (void)unifrog_log_flush();
   if (!audio_only) {
      HCPlayerVideoInfo current_video;

      memset(&current_video, 0, sizeof(current_video));
      if (hcplayer_get_cur_video_stream_info(player, &current_video) == 0) {
         printf("unifrog media stream video current codec=0x%x %dx%d fps=%d\n",
            current_video.codec_id, current_video.width,
            current_video.height, (int)current_video.frame_rate);
         (void)set_video_layer_visible(1, current_video.width,
            current_video.height, VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
         (void)set_player_display_rect(player, current_video.width,
            current_video.height, VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
      } else {
         int change_ret = video_stream_count > 0 ?
            hcplayer_change_video_track(player, 0) : -1;

         printf("unifrog media stream video current unavailable change_ret=%d count=%d\n",
            change_ret, video_stream_count);
         if (change_ret == 0) {
            msleep(40);
            if (hcplayer_get_cur_video_stream_info(player,
                &current_video) == 0) {
               printf("unifrog media stream video after_change codec=0x%x %dx%d fps=%d\n",
                  current_video.codec_id, current_video.width,
                  current_video.height, (int)current_video.frame_rate);
               (void)set_video_layer_visible(1, current_video.width,
                  current_video.height, VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
               (void)set_player_display_rect(player, current_video.width,
                  current_video.height, VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
            }
         }
      }
   }
   if (!force_no_audio && !audio_output_enabled) {
      for (unsigned i = 0; i < 5u; i++) {
         msleep(20);
         if (hcplayer_get_nth_audio_stream_info(player, 0, &audio_info) == 0) {
            audio_output_enabled = 1;
            (void)hcplayer_set_audio_output_dev(player, AUDDEV_I2SO);
            printf("unifrog media stream audio after_play codec=0x%x rate=%d ch=%d try=%u\n",
               audio_info.codec_id, audio_info.sample_rate,
               audio_info.channels, i + 1u);
            break;
         }
      }
   }
   if (!force_no_audio && !audio_output_enabled &&
       (audio_only || force_audio)) {
      audio_output_enabled = 1;
      (void)hcplayer_set_audio_output_dev(player, AUDDEV_I2SO);
      printf("unifrog media audio output forced reason=%s\n",
         force_audio ? "force_audio" : "audio_only");
   }
   if (audio_output_enabled) {
      (void)hcplayer_set_audio_output_dev(player, AUDDEV_I2SO);
      (void)unifrog_audio_set_system_volume(MEDIA_AUDIO_VOLUME);
      msleep(60);
      if (force_audio || audio_only || !force_no_audio) {
         unifrog_audio_set_system_output_enabled(1);
         printf("unifrog media audio gate enabled after player start reason=%s\n",
            force_audio ? "force" : (audio_only ? "audio_only" : "stream"));
      } else {
         audio_output_enabled = 0;
         unifrog_audio_set_system_output_enabled(0);
         printf("unifrog media audio gate suppressed reason=no_audio\n");
      }
   } else {
      unifrog_audio_set_system_output_enabled(0);
   }
   unifrog_audio_debug_dump(NULL, "media_after_play");
   printf("unifrog media playing path=%s\n", path);
   (void)unifrog_log_flush();

   for (;;) {
      if (media_exit_down()) {
         if (++exit_hold >= VIDEO_EXIT_HOLD_POLLS) {
            printf("unifrog media exit input held polls=%u\n", exit_hold);
            break;
         }
      } else {
         exit_hold = 0;
      }

      if (!image_file && ++monitor_polls >= VIDEO_MONITOR_POLLS) {
         int64_t pos = hcplayer_get_position(player);
         int64_t dur = hcplayer_get_duration(player);
         int buffering = hcplayer_get_buffering_percent(player);
         HCPlayerVideoInfo monitor_video;
         int video_ret = -1;

         memset(&monitor_video, 0, sizeof(monitor_video));
         if (!audio_only)
            video_ret = hcplayer_get_cur_video_stream_info(player,
               &monitor_video);
         monitor_polls = 0;
         printf("unifrog media monitor pos=%lld dur=%lld buf=%d stall=%u video_ret=%d video=0x%x %dx%d\n",
            pos, dur, buffering, stall_count, video_ret,
            monitor_video.codec_id, monitor_video.width,
            monitor_video.height);
         if (pos < 0 || dur < 0) {
            printf("unifrog media monitor query unsupported pos=%lld dur=%lld\n",
               pos, dur);
            continue;
         }
         if (dur > 0 && pos >= dur - 250)
            break;
         if (pos == last_pos)
            stall_count++;
         else
            stall_count = 0;
         last_pos = pos;
         if (stall_count >= VIDEO_STALL_LIMIT) {
            printf("unifrog media stall stop pos=%lld dur=%lld\n", pos, dur);
            break;
         }
      }
      unifrog_input_poll_with_wireless_divisor(4);
      if (unifrog_input_pressed(UNIFROG_BUTTON_LEFT) ||
          unifrog_input_pressed(UNIFROG_BUTTON_RIGHT)) {
         int64_t pos = hcplayer_get_position(player);
         int64_t dur = hcplayer_get_duration(player);
         int64_t target = pos;

         if (pos >= 0) {
            if (unifrog_input_pressed(UNIFROG_BUTTON_RIGHT))
               target += MEDIA_SEEK_STEP_MS;
            else
               target -= MEDIA_SEEK_STEP_MS;
            if (target < 0)
               target = 0;
            if (dur > 0 && target > dur)
               target = dur;
            printf("unifrog media seek request pos=%lld dur=%lld target=%lld\n",
               pos, dur, target);
            hcplayer_pause2(player);
            {
               int seek_ret = hcplayer_seek(player, target);
               int64_t after;

               msleep(80);
               hcplayer_resume2(player);
               msleep(80);
               after = hcplayer_get_position(player);
               printf("unifrog media seek ret=%d target=%lld after=%lld\n",
                  seek_ret, target, after);
            }
         } else {
            printf("unifrog media seek unsupported pos=%lld dur=%lld\n",
               pos, dur);
         }
      }
      msleep(20);
   }

   ret = 0;

out:
   if (player) {
      unifrog_audio_debug_dump(NULL, "media_before_stop");
      hcplayer_stop2(player, true, false);
      unifrog_audio_debug_dump(NULL, "media_after_stop");
   }
   unifrog_audio_set_system_output_enabled(0);
   close_display();
   if (direct_audio_fallback)
      ret = media_play_direct_audio(path);
   printf("unifrog media end ret=%d path=%s\n", ret, path ? path : "");
   unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
   (void)unifrog_log_flush();
   return ret;
#endif
}

int unifrog_media_play_video(const char *path)
{
   return unifrog_media_play_video_ex(path, NULL);
}
