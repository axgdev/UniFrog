#include <unifrog/media.h>

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
#include <hcuapi/dis.h>
#include <ffplayer.h>

#include <unifrog/abi.h>
#include <unifrog/audio.h>
#include <unifrog/input.h>
#include <unifrog/log.h>
#include <unifrog/runtime.h>
#include <unifrog/text.h>

#define printf unifrog_log

#define VIDEO_STREAM_CACHE_BYTES UNIFROG_APP_STREAM_BUFFER_DEFAULT_BYTES
#define VIDEO_SOURCE_W 1920
#define VIDEO_SOURCE_H 1080
#define VIDEO_OUTPUT_W 1920
#define VIDEO_OUTPUT_H 1080
#define VIDEO_EXIT_HOLD_POLLS 12u
#define VIDEO_MONITOR_POLLS 30u
#define VIDEO_STALL_LIMIT 8u
#define VIDEO_LOG_AUTO_FLUSH_BYTES (64u * 1024u)

struct playback_preset {
   const char *name;
   HCPlayerSyncType sync_type;
   bool quick_mode;
   int qm_drop_thresh;
   int audio_flush_thres;
   bool buffering_enable;
};

static const struct playback_preset playback_presets[] = {
   {"audio loose", HCPLAYER_AUDIO_MASTER, false, 3, 0, false},
   {"stc sync", HCPLAYER_SYNC_STC, false, 1, 0, false},
   {"freerun", HCPLAYER_FREERUN, false, 1, 0, false},
   {"audio quick", HCPLAYER_AUDIO_MASTER, true, 1, 0, false},
   {"video master", HCPLAYER_VIDEO_MASTER, false, 1, 0, false},
   {"stc buffered", HCPLAYER_SYNC_STC, false, 1, 0, true},
   {"audio buffered", HCPLAYER_AUDIO_MASTER, false, 3, 0, true},
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
   };

   return media_has_suffix(path, suffixes,
      sizeof(suffixes) / sizeof(suffixes[0]));
}

static int media_is_image_path(const char *path)
{
   static const char *const suffixes[] = {
      ".jpg", ".jpeg", ".png", ".gif", ".bmp",
   };

   return media_has_suffix(path, suffixes,
      sizeof(suffixes) / sizeof(suffixes[0]));
}

struct media_stream {
   int fd;
   uint8_t *cache;
   off_t size;
   off_t pos;
   off_t cache_start;
   size_t cache_size;
   int active;
};

static struct media_stream video_stream;
static int dis_fd = -1;
static int fb_fd = -1;

static void close_stream(void)
{
   if (video_stream.fd >= 0)
      close(video_stream.fd);
   free(video_stream.cache);
   memset(&video_stream, 0, sizeof(video_stream));
   video_stream.fd = -1;
   video_stream.cache_start = -1;
}

static int open_stream(const char *path)
{
   struct stat st;

   close_stream();
   video_stream.fd = open(path, O_RDONLY);
   if (video_stream.fd < 0) {
      printf("unifrog media stream open failed path=%s\n", path);
      return -1;
   }

   if (fstat(video_stream.fd, &st) != 0 || st.st_size <= 0) {
      printf("unifrog media stream stat failed path=%s\n", path);
      close_stream();
      return -1;
   }

   video_stream.cache = malloc(VIDEO_STREAM_CACHE_BYTES);
   if (!video_stream.cache) {
      printf("unifrog media stream cache malloc failed bytes=%u\n",
         (unsigned)VIDEO_STREAM_CACHE_BYTES);
      close_stream();
      return -1;
   }

   video_stream.size = st.st_size;
   video_stream.pos = 0;
   video_stream.cache_start = -1;
   video_stream.cache_size = 0;
   video_stream.active = 1;
   printf("unifrog media stream open ok size=%lld cache=%u path=%s\n",
      (long long)video_stream.size, (unsigned)VIDEO_STREAM_CACHE_BYTES, path);
   return 0;
}

static int refill_stream(struct media_stream *stream)
{
   ssize_t got;
   size_t wanted;

   if (lseek(stream->fd, stream->pos, SEEK_SET) < 0)
      return -1;

   wanted = VIDEO_STREAM_CACHE_BYTES;
   if ((off_t)wanted > stream->size - stream->pos)
      wanted = (size_t)(stream->size - stream->pos);

   got = read(stream->fd, stream->cache, wanted);
   if (got <= 0)
      return -1;

   stream->cache_start = stream->pos;
   stream->cache_size = (size_t)got;
   printf("unifrog media stream refill pos=%lld bytes=%u\n",
      (long long)stream->cache_start, (unsigned)stream->cache_size);
   return 0;
}

static int stream_read(void *opaque, uint8_t *buf, int bufsize)
{
   struct media_stream *stream = (struct media_stream *)opaque;
   size_t total = 0;

   if (!stream || !stream->active || !buf || bufsize <= 0)
      return 0;

   while (total < (size_t)bufsize && stream->pos < stream->size) {
      off_t cache_end = stream->cache_start + (off_t)stream->cache_size;
      size_t offset;
      size_t available;
      size_t copy_size;

      if (stream->cache_start < 0 || stream->pos < stream->cache_start ||
          stream->pos >= cache_end) {
         if (refill_stream(stream) != 0)
            break;
         cache_end = stream->cache_start + (off_t)stream->cache_size;
      }

      offset = (size_t)(stream->pos - stream->cache_start);
      available = stream->cache_size - offset;
      copy_size = (size_t)bufsize - total;
      if (copy_size > available)
         copy_size = available;

      memcpy(buf + total, stream->cache + offset, copy_size);
      stream->pos += (off_t)copy_size;
      total += copy_size;
   }

   return (int)total;
}

static int64_t stream_seek(void *opaque, int64_t offset, int whence)
{
   struct media_stream *stream = (struct media_stream *)opaque;
   int64_t base;
   int64_t next;

   if (!stream || !stream->active)
      return -1;

   if (whence == SEEK_SET)
      base = 0;
   else if (whence == SEEK_CUR)
      base = (int64_t)stream->pos;
   else if (whence == SEEK_END)
      base = (int64_t)stream->size;
   else
      return -1;

   next = base + offset;
   if (next < 0 || next > (int64_t)stream->size)
      return -1;

   stream->pos = (off_t)next;
   return next;
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

static void set_video_layer_visible(int visible, int src_w, int src_h,
   int dst_w, int dst_h)
{
   struct dis_layer_blend_order order;
   struct dis_zoom zoom;

   open_display();
   if (dis_fd < 0)
      return;

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
   (void)ioctl(dis_fd, DIS_SET_LAYER_ORDER, &order);

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
   (void)ioctl(dis_fd, DIS_SET_ZOOM, &zoom);

   printf("unifrog media layer visible=%d src=%ux%u dst=%ux%u\n",
      visible, zoom.src_area.w, zoom.src_area.h,
      zoom.dst_area.w, zoom.dst_area.h);
}

static void close_display(void)
{
   if (fb_fd >= 0) {
      (void)ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
      close(fb_fd);
      fb_fd = -1;
   }
   if (dis_fd >= 0) {
      set_video_layer_visible(0, 0, 0, 0, 0);
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

static int probe_audio_stream(void *player, const char *tag,
   HCPlayerAudioInfo *audio_info)
{
   int streams;
   int info_ret = -1;

   memset(audio_info, 0, sizeof(*audio_info));
   streams = hcplayer_get_audio_streams_count(player);
   if (streams == 0) {
      printf("unifrog media stream audio disabled tag=%s count=0\n", tag);
      return 0;
   }
   if (streams > 0) {
      info_ret = hcplayer_get_nth_audio_stream_info(player, 0, audio_info);
      if (info_ret == 0) {
         printf("unifrog media stream audio tag=%s count=%d codec=0x%x ch=%d rate=%d depth=%d\n",
            tag, streams, audio_info->codec_id, audio_info->channels,
            audio_info->sample_rate, audio_info->depth);
      } else {
         printf("unifrog media stream audio tag=%s count=%d info_ret=%d\n",
            tag, streams, info_ret);
      }
      return 1;
   }

   info_ret = hcplayer_get_cur_audio_stream_info(player, audio_info);
   if (info_ret == 0) {
      printf("unifrog media stream audio tag=%s current codec=0x%x ch=%d rate=%d depth=%d\n",
         tag, audio_info->codec_id, audio_info->channels,
         audio_info->sample_rate, audio_info->depth);
      return 1;
   }

   printf("unifrog media stream audio unknown tag=%s count=%d\n",
      tag, streams);
   return -1;
}

static void media_init_drivers_once(void)
{
   static int initialized;

   if (initialized)
      return;

   printf("unifrog media driver_init skipped explicit SDK init; hcplayer owns setup\n");
   (void)unifrog_log_flush();
   initialized = 1;
}

int unifrog_media_play_video_ex(const char *path,
   const struct unifrog_media_video_options *options)
{
   HCPlayerInitArgs init_args;
   HCPlayerAudioInfo audio_info;
   HCPlayerVideoInfo video_info;
   const struct playback_preset *preset = &playback_presets[0];
   void *player = NULL;
   unsigned exit_hold = 0;
   unsigned monitor_polls = 0;
   unsigned stall_count = 0;
   int audio_output_enabled = 0;
   int audio_probe = -1;
   int audio_only = media_is_audio_path(path);
   int image_file = media_is_image_path(path);
   int force_no_audio = options && options->disable_audio;
   int64_t last_pos = -1;
   size_t old_log_auto_flush;
   int ret = -1;

   if (!path || !path[0])
      return -1;
   if (options && options->preset >= 0 &&
      (unsigned)options->preset < sizeof(playback_presets) / sizeof(playback_presets[0]))
      preset = &playback_presets[options->preset];

   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(VIDEO_LOG_AUTO_FLUSH_BYTES);
   printf("unifrog media start path=%s audio_only=%d image=%d\n",
      path, audio_only, image_file);
   (void)unifrog_log_flush();
   media_init_drivers_once();

   if (open_stream(path) != 0)
      goto out;

   memset(&init_args, 0, sizeof(init_args));
   init_args.readdata_callback = stream_read;
   init_args.readdata_opaque = &video_stream;
   init_args.seekdata_callback = stream_seek;
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

   printf("unifrog media opts source=stream preset=%s sync=%d quick=%d drop=%d "
          "audio_flush=%d buffering=%d cache=%u audio_only=%d image=%d no_audio=%d\n",
      preset->name, init_args.sync_type, init_args.quick_mode ? 1 : 0,
      init_args.qm_drop_thresh, init_args.audio_flush_thres,
      init_args.buffering_enable ? 1 : 0,
      (unsigned)VIDEO_STREAM_CACHE_BYTES, audio_only, image_file,
      force_no_audio);
   hcplayer_init(LOG_INFO);
   unifrog_audio_set_system_output_enabled(0);
   unifrog_audio_debug_dump(NULL, "media_before_create");
   player = hcplayer_create(&init_args);
   if (!player) {
      printf("unifrog media create failed path=%s\n", path);
      goto out;
   }
   printf("unifrog media audio config snd_devs=0x%lx audsink=%d\n",
      (unsigned long)init_args.snd_devs, init_args.enable_audsink ? 1 : 0);

   audio_probe = force_no_audio ? 0 :
      probe_audio_stream(player, "create", &audio_info);
   if (!force_no_audio && audio_probe > 0) {
      audio_output_enabled = 1;
      (void)hcplayer_set_audio_output_dev(player, AUDDEV_I2SO);
   }

   memset(&video_info, 0, sizeof(video_info));
   if (!audio_only &&
       hcplayer_get_nth_video_stream_info(player, 0, &video_info) == 0) {
      printf("unifrog media stream video codec=0x%x %dx%d fps=%d\n",
         video_info.codec_id, video_info.width, video_info.height,
         (int)video_info.frame_rate);
      set_video_layer_visible(1, video_info.width, video_info.height,
         VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
   } else if (!audio_only) {
      printf("unifrog media stream info unavailable\n");
      set_video_layer_visible(1, VIDEO_SOURCE_W, VIDEO_SOURCE_H,
         VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
   } else {
      printf("unifrog media stream video disabled audio_only=1\n");
   }

   if (!audio_only && fb_fd >= 0)
      (void)ioctl(fb_fd, FBIOBLANK, FB_BLANK_NORMAL);
   unifrog_audio_debug_dump(NULL, "media_before_play");
   hcplayer_play(player);
   if (!force_no_audio && audio_probe < 0) {
      for (unsigned i = 0; i < 5u && audio_probe < 0; i++) {
         msleep(20);
         audio_probe = probe_audio_stream(player, "play", &audio_info);
      }
      if (audio_probe > 0) {
         audio_output_enabled = 1;
         (void)hcplayer_set_audio_output_dev(player, AUDDEV_I2SO);
      } else if (audio_probe < 0 && audio_only) {
         audio_output_enabled = 1;
         (void)hcplayer_set_audio_output_dev(player, AUDDEV_I2SO);
         printf("unifrog media stream audio fallback enabled for audio file after unknown probe\n");
      } else if (audio_probe < 0) {
         printf("unifrog media stream audio remains disabled after unknown probe\n");
      }
   }
   unifrog_audio_set_system_output_enabled(audio_output_enabled);
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

         monitor_polls = 0;
         printf("unifrog media monitor pos=%lld dur=%lld stall=%u\n",
            pos, dur, stall_count);
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
   close_stream();
   printf("unifrog media end ret=%d path=%s\n", ret, path ? path : "");
   unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
   (void)unifrog_log_flush();
   return ret;
}

int unifrog_media_play_video(const char *path)
{
   return unifrog_media_play_video_ex(path, NULL);
}
