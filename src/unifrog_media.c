#include <unifrog/media.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
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
#include <hcuapi/snd.h>
#include <hcuapi/viddec.h>

#ifndef UNIFROG_ENABLE_HCPLAYER
#define UNIFROG_ENABLE_HCPLAYER 0
#endif
#if UNIFROG_ENABLE_HCPLAYER
#include <vendor/ffplayer.h>
#endif
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#ifndef AVSEEK_FORCE
#define AVSEEK_FORCE 0
#endif

#include <unifrog/abi.h>
#include <unifrog/audio.h>
#include <unifrog/diag.h>
#include <unifrog/exception_record.h>
#include <unifrog/fb.h>
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
 * using 320x240 leaves decoded video in a small top-left rectangle, and using
 * the decoded stream size as the source crops lower-resolution video.
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
#define VIDEO_WRITE_SPACE_TIMEOUT_MS 1500u
#define VIDEO_WRITE_SPACE_POLL_US 2000u
#define VIDEO_EOS_TIMEOUT_MS 1000u
#define VIDEO_LOG_AUTO_FLUSH_BYTES (64u * 1024u)
#define MEDIA_AUDIO_WRITE_SPACE_TIMEOUT_MS 5000u
#define MEDIA_AUDIO_PROGRESS_POLL_MS 100u
#define MEDIA_GB300_AUDDEC_ROUTE_VARIANTS 1u
#define MEDIA_GB300_RAW_AUDDEC_ROUTE_VARIANTS 1u
#ifndef UNIFROG_MEDIA_VIDEO_FEED_LEAD_MS
#define UNIFROG_MEDIA_VIDEO_FEED_LEAD_MS 500
#endif
#ifndef UNIFROG_MEDIA_AUDIO_FEED_LEAD_MS
#define UNIFROG_MEDIA_AUDIO_FEED_LEAD_MS 3000
#endif
#ifndef UNIFROG_MEDIA_VIDEO_LOWRES_KSHM_SIZE
#define UNIFROG_MEDIA_VIDEO_LOWRES_KSHM_SIZE 0x00800000u
#endif
#ifndef UNIFROG_MEDIA_FILE_BUFFER_SIZE
#define UNIFROG_MEDIA_FILE_BUFFER_SIZE 65536
#endif
#ifndef UNIFROG_MEDIA_FILE_BUFFER_MIN_SIZE
#define UNIFROG_MEDIA_FILE_BUFFER_MIN_SIZE 16384
#endif
#ifndef UNIFROG_MEDIA_FILE_READAHEAD_SIZE
#define UNIFROG_MEDIA_FILE_READAHEAD_SIZE 2097152
#endif
#ifndef UNIFROG_MEDIA_FILE_READAHEAD_MIN_SIZE
#define UNIFROG_MEDIA_FILE_READAHEAD_MIN_SIZE 524288
#endif
#ifndef UNIFROG_MEDIA_FILE_READAHEAD_SLOTS
#define UNIFROG_MEDIA_FILE_READAHEAD_SLOTS 1
#endif
#ifndef UNIFROG_MEDIA_VIDEO_READAHEAD_SIZE
#define UNIFROG_MEDIA_VIDEO_READAHEAD_SIZE 524288
#endif
#ifndef UNIFROG_MEDIA_VIDEO_READAHEAD_MIN_SIZE
#define UNIFROG_MEDIA_VIDEO_READAHEAD_MIN_SIZE 262144
#endif
#ifndef UNIFROG_MEDIA_VIDEO_READAHEAD_SLOTS
#define UNIFROG_MEDIA_VIDEO_READAHEAD_SLOTS 16
#endif
#ifndef UNIFROG_MEDIA_VIDEO_PREFILL_TARGET_MS
#define UNIFROG_MEDIA_VIDEO_PREFILL_TARGET_MS 5000
#endif
#ifndef UNIFROG_MEDIA_VIDEO_PREFILL_MIN_BYTES
#define UNIFROG_MEDIA_VIDEO_PREFILL_MIN_BYTES 524288
#endif
#ifndef UNIFROG_MEDIA_VIDEO_PREFILL_MAX_BYTES
#define UNIFROG_MEDIA_VIDEO_PREFILL_MAX_BYTES 2097152
#endif
#ifndef UNIFROG_MEDIA_VIDEO_PRELOAD_MAX_BYTES
#define UNIFROG_MEDIA_VIDEO_PRELOAD_MAX_BYTES 0
#endif
#ifndef UNIFROG_MEDIA_AUDIO_MAX_HW_AHEAD_MS
#define UNIFROG_MEDIA_AUDIO_MAX_HW_AHEAD_MS 4000
#endif
#ifndef UNIFROG_MEDIA_VIDEO_MAX_HW_AHEAD_MS
#define UNIFROG_MEDIA_VIDEO_MAX_HW_AHEAD_MS 4000
#endif
#ifndef UNIFROG_MEDIA_SEEK_WARMUP_PACKETS
#define UNIFROG_MEDIA_SEEK_WARMUP_PACKETS 96
#endif
#ifndef UNIFROG_MEDIA_HW_AHEAD_MAX_WAIT_MS
#define UNIFROG_MEDIA_HW_AHEAD_MAX_WAIT_MS 2500
#endif
#ifndef UNIFROG_MEDIA_SEEK_ACCELERATE_FRAMES
#define UNIFROG_MEDIA_SEEK_ACCELERATE_FRAMES 0
#endif
#ifndef UNIFROG_MEDIA_VIDEO_STUCK_BEHIND_MS
#define UNIFROG_MEDIA_VIDEO_STUCK_BEHIND_MS 3000
#endif
#ifndef UNIFROG_MEDIA_FILE_SLOW_READ_LOG_MS
#define UNIFROG_MEDIA_FILE_SLOW_READ_LOG_MS 250
#endif
#ifndef UNIFROG_MEDIA_AUDIO_BUFFERING_START_MS
#define UNIFROG_MEDIA_AUDIO_BUFFERING_START_MS 500
#endif
#ifndef UNIFROG_MEDIA_AUDIO_BUFFERING_END_MS
#define UNIFROG_MEDIA_AUDIO_BUFFERING_END_MS 3000
#endif
#ifndef UNIFROG_MEDIA_VIDEO_BUFFERING_START_MS
#define UNIFROG_MEDIA_VIDEO_BUFFERING_START_MS 500
#endif
#ifndef UNIFROG_MEDIA_VIDEO_BUFFERING_END_MS
#define UNIFROG_MEDIA_VIDEO_BUFFERING_END_MS 3000
#endif
#ifndef UNIFROG_MEDIA_RESET_VIDDEC_ON_FAIL
#define UNIFROG_MEDIA_RESET_VIDDEC_ON_FAIL 1
#endif
#ifndef UNIFROG_MEDIA_GB300_AUDDEC_PROBE_ONCE
#define UNIFROG_MEDIA_GB300_AUDDEC_PROBE_ONCE 0
#endif
/* Decoder rings are live after START; large leads play as startup bursts. */
#define MEDIA_VIDEO_FEED_LEAD_MS ((unsigned)UNIFROG_MEDIA_VIDEO_FEED_LEAD_MS)
#define MEDIA_AUDIO_FEED_LEAD_MS ((unsigned)UNIFROG_MEDIA_AUDIO_FEED_LEAD_MS)
#define MEDIA_VIDEO_MAX_HW_AHEAD_MS \
   ((unsigned)UNIFROG_MEDIA_VIDEO_MAX_HW_AHEAD_MS)
#define MEDIA_AUDIO_MAX_HW_AHEAD_MS \
   ((unsigned)UNIFROG_MEDIA_AUDIO_MAX_HW_AHEAD_MS)
#define MEDIA_HW_AHEAD_POLL_US 10000u
#define MEDIA_HW_AHEAD_LOG_MS 500u
#define MEDIA_HW_AHEAD_LOG_MIN_MS 100u
#define MEDIA_HW_AHEAD_MAX_WAIT_MS \
   ((unsigned)UNIFROG_MEDIA_HW_AHEAD_MAX_WAIT_MS)
#define MEDIA_SEEK_WARMUP_PACKETS ((unsigned)UNIFROG_MEDIA_SEEK_WARMUP_PACKETS)
#define MEDIA_SEEK_ACCELERATE_FRAMES \
   ((int)UNIFROG_MEDIA_SEEK_ACCELERATE_FRAMES)
#define MEDIA_VIDEO_STUCK_BEHIND_MS \
   ((unsigned)UNIFROG_MEDIA_VIDEO_STUCK_BEHIND_MS)
#define MEDIA_AUDIO_PACE_MAX_SLEEP_MS 100u
#define MEDIA_AUDIO_VOLUME 75u
#define MEDIA_WAV_CHUNK_FRAMES 512u
#define MEDIA_FFMPEG_CHUNK_FRAMES 512u
#define MEDIA_VIDEO_AUDIO_PERIOD_BYTES 2048u
#define MEDIA_VIDEO_AUDIO_PERIODS 16u
#define MEDIA_AUDIO_KSHM_SIZE 0x000a0000u
#define MEDIA_GB300_AUDDEC_PROBE_RATE 44100u
#define MEDIA_GB300_AUDDEC_PROBE_CHANNELS 2u
#define MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES 2048u
#define MEDIA_GB300_AUDDEC_PROBE_PACKETS 4u
#define MEDIA_GB300_AUDDEC_PROBE_PAUSE_US 280000u
#define MEDIA_GB300_PRODUCTION_TONE_FRAMES 2048u
#define MEDIA_GB300_PRODUCTION_TONE_PACKETS 12u
#define MEDIA_GB300_I2SO_PRIME_PERIOD_BYTES 3072u
#define MEDIA_GB300_I2SO_PRIME_PERIODS 40u
#define MEDIA_GB300_I2SO_PRIME_START_THRESHOLD 2u
#define MEDIA_GB300_I2SO_PRIME_NONE 0
#define MEDIA_GB300_I2SO_PRIME_BEFORE_INIT 1
#define MEDIA_GB300_I2SO_PRIME_AFTER_START 2
#define MEDIA_VIDEO_KSHM_SIZE 0x01000000u
#define MEDIA_VIDEO_LOWRES_KSHM_SIZE \
   ((unsigned)UNIFROG_MEDIA_VIDEO_LOWRES_KSHM_SIZE)
#define MEDIA_VIDEO_LOWRES_MAX_PIXELS (640u * 360u)
#define MEDIA_FILE_BUFFER_SIZE ((size_t)UNIFROG_MEDIA_FILE_BUFFER_SIZE)
#define MEDIA_FILE_BUFFER_MIN_SIZE \
   ((size_t)UNIFROG_MEDIA_FILE_BUFFER_MIN_SIZE)
#define MEDIA_FILE_READAHEAD_SIZE \
   ((size_t)UNIFROG_MEDIA_FILE_READAHEAD_SIZE)
#define MEDIA_FILE_READAHEAD_MIN_SIZE \
   ((size_t)UNIFROG_MEDIA_FILE_READAHEAD_MIN_SIZE)
#define MEDIA_FILE_READAHEAD_SLOTS \
   ((unsigned)UNIFROG_MEDIA_FILE_READAHEAD_SLOTS)
#define MEDIA_VIDEO_READAHEAD_SIZE \
   ((size_t)UNIFROG_MEDIA_VIDEO_READAHEAD_SIZE)
#define MEDIA_VIDEO_READAHEAD_MIN_SIZE \
   ((size_t)UNIFROG_MEDIA_VIDEO_READAHEAD_MIN_SIZE)
#define MEDIA_VIDEO_READAHEAD_SLOTS \
   ((unsigned)UNIFROG_MEDIA_VIDEO_READAHEAD_SLOTS)
#define MEDIA_VIDEO_PREFILL_TARGET_MS \
   ((unsigned)UNIFROG_MEDIA_VIDEO_PREFILL_TARGET_MS)
#define MEDIA_VIDEO_PREFILL_MIN_BYTES \
   ((size_t)UNIFROG_MEDIA_VIDEO_PREFILL_MIN_BYTES)
#define MEDIA_VIDEO_PREFILL_MAX_BYTES \
   ((size_t)UNIFROG_MEDIA_VIDEO_PREFILL_MAX_BYTES)
#define MEDIA_VIDEO_PRELOAD_MAX_BYTES \
   ((size_t)UNIFROG_MEDIA_VIDEO_PRELOAD_MAX_BYTES)
#define MEDIA_FILE_SLOW_READ_LOG_MS \
   ((unsigned)UNIFROG_MEDIA_FILE_SLOW_READ_LOG_MS)
#define MEDIA_AUDIO_BUFFERING_START_MS \
   ((int)UNIFROG_MEDIA_AUDIO_BUFFERING_START_MS)
#define MEDIA_AUDIO_BUFFERING_END_MS \
   ((int)UNIFROG_MEDIA_AUDIO_BUFFERING_END_MS)
#define MEDIA_VIDEO_BUFFERING_START_MS \
   ((int)UNIFROG_MEDIA_VIDEO_BUFFERING_START_MS)
#define MEDIA_VIDEO_BUFFERING_END_MS \
   ((int)UNIFROG_MEDIA_VIDEO_BUFFERING_END_MS)
#define MEDIA_RESET_VIDDEC_ON_FAIL \
   ((int)UNIFROG_MEDIA_RESET_VIDDEC_ON_FAIL)
#define MEDIA_SW_AUDIO_VIDEO_LEAD_MS 60u
#define MEDIA_SW_AUDIO_MAX_WAIT_MS 160u
#define MEDIA_SW_AUDIO_WAIT_POLL_US 2000u
#define MEDIA_SWVIDEO_DISPLAY_FAIL_LIMIT 3u
#define MEDIA_READAHEAD_MAX_SLOTS 16u
#define MEDIA_PROGRESS_OVERLAY_MIN_MS 500u
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MEDIA_SEEK_STEP_MS 10000
#define MEDIA_SWVIDEO_MMZ_ID 0
#define MEDIA_TIME_UNSET INT32_MIN
#define MEDIA_GB300_AUDDEC_STALL_PACKETS 32u
#define MEDIA_GB300_AUDDEC_STALL_MS 2000u

#if UNIFROG_ENABLE_HCPLAYER
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
#endif

struct media_auddec {
   int fd;
   int prime_fd;
   int stream;
   AVRational time_base;
   uint32_t packets;
   int freerun;
   int output_enabled;
   unsigned write_timeout_ms;
   unsigned aac_profile;
   unsigned aac_sample_rate_index;
   unsigned aac_channels;
};

struct media_audio_pacer {
   int started;
   int64_t base_ms;
   int64_t next_ms;
   uint32_t wall_start_ms;
   unsigned seek_warmup_packets;
   unsigned seek_warmup_total;
};

struct media_controls {
   int exit_down;
   int seek_delta_ms;
   int overlay_toggle;
};

struct media_progress_overlay {
   int hidden;
   uint32_t last_draw_ms;
};

static void media_audio_pacer_wait(struct media_audio_pacer *pacer,
   const AVPacket *packet, AVRational time_base);
static void media_audio_pacer_wait_lead(struct media_audio_pacer *pacer,
   const AVPacket *packet, AVRational time_base, unsigned feed_lead_ms);
static void media_audio_pacer_seek_reset(struct media_audio_pacer *pacer,
   int64_t target_ms);
static int media_wait_hardware_ahead(const char *kind, int fd, int video,
   struct media_audio_pacer *pacer, unsigned max_ahead_ms,
   const char *path);
static int64_t media_format_duration_ms(AVFormatContext *fmt);
static int64_t media_seek_target_ms(int64_t current_ms, int delta_ms,
   int64_t duration_ms);
static int media_seek_format_ms(AVFormatContext *fmt, int64_t target_ms,
   const char *tag, const char *path);
static void media_flush_auddec_for_seek(struct media_auddec *auddec,
   const char *tag, const char *path);
static void media_flush_viddec_for_seek(int video_fd, const char *tag,
   const char *path);
static uint32_t media_audio_frames_to_ms(uint32_t frames, int rate);
static unsigned media_auddec_rotated_variant_index(unsigned order,
   unsigned gb300_count, unsigned offset);
static int media_gb300_auddec_fallback_backend(const char *reason);

struct media_auddec_variant {
   const char *label;
   int force_rate;
   uint32_t snd_devs;
   int enable_audsink;
   int full_stream_fields;
   int audio_flush_thres;
   int kshm_size;
   int gb300_i2so_prime;
   uint32_t prime_dest;
};

struct media_raw_auddec_variant {
   const char *label;
   uint32_t snd_devs;
   int enable_audsink;
   int audio_flush_thres;
   int kshm_size;
   int gb300_i2so_prime;
   uint32_t prime_dest;
};

static int media_auddec_variant_allowed(const char *label)
{
   if (!label)
      return 1;
   if (strncmp(label, "gb300_", 6) == 0)
      return unifrog_audio_prefers_stereo_output();
   if (strncmp(label, "sf2000_", 7) == 0)
      return !unifrog_audio_prefers_stereo_output();
   return 1;
}

static uint32_t media_audio_preferred_snd_devs(void)
{
   if (unifrog_audio_prefers_stereo_output())
      return AUDDEV_I2SO;
   return AUDDEV_I2SO;
}

static unsigned media_gb300_raw_auddec_route_counter;
static unsigned media_gb300_auddec_route_counter;

static unsigned media_auddec_rotated_variant_index(unsigned order,
   unsigned gb300_count, unsigned offset)
{
   if (offset && order < gb300_count)
      return (order + offset) % gb300_count;
   return order;
}

struct media_readahead_slot {
   size_t size;
   int64_t start;
   uint32_t last_used;
};

struct media_buffered_input {
   int fd;
   AVIOContext *avio;
   const char *tag;
   const char *path;
   size_t buffer_size;
   uint8_t *readahead;
   size_t readahead_size;
   size_t readahead_total_size;
   unsigned readahead_slot_count;
   struct media_readahead_slot readahead_slots[MEDIA_READAHEAD_MAX_SLOTS];
   uint32_t readahead_clock;
   int64_t logical_pos;
   int64_t fd_pos;
   int64_t file_size;
   int readahead_enabled;
   int readahead_preload;
   uint64_t read_calls;
   uint64_t read_bytes;
   uint64_t read_requested;
   uint64_t disk_read_calls;
   uint64_t disk_read_bytes;
   uint64_t readahead_hits;
   uint64_t readahead_hit_bytes;
   uint64_t readahead_fills;
   uint64_t readahead_misses;
   uint64_t readahead_evictions;
   uint64_t readahead_seek_hits;
   uint64_t slow_disk_reads;
   uint64_t disk_read_ms_total;
   uint32_t short_reads;
   uint32_t seek_calls;
   int max_request;
   int max_read;
   int max_disk_read;
   uint32_t max_disk_read_ms;
   int64_t max_disk_read_pos;
   int last_errno;
};

extern void *mmz_memalign(int id, size_t alignment, size_t size)
   __attribute__((weak));
extern void mmz_free(int id, void *ptr) __attribute__((weak));
extern size_t mmz_total(int id) __attribute__((weak));

struct media_sw_video {
   void *buffer;
   size_t buffer_size;
   int width;
   int height;
   uint32_t frames;
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
static int vidsink_fd = -1;
static unsigned media_video_debug_packets;
static int media_caps_logged;
static unsigned media_sd_read_depth;
static unsigned media_disk_suspend_depth;
static uint32_t media_disk_suspend_start_ms;
static uint32_t media_video_activity_marker;
static uint32_t media_audio_activity_marker;
static int media_h264_packet_mode;
static int media_h264_nal_length_size;
static int media_h264_extra_delivery;
static int media_pending_seek_delta_ms;
static int media_pending_overlay_toggle;
static int media_controls_wait_release;
static int media_controls_wait_logged;

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
static int media_exit_down(void);
static int media_auddec_send_packet(struct media_auddec *auddec,
   const AVPacket *packet);
static int media_auddec_status_decode_stalled(
   const struct audio_decore_status *status);
static int media_auddec_clock_has_progress(int time_ok, int64_t cur_time);
static int media_auddec_runtime_decode_stalled(
   const struct audio_decore_status *status, int time_ok, int64_t cur_time);
static int media_auddec_status_has_progress(
   const struct audio_decore_status *status);
static void media_auddec_enable_output_on_progress(
   struct media_auddec *auddec, const struct audio_decore_status *status,
   const char *scope, uint32_t packet_index);
static void media_auddec_enable_output_on_clock_progress(
   struct media_auddec *auddec, int64_t cur_time, const char *scope,
   uint32_t packet_index);
static void media_auddec_log_packet_status(struct media_auddec *auddec,
   const char *scope, uint32_t packet_index, const uint8_t *data, size_t size,
   int32_t pts, int32_t dur);
static void media_auddec_finish(struct media_auddec *auddec,
   unsigned timeout_ms);
static void media_auddec_release_fd(int *fdp, const char *tag);
static void media_auddec_close(struct media_auddec *auddec);
static int media_run_gb300_auddec_probe(const char *tag);
static void media_run_gb300_auddec_probe_once(const char *tag);
static int media_video_wait_write_space(int fd, uint32_t need,
   unsigned packet_index);

static unsigned media_audio_output_channels(void)
{
   return unifrog_audio_output_channels();
}

static unsigned media_audio_mix_channels(unsigned output_channels)
{
   if (unifrog_audio_prefers_stereo_output() && output_channels > 1u)
      return 1u;
   return output_channels;
}

static uint64_t media_audio_output_layout(unsigned channels)
{
   return channels > 1u ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
}

static audio_channel_select_t media_audio_channel_select(void)
{
   return media_audio_output_channels() > 1u ? AUDIO_STEREO :
      AUDIO_MONO_LEFT;
}

static uint8_t media_audio_runtime_volume(void)
{
   return unifrog_audio_prefers_stereo_output() ? 90u :
      (uint8_t)MEDIA_AUDIO_VOLUME;
}

static int media_gb300_i2so_prime_open(const char *tag,
   unsigned sample_rate, unsigned channels, unsigned bits, int sync_mode,
   uint32_t pcm_dest)
{
   struct snd_pcm_params params;
   struct snd_hw_info hw;
   snd_pcm_uframes_t avail_min = MEDIA_GB300_I2SO_PRIME_PERIOD_BYTES;
   uint8_t volume = media_audio_runtime_volume();
   int fd;
   int hw_ret;
   int hw_errno;
   int avail_ret;
   int avail_errno;
   int volume_ret;
   int volume_errno;
   int mute_ret;
   int mute_errno;
   int start_ret;
   int start_errno;
   int info_ret;
   int info_errno;

   if (!unifrog_audio_prefers_stereo_output())
      return -1;
   if (sample_rate == 0)
      sample_rate = MEDIA_GB300_AUDDEC_PROBE_RATE;
   if (bits == 0 || bits > 32u)
      bits = 16u;
   channels = media_audio_output_channels();
   media_init_drivers_once();
   fd = open("/dev/sndC0i2so", O_RDWR);
   if (fd < 0) {
      printf("unifrog media gb300_i2so_prime open_fail tag=%s errno=%d rate=%u ch=%u bits=%u sync=%d dest=%lu\n",
         tag ? tag : "?", errno, sample_rate, channels, bits, sync_mode,
         (unsigned long)pcm_dest);
      return -1;
   }

   memset(&params, 0, sizeof(params));
   params.access = SND_PCM_ACCESS_RW_INTERLEAVED;
   params.format = SND_PCM_FORMAT_S16_LE;
   params.sync_mode = (uint8_t)sync_mode;
   params.align = SND_PCM_ALIGN_LEFT;
   params.rate = sample_rate;
   params.channels = channels;
   params.period_size = MEDIA_GB300_I2SO_PRIME_PERIOD_BYTES;
   params.periods = MEDIA_GB300_I2SO_PRIME_PERIODS;
   params.bitdepth = (uint8_t)bits;
   params.start_threshold = MEDIA_GB300_I2SO_PRIME_START_THRESHOLD;
   params.pcm_source = SND_PCM_SOURCE_AUDPAD;
   params.pcm_dest = pcm_dest;

   errno = 0;
   hw_ret = ioctl(fd, SND_IOCTL_HW_PARAMS, &params);
   hw_errno = errno;
   if (hw_ret != 0) {
      printf("unifrog media gb300_i2so_prime hw_fail tag=%s fd=%d ret=%d errno=%d rate=%u ch=%u bits=%u sync=%d period=%lu periods=%lu dest=%lu\n",
         tag ? tag : "?", fd, hw_ret, hw_errno, params.rate,
         params.channels, params.bitdepth, params.sync_mode,
         (unsigned long)params.period_size, (unsigned long)params.periods,
         (unsigned long)pcm_dest);
      close(fd);
      return -1;
   }

   errno = 0;
   avail_ret = ioctl(fd, SND_IOCTL_AVAIL_MIN, &avail_min);
   avail_errno = errno;
   errno = 0;
   volume_ret = ioctl(fd, SND_IOCTL_SET_VOLUME, &volume);
   volume_errno = errno;
   errno = 0;
   mute_ret = ioctl(fd, SND_IOCTL_SET_MUTE, 0);
   mute_errno = errno;
   errno = 0;
   start_ret = ioctl(fd, SND_IOCTL_START, 0);
   start_errno = errno;
   memset(&hw, 0, sizeof(hw));
   errno = 0;
   info_ret = ioctl(fd, SND_IOCTL_GET_HW_INFO, &hw);
   info_errno = errno;
   printf("unifrog media gb300_i2so_prime open tag=%s fd=%d hw=%d hw_errno=%d avail=%d avail_errno=%d avail_min=%lu volume=%d volume_errno=%d mute=%d mute_errno=%d start=%d start_errno=%d info=%d info_errno=%d rate=%u ch=%u bits=%u sync=%d period=%lu periods=%lu dest=%lu dma=0x%08lx/%lu hw_period=%lu hw_periods=%lu\n",
      tag ? tag : "?", fd, hw_ret, hw_errno, avail_ret, avail_errno,
      (unsigned long)avail_min, volume_ret, volume_errno, mute_ret,
      mute_errno, start_ret, start_errno, info_ret, info_errno, params.rate,
      params.channels, params.bitdepth, params.sync_mode,
      (unsigned long)params.period_size, (unsigned long)params.periods,
      (unsigned long)pcm_dest, (unsigned long)hw.dma_addr,
      (unsigned long)hw.dma_size,
      (unsigned long)hw.pcm_params.period_size,
      (unsigned long)hw.pcm_params.periods);
   if (start_ret != 0) {
      (void)ioctl(fd, SND_IOCTL_DROP, 0);
      (void)ioctl(fd, SND_IOCTL_HW_FREE, 0);
      close(fd);
      return -1;
   }
   return fd;
}

static void media_gb300_i2so_prime_close(int *fdp, const char *tag)
{
   int fd;
   int mute_ret;
   int mute_errno;
   int drop_ret;
   int drop_errno;
   int free_ret;
   int free_errno;

   if (!fdp || *fdp < 0)
      return;
   fd = *fdp;
   *fdp = -1;
   errno = 0;
   mute_ret = ioctl(fd, SND_IOCTL_SET_MUTE, 1);
   mute_errno = errno;
   errno = 0;
   drop_ret = ioctl(fd, SND_IOCTL_DROP, 0);
   drop_errno = errno;
   errno = 0;
   free_ret = ioctl(fd, SND_IOCTL_HW_FREE, 0);
   free_errno = errno;
   close(fd);
   printf("unifrog media gb300_i2so_prime close tag=%s fd=%d mute_ret=%d mute_errno=%d drop=%d drop_errno=%d free=%d free_errno=%d\n",
      tag ? tag : "?", fd, mute_ret, mute_errno, drop_ret, drop_errno,
      free_ret, free_errno);
}

static void media_expand_mono_to_output(const int16_t *mono, int16_t *output,
   unsigned frames, unsigned channels)
{
   if (!mono || !output || frames == 0 || channels <= 1u)
      return;
   for (unsigned i = 0; i < frames; i++) {
      output[i * channels] = mono[i];
      for (unsigned ch = 1; ch < channels; ch++)
         output[i * channels + ch] = mono[i];
   }
}

static void media_expand_mono_to_output_inplace(int16_t *buffer,
   unsigned frames, unsigned channels)
{
   if (!buffer || frames == 0 || channels <= 1u)
      return;
   for (unsigned i = frames; i > 0; i--) {
      int16_t sample = buffer[i - 1u];
      unsigned out = (i - 1u) * channels;

      buffer[out] = sample;
      for (unsigned ch = 1; ch < channels; ch++)
         buffer[out + ch] = sample;
   }
}

static int media_audio_write_mono_output(struct unifrog_audio *audio,
   const int16_t *mono, int16_t *scratch, unsigned frames,
   unsigned channels)
{
   if (!audio || !mono || frames == 0)
      return -1;
   if (channels <= 1u)
      return unifrog_audio_write(audio, mono, frames);
   if (!scratch)
      return -1;
   media_expand_mono_to_output(mono, scratch, frames, channels);
   return unifrog_audio_write(audio, scratch, frames);
}

static void media_log_pcm_stats(const char *scope,
   const struct unifrog_audio *audio, const int16_t *pcm, unsigned frames,
   const char *path)
{
   static unsigned log_count;
   static unsigned nonzero_log_count;
   unsigned channels;
   unsigned samples;
   unsigned nonzero = 0;
   unsigned abs_max = 0;
   unsigned left_nonzero = 0;
   unsigned right_nonzero = 0;
   unsigned left_abs_max = 0;
   unsigned right_abs_max = 0;
   int min = 0;
   int max = 0;
   int first_left = 0;
   int first_right = 0;
   uint64_t abs_sum = 0;
   int should_log;

   if (!audio || !pcm || frames == 0)
      return;
   channels = audio->channels ? audio->channels : 1u;
   samples = frames * channels;
   first_left = pcm[0];
   first_right = channels > 1u ? pcm[1] : 0;
   for (unsigned i = 0; i < samples; i++) {
      int sample = pcm[i];
      unsigned abs_value;
      unsigned channel = channels ? i % channels : 0u;

      if (i == 0 || sample < min)
         min = sample;
      if (i == 0 || sample > max)
         max = sample;
      abs_value = sample < 0 ? (unsigned)-sample : (unsigned)sample;
      if (abs_value > 4u)
         nonzero++;
      if (abs_value > abs_max)
         abs_max = abs_value;
      if (channel == 0u) {
         if (abs_value > 4u)
            left_nonzero++;
         if (abs_value > left_abs_max)
            left_abs_max = abs_value;
      } else if (channel == 1u) {
         if (abs_value > 4u)
            right_nonzero++;
         if (abs_value > right_abs_max)
            right_abs_max = abs_value;
      }
      abs_sum += abs_value;
   }
   should_log = log_count < 16u;
   if (nonzero > 0 && nonzero_log_count < 16u) {
      should_log = 1;
      nonzero_log_count++;
   }
   if (!should_log)
      return;
   log_count++;
   printf("unifrog media pcm_stats scope=%s idx=%u backend=%d fd=%d frames=%u ch=%u nonzero=%u/%u l_nonzero=%u r_nonzero=%u min=%d max=%d abs_max=%u l_abs_max=%u r_abs_max=%u abs_avg=%lu first_l=%d first_r=%d path=%s\n",
      scope ? scope : "?", log_count, audio->backend, audio->fd, frames,
      channels, nonzero, samples, left_nonzero, right_nonzero, min, max,
      abs_max, left_abs_max, right_abs_max,
      samples ? (unsigned long)(abs_sum / samples) : 0ul,
      first_left, first_right, path ? path : "");
}

static size_t media_mmz_total0(void)
{
   return mmz_total ? mmz_total(MEDIA_SWVIDEO_MMZ_ID) : 0u;
}

static uint32_t media_video_activity_mark_value(void)
{
   if (media_video_activity_marker == 0)
      media_video_activity_marker =
         unifrog_exception_activity_hash("native_video");
   return media_video_activity_marker;
}

static void media_video_activity_stage(uint32_t stage, uint32_t detail0,
   uint32_t detail1)
{
   uint32_t packed = ((stage & 0xffu) << 24) | (detail0 & 0x00ffffffu);

   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_MEDIA_VIDEO,
      media_video_activity_mark_value(), packed, detail1);
}

static uint32_t media_audio_activity_mark_value(void)
{
   if (media_audio_activity_marker == 0)
      media_audio_activity_marker =
         unifrog_exception_activity_hash("native_audio");
   return media_audio_activity_marker;
}

static void media_audio_activity_stage(uint32_t stage, uint32_t detail0,
   uint32_t detail1)
{
   uint32_t packed = ((stage & 0xffu) << 24) | (detail0 & 0x00ffffffu);

   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_MEDIA_AUDIO,
      media_audio_activity_mark_value(), packed, detail1);
}

static void media_video_progress(
   const struct unifrog_media_video_options *options, const char *stage,
   uint64_t done, uint64_t total)
{
   if (!options || !options->progress)
      return;
   if (done > UINT_MAX)
      done = UINT_MAX;
   if (total > UINT_MAX)
      total = UINT_MAX;
   options->progress(options->progress_userdata, stage ? stage : "",
      (unsigned)done, (unsigned)total);
}

static void media_disk_suspend_begin(const char *tag, const char *path)
{
   if (media_disk_suspend_depth++ == 0) {
      media_disk_suspend_start_ms = unifrog_perf_time_ms();
      printf("unifrog media disk_suspend begin tag=%s path=%s pending=%lu\n",
         tag ? tag : "", path ? path : "",
         (unsigned long)unifrog_log_pending());
      unifrog_diag_memory_snapshot(tag ? tag : "media.disk_suspend_begin");
      unifrog_log_set_disk_suspended(1);
      /*
       * Suspend log disk writes before any explicit flush attempt.
       * In unstable SD windows a blocking flush here can deadlock launch.
       */
      (void)unifrog_log_flush();
      printf("unifrog media disk_suspend active tag=%s path=%s\n",
         tag ? tag : "", path ? path : "");
   } else {
      printf("unifrog media disk_suspend nested depth=%u tag=%s path=%s\n",
         media_disk_suspend_depth, tag ? tag : "", path ? path : "");
   }
}

static void media_disk_suspend_end(const char *tag, const char *path)
{
   if (media_disk_suspend_depth == 0)
      return;
   media_disk_suspend_depth--;
   if (media_disk_suspend_depth == 0) {
      uint32_t now = unifrog_perf_time_ms();
      uint32_t elapsed_ms = now - media_disk_suspend_start_ms;

      printf("unifrog media disk_suspend end tag=%s path=%s pending=%lu\n",
         tag ? tag : "", path ? path : "",
         (unsigned long)unifrog_log_pending());
      printf("unifrog media disk_suspend elapsed tag=%s path=%s ms=%lu\n",
         tag ? tag : "", path ? path : "", (unsigned long)elapsed_ms);
      unifrog_diag_memory_snapshot(tag ? tag : "media.disk_suspend_end");
      unifrog_log_set_disk_suspended(0);
   } else {
      printf("unifrog media disk_suspend nested_end depth=%u tag=%s path=%s\n",
         media_disk_suspend_depth, tag ? tag : "", path ? path : "");
   }
}

static void media_sd_read_begin(const char *tag, const char *path)
{
   if (media_sd_read_depth++ == 0) {
      printf("unifrog media sd_read begin tag=%s path=%s pending=%lu\n",
         tag ? tag : "", path ? path : "",
         (unsigned long)unifrog_log_pending());
      media_disk_suspend_begin(tag ? tag : "media.sd_read_begin", path);
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
      media_disk_suspend_end(tag ? tag : "media.sd_read_end", path);
   } else {
      printf("unifrog media sd_read nested_end depth=%u tag=%s path=%s\n",
         media_sd_read_depth, tag ? tag : "", path ? path : "");
   }
}

static void media_sd_read_recover_stale(const char *tag)
{
   if (media_sd_read_depth == 0 && media_disk_suspend_depth == 0)
      return;
   printf("unifrog media sd_read recover_stale tag=%s read_depth=%u disk_depth=%u pending=%lu\n",
      tag ? tag : "", media_sd_read_depth, media_disk_suspend_depth,
      (unsigned long)unifrog_log_pending());
   media_sd_read_depth = 0;
   media_disk_suspend_depth = 0;
   unifrog_log_set_disk_suspended(0);
}

static uint8_t *media_alloc_file_buffer(size_t *size_out)
{
   size_t size = MEDIA_FILE_BUFFER_SIZE;
   size_t min_size = MEDIA_FILE_BUFFER_MIN_SIZE;
   uint8_t *buffer = NULL;

   if (size_out)
      *size_out = 0;
   if (size < min_size)
      min_size = size;
   while (size >= min_size && size > 0) {
      buffer = av_malloc(size);
      if (buffer) {
         if (size_out)
            *size_out = size;
         return buffer;
      }
      size /= 2u;
   }
   return NULL;
}

static uint8_t *media_alloc_readahead_buffer(size_t *slot_size_out,
   unsigned *slot_count_out, size_t want, size_t min_size,
   unsigned want_slots)
{
   size_t size = want;
   unsigned slots = want_slots;
   uint8_t *buffer = NULL;

   if (slot_size_out)
      *slot_size_out = 0;
   if (slot_count_out)
      *slot_count_out = 0;
   if (size == 0)
      return NULL;
   if (size < min_size)
      min_size = size;
   if (slots == 0)
      slots = 1;
   if (slots > MEDIA_READAHEAD_MAX_SLOTS)
      slots = MEDIA_READAHEAD_MAX_SLOTS;
   while (slots > 0) {
      size = want;
      while (size >= min_size && size > 0) {
         size_t total = size * (size_t)slots;

         if (total / size != (size_t)slots)
            break;
         buffer = av_malloc(total);
         if (buffer) {
            if (slot_size_out)
               *slot_size_out = size;
            if (slot_count_out)
               *slot_count_out = slots;
            return buffer;
         }
         size /= 2u;
      }
      slots /= 2u;
   }
   return NULL;
}

static int media_buffered_tag_is_video(const char *tag)
{
   return tag && strstr(tag, "video") != NULL;
}

static void media_buffered_record_disk_read(struct media_buffered_input *input,
   int64_t pos, ssize_t got, uint32_t elapsed_ms, size_t want)
{
   if (!input)
      return;
   input->disk_read_calls++;
   input->disk_read_ms_total += elapsed_ms;
   if (got > input->max_disk_read)
      input->max_disk_read = (int)got;
   if (elapsed_ms > input->max_disk_read_ms) {
      input->max_disk_read_ms = elapsed_ms;
      input->max_disk_read_pos = pos;
   }
   if (elapsed_ms >= MEDIA_FILE_SLOW_READ_LOG_MS) {
      input->slow_disk_reads++;
      printf("unifrog media buffered_io slow_read tag=%s ms=%lu pos=%lld want=%lu got=%ld disk_reads=%llu seeks=%lu path=%s\n",
         input->tag ? input->tag : "", (unsigned long)elapsed_ms,
         (long long)pos, (unsigned long)want, (long)got,
         (unsigned long long)input->disk_read_calls,
         (unsigned long)input->seek_calls,
         input->path ? input->path : "");
   }
}

static void media_buffered_input_invalidate_cache(
   struct media_buffered_input *input)
{
   if (!input)
      return;
   for (unsigned i = 0; i < MEDIA_READAHEAD_MAX_SLOTS; i++) {
      input->readahead_slots[i].size = 0;
      input->readahead_slots[i].start = 0;
      input->readahead_slots[i].last_used = 0;
   }
   input->readahead_clock = 0;
}

static uint8_t *media_buffered_readahead_slot_data(
   const struct media_buffered_input *input, unsigned slot)
{
   if (!input || !input->readahead || input->readahead_size == 0 ||
       slot >= input->readahead_slot_count)
      return NULL;
   return input->readahead + (size_t)slot * input->readahead_size;
}

static int media_buffered_readahead_find_slot(
   const struct media_buffered_input *input, int64_t pos, int allow_end)
{
   if (!input || pos < 0)
      return -1;
   for (unsigned i = 0; i < input->readahead_slot_count; i++) {
      const struct media_readahead_slot *slot = &input->readahead_slots[i];
      int64_t end;

      if (slot->size == 0)
         continue;
      end = slot->start + (int64_t)slot->size;
      if (pos >= slot->start && (pos < end || (allow_end && pos == end)))
         return (int)i;
   }
   return -1;
}

static unsigned media_buffered_readahead_choose_slot(
   struct media_buffered_input *input)
{
   unsigned best = 0;
   uint32_t best_used = UINT32_MAX;

   if (!input || input->readahead_slot_count == 0)
      return 0;
   for (unsigned i = 0; i < input->readahead_slot_count; i++) {
      if (input->readahead_slots[i].size == 0)
         return i;
      if (input->readahead_slots[i].last_used < best_used) {
         best = i;
         best_used = input->readahead_slots[i].last_used;
      }
   }
   input->readahead_evictions++;
   return best;
}

static void media_buffered_readahead_touch(struct media_buffered_input *input,
   unsigned slot)
{
   if (!input || slot >= input->readahead_slot_count)
      return;
   input->readahead_clock++;
   if (input->readahead_clock == 0) {
      for (unsigned i = 0; i < input->readahead_slot_count; i++)
         input->readahead_slots[i].last_used = 0;
      input->readahead_clock = 1;
   }
   input->readahead_slots[slot].last_used = input->readahead_clock;
}

static int media_buffered_sync_fd(struct media_buffered_input *input)
{
   off_t pos;

   if (!input || input->fd < 0)
      return AVERROR(EINVAL);
   if (input->fd_pos == input->logical_pos)
      return 0;
   errno = 0;
   pos = lseek(input->fd, (off_t)input->logical_pos, SEEK_SET);
   if (pos < 0) {
      input->last_errno = errno;
      return AVERROR(errno ? errno : EIO);
   }
   input->fd_pos = (int64_t)pos;
   return 0;
}

static int media_buffered_read_direct(struct media_buffered_input *input,
   uint8_t *buf, int buf_size)
{
   int ret;
   ssize_t got;
   int64_t pos;
   uint32_t start_ms;
   uint32_t elapsed_ms;

   ret = media_buffered_sync_fd(input);
   if (ret < 0)
      return ret;
   pos = input->logical_pos;
   start_ms = unifrog_perf_time_ms();
   errno = 0;
   got = read(input->fd, buf, (size_t)buf_size);
   elapsed_ms = unifrog_perf_time_ms() - start_ms;
   media_buffered_record_disk_read(input, pos, got, elapsed_ms,
      (size_t)buf_size);
   if (got < 0) {
      input->last_errno = errno;
      return AVERROR(errno ? errno : EIO);
   }
   if (got == 0)
      return AVERROR_EOF;
   input->disk_read_bytes += (uint64_t)got;
   input->logical_pos += (int64_t)got;
   input->fd_pos += (int64_t)got;
   return (int)got;
}

static int media_buffered_fill_readahead(struct media_buffered_input *input)
{
   int ret;
   ssize_t got;
   int64_t pos;
   uint32_t start_ms;
   uint32_t elapsed_ms;
   unsigned slot_index;
   uint8_t *slot_data;

   if (!input || !input->readahead || input->readahead_size == 0 ||
       input->readahead_slot_count == 0)
      return AVERROR(EINVAL);
   slot_index = media_buffered_readahead_choose_slot(input);
   slot_data = media_buffered_readahead_slot_data(input, slot_index);
   if (!slot_data)
      return AVERROR(EINVAL);
   ret = media_buffered_sync_fd(input);
   if (ret < 0)
      return ret;
   pos = input->logical_pos;
   start_ms = unifrog_perf_time_ms();
   errno = 0;
   got = read(input->fd, slot_data, input->readahead_size);
   elapsed_ms = unifrog_perf_time_ms() - start_ms;
   media_buffered_record_disk_read(input, pos, got, elapsed_ms,
      input->readahead_size);
   input->readahead_slots[slot_index].start = input->logical_pos;
   input->readahead_slots[slot_index].size = 0;
   if (got < 0) {
      input->last_errno = errno;
      return AVERROR(errno ? errno : EIO);
   }
   if (got == 0)
      return AVERROR_EOF;
   input->disk_read_bytes += (uint64_t)got;
   input->fd_pos += (int64_t)got;
   input->readahead_slots[slot_index].size = (size_t)got;
   media_buffered_readahead_touch(input, slot_index);
   input->readahead_fills++;
   return 0;
}

static int media_buffered_fill_readahead_at(
   struct media_buffered_input *input, int64_t pos, size_t *got_out)
{
   int64_t saved_logical;
   int slot_index;
   int ret;

   if (got_out)
      *got_out = 0;
   if (!input || pos < 0)
      return AVERROR(EINVAL);
   slot_index = media_buffered_readahead_find_slot(input, pos, 0);
   if (slot_index >= 0) {
      struct media_readahead_slot *slot =
         &input->readahead_slots[(unsigned)slot_index];
      int64_t end = slot->start + (int64_t)slot->size;

      media_buffered_readahead_touch(input, (unsigned)slot_index);
      if (got_out && end > pos)
         *got_out = (size_t)(end - pos);
      return 0;
   }

   saved_logical = input->logical_pos;
   input->logical_pos = pos;
   ret = media_buffered_fill_readahead(input);
   input->logical_pos = saved_logical;
   if (ret < 0)
      return ret;
   slot_index = media_buffered_readahead_find_slot(input, pos, 0);
   if (slot_index >= 0 && got_out)
      *got_out = input->readahead_slots[(unsigned)slot_index].size;
   return 0;
}

static size_t media_buffered_prefill_readahead(
   struct media_buffered_input *input, int64_t start, size_t target_bytes,
   const struct unifrog_media_video_options *options, const char *stage,
   const char *path)
{
   uint64_t before_disk_bytes;
   uint64_t before_disk_ms;
   uint64_t disk_bytes;
   uint64_t disk_ms;
   uint64_t kib_s = 0;
   size_t done = 0;
   int64_t pos = start;

   if (!input || !input->readahead_enabled || target_bytes == 0 || pos < 0)
      return 0;
   before_disk_bytes = input->disk_read_bytes;
   before_disk_ms = input->disk_read_ms_total;
   media_video_progress(options, stage, 0, target_bytes);
   while (!media_exit_down() && done < target_bytes) {
      size_t got = 0;
      size_t count;
      int ret;

      if (input->file_size >= 0 && pos >= input->file_size)
         break;
      ret = media_buffered_fill_readahead_at(input, pos, &got);
      if (ret < 0 || got == 0)
         break;
      count = got;
      if (count > target_bytes - done)
         count = target_bytes - done;
      done += count;
      pos += (int64_t)got;
      media_video_progress(options, stage, done, target_bytes);
   }
   disk_bytes = input->disk_read_bytes - before_disk_bytes;
   disk_ms = input->disk_read_ms_total - before_disk_ms;
   if (disk_ms > 0)
      kib_s = (disk_bytes * 1000ull) / (disk_ms * 1024ull);
   printf("unifrog media buffered_io prefill tag=%s stage=%s start=%lld target=%lu cached=%lu disk_bytes=%llu disk_ms=%llu kib_s=%llu file=%lld path=%s\n",
      input->tag ? input->tag : "", stage ? stage : "",
      (long long)start, (unsigned long)target_bytes, (unsigned long)done,
      (unsigned long long)disk_bytes, (unsigned long long)disk_ms,
      (unsigned long long)kib_s, (long long)input->file_size,
      path ? path : "");
   return done;
}

static int media_buffered_read(void *opaque, uint8_t *buf, int buf_size)
{
   struct media_buffered_input *input = opaque;
   int total = 0;

   if (!input || input->fd < 0 || !buf || buf_size <= 0)
      return AVERROR(EINVAL);
   input->read_calls++;
   input->read_requested += (uint64_t)buf_size;
   if (buf_size > input->max_request)
      input->max_request = buf_size;
   if (!input->readahead_enabled || !input->readahead ||
       input->readahead_size == 0 || input->readahead_slot_count == 0) {
      int got = media_buffered_read_direct(input, buf, buf_size);

      if (got < 0)
         return got;
      input->read_bytes += (uint64_t)got;
      if (got > input->max_read)
         input->max_read = got;
      if (got < buf_size)
         input->short_reads++;
      return got;
   }

   while (total < buf_size) {
      int slot_index;
      struct media_readahead_slot *slot;
      int64_t cache_end;
      size_t available = 0;
      size_t to_copy;

      slot_index = media_buffered_readahead_find_slot(input,
         input->logical_pos, 0);
      if (slot_index >= 0) {
         slot = &input->readahead_slots[(unsigned)slot_index];
         cache_end = slot->start + (int64_t)slot->size;
         available = (size_t)(cache_end - input->logical_pos);
      } else {
         int fill_ret;

         input->readahead_misses++;
         fill_ret = media_buffered_fill_readahead(input);
         if (fill_ret < 0) {
            if (total > 0)
               break;
            return fill_ret;
         }
         continue;
      }

      to_copy = (size_t)(buf_size - total);
      if (to_copy > available)
         to_copy = available;
      memcpy(buf + total, media_buffered_readahead_slot_data(input,
            (unsigned)slot_index) +
         (size_t)(input->logical_pos - slot->start), to_copy);
      input->logical_pos += (int64_t)to_copy;
      total += (int)to_copy;
      media_buffered_readahead_touch(input, (unsigned)slot_index);
      input->readahead_hits++;
      input->readahead_hit_bytes += (uint64_t)to_copy;
   }

   if (total <= 0)
      return AVERROR_EOF;
   input->read_bytes += (uint64_t)total;
   if (total > input->max_read)
      input->max_read = total;
   if (total < buf_size)
      input->short_reads++;
   return total;
}

static int64_t media_buffered_seek(void *opaque, int64_t offset, int whence)
{
   struct media_buffered_input *input = opaque;
   off_t pos;
   int seek_whence;
   int64_t target;

   if (!input || input->fd < 0)
      return AVERROR(EINVAL);
   if (whence == AVSEEK_SIZE)
      return input->file_size >= 0 ? input->file_size : AVERROR(ENOSYS);
   seek_whence = whence & ~AVSEEK_FORCE;
   if (seek_whence != SEEK_SET && seek_whence != SEEK_CUR &&
       seek_whence != SEEK_END)
      return AVERROR(EINVAL);
   input->seek_calls++;
   if (seek_whence == SEEK_SET) {
      target = offset;
   } else if (seek_whence == SEEK_CUR) {
      target = input->logical_pos + offset;
   } else if (input->file_size >= 0) {
      target = input->file_size + offset;
   } else {
      errno = 0;
      pos = lseek(input->fd, (off_t)offset, seek_whence);
      if (pos < 0) {
         input->last_errno = errno;
         return AVERROR(errno ? errno : EIO);
      }
      input->logical_pos = (int64_t)pos;
      input->fd_pos = (int64_t)pos;
      return input->logical_pos;
   }
   if (target < 0)
      return AVERROR(EINVAL);
   {
      int slot_index = media_buffered_readahead_find_slot(input, target, 1);

      if (slot_index >= 0) {
         media_buffered_readahead_touch(input, (unsigned)slot_index);
         input->readahead_seek_hits++;
         input->logical_pos = target;
         return input->logical_pos;
      }
   }
   input->logical_pos = target;
   if (input->readahead_enabled) {
      /*
       * Keep older windows alive across MP4 demux seeks. The physical lseek is
       * deferred until a cache miss actually needs to read from this target.
       */
      return input->logical_pos;
   }
   errno = 0;
   pos = lseek(input->fd, (off_t)target, SEEK_SET);
   if (pos < 0) {
      input->last_errno = errno;
      return AVERROR(errno ? errno : EIO);
   }
   input->fd_pos = (int64_t)pos;
   input->logical_pos = (int64_t)pos;
   return input->logical_pos;
}

static size_t media_buffered_readahead_choose_size(size_t want,
   size_t min_size, unsigned slots)
{
   size_t min_total;

   if (want == 0)
      return 0;
   if (slots == 0)
      slots = 1;
   if (slots > MEDIA_READAHEAD_MAX_SLOTS)
      slots = MEDIA_READAHEAD_MAX_SLOTS;
   min_total = min_size * (size_t)slots;
   if (min_size != 0 && min_total / min_size != (size_t)slots)
      return want;
   if (want < min_size)
      want = min_size;
   return want;
}

static uint64_t media_buffered_readahead_cover_ms(size_t size,
   int64_t bit_rate)
{
   if (size == 0 || bit_rate <= 0)
      return 0;
   return ((uint64_t)size * 8000ull) / (uint64_t)bit_rate;
}

static size_t media_video_startup_prefill_bytes(
   const struct media_buffered_input *input, const AVFormatContext *fmt)
{
   uint64_t by_time = 0;
   size_t target = MEDIA_VIDEO_PREFILL_MIN_BYTES;
   int64_t bit_rate = fmt ? fmt->bit_rate : 0;

   if (!input || MEDIA_VIDEO_PREFILL_MAX_BYTES == 0)
      return 0;
   if (MEDIA_VIDEO_PREFILL_TARGET_MS > 0 && bit_rate > 0)
      by_time = ((uint64_t)bit_rate *
         (uint64_t)MEDIA_VIDEO_PREFILL_TARGET_MS + 7999ull) / 8000ull;
   if (by_time > (uint64_t)target)
      target = by_time > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)by_time;
   if (target > MEDIA_VIDEO_PREFILL_MAX_BYTES)
      target = MEDIA_VIDEO_PREFILL_MAX_BYTES;
   if (target > input->readahead_total_size)
      target = input->readahead_total_size;
   if (input->file_size >= 0 && target > (size_t)input->file_size)
      target = (size_t)input->file_size;
   if (input->readahead_size > 0 && target > 0) {
      size_t rounded = ((target + input->readahead_size - 1u) /
         input->readahead_size) * input->readahead_size;

      if (rounded > target && rounded <= input->readahead_total_size)
         target = rounded;
   }
   if (input->file_size >= 0 && target > (size_t)input->file_size)
      target = (size_t)input->file_size;
   return target;
}

static int media_buffered_readahead_mode_values(const char *tag,
   size_t *want_out, size_t *min_out, unsigned *slots_out)
{
   int video = media_buffered_tag_is_video(tag);

   if (want_out)
      *want_out = video ? MEDIA_VIDEO_READAHEAD_SIZE :
         MEDIA_FILE_READAHEAD_SIZE;
   if (min_out)
      *min_out = video ? MEDIA_VIDEO_READAHEAD_MIN_SIZE :
         MEDIA_FILE_READAHEAD_MIN_SIZE;
   if (slots_out)
      *slots_out = video ? MEDIA_VIDEO_READAHEAD_SLOTS :
         MEDIA_FILE_READAHEAD_SLOTS;
   return video;
}

static int media_buffered_readahead_sane_config(size_t *want,
   size_t *min_size, unsigned *slots)
{
   if (!want || !min_size || !slots)
      return 0;
   if (*want == 0)
      return 0;
   if (*slots == 0)
      *slots = 1;
   if (*slots > MEDIA_READAHEAD_MAX_SLOTS)
      *slots = MEDIA_READAHEAD_MAX_SLOTS;
   *want = media_buffered_readahead_choose_size(*want, *min_size, *slots);
   if (*min_size == 0 || *min_size > *want)
      *min_size = *want;
   return 1;
}

static int media_buffered_input_enable_readahead(
   struct media_buffered_input *input, const AVFormatContext *fmt,
   const char *tag, const char *path)
{
   uint64_t cover_ms = 0;
   uint64_t total_cover_ms = 0;
   int64_t bit_rate = fmt ? fmt->bit_rate : 0;
   size_t want;
   size_t min_size;
   unsigned slots;
   int video = media_buffered_readahead_mode_values(tag, &want, &min_size,
      &slots);
   size_t normal_want;
   size_t normal_min_size;
   unsigned normal_slots;
   int preload = 0;

   if (!input || input->fd < 0)
      return -1;
   if (input->readahead_enabled)
      return 0;
   if (!media_buffered_readahead_sane_config(&want, &min_size, &slots)) {
      printf("unifrog media buffered_io readahead disabled tag=%s mode=%s path=%s\n",
         tag ? tag : "", video ? "video" : "audio", path ? path : "");
      return -1;
   }
   normal_want = want;
   normal_min_size = min_size;
   normal_slots = slots;
   if (video && MEDIA_VIDEO_PRELOAD_MAX_BYTES > 0 && input->file_size > 0 &&
       (uint64_t)input->file_size <= (uint64_t)MEDIA_VIDEO_PRELOAD_MAX_BYTES) {
      want = (size_t)input->file_size;
      min_size = want;
      slots = 1;
      preload = 1;
   }
   input->readahead = media_alloc_readahead_buffer(&input->readahead_size,
      &input->readahead_slot_count, want, min_size, slots);
   if (!input->readahead && preload) {
      printf("unifrog media buffered_io preload alloc_failed tag=%s path=%s want=%lu fallback_want=%lu fallback_slots=%u\n",
         tag ? tag : "", path ? path : "", (unsigned long)want,
         (unsigned long)normal_want, normal_slots);
      want = normal_want;
      min_size = normal_min_size;
      slots = normal_slots;
      preload = 0;
      input->readahead = media_alloc_readahead_buffer(&input->readahead_size,
         &input->readahead_slot_count, want, min_size, slots);
   }
   if (!input->readahead) {
      printf("unifrog media buffered_io readahead alloc_failed tag=%s mode=%s path=%s want=%lu min=%lu slots=%u\n",
         tag ? tag : "", video ? "video" : "audio", path ? path : "",
         (unsigned long)want, (unsigned long)min_size, slots);
      return -1;
   }
   input->readahead_total_size =
      input->readahead_size * (size_t)input->readahead_slot_count;
   input->readahead_enabled = 1;
   input->readahead_preload = preload;
   media_buffered_input_invalidate_cache(input);
   cover_ms = media_buffered_readahead_cover_ms(input->readahead_size,
      bit_rate);
   total_cover_ms = media_buffered_readahead_cover_ms(
      input->readahead_total_size, bit_rate);
   printf("unifrog media buffered_io readahead enabled tag=%s mode=%s preload=%d slot=%lu total=%lu slots=%u want=%lu min=%lu bitrate=%lld cover_ms=%llu total_cover_ms=%llu logical=%lld fd_pos=%lld path=%s\n",
      tag ? tag : "", video ? "video" : "audio",
      preload, (unsigned long)input->readahead_size,
      (unsigned long)input->readahead_total_size,
      input->readahead_slot_count, (unsigned long)want,
      (unsigned long)min_size, (long long)bit_rate,
      (unsigned long long)cover_ms, (unsigned long long)total_cover_ms,
      (long long)input->logical_pos, (long long)input->fd_pos,
      path ? path : "");
   return 0;
}

static void media_buffered_input_enable_video_readahead(
   struct media_buffered_input *input, const AVFormatContext *fmt,
   const struct unifrog_media_video_options *options, const char *path)
{
   if (!input || input->readahead_enabled)
      return;
   (void)media_buffered_input_enable_readahead(input, fmt, "native_video",
      path);
   if (!input->readahead_enabled)
      return;
   if (input->readahead_preload && input->file_size > 0) {
      (void)media_buffered_prefill_readahead(input, 0,
         (size_t)input->file_size, options, "preload", path);
   } else {
      size_t prefill = media_video_startup_prefill_bytes(input, fmt);

      if (prefill > 0)
         (void)media_buffered_prefill_readahead(input, input->logical_pos,
            prefill, options, "buffering", path);
   }
}

static int media_buffered_input_open(AVFormatContext **fmt_out,
   struct media_buffered_input *input, const char *path, const char *tag)
{
   AVFormatContext *fmt = NULL;
   uint8_t *buffer = NULL;
   size_t buffer_size = 0;
   struct stat st;
   int ret;

   if (fmt_out)
      *fmt_out = NULL;
   if (!fmt_out || !input || !path)
      return AVERROR(EINVAL);
   memset(input, 0, sizeof(*input));
   input->fd = -1;
   input->tag = tag;
   input->path = path;
   input->file_size = -1;
   input->logical_pos = 0;
   input->fd_pos = 0;
   printf("unifrog media buffered_io stage=open_begin tag=%s path=%s\n",
      tag ? tag : "", path);
   errno = 0;
   input->fd = open(path, O_RDONLY);
   if (input->fd < 0) {
      input->last_errno = errno;
      printf("unifrog media buffered_io open_failed tag=%s path=%s errno=%d\n",
         tag ? tag : "", path, errno);
      return AVERROR(errno ? errno : EIO);
   }
   printf("unifrog media buffered_io stage=open_done tag=%s fd=%d path=%s\n",
      tag ? tag : "", input->fd, path);
   memset(&st, 0, sizeof(st));
   if (fstat(input->fd, &st) == 0)
      input->file_size = (int64_t)st.st_size;
   printf("unifrog media buffered_io stage=fstat_done tag=%s fd=%d file=%lld path=%s\n",
      tag ? tag : "", input->fd, (long long)input->file_size, path);
   printf("unifrog media buffered_io stage=buffer_alloc_begin tag=%s want=%lu min=%lu path=%s\n",
      tag ? tag : "", (unsigned long)MEDIA_FILE_BUFFER_SIZE,
      (unsigned long)MEDIA_FILE_BUFFER_MIN_SIZE, path);
   buffer = media_alloc_file_buffer(&buffer_size);
   if (!buffer) {
      printf("unifrog media buffered_io alloc_failed tag=%s path=%s want=%lu min=%lu\n",
         tag ? tag : "", path, (unsigned long)MEDIA_FILE_BUFFER_SIZE,
         (unsigned long)MEDIA_FILE_BUFFER_MIN_SIZE);
      close(input->fd);
      input->fd = -1;
      return AVERROR(ENOMEM);
   }
   printf("unifrog media buffered_io stage=buffer_alloc_done tag=%s buffer=%lu path=%s\n",
      tag ? tag : "", (unsigned long)buffer_size, path);
   input->buffer_size = buffer_size;
   printf("unifrog media buffered_io stage=avio_alloc_begin tag=%s path=%s\n",
      tag ? tag : "", path);
   input->avio = avio_alloc_context(buffer, (int)buffer_size, 0, input,
      media_buffered_read, NULL, media_buffered_seek);
   if (!input->avio) {
      av_freep(&buffer);
      close(input->fd);
      input->fd = -1;
      return AVERROR(ENOMEM);
   }
   printf("unifrog media buffered_io stage=avio_alloc_done tag=%s avio=0x%08lx path=%s\n",
      tag ? tag : "", (unsigned long)(uintptr_t)input->avio, path);
   printf("unifrog media buffered_io stage=format_alloc_begin tag=%s path=%s\n",
      tag ? tag : "", path);
   fmt = avformat_alloc_context();
   if (!fmt) {
      av_freep(&input->avio->buffer);
      avio_context_free(&input->avio);
      close(input->fd);
      input->fd = -1;
      return AVERROR(ENOMEM);
   }
   printf("unifrog media buffered_io stage=format_alloc_done tag=%s fmt=0x%08lx path=%s\n",
      tag ? tag : "", (unsigned long)(uintptr_t)fmt, path);
   fmt->pb = input->avio;
   printf("unifrog media buffered_io stage=avformat_open_begin tag=%s fmt=0x%08lx path=%s\n",
      tag ? tag : "", (unsigned long)(uintptr_t)fmt, path);
   ret = avformat_open_input(&fmt, path, NULL, NULL);
   printf("unifrog media buffered_io open tag=%s ret=%d fd=%d buffer=%lu file=%lld reads=%llu bytes=%llu path=%s\n",
      tag ? tag : "", ret, input->fd, (unsigned long)input->buffer_size,
      (long long)input->file_size, (unsigned long long)input->read_calls,
      (unsigned long long)input->read_bytes, path);
   if (ret < 0) {
      if (fmt)
         avformat_close_input(&fmt);
      return ret;
   }
   *fmt_out = fmt;
   return 0;
}

static void media_buffered_input_log_coverage(
   const struct media_buffered_input *input, const AVFormatContext *fmt,
   const char *tag, const char *path)
{
   uint64_t cover_ms = 0;
   int64_t bit_rate = fmt ? fmt->bit_rate : 0;

   if (!input || input->buffer_size == 0)
      return;
   if (bit_rate > 0)
      cover_ms = ((uint64_t)input->buffer_size * 8000ull) / (uint64_t)bit_rate;
   printf("unifrog media buffered_io coverage tag=%s io_chunk=%lu min=%lu bitrate=%lld chunk_cover_ms=%llu duration_us=%lld path=%s\n",
      tag ? tag : "", (unsigned long)input->buffer_size,
      (unsigned long)MEDIA_FILE_BUFFER_MIN_SIZE, (long long)bit_rate,
      (unsigned long long)cover_ms, fmt ? (long long)fmt->duration : -1ll,
      path ? path : "");
}

static void media_buffered_input_close(struct media_buffered_input *input,
   const char *tag, const char *path)
{
   if (!input || (input->fd < 0 && !input->avio))
      return;
   printf("unifrog media buffered_io close tag=%s fd=%d buffer=%lu readahead=%lu slot=%lu slots=%u preload=%d file=%lld reads=%llu bytes=%llu requested=%llu disk_reads=%llu disk_bytes=%llu hits=%llu hit_bytes=%llu misses=%llu fills=%llu evict=%llu seek_hits=%llu slow=%llu disk_ms=%llu max_disk_ms=%lu max_disk_pos=%lld max_req=%d max_read=%d max_disk=%d short=%lu seeks=%lu logical=%lld fd_pos=%lld errno=%d path=%s\n",
      tag ? tag : "", input->fd, (unsigned long)input->buffer_size,
      (unsigned long)input->readahead_total_size,
      (unsigned long)input->readahead_size, input->readahead_slot_count,
      input->readahead_preload, (long long)input->file_size,
      (unsigned long long)input->read_calls,
      (unsigned long long)input->read_bytes,
      (unsigned long long)input->read_requested,
      (unsigned long long)input->disk_read_calls,
      (unsigned long long)input->disk_read_bytes,
      (unsigned long long)input->readahead_hits,
      (unsigned long long)input->readahead_hit_bytes,
      (unsigned long long)input->readahead_misses,
      (unsigned long long)input->readahead_fills,
      (unsigned long long)input->readahead_evictions,
      (unsigned long long)input->readahead_seek_hits,
      (unsigned long long)input->slow_disk_reads,
      (unsigned long long)input->disk_read_ms_total,
      (unsigned long)input->max_disk_read_ms,
      (long long)input->max_disk_read_pos, input->max_request,
      input->max_read, input->max_disk_read,
      (unsigned long)input->short_reads, (unsigned long)input->seek_calls,
      (long long)input->logical_pos, (long long)input->fd_pos,
      input->last_errno, path ? path : "");
   if (input->avio) {
      av_freep(&input->avio->buffer);
      avio_context_free(&input->avio);
   }
   av_freep(&input->readahead);
   if (input->fd >= 0)
      close(input->fd);
   memset(input, 0, sizeof(*input));
   input->fd = -1;
   input->file_size = -1;
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
   AVStream **stream_list;
   unsigned stream_count;

   if (!fmt)
      return;
   stream_count = fmt->nb_streams;
   stream_list = fmt->streams;
   printf("unifrog media format tag=%s path=%s streams=%lu stream_list=0x%08lx duration=%lld bitrate=%lld\n",
      tag ? tag : "", path ? path : "", (unsigned long)stream_count,
      (unsigned long)(uintptr_t)stream_list, (long long)fmt->duration,
      (long long)fmt->bit_rate);
   (void)unifrog_log_flush();
   if (!stream_list) {
      printf("unifrog media stream_list missing tag=%s path=%s streams=%lu\n",
         tag ? tag : "", path ? path : "", (unsigned long)stream_count);
      (void)unifrog_log_flush();
      return;
   }
   if (stream_count > 32u) {
      printf("unifrog media stream_list clamp tag=%s streams=%lu max=32\n",
         tag ? tag : "", (unsigned long)stream_count);
      stream_count = 32u;
      (void)unifrog_log_flush();
   }
   for (unsigned i = 0; i < stream_count; i++) {
      AVStream *stream;
      AVCodecParameters *par;

      printf("unifrog media stream_probe begin tag=%s idx=%u slot=0x%08lx\n",
         tag ? tag : "", i, (unsigned long)(uintptr_t)&stream_list[i]);
      (void)unifrog_log_flush();
      stream = stream_list[i];
      printf("unifrog media stream_probe stream tag=%s idx=%u stream=0x%08lx\n",
         tag ? tag : "", i, (unsigned long)(uintptr_t)stream);
      (void)unifrog_log_flush();
      par = stream ? stream->codecpar : NULL;
      printf("unifrog media stream_probe par tag=%s idx=%u par=0x%08lx\n",
         tag ? tag : "", i, (unsigned long)(uintptr_t)par);
      (void)unifrog_log_flush();

      if (!stream || !par) {
         printf("unifrog media stream tag=%s idx=%u missing stream=%d par=%d\n",
            tag ? tag : "", i, stream ? 1 : 0, par ? 1 : 0);
         (void)unifrog_log_flush();
         continue;
      }
      printf("unifrog media stream tag=%s idx=%u type=%d codec=%d tag=0x%lx %dx%d rate=%d ch=%d bits=%d extra=%d tb=%d/%d\n",
         tag ? tag : "", i, par->codec_type, par->codec_id,
         (unsigned long)par->codec_tag, par->width, par->height,
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
   printf("unifrog media ffmpeg abi hdr_avformat=%lu lib_avformat=%lu hdr_avcodec=%lu lib_avcodec=%lu hdr_avutil=%lu lib_avutil=%lu sizeof_packet=%lu sizeof_stream=%lu off_stream_codecpar=%lu\n",
      (unsigned long)LIBAVFORMAT_VERSION_INT,
      (unsigned long)avformat_version(),
      (unsigned long)LIBAVCODEC_VERSION_INT,
      (unsigned long)avcodec_version(),
      (unsigned long)LIBAVUTIL_VERSION_INT,
      (unsigned long)avutil_version(),
      (unsigned long)sizeof(AVPacket),
      (unsigned long)sizeof(AVStream),
      (unsigned long)offsetof(AVStream, codecpar));
}

static void open_display_controller(void)
{
   if (dis_fd < 0) {
      errno = 0;
      dis_fd = open("/dev/dis", O_RDWR);
      printf("unifrog media dis open %s fd=%d errno=%d\n",
         dis_fd >= 0 ? "ok" : "failed", dis_fd, errno);
   }
}

static void open_display(void)
{
   open_display_controller();
   if (fb_fd < 0) {
      errno = 0;
      fb_fd = open("/dev/fb0", O_RDWR);
      printf("unifrog media fb open %s fd=%d errno=%d\n",
         fb_fd >= 0 ? "ok" : "failed", fb_fd, errno);
   }
}

static void open_video_sink(void)
{
   if (vidsink_fd < 0) {
      errno = 0;
      vidsink_fd = open("/dev/vidsink", O_WRONLY);
      printf("unifrog media vidsink open %s fd=%d errno=%d\n",
         vidsink_fd >= 0 ? "ok" : "failed", vidsink_fd, errno);
   }
}

static int set_video_layer_visible(int visible, int src_w, int src_h,
   int dst_w, int dst_h)
{
   struct dis_layer_blend_order order;
   struct dis_zoom zoom;
   int order_ret;
   int zoom_ret;

   open_display_controller();
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

#if UNIFROG_ENABLE_HCPLAYER
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
#endif

static void media_set_aspect_mode(dis_tv_mode_e ratio, dis_mode_e mode)
{
   dis_aspect_mode_t aspect;
   int ret;

   open_display_controller();
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
   if (vidsink_fd >= 0) {
      close(vidsink_fd);
      vidsink_fd = -1;
   }
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

static uint16_t media_blend_rgb565(uint16_t dst, uint16_t src,
   unsigned alpha)
{
   unsigned inv;
   unsigned dr;
   unsigned dg;
   unsigned db;
   unsigned sr;
   unsigned sg;
   unsigned sb;
   unsigned r;
   unsigned g;
   unsigned b;

   if (alpha >= 255u)
      return src;
   if (alpha == 0u)
      return dst;
   inv = 255u - alpha;
   dr = (dst >> 11) & 0x1fu;
   dg = (dst >> 5) & 0x3fu;
   db = dst & 0x1fu;
   sr = (src >> 11) & 0x1fu;
   sg = (src >> 5) & 0x3fu;
   sb = src & 0x1fu;
   r = (dr * inv + sr * alpha) / 255u;
   g = (dg * inv + sg * alpha) / 255u;
   b = (db * inv + sb * alpha) / 255u;
   return (uint16_t)((r << 11) | (g << 5) | b);
}

static void media_fb_blend_rect(struct unifrog_fb *fb, unsigned x,
   unsigned y, unsigned w, unsigned h, uint16_t color, unsigned alpha)
{
   if (!fb || !fb->pixels || fb->bpp != 16 || x >= fb->width ||
       y >= fb->height || !w || !h)
      return;
   if (x + w > fb->width)
      w = fb->width - x;
   if (y + h > fb->height)
      h = fb->height - y;
   for (unsigned row = 0; row < h; row++) {
      uint16_t *dst = fb->pixels + (size_t)(y + row) * fb->stride_pixels + x;

      for (unsigned col = 0; col < w; col++)
         dst[col] = media_blend_rgb565(dst[col], color, alpha);
   }
}

static void media_clear_graphics_black(const char *tag, const char *path)
{
   struct unifrog_fb fb;
   uint32_t start_ms;
   int ret;

   memset(&fb, 0, sizeof(fb));
   fb.fd = -1;
   start_ms = unifrog_perf_time_ms();
   ret = unifrog_fb_open(&fb, UNIFROG_FB_OPEN_DEFAULT);
   printf("unifrog media fb_clear tag=%s ret=%d ms=%lu path=%s\n",
      tag ? tag : "", ret,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      path ? path : "");
   if (ret == 0)
      unifrog_fb_close(&fb);
}

static int media_overlay_open_fb(struct unifrog_fb *fb, const char *tag,
   int64_t pos_ms, int64_t dur_ms, const char *path)
{
   int ret;

   if (!fb)
      return -1;
   memset(fb, 0, sizeof(*fb));
   fb->fd = -1;
   ret = unifrog_fb_open(fb, UNIFROG_FB_OPEN_PRESERVE);
   if (ret != 0 || fb->bpp != 16) {
      printf("unifrog media overlay tag=%s ret=%d bpp=%u pos=%lld dur=%lld path=%s\n",
         tag ? tag : "", ret, fb->bpp, (long long)pos_ms,
         (long long)dur_ms, path ? path : "");
      if (ret == 0)
         unifrog_fb_close(fb);
      return -1;
   }
   return 0;
}

static void media_overlay_geometry(const struct unifrog_fb *fb,
   unsigned *bar_x, unsigned *bar_y, unsigned *bar_w)
{
   unsigned x = 0;
   unsigned y = 0;
   unsigned w = 0;

   if (fb) {
      x = fb->width > 300u ? (fb->width - 300u) / 2u : 8u;
      w = fb->width > 300u ? 300u :
         (fb->width > 16u ? fb->width - 16u : fb->width);
      y = fb->height > 14u ? fb->height - 12u : 0u;
   }
   if (bar_x)
      *bar_x = x;
   if (bar_y)
      *bar_y = y;
   if (bar_w)
      *bar_w = w;
}

static void media_clear_progress_overlay(struct media_progress_overlay *overlay,
   const char *tag, const char *path)
{
   struct unifrog_fb fb;
   unsigned bar_x;
   unsigned bar_y;
   unsigned bar_w;

   if (media_overlay_open_fb(&fb, tag, 0, 0, path) != 0)
      return;
   media_overlay_geometry(&fb, &bar_x, &bar_y, &bar_w);
   media_fb_blend_rect(&fb, bar_x, bar_y, bar_w, 7u, 0x0000, 255u);
   unifrog_fb_flush(&fb);
   unifrog_fb_close(&fb);
   if (overlay)
      overlay->last_draw_ms = 0;
   printf("unifrog media overlay hidden tag=%s path=%s\n",
      tag ? tag : "", path ? path : "");
}

static void media_draw_progress_overlay(struct media_progress_overlay *overlay,
   const char *tag, int64_t pos_ms, int64_t dur_ms, int force,
   const char *path)
{
   struct unifrog_fb fb;
   uint32_t now;
   unsigned bar_x;
   unsigned bar_y;
   unsigned bar_w;
   unsigned fill_w;

   if (!overlay || overlay->hidden || dur_ms <= 0)
      return;
   if (pos_ms < 0)
      pos_ms = 0;
   if (pos_ms > dur_ms)
      pos_ms = dur_ms;
   now = unifrog_perf_time_ms();
   if (!force && overlay->last_draw_ms &&
       now - overlay->last_draw_ms < MEDIA_PROGRESS_OVERLAY_MIN_MS)
      return;
   overlay->last_draw_ms = now;
   if (media_overlay_open_fb(&fb, tag, pos_ms, dur_ms, path) != 0)
      return;
   media_overlay_geometry(&fb, &bar_x, &bar_y, &bar_w);
   fill_w = (unsigned)(((uint64_t)bar_w * (uint64_t)pos_ms) /
      (uint64_t)dur_ms);
   if (fill_w > bar_w)
      fill_w = bar_w;
   media_fb_blend_rect(&fb, bar_x, bar_y, bar_w, 5u, 0x0000, 170u);
   media_fb_blend_rect(&fb, bar_x, bar_y, fill_w, 5u, 0x07e0, 210u);
   media_fb_blend_rect(&fb, bar_x, bar_y + 6u, bar_w, 1u, 0xffff, 70u);
   unifrog_fb_flush(&fb);
   unifrog_fb_close(&fb);
   if (force)
      printf("unifrog media overlay tag=%s pos=%lld dur=%lld fill=%u/%u path=%s\n",
         tag ? tag : "", (long long)pos_ms, (long long)dur_ms, fill_w,
         bar_w, path ? path : "");
}

static void media_toggle_progress_overlay(struct media_progress_overlay *overlay,
   const char *tag, int64_t pos_ms, int64_t dur_ms, const char *path)
{
   if (!overlay)
      return;
   overlay->hidden = !overlay->hidden;
   if (overlay->hidden) {
      media_clear_progress_overlay(overlay, tag, path);
      return;
   }
   printf("unifrog media overlay shown tag=%s path=%s\n",
      tag ? tag : "", path ? path : "");
   media_draw_progress_overlay(overlay, tag, pos_ms, dur_ms, 1, path);
}

static int media_native_video_reveal_if_ready(int video_fd, int *revealed,
   unsigned long *frames_decoded, unsigned long *frames_displayed,
   const char *path)
{
   struct vdec_decore_status status;

   if (!revealed || *revealed || video_fd < 0)
      return revealed ? *revealed : 0;
   memset(&status, 0, sizeof(status));
   if (ioctl(video_fd, VIDDEC_GET_STATUS, &status) != 0)
      return 0;
   if (frames_decoded)
      *frames_decoded = status.frames_decoded;
   if (frames_displayed)
      *frames_displayed = status.frames_displayed;
   if (!status.first_pic_decoded && !status.first_pic_showed &&
       status.frames_decoded == 0 && status.frames_displayed == 0)
      return 0;
   media_set_aspect_mode(DIS_TV_16_9, DIS_PILLBOX);
   (void)set_video_layer_visible(1, VIDEO_SOURCE_W, VIDEO_SOURCE_H,
      VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
   *revealed = 1;
   printf("unifrog media native video reveal decoded=%lu displayed=%lu hdr=%d pic=%d show=%d path=%s\n",
      (unsigned long)status.frames_decoded,
      (unsigned long)status.frames_displayed, status.first_header_got,
      status.first_pic_decoded, status.first_pic_showed, path ? path : "");
   return 1;
}

static void media_swvideo_close(struct media_sw_video *video)
{
   if (!video)
      return;
   if (video->buffer && mmz_free)
      mmz_free(MEDIA_SWVIDEO_MMZ_ID, video->buffer);
   video->buffer = NULL;
   video->buffer_size = 0;
   video->width = 0;
   video->height = 0;
   video->frames = 0;
}

static int media_swvideo_open_sink(struct media_sw_video *video, int src_w,
   int src_h)
{
   dis_win_onoff_t win;
   int win_ret = -1;

   if (!video)
      return -1;
   open_display();
   open_video_sink();
   if (dis_fd < 0 || vidsink_fd < 0)
      return -1;
   memset(&win, 0, sizeof(win));
   win.distype = DIS_TYPE_HD;
   win.layer = DIS_LAYER_MAIN;
   win.on = true;
   errno = 0;
   win_ret = ioctl(dis_fd, DIS_SET_WIN_ONOFF, &win);
   printf("unifrog media swvideo sink win_ret=%d win_errno=%d vidsink=%d src=%dx%d\n",
      win_ret, errno, vidsink_fd, src_w, src_h);
   (void)set_video_layer_visible(1, src_w, src_h, VIDEO_OUTPUT_W,
      VIDEO_OUTPUT_H);
   return win_ret == 0 ? 0 : -1;
}

static int media_swvideo_prepare(struct media_sw_video *video, int width,
   int height)
{
   size_t y_size;
   size_t uv_size;
   size_t need;

   if (!video || width <= 0 || height <= 0)
      return -1;
   y_size = (size_t)width * (size_t)height;
   uv_size = ((size_t)width + 1u) / 2u * (((size_t)height + 1u) / 2u);
   need = y_size + uv_size * 2u;
   if (need == 0)
      return -1;
   if (video->buffer && video->buffer_size >= need &&
       video->width == width && video->height == height)
      return 0;
   media_swvideo_close(video);
   if (!mmz_memalign) {
      printf("unifrog media swvideo mmz missing width=%d height=%d bytes=%lu\n",
         width, height, (unsigned long)need);
      return -1;
   }
   video->buffer = mmz_memalign(MEDIA_SWVIDEO_MMZ_ID, 32, need);
   if (!video->buffer) {
      printf("unifrog media swvideo mmz alloc failed width=%d height=%d bytes=%lu\n",
         width, height, (unsigned long)need);
      return -1;
   }
   video->buffer_size = need;
   video->width = width;
   video->height = height;
   memset(video->buffer, 0, need);
   printf("unifrog media swvideo buffer ptr=%p phys=0x%08lx width=%d height=%d bytes=%lu\n",
      video->buffer, (unsigned long)unifrog_perf_phys_addr(video->buffer),
      width, height, (unsigned long)need);
   return 0;
}

static int media_swvideo_copy_yuv420(struct media_sw_video *video,
   const AVFrame *frame)
{
   uint8_t *y_dst;
   uint8_t *u_dst;
   uint8_t *v_dst;
   size_t y_size;
   size_t cw;
   size_t ch;
   size_t uv_size;

   if (!video || !video->buffer || !frame)
      return -1;
   if (frame->format != AV_PIX_FMT_YUV420P &&
       frame->format != AV_PIX_FMT_YUVJ420P)
      return -1;
   y_size = (size_t)frame->width * (size_t)frame->height;
   cw = ((size_t)frame->width + 1u) / 2u;
   ch = ((size_t)frame->height + 1u) / 2u;
   uv_size = cw * ch;
   y_dst = (uint8_t *)video->buffer;
   u_dst = y_dst + y_size;
   v_dst = u_dst + uv_size;
   for (int y = 0; y < frame->height; y++)
      memcpy(y_dst + (size_t)y * (size_t)frame->width,
         frame->data[0] + (size_t)y * (size_t)frame->linesize[0],
         (size_t)frame->width);
   for (int y = 0; y < (int)ch; y++) {
      memcpy(u_dst + (size_t)y * cw,
         frame->data[1] + (size_t)y * (size_t)frame->linesize[1], cw);
      memcpy(v_dst + (size_t)y * cw,
         frame->data[2] + (size_t)y * (size_t)frame->linesize[2], cw);
   }
   unifrog_perf_cache_flush(video->buffer, video->buffer_size);
   return 0;
}

static int media_swvideo_present(struct media_sw_video *video,
   const AVFrame *frame)
{
   struct vframe_info vframe;
   size_t y_size;
   size_t uv_size;
   size_t cw;
   size_t ch;
   uint8_t *y_plane;
   uint8_t *u_plane;
   uint8_t *v_plane;
   int ret;

   if (!video || !frame || frame->width <= 0 || frame->height <= 0)
      return -1;
   if (media_swvideo_prepare(video, frame->width, frame->height) != 0)
      return -1;
   if (media_swvideo_copy_yuv420(video, frame) != 0)
      return -1;
   if (video->frames == 0 &&
       media_swvideo_open_sink(video, frame->width, frame->height) != 0)
      return -1;
   if (vidsink_fd < 0)
      return -1;
   y_size = (size_t)frame->width * (size_t)frame->height;
   cw = ((size_t)frame->width + 1u) / 2u;
   ch = ((size_t)frame->height + 1u) / 2u;
   uv_size = cw * ch;
   y_plane = (uint8_t *)video->buffer;
   u_plane = y_plane + y_size;
   v_plane = u_plane + uv_size;
   memset(&vframe, 0, sizeof(vframe));
   vframe.pixfmt = FF_PIX_FMT_YUV420P;
   vframe.width = (int16_t)frame->width;
   vframe.height = (int16_t)frame->height;
   vframe.src_width = (int16_t)frame->width;
   vframe.src_height = (int16_t)frame->height;
   vframe.pixels[0] = y_plane;
   vframe.pixels[1] = u_plane;
   vframe.pixels[2] = v_plane;
   vframe.pitch[0] = frame->width;
   vframe.pitch[1] = (int)cw;
   vframe.pitch[2] = (int)cw;
   vframe.mode = IMG_DIS_FULLSCREEN;
   vframe.angle = ROTATE_TYPE_0;
   vframe.mirror = MIRROR_TYPE_NONE;
   vframe.img_effect.mode = IMG_SHOW_NULL;
   vframe.src_area.x = 0;
   vframe.src_area.y = 0;
   vframe.src_area.w = (uint16_t)frame->width;
   vframe.src_area.h = (uint16_t)frame->height;
   vframe.dst_area.x = 0;
   vframe.dst_area.y = 0;
   vframe.dst_area.w = VIDEO_OUTPUT_W;
   vframe.dst_area.h = VIDEO_OUTPUT_H;
   vframe.preview_enable = true;
   vframe.bg_disable = false;
   ret = ioctl(vidsink_fd, VIDSINK_DISPLAY_FRAME, &vframe);
   if (ret == 0) {
      video->frames++;
      if (video->frames <= 3u || (video->frames % 120u) == 0)
         printf("unifrog media swvideo presented frame=%lu %dx%d fmt=%d pitch=%d/%d/%d plane=%p/%p/%p ptr=%p phys=0x%08lx\n",
            (unsigned long)video->frames, frame->width, frame->height,
            vframe.pixfmt, vframe.pitch[0], vframe.pitch[1], vframe.pitch[2],
            vframe.pixels[0], vframe.pixels[1], vframe.pixels[2],
            video->buffer, (unsigned long)unifrog_perf_phys_addr(video->buffer));
   } else {
      printf("unifrog media swvideo display failed ret=%d errno=%d frame=%lu %dx%d fmt=%d pitch=%d/%d/%d plane=%p/%p/%p\n",
         ret, errno, (unsigned long)video->frames, frame->width,
         frame->height, vframe.pixfmt, vframe.pitch[0], vframe.pitch[1],
         vframe.pitch[2], vframe.pixels[0], vframe.pixels[1],
         vframe.pixels[2]);
   }
   return ret;
}

static void media_controls_reset_for_playback(const char *tag,
   const char *path)
{
   media_pending_seek_delta_ms = 0;
   media_pending_overlay_toggle = 0;
   media_controls_wait_release = 1;
   media_controls_wait_logged = 0;
   printf("unifrog media controls reset tag=%s wait_release=1 path=%s\n",
      tag ? tag : "?", path ? path : "");
}

static void media_poll_controls_internal(struct media_controls *controls,
   int consume_seek)
{
   uint32_t buttons;
   uint32_t action_mask =
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A) |
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT) |
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT);
   int seek_delta_ms = 0;

   if (controls)
      memset(controls, 0, sizeof(*controls));
   unifrog_input_save_previous();
   unifrog_input_poll_with_wireless_divisor(2);
   buttons = unifrog_input_menu_buttons();
   if (!controls)
      return;
   controls->exit_down =
      (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B)) ||
      ((buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT)) &&
       (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START)));
   if (media_controls_wait_release) {
      if (buttons & action_mask) {
         if (!media_controls_wait_logged) {
            media_controls_wait_logged = 1;
            printf("unifrog media controls waiting_release buttons=0x%08lx consume=%d\n",
               (unsigned long)buttons, consume_seek);
         }
         return;
      }
      media_controls_wait_release = 0;
      if (media_controls_wait_logged)
         printf("unifrog media controls armed buttons=0x%08lx\n",
            (unsigned long)buttons);
   }
   if (unifrog_input_pressed(UNIFROG_BUTTON_A))
      media_pending_overlay_toggle = 1;
   if (unifrog_input_pressed(UNIFROG_BUTTON_RIGHT))
      seek_delta_ms = MEDIA_SEEK_STEP_MS;
   else if (unifrog_input_pressed(UNIFROG_BUTTON_LEFT))
      seek_delta_ms = -MEDIA_SEEK_STEP_MS;
   if (seek_delta_ms)
      media_pending_seek_delta_ms = seek_delta_ms;
   if (consume_seek && media_pending_seek_delta_ms) {
      controls->seek_delta_ms = media_pending_seek_delta_ms;
      media_pending_seek_delta_ms = 0;
   }
   if (consume_seek && media_pending_overlay_toggle) {
      controls->overlay_toggle = 1;
      media_pending_overlay_toggle = 0;
   }
}

static void media_poll_controls(struct media_controls *controls)
{
   media_poll_controls_internal(controls, 1);
}

static int media_exit_down(void)
{
   struct media_controls controls;

   media_poll_controls_internal(&controls, 0);
   return controls.exit_down;
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

static const char *media_pixel_format_name(enum AVPixelFormat fmt)
{
   const char *name = av_get_pix_fmt_name(fmt);

   return name ? name : "?";
}

struct media_ffmpeg_audio_converter {
   SwrContext *swr;
   int src_rate;
   int src_channels;
   uint64_t src_layout;
   enum AVSampleFormat src_fmt;
   int dst_rate;
   int dst_channels;
   uint64_t dst_layout;
};

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

static uint64_t media_ffmpeg_frame_layout(const AVCodecContext *codec_ctx,
   const AVFrame *frame, int *channels_out)
{
   uint64_t layout = 0;
   int channels = 0;

   if (frame) {
      layout = frame->channel_layout;
      channels = frame->channels;
   }
   if ((!layout || channels <= 0) && codec_ctx) {
      if (!layout)
         layout = codec_ctx->channel_layout;
      if (channels <= 0)
         channels = codec_ctx->channels;
   }
   if (!layout && channels > 0)
      layout = (uint64_t)av_get_default_channel_layout(channels);
   if (channels <= 0 && layout)
      channels = av_get_channel_layout_nb_channels(layout);
   if (channels_out)
      *channels_out = channels;
   return layout;
}

static int media_ffmpeg_converter_configure(
   struct media_ffmpeg_audio_converter *converter,
   const AVCodecContext *codec_ctx, const AVFrame *frame,
   int dst_rate, int dst_channels, const char *path)
{
   SwrContext *swr;
   enum AVSampleFormat src_fmt;
   int bytes;
   int channels;
   int src_rate;
   uint64_t layout;
   uint64_t dst_layout;
   int ret;

   if (!converter || !frame || dst_rate <= 0 ||
       dst_channels <= 0 || dst_channels > 2)
      return -1;
   src_fmt = (enum AVSampleFormat)frame->format;
   if (src_fmt == AV_SAMPLE_FMT_NONE && codec_ctx)
      src_fmt = codec_ctx->sample_fmt;
   bytes = av_get_bytes_per_sample(av_get_packed_sample_fmt(src_fmt));
   src_rate = frame->sample_rate > 0 ? frame->sample_rate :
      (codec_ctx ? codec_ctx->sample_rate : 0);
   layout = media_ffmpeg_frame_layout(codec_ctx, frame, &channels);
   if (!layout || channels <= 0 || src_rate <= 0 || bytes <= 0)
      return -1;

   dst_layout = dst_channels > 1 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
   if (converter->swr && converter->src_rate == src_rate &&
       converter->src_channels == channels &&
       converter->src_layout == layout && converter->src_fmt == src_fmt &&
       converter->dst_rate == dst_rate &&
       converter->dst_channels == dst_channels &&
       converter->dst_layout == dst_layout)
      return 0;

   swr_free(&converter->swr);
   swr = swr_alloc_set_opts(NULL, (int64_t)dst_layout, AV_SAMPLE_FMT_S16,
      dst_rate, (int64_t)layout, src_fmt, src_rate, 0, NULL);
   if (!swr) {
      printf("unifrog media ffmpeg swr_alloc failed src_rate=%d ch=%d layout=0x%lx fmt=%s dst_rate=%d dst_ch=%d dst_layout=0x%lx path=%s\n",
         src_rate, channels, (unsigned long)layout,
         media_sample_format_name(src_fmt), dst_rate, dst_channels,
         (unsigned long)dst_layout, path ? path : "");
      return -1;
   }
   ret = swr_init(swr);
   if (ret < 0) {
      printf("unifrog media ffmpeg swr_init failed ret=%d src_rate=%d ch=%d layout=0x%lx fmt=%s dst_rate=%d dst_ch=%d dst_layout=0x%lx path=%s\n",
         ret, src_rate, channels, (unsigned long)layout,
         media_sample_format_name(src_fmt), dst_rate, dst_channels,
         (unsigned long)dst_layout, path ? path : "");
      swr_free(&swr);
      return -1;
   }

   converter->swr = swr;
   converter->src_rate = src_rate;
   converter->src_channels = channels;
   converter->src_layout = layout;
   converter->src_fmt = src_fmt;
   converter->dst_rate = dst_rate;
   converter->dst_channels = dst_channels;
   converter->dst_layout = dst_layout;
   printf("unifrog media ffmpeg swr config src_rate=%d ch=%d layout=0x%lx fmt=%s dst_rate=%d dst_ch=%d dst_layout=0x%lx path=%s\n",
      src_rate, channels, (unsigned long)layout,
      media_sample_format_name(src_fmt), dst_rate, dst_channels,
      (unsigned long)dst_layout, path ? path : "");
   return 0;
}

static int media_ffmpeg_frame_data_at(const AVFrame *frame,
   const struct media_ffmpeg_audio_converter *converter, unsigned offset,
   const uint8_t *src_data[AV_NUM_DATA_POINTERS])
{
   int bytes;

   if (!frame || !converter || !src_data || converter->src_channels <= 0)
      return -1;
   memset(src_data, 0, sizeof(const uint8_t *) * AV_NUM_DATA_POINTERS);
   bytes = av_get_bytes_per_sample(
      av_get_packed_sample_fmt((enum AVSampleFormat)frame->format));
   if (bytes <= 0)
      return -1;
   if (av_sample_fmt_is_planar((enum AVSampleFormat)frame->format)) {
      if (converter->src_channels > AV_NUM_DATA_POINTERS)
         return -1;
      for (int ch = 0; ch < converter->src_channels; ch++) {
         if (!frame->extended_data[ch])
            return -1;
         src_data[ch] = frame->extended_data[ch] + offset * (unsigned)bytes;
      }
   } else {
      if (!frame->extended_data[0])
         return -1;
      src_data[0] = frame->extended_data[0] +
         offset * (unsigned)converter->src_channels * (unsigned)bytes;
   }
   return 0;
}

static int media_ffmpeg_write_pcm(struct unifrog_audio *audio,
   const int16_t *pcm, unsigned frames, uint32_t *played, const char *path)
{
   int ret;

   if (!audio || !pcm || frames == 0)
      return -1;
   ret = unifrog_audio_write(audio, pcm, frames);
   if (ret < 0) {
      printf("unifrog media ffmpeg audio_write failed ret=%d frames=%u rate=%u ch=%u path=%s\n",
         ret, frames, audio->rate, audio->channels, path ? path : "");
      return -1;
   }
   if (played)
      *played += frames;
   return 0;
}

static int media_ffmpeg_open_audio(const char *path, unsigned output_channels,
   AVFormatContext **fmt_out, AVCodecContext **codec_out, int *stream_out,
   AVCodec **decoder_out)
{
   AVFormatContext *fmt = NULL;
   AVCodecContext *codec_ctx = NULL;
   AVCodec *decoder = NULL;
   int stream;
   int ret;
   int sd_read_active = 0;
   uint64_t output_layout = media_audio_output_layout(
      media_audio_mix_channels(output_channels));

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
   printf("unifrog media ffmpeg find_audio begin streams=%u path=%s\n",
      fmt ? fmt->nb_streams : 0, path ? path : "");
   stream = media_find_stream_type(fmt, AVMEDIA_TYPE_AUDIO);
   printf("unifrog media ffmpeg find_audio done stream=%d path=%s\n",
      stream, path ? path : "");
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
   codec_ctx->request_channel_layout = output_layout;
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
   const AVCodecContext *codec_ctx,
   struct media_ffmpeg_audio_converter *converter,
   const AVFrame *frame, int16_t *pcm, unsigned pcm_capacity,
   uint32_t *played, const char *path)
{
   unsigned offset = 0;
   unsigned mix_channels;

   if (!audio || !converter || !frame || !pcm || pcm_capacity == 0)
      return -1;
   mix_channels = media_audio_mix_channels(audio->channels);
   if (media_ffmpeg_converter_configure(converter, codec_ctx, frame,
       (int)audio->rate, (int)mix_channels, path) != 0)
      return -1;
   while (offset < (unsigned)frame->nb_samples) {
      unsigned chunk = (unsigned)frame->nb_samples - offset;
      const uint8_t *src_data[AV_NUM_DATA_POINTERS];
      uint8_t *dst_data[1];
      int dst_need;
      int got;

      if (chunk > pcm_capacity)
         chunk = pcm_capacity;
      for (;;) {
         dst_need = (int)av_rescale_rnd(
            swr_get_delay(converter->swr, converter->src_rate) +
               (int64_t)chunk,
            converter->dst_rate, converter->src_rate, AV_ROUND_UP);
         if (dst_need <= (int)pcm_capacity || chunk <= 1u)
            break;
         chunk = (chunk * pcm_capacity) / (unsigned)dst_need;
         if (chunk == 0)
            chunk = 1;
      }
      if (media_ffmpeg_frame_data_at(frame, converter, offset, src_data) != 0)
         return -1;
      dst_data[0] = (uint8_t *)pcm;
      got = swr_convert(converter->swr, dst_data, (int)pcm_capacity,
         src_data, (int)chunk);
      if (got <= 0)
         return -1;
      if (mix_channels == 1u && audio->channels > 1u)
         media_expand_mono_to_output_inplace(pcm, (unsigned)got,
            audio->channels);
      media_log_pcm_stats("ffmpeg", audio, pcm, (unsigned)got, path);
      if (media_ffmpeg_write_pcm(audio, pcm, (unsigned)got, played,
          path) != 0)
         return -1;
      offset += chunk;
      if (media_exit_down())
         return 1;
   }
   return 0;
}

static int media_gb300_auddec_fallback_backend(const char *reason)
{
   if (!unifrog_audio_prefers_stereo_output() || !reason)
      return UNIFROG_AUDIO_BACKEND_AUTO;
   if (strncmp(reason, "auddec", 6) != 0)
      return UNIFROG_AUDIO_BACKEND_AUTO;
   return UNIFROG_AUDIO_BACKEND_SND;
}

static int media_play_ffmpeg_audio_backend(const char *path, int backend,
   const char *reason)
{
   struct unifrog_audio audio;
   AVFormatContext *fmt = NULL;
   AVCodecContext *codec_ctx = NULL;
   AVCodec *decoder = NULL;
   AVPacket *packet = NULL;
   AVFrame *frame = NULL;
   struct media_ffmpeg_audio_converter converter;
   int16_t *pcm = NULL;
   int stream = -1;
   uint32_t played = 0;
   uint32_t loop_polls = 0;
   int64_t duration_ms = -1;
   unsigned output_channels = media_audio_output_channels();
   struct media_progress_overlay overlay;
   int saw_frame = 0;
   int ret = -1;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   memset(&converter, 0, sizeof(converter));
   memset(&overlay, 0, sizeof(overlay));
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_MEDIA_AUDIO,
      unifrog_exception_activity_hash(path ? path : "ffmpeg_audio"), 0, 1);
   if (media_ffmpeg_open_audio(path, output_channels, &fmt, &codec_ctx, &stream,
       &decoder) != 0)
      goto out;
   duration_ms = media_format_duration_ms(fmt);
   if (codec_ctx->sample_rate < 8000 || codec_ctx->sample_rate > 48000) {
      printf("unifrog media ffmpeg unsupported_rate rate=%d path=%s\n",
         codec_ctx->sample_rate, path);
      goto out;
   }

   packet = av_packet_alloc();
   frame = av_frame_alloc();
   pcm = malloc(sizeof(*pcm) * MEDIA_FFMPEG_CHUNK_FRAMES * output_channels);
   if (!packet || !frame || !pcm) {
      printf("unifrog media ffmpeg alloc failed packet=%p frame=%p pcm=%p path=%s\n",
         (void *)packet, (void *)frame, (void *)pcm, path);
      goto out;
   }
   if (unifrog_audio_open_backend(&audio, (unsigned)codec_ctx->sample_rate,
       output_channels, 512, 8, backend) != 0) {
      printf("unifrog media ffmpeg audio_open failed rate=%d ch=%u backend=%d reason=%s path=%s\n",
         codec_ctx->sample_rate, output_channels, backend,
         reason ? reason : "?", path);
      goto out;
   }
   (void)unifrog_audio_set_volume(&audio, media_audio_runtime_volume());
   (void)unifrog_audio_set_mute(&audio, 1);
   (void)unifrog_audio_start(&audio);
   (void)unifrog_audio_set_output_enabled(&audio, 1);
   unifrog_audio_debug_dump(&audio, "ffmpeg_after_start");
   printf("unifrog media ffmpeg audio start codec=%s stream=%d rate=%d src_ch=%d out_ch=%u backend=%d reason=%s fmt=%s duration=%lld overlay=1 overlay_hide=A path=%s\n",
      decoder && decoder->name ? decoder->name : "?",
      stream, codec_ctx->sample_rate, codec_ctx->channels, output_channels,
      backend, reason ? reason : "?",
      media_sample_format_name(codec_ctx->sample_fmt), (long long)duration_ms,
      path);
   media_controls_reset_for_playback("ffmpeg_audio", path);
   media_draw_progress_overlay(&overlay, "audio_start", 0, duration_ms, 1,
      path);

   for (;;) {
      struct media_controls controls;
      int read_ret;

      media_poll_controls(&controls);
      if (controls.exit_down)
         break;
      if (controls.overlay_toggle) {
         media_toggle_progress_overlay(&overlay, "audio_toggle",
            media_audio_frames_to_ms(played, audio.rate), duration_ms, path);
      }
      read_ret = av_read_frame(fmt, packet);
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
            write_ret = media_ffmpeg_write_frame(&audio, codec_ctx,
               &converter, frame, pcm, MEDIA_FFMPEG_CHUNK_FRAMES,
               &played, path);
            if (write_ret < 0) {
               printf("unifrog media ffmpeg frame output failed fmt=%s ch=%d samples=%d path=%s\n",
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
      loop_polls++;
      if ((loop_polls % 32u) == 0)
         media_draw_progress_overlay(&overlay, "audio",
            media_audio_frames_to_ms(played, audio.rate), duration_ms, 0, path);
   }

   (void)avcodec_send_packet(codec_ctx, NULL);
   for (;;) {
      int recv_ret = avcodec_receive_frame(codec_ctx, frame);
      int write_ret;

      if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
         break;
      if (recv_ret < 0)
         break;
      write_ret = media_ffmpeg_write_frame(&audio, codec_ctx, &converter,
         frame, pcm, MEDIA_FFMPEG_CHUNK_FRAMES, &played, path);
      if (write_ret != 0)
         break;
   }
   ret = played ? 0 : -1;

out:
   printf("unifrog media ffmpeg audio end ret=%d frames=%lu path=%s\n",
      ret, (unsigned long)played, path ? path : "");
   unifrog_exception_activity_clear();
   if (audio.fd >= 0)
      unifrog_audio_close(&audio);
   swr_free(&converter.swr);
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

static int media_play_ffmpeg_audio(const char *path)
{
   return media_play_ffmpeg_audio_backend(path, UNIFROG_AUDIO_BACKEND_AUTO,
      "normal");
}

static int media_play_native_audio_compressed(const char *path)
{
   AVFormatContext *fmt = NULL;
   AVPacket *packet = NULL;
   struct media_auddec auddec;
   struct media_audio_pacer pacer;
   struct media_buffered_input input;
   struct media_progress_overlay overlay;
   int stream = -1;
   int ret = -1;
   unsigned finish_timeout_ms = 600000u;
   uint32_t loop_polls = 0;
   uint32_t last_progress_poll_ms = 0;
   uint32_t last_stall_poll_ms = 0;
   uint32_t start_ms = unifrog_perf_time_ms();
   int64_t duration_ms = -1;
   int sd_read_active = 0;
   int write_failed = 0;
   int decode_stalled = 0;
   int eof_seen = 0;
   unsigned long last_decoded = 0;
   unsigned last_header_seen = 0;
   int64_t last_audio_time = -1;
   int last_status_ok = 0;
   int last_time_ok = 0;

   memset(&auddec, 0, sizeof(auddec));
   memset(&pacer, 0, sizeof(pacer));
   memset(&input, 0, sizeof(input));
   memset(&overlay, 0, sizeof(overlay));
   auddec.fd = -1;
   auddec.prime_fd = -1;
   input.fd = -1;
   media_ffmpeg_register_once();
   media_sd_read_begin("auddec_open", path);
   sd_read_active = 1;
   printf("unifrog media auddec open_input begin path=%s\n",
      path ? path : "");
   int open_ret = media_buffered_input_open(&fmt, &input, path, "auddec");
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
   media_buffered_input_log_coverage(&input, fmt, "auddec", path);
   (void)media_buffered_input_enable_readahead(&input, fmt, "auddec", path);
   media_log_format_streams(fmt, path, "auddec_open");
   printf("unifrog media auddec find_audio begin streams=%u path=%s\n",
      fmt ? fmt->nb_streams : 0, path ? path : "");
   stream = media_find_stream_type(fmt, AVMEDIA_TYPE_AUDIO);
   printf("unifrog media auddec find_audio done stream=%d path=%s\n",
      stream, path ? path : "");
   if (stream < 0) {
      printf("unifrog media auddec audio_stream missing ret=%d path=%s\n",
         stream, path);
      goto out;
   }
   if (fmt->duration > 0) {
      duration_ms = media_format_duration_ms(fmt);

      if (duration_ms > 0 && duration_ms < 3600000)
         finish_timeout_ms = (unsigned)duration_ms + 5000u;
   }
   media_init_drivers_once();
   if (media_auddec_open(fmt, stream, AVSYNC_TYPE_FREERUN, &auddec) != 0 &&
       media_auddec_open(fmt, stream, AVSYNC_TYPE_UPDATESTC, &auddec) != 0)
      goto out;
   auddec.write_timeout_ms = MEDIA_AUDIO_WRITE_SPACE_TIMEOUT_MS;
   packet = av_packet_alloc();
   if (!packet)
      goto out;
   printf("unifrog media auddec play stream=%d timeout=%u feed_lead_ms=%u max_hw_ahead_ms=%u duration=%lld overlay=1 overlay_hide=A tb=%d/%d path=%s\n",
      stream, auddec.write_timeout_ms, MEDIA_AUDIO_FEED_LEAD_MS,
      MEDIA_AUDIO_MAX_HW_AHEAD_MS, (long long)duration_ms,
      fmt->streams[stream]->time_base.num, fmt->streams[stream]->time_base.den,
      path);
   media_controls_reset_for_playback("auddec_audio", path);
   media_draw_progress_overlay(&overlay, "audio_start", 0, duration_ms, 1,
      path);
   for (;;) {
      struct media_controls controls;
      int read_ret;

      media_poll_controls(&controls);
      if (controls.exit_down)
         break;
      if (controls.overlay_toggle) {
         int64_t cur_time = -1;

         (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &cur_time);
         media_toggle_progress_overlay(&overlay, "audio_toggle",
            cur_time >= 0 ? cur_time : pacer.next_ms, duration_ms, path);
      }
      if (controls.seek_delta_ms && duration_ms > 0) {
         int64_t cur_time = -1;
         int64_t target_ms;

         (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &cur_time);
         target_ms = media_seek_target_ms(cur_time, controls.seek_delta_ms,
            duration_ms);
         printf("unifrog media seek audio request cur=%lld dur=%lld delta=%d target=%lld path=%s\n",
            (long long)cur_time, (long long)duration_ms,
            controls.seek_delta_ms, (long long)target_ms, path ? path : "");
         media_flush_auddec_for_seek(&auddec, "audio", path);
         if (media_seek_format_ms(fmt, target_ms, "audio", path) == 0) {
            media_audio_pacer_seek_reset(&pacer, target_ms);
            eof_seen = 0;
            media_draw_progress_overlay(&overlay, "audio_seek", target_ms,
               duration_ms, 1, path);
         }
         continue;
      }

      read_ret = av_read_frame(fmt, packet);
      if (read_ret < 0) {
         eof_seen = 1;
         break;
      }
      if (packet->stream_index == stream) {
         media_audio_pacer_wait(&pacer, packet,
            fmt->streams[stream]->time_base);
         (void)media_wait_hardware_ahead("auddec", auddec.fd, 0, &pacer,
            MEDIA_AUDIO_MAX_HW_AHEAD_MS, path);
         if (media_auddec_send_packet(&auddec, packet) != 0) {
            printf("unifrog media auddec write failed packets=%lu path=%s\n",
               (unsigned long)auddec.packets, path);
            write_failed = 1;
            av_packet_unref(packet);
            break;
         }
      }
      av_packet_unref(packet);
      if (unifrog_audio_prefers_stereo_output() &&
          auddec.packets >= MEDIA_GB300_AUDDEC_STALL_PACKETS &&
          !decode_stalled) {
         uint32_t now_ms = unifrog_perf_time_ms();
         uint32_t elapsed_ms = now_ms - start_ms;

         if (elapsed_ms >= MEDIA_GB300_AUDDEC_STALL_MS &&
             now_ms - last_stall_poll_ms >= MEDIA_AUDIO_PROGRESS_POLL_MS) {
            struct audio_decore_status status;
            int64_t cur_time = -1;
            int status_ok;
            int time_ok;

            last_stall_poll_ms = now_ms;
            memset(&status, 0, sizeof(status));
            status_ok = ioctl(auddec.fd, AUDDEC_GET_STATUS, &status) == 0;
            time_ok = ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &cur_time) == 0;
            last_status_ok = status_ok;
            last_time_ok = time_ok;
            if (status_ok) {
               last_decoded = (unsigned long)status.frames_decoded;
               last_header_seen = status.first_header_got ||
                  status.first_header_parsed;
            }
            if (time_ok)
               last_audio_time = cur_time;
            if (status_ok &&
                media_auddec_runtime_decode_stalled(&status, time_ok,
                   cur_time)) {
               printf("unifrog media auddec fallback trigger reason=decode_stall packets=%lu ms=%lu decoded=%lu hdr=%u/%u time_ok=%d atime=%lld path=%s\n",
                  (unsigned long)auddec.packets, (unsigned long)elapsed_ms,
                  (unsigned long)status.frames_decoded,
                  status.first_header_got, status.first_header_parsed,
                  time_ok, (long long)cur_time, path ? path : "");
               decode_stalled = 1;
               break;
            }
            if (time_ok)
               media_auddec_enable_output_on_clock_progress(&auddec, cur_time,
                  "audio_stall_poll", auddec.packets);
            if (status_ok)
               media_auddec_enable_output_on_progress(&auddec, &status,
                  "audio_stall_poll", auddec.packets);
         }
      }
      loop_polls++;
      if (unifrog_perf_time_ms() - last_progress_poll_ms >=
          MEDIA_AUDIO_PROGRESS_POLL_MS) {
         int64_t cur_time = -1;

         last_progress_poll_ms = unifrog_perf_time_ms();
         (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &cur_time);
         media_draw_progress_overlay(&overlay, "audio",
            cur_time >= 0 ? cur_time : pacer.next_ms, duration_ms, 0, path);
      }
      if ((loop_polls % 240u) == 0) {
         struct audio_decore_status status;
         int64_t cur_time = -1;
         uint32_t elapsed_ms = unifrog_perf_time_ms() - start_ms;

         memset(&status, 0, sizeof(status));
         last_time_ok = ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &cur_time) == 0;
         last_status_ok = ioctl(auddec.fd, AUDDEC_GET_STATUS, &status) == 0;
         if (last_status_ok) {
            last_decoded = (unsigned long)status.frames_decoded;
            last_header_seen = status.first_header_got ||
               status.first_header_parsed;
            media_auddec_enable_output_on_progress(&auddec, &status,
               "audio_monitor", auddec.packets);
         }
         if (last_time_ok)
            last_audio_time = cur_time;
         if (last_status_ok)
            printf("unifrog media auddec monitor packets=%lu decoded=%lu rate=%lu ch=%u bits=%u ms=%lu atime=%lld feed_ms=%lld feed_lead=%lld ahead_ms=%lld\n",
               (unsigned long)auddec.packets,
               (unsigned long)status.frames_decoded,
               (unsigned long)status.sample_rate,
               status.channels, status.bits_per_sample,
               (unsigned long)elapsed_ms,
               (long long)cur_time, (long long)pacer.next_ms,
               (long long)(pacer.next_ms - (int64_t)elapsed_ms),
               cur_time >= 0 ?
               (long long)(pacer.next_ms - cur_time) : -1ll);
         media_draw_progress_overlay(&overlay, "audio",
            cur_time >= 0 ? cur_time : pacer.next_ms, duration_ms, 0, path);
      }
   }
   ret = auddec.packets && !write_failed && !decode_stalled ? 0 : -1;
   if (auddec.fd >= 0) {
      struct audio_decore_status status;
      int64_t cur_time = -1;
      int status_ok;
      int time_ok;

      memset(&status, 0, sizeof(status));
      status_ok = ioctl(auddec.fd, AUDDEC_GET_STATUS, &status) == 0;
      time_ok = ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &cur_time) == 0;
      if (status_ok) {
         last_status_ok = 1;
         last_decoded = (unsigned long)status.frames_decoded;
         last_header_seen = status.first_header_got ||
            status.first_header_parsed;
         media_auddec_enable_output_on_progress(&auddec, &status,
            "audio_final", auddec.packets);
      }
      if (time_ok) {
         last_time_ok = 1;
         last_audio_time = cur_time;
      }
   }
   if (ret == 0 &&
       unifrog_audio_prefers_stereo_output() &&
       auddec.packets >= MEDIA_GB300_AUDDEC_STALL_PACKETS &&
       last_status_ok &&
       last_decoded == 0 &&
       !last_header_seen &&
       !media_auddec_clock_has_progress(last_time_ok, last_audio_time)) {
      printf("unifrog media auddec fallback trigger reason=decode_stall packets=%lu decoded=%lu hdr=%u time_ok=%d atime=%lld path=%s\n",
         (unsigned long)auddec.packets, last_decoded, last_header_seen,
         last_time_ok, (long long)last_audio_time, path ? path : "");
      ret = -1;
   }

out:
   printf("unifrog media auddec end ret=%d packets=%lu write_failed=%d eof=%d ms=%lu path=%s\n",
      ret, (unsigned long)auddec.packets,
      write_failed, eof_seen,
      (unsigned long)(unifrog_perf_time_ms() - start_ms), path ? path : "");
   if (ret == 0)
      media_auddec_finish(&auddec, finish_timeout_ms);
   media_auddec_close(&auddec);
   if (packet)
      av_packet_free(&packet);
   if (fmt)
      avformat_close_input(&fmt);
   media_buffered_input_close(&input, "auddec_close", path);
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

static void media_audio_pacer_wait_ms(struct media_audio_pacer *pacer,
   int32_t pts_ms, int32_t dur_ms, unsigned feed_lead_ms)
{
   int64_t target_ms;
   int64_t elapsed_ms;

   if (!pacer)
      return;
   if (!pacer->started) {
      pacer->started = 1;
      pacer->base_ms = pts_ms >= 0 ? pts_ms : 0;
      pacer->next_ms = 0;
      pacer->wall_start_ms = unifrog_perf_time_ms();
   }
   target_ms = pts_ms >= 0 ? (int64_t)pts_ms - pacer->base_ms :
      pacer->next_ms;
   if (target_ms < 0)
      target_ms = 0;
   for (;;) {
      int64_t lead_ms;

      elapsed_ms = (int64_t)(unifrog_perf_time_ms() -
         pacer->wall_start_ms);
      lead_ms = target_ms - elapsed_ms;
      if (lead_ms <= (int64_t)feed_lead_ms)
         break;
      if (media_exit_down())
         break;
      if (lead_ms > (int64_t)MEDIA_AUDIO_PACE_MAX_SLEEP_MS)
         lead_ms = MEDIA_AUDIO_PACE_MAX_SLEEP_MS;
      usleep((unsigned)lead_ms * 1000u);
   }
   if (dur_ms > 0) {
      int64_t end_ms = target_ms + dur_ms;

      if (end_ms > pacer->next_ms)
         pacer->next_ms = end_ms;
   } else if (target_ms > pacer->next_ms) {
      pacer->next_ms = target_ms;
   }
}

static void media_audio_pacer_wait_lead(struct media_audio_pacer *pacer,
   const AVPacket *packet, AVRational time_base, unsigned feed_lead_ms)
{
   int32_t pts_ms;
   int32_t dur_ms;

   if (!pacer || !packet)
      return;
   pts_ms = media_packet_pts_ms(packet, time_base);
   dur_ms = media_packet_duration_ms(packet, time_base);
   media_audio_pacer_wait_ms(pacer, pts_ms, dur_ms, feed_lead_ms);
}

static void media_audio_pacer_seek_reset(struct media_audio_pacer *pacer,
   int64_t target_ms)
{
   uint32_t now;

   if (!pacer)
      return;
   if (target_ms < 0)
      target_ms = 0;
   if (target_ms > INT32_MAX)
      target_ms = INT32_MAX;
   now = unifrog_perf_time_ms();
   memset(pacer, 0, sizeof(*pacer));
   pacer->started = 1;
   pacer->base_ms = 0;
   /* Align wall time to the seek target. The decoder clock is reset by flush,
    * so let a bounded packet burst through before enforcing clock-ahead caps. */
   pacer->next_ms = 0;
   pacer->wall_start_ms = now - (uint32_t)target_ms;
   pacer->seek_warmup_packets = MEDIA_SEEK_WARMUP_PACKETS;
   pacer->seek_warmup_total = MEDIA_SEEK_WARMUP_PACKETS;
}

static void media_audio_pacer_wait(struct media_audio_pacer *pacer,
   const AVPacket *packet, AVRational time_base)
{
   media_audio_pacer_wait_lead(pacer, packet, time_base,
      MEDIA_AUDIO_FEED_LEAD_MS);
}

static int media_wait_hardware_ahead(const char *kind, int fd, int video,
   struct media_audio_pacer *pacer, unsigned max_ahead_ms,
   const char *path)
{
   uint32_t start_ms;
   uint32_t last_log_ms = 0;
   int logged = 0;

   if (fd < 0 || !pacer || !pacer->started || max_ahead_ms == 0)
      return 0;
   if (pacer->seek_warmup_packets > 0) {
      if (pacer->seek_warmup_packets == pacer->seek_warmup_total) {
         printf("unifrog media hw_ahead seek_warmup kind=%s packets=%u feed=%lld max=%u path=%s\n",
            kind ? kind : "?", pacer->seek_warmup_total,
            (long long)pacer->next_ms, max_ahead_ms, path ? path : "");
      }
      pacer->seek_warmup_packets--;
      return 0;
   }
   start_ms = unifrog_perf_time_ms();
   for (;;) {
      int64_t cur_time = -1;
      int64_t ahead_ms;
      uint32_t now_ms;
      uint32_t waited_ms;

      errno = 0;
      if (ioctl(fd, video ? VIDDEC_GET_CUR_TIME : AUDDEC_GET_CUR_TIME,
            &cur_time) != 0 || cur_time < 0)
         return 0;
      ahead_ms = pacer->next_ms - cur_time;
      if (ahead_ms <= (int64_t)max_ahead_ms)
         break;
      if (media_exit_down())
         break;
      now_ms = unifrog_perf_time_ms();
      waited_ms = now_ms - start_ms;
      if (waited_ms >= MEDIA_HW_AHEAD_LOG_MIN_MS &&
          (!logged || now_ms - last_log_ms >= MEDIA_HW_AHEAD_LOG_MS)) {
         printf("unifrog media hw_ahead wait kind=%s ahead=%lld max=%u clock=%lld feed=%lld waited=%lu path=%s\n",
            kind ? kind : "?", (long long)ahead_ms, max_ahead_ms,
            (long long)cur_time, (long long)pacer->next_ms,
            (unsigned long)waited_ms, path ? path : "");
         logged = 1;
         last_log_ms = now_ms;
      }
      if (MEDIA_HW_AHEAD_MAX_WAIT_MS &&
          waited_ms >= MEDIA_HW_AHEAD_MAX_WAIT_MS) {
         int64_t cap_ms = cur_time + (int64_t)max_ahead_ms;
         int64_t feed_ms = pacer->next_ms;

         if (cur_time >= 0 && pacer->next_ms > cap_ms)
            pacer->next_ms = cap_ms;
         printf("unifrog media hw_ahead timeout kind=%s ahead=%lld max=%u clock=%lld feed=%lld capped_feed=%lld waited=%lu limit=%u path=%s\n",
            kind ? kind : "?", (long long)ahead_ms, max_ahead_ms,
            (long long)cur_time, (long long)feed_ms,
            (long long)pacer->next_ms,
            (unsigned long)waited_ms, MEDIA_HW_AHEAD_MAX_WAIT_MS,
            path ? path : "");
         return 1;
      }
      usleep(MEDIA_HW_AHEAD_POLL_US);
   }
   if (logged) {
      int64_t cur_time = -1;
      uint32_t waited_ms = unifrog_perf_time_ms() - start_ms;

      (void)ioctl(fd, video ? VIDDEC_GET_CUR_TIME : AUDDEC_GET_CUR_TIME,
         &cur_time);
      printf("unifrog media hw_ahead done kind=%s clock=%lld feed=%lld ahead=%lld waited=%lu path=%s\n",
         kind ? kind : "?", (long long)cur_time, (long long)pacer->next_ms,
         cur_time >= 0 ? (long long)(pacer->next_ms - cur_time) : -1ll,
         (unsigned long)waited_ms, path ? path : "");
   }
   return 0;
}

static int64_t media_format_duration_ms(AVFormatContext *fmt)
{
   if (!fmt || fmt->duration <= 0)
      return -1;
   return fmt->duration / (AV_TIME_BASE / 1000);
}

static int64_t media_seek_target_ms(int64_t current_ms, int delta_ms,
   int64_t duration_ms)
{
   int64_t target = current_ms >= 0 ? current_ms : 0;

   target += delta_ms;
   if (target < 0)
      target = 0;
   if (duration_ms > 0 && target > duration_ms)
      target = duration_ms;
   return target;
}

static int media_seek_format_ms(AVFormatContext *fmt, int64_t target_ms,
   const char *tag, const char *path)
{
   int64_t target_us;
   int ret;
   int fallback_ret = 0;

   if (!fmt || target_ms < 0)
      return -1;
   target_us = target_ms * (AV_TIME_BASE / 1000);
   errno = 0;
   ret = avformat_seek_file(fmt, -1, INT64_MIN, target_us, INT64_MAX,
      AVSEEK_FLAG_BACKWARD);
   if (ret < 0) {
      fallback_ret = av_seek_frame(fmt, -1, target_us, AVSEEK_FLAG_BACKWARD);
      if (fallback_ret >= 0)
         ret = fallback_ret;
   }
   printf("unifrog media seek demux tag=%s target=%lld ret=%d fallback=%d errno=%d path=%s\n",
      tag ? tag : "", (long long)target_ms, ret, fallback_ret, errno,
      path ? path : "");
   return ret;
}

static void media_set_avsync_timebase(int64_t target_ms, const char *tag,
   const char *path)
{
   int fd;
   int ret = -1;
   int saved_errno = 0;
   unsigned int stc_ms;

   if (target_ms < 0)
      target_ms = 0;
   if (target_ms > UINT32_MAX)
      target_ms = UINT32_MAX;
   stc_ms = (unsigned int)target_ms;
   errno = 0;
   fd = open("/dev/avsync0", O_RDWR);
   if (fd >= 0) {
      errno = 0;
      ret = ioctl(fd, AVSYNC_SET_STC_MS, stc_ms);
      saved_errno = errno;
      close(fd);
   } else {
      saved_errno = errno;
   }
   printf("unifrog media seek avsync_set tag=%s fd=%d target=%lu ret=%d errno=%d path=%s\n",
      tag ? tag : "", fd, (unsigned long)stc_ms, ret, saved_errno,
      path ? path : "");
}

static void media_flush_auddec_for_seek(struct media_auddec *auddec,
   const char *tag, const char *path)
{
   int pause_ret = -1;
   int flush_ret = -1;
   int start_ret = -1;

   if (!auddec || auddec->fd < 0)
      return;
   errno = 0;
   pause_ret = ioctl(auddec->fd, AUDDEC_PAUSE, 0);
   errno = 0;
   flush_ret = ioctl(auddec->fd, AUDDEC_FLUSH, 0);
   errno = 0;
   start_ret = ioctl(auddec->fd, AUDDEC_START, 0);
   printf("unifrog media seek auddec_flush tag=%s fd=%d pause=%d flush=%d start=%d errno=%d path=%s\n",
      tag ? tag : "", auddec->fd, pause_ret, flush_ret, start_ret, errno,
      path ? path : "");
}

static void media_flush_viddec_for_seek(int video_fd, const char *tag,
   const char *path)
{
   float rate = 1.0f;
   int pause_ret = -1;
   int flush_ret = -1;
   int start_ret = -1;

   if (video_fd < 0)
      return;
   errno = 0;
   pause_ret = ioctl(video_fd, VIDDEC_PAUSE, 0);
   errno = 0;
   flush_ret = ioctl(video_fd, VIDDEC_FLUSH, &rate);
   errno = 0;
   start_ret = ioctl(video_fd, VIDDEC_START, 0);
   printf("unifrog media seek viddec_flush tag=%s fd=%d pause=%d flush=%d start=%d errno=%d rate_milli=1000 path=%s\n",
      tag ? tag : "", video_fd, pause_ret, flush_ret, start_ret, errno,
      path ? path : "");
}

static void media_video_release_decoder(int fd, int closevp, int fillblack,
   const char *tag, const char *path)
{
   struct vdec_rls_param rls;
   float rate = 1.0f;
   int flush_ret;
   int flush_errno;
   int rls_ret;
   int rls_errno;

   if (fd < 0)
      return;

   errno = 0;
   flush_ret = ioctl(fd, VIDDEC_FLUSH, &rate);
   flush_errno = errno;
   memset(&rls, 0, sizeof(rls));
   rls.closevp = closevp ? 1 : 0;
   rls.fillblack = fillblack ? 1 : 0;
   errno = 0;
   rls_ret = ioctl(fd, VIDDEC_RLS, (unsigned long)&rls);
   rls_errno = errno;
   printf("unifrog media native video release tag=%s fd=%d closevp=%d fillblack=%d flush=%d flush_errno=%d rls=%d rls_errno=%d path=%s\n",
      tag ? tag : "", fd, rls.closevp, rls.fillblack, flush_ret,
      flush_errno, rls_ret, rls_errno, path ? path : "");
}

static void media_video_reset_modules(const char *tag, const char *path)
{
   int exit_ret;
   int exit_errno;
   int init_ret;
   int init_errno;
   int llav_ret;
   int llav_errno;
   int vidsink_ret;
   int vidsink_errno;

   if (!MEDIA_RESET_VIDDEC_ON_FAIL)
      return;

   errno = 0;
   exit_ret = module_exit("viddec");
   exit_errno = errno;
   msleep(20);
   errno = 0;
   init_ret = module_init("viddec");
   init_errno = errno;
   errno = 0;
   llav_ret = module_init("llav_vdec");
   llav_errno = errno;
   errno = 0;
   vidsink_ret = module_init("vidsink");
   vidsink_errno = errno;
   printf("unifrog media native video module_reset tag=%s exit=%d exit_errno=%d init=%d init_errno=%d llav=%d llav_errno=%d vidsink=%d vidsink_errno=%d path=%s\n",
      tag ? tag : "", exit_ret, exit_errno, init_ret, init_errno, llav_ret,
      llav_errno, vidsink_ret, vidsink_errno, path ? path : "");
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

static int media_write_all_timeout(int fd, const void *data, size_t size,
   unsigned timeout_ms, const char *scope)
{
   const uint8_t *p = (const uint8_t *)data;
   size_t written = 0;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned retries = 0;

   while (written < size) {
      ssize_t ret = write(fd, p + written, size - written);

	      if (ret < 0) {
	         int retry_write = errno == EAGAIN || errno == EWOULDBLOCK ||
	            errno == EBUSY ||
	            (errno == EPERM && scope && strcmp(scope, "video") == 0);

	         if (errno == EINTR)
	            continue;
	         if (retry_write) {
	            uint32_t elapsed = unifrog_perf_time_ms() - start_ms;

            if (elapsed >= timeout_ms) {
               printf("unifrog media write timeout scope=%s fd=%d size=%lu written=%lu errno=%d retries=%u waited=%lu limit=%u\n",
                  scope ? scope : "?",
                  fd, (unsigned long)size, (unsigned long)written, errno,
                  retries, (unsigned long)elapsed, timeout_ms);
               return -1;
            }
            if (retries == 0 || (retries % 200u) == 0)
               printf("unifrog media write retry scope=%s fd=%d size=%lu written=%lu errno=%d retries=%u waited=%lu limit=%u\n",
                  scope ? scope : "?",
                  fd, (unsigned long)size, (unsigned long)written, errno,
                  retries, (unsigned long)elapsed, timeout_ms);
            retries++;
            usleep(VIDEO_WRITE_SPACE_POLL_US);
            continue;
         }
         printf("unifrog media write fatal scope=%s fd=%d size=%lu written=%lu errno=%d retries=%u waited=%lu\n",
            scope ? scope : "?", fd, (unsigned long)size,
            (unsigned long)written, errno, retries,
            (unsigned long)(unifrog_perf_time_ms() - start_ms));
         return -1;
      }
      if (ret == 0) {
         uint32_t elapsed = unifrog_perf_time_ms() - start_ms;

         if (elapsed >= timeout_ms) {
            printf("unifrog media write zero timeout scope=%s fd=%d size=%lu written=%lu retries=%u waited=%lu limit=%u\n",
               scope ? scope : "?",
               fd, (unsigned long)size, (unsigned long)written, retries,
               (unsigned long)elapsed, timeout_ms);
            return -1;
         }
         retries++;
         usleep(VIDEO_WRITE_SPACE_POLL_US);
         continue;
      }
      written += (size_t)ret;
   }
   return 0;
}

static int media_write_all(int fd, const void *data, size_t size)
{
   return media_write_all_timeout(fd, data, size,
      VIDEO_WRITE_SPACE_TIMEOUT_MS, "video");
}

static int media_auddec_write_all(struct media_auddec *auddec,
   const void *data, size_t size)
{
   unsigned timeout_ms = auddec && auddec->write_timeout_ms ?
      auddec->write_timeout_ms : VIDEO_WRITE_SPACE_TIMEOUT_MS;

   return media_write_all_timeout(auddec ? auddec->fd : -1, data, size,
      timeout_ms, "auddec");
}

static const char *media_avcodec_name(enum AVCodecID codec_id)
{
   const AVCodecDescriptor *desc = avcodec_descriptor_get(codec_id);

   return desc && desc->name ? desc->name : "?";
}

enum {
   MEDIA_H264_MODE_UNKNOWN = 0,
   MEDIA_H264_MODE_ANNEXB,
   MEDIA_H264_MODE_AVCC,
};

enum {
   MEDIA_H264_EXTRA_NONE = 0,
   MEDIA_H264_EXTRA_CFG_AVCC,
   MEDIA_H264_EXTRA_CFG_RAW,
   MEDIA_H264_EXTRA_PRE_EXTRA,
   MEDIA_H264_EXTRA_POST_ES,
};

static const char *media_h264_mode_name(int mode)
{
   switch (mode) {
   case MEDIA_H264_MODE_ANNEXB:
      return "annexb";
   case MEDIA_H264_MODE_AVCC:
      return "avcc";
   default:
      return "unknown";
   }
}

static const char *media_h264_extra_delivery_name(int delivery)
{
   switch (delivery) {
   case MEDIA_H264_EXTRA_CFG_AVCC:
      return "cfg_avcc";
   case MEDIA_H264_EXTRA_CFG_RAW:
      return "cfg_raw";
   case MEDIA_H264_EXTRA_PRE_EXTRA:
      return "pre_extra";
   case MEDIA_H264_EXTRA_POST_ES:
      return "post_es";
   default:
      return "none";
   }
}

static int media_h264_has_annexb_start(const uint8_t *data, size_t size)
{
   return data && size >= 4u && data[0] == 0 && data[1] == 0 &&
      (data[2] == 1 || (data[2] == 0 && data[3] == 1));
}

static int media_h264_avcc_length_size(const uint8_t *data, size_t size)
{
   int length_size;

   if (!data || size < 7u || data[0] != 1)
      return 0;
   length_size = (data[4] & 0x03) + 1;
   return length_size >= 1 && length_size <= 4 ? length_size : 0;
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

static void media_h264_add_nal_mask(unsigned *mask, uint8_t nal_type)
{
   if (!mask)
      return;
   if (nal_type == 1)
      *mask |= 1u << 0;
   else if (nal_type == 5)
      *mask |= 1u << 1;
   else if (nal_type == 6)
      *mask |= 1u << 2;
   else if (nal_type == 7)
      *mask |= 1u << 3;
   else if (nal_type == 8)
      *mask |= 1u << 4;
   else if (nal_type == 9)
      *mask |= 1u << 5;
}

static unsigned media_h264_nal_mask(const uint8_t *data, size_t size,
   unsigned *nal_count_out)
{
   size_t i = 0;
   unsigned mask = 0;
   unsigned count = 0;

   if (nal_count_out)
      *nal_count_out = 0;
   if (!data || size < 5)
      return 0;
   while (i + 4 < size) {
      size_t nal_pos = size;
      uint8_t nal_type;

      for (; i + 3 < size; i++) {
         if (data[i] != 0 || data[i + 1] != 0)
            continue;
         if (data[i + 2] == 1) {
            nal_pos = i + 3;
            break;
         }
         if (i + 4 < size && data[i + 2] == 0 && data[i + 3] == 1) {
            nal_pos = i + 4;
            break;
         }
      }
      if (nal_pos >= size)
         break;
      nal_type = data[nal_pos] & 0x1f;
      count++;
      media_h264_add_nal_mask(&mask, nal_type);
      i = nal_pos + 1;
   }
   if (nal_count_out)
      *nal_count_out = count;
   return mask;
}

static unsigned media_h264_avcc_nal_mask(const uint8_t *data, size_t size,
   int length_size, unsigned *nal_count_out, int *first_nal_out,
   int *truncated_out)
{
   size_t i = 0;
   unsigned mask = 0;
   unsigned count = 0;
   int first_nal = -1;
   int truncated = 0;

   if (nal_count_out)
      *nal_count_out = 0;
   if (first_nal_out)
      *first_nal_out = -1;
   if (truncated_out)
      *truncated_out = 0;
   if (!data || size == 0 || length_size <= 0 || length_size > 4)
      return 0;
   while (i + (size_t)length_size <= size) {
      uint32_t nal_size = 0;
      uint8_t nal_type;

      for (int j = 0; j < length_size; j++)
         nal_size = (nal_size << 8) | data[i + (size_t)j];
      i += (size_t)length_size;
      if (nal_size == 0)
         continue;
      if (i + nal_size > size) {
         truncated = 1;
         break;
      }
      nal_type = data[i] & 0x1f;
      if (first_nal < 0)
         first_nal = nal_type;
      count++;
      media_h264_add_nal_mask(&mask, nal_type);
      i += nal_size;
   }
   if (i != size)
      truncated = 1;
   if (nal_count_out)
      *nal_count_out = count;
   if (first_nal_out)
      *first_nal_out = first_nal;
   if (truncated_out)
      *truncated_out = truncated;
   return mask;
}

static int media_h264_guess_avcc_length_size(const uint8_t *data, size_t size)
{
   static const int candidates[] = { 4, 2, 1, 3 };

   for (unsigned i = 0; i < ARRAY_SIZE(candidates); i++) {
      unsigned count = 0;
      int truncated = 0;

      (void)media_h264_avcc_nal_mask(data, size, candidates[i], &count,
         NULL, &truncated);
      if (count > 0 && !truncated)
         return candidates[i];
   }
   return 0;
}

static int media_video_packet_time_ms(const AVPacket *packet,
   AVRational time_base, int prefer_dts)
{
   int64_t t;

   if (!packet)
      return -1;
   t = prefer_dts ? packet->dts : packet->pts;
   if (t == AV_NOPTS_VALUE)
      t = prefer_dts ? packet->pts : packet->dts;
   if (t == AV_NOPTS_VALUE)
      return -1;
   t = av_rescale_q(t, time_base, (AVRational){ 1, 1000 });
   if (t > INT32_MAX)
      return INT32_MAX;
   if (t < INT32_MIN)
      return INT32_MIN;
   return (int32_t)t;
}

static int media_video_send_packet(int fd, const AVPacket *packet,
   AVRational time_base, int freerun, int h264)
{
   AvPktHd header;
   unsigned packet_index;
   unsigned nal_count = 0;
   unsigned nal_mask = 0;
   int packet_mode = MEDIA_H264_MODE_UNKNOWN;
   int first_nal = -1;
   int nal_length_size = media_h264_nal_length_size;
   int truncated = 0;
   int log_packet;
   int prefer_dts = h264;
   int packet_pts;
   int packet_dur;

   if (fd < 0 || !packet || !packet->data || packet->size <= 0)
      return -1;
   packet_index = media_video_debug_packets++;
   packet_pts = media_video_packet_time_ms(packet, time_base, prefer_dts);
   packet_dur = media_packet_duration_ms(packet, time_base);
   if (packet_dur < 0)
      packet_dur = 0;
   if (freerun) {
      /*
       * No hardware AV sync clock is available in freerun mode (for example
       * when audio is software-decoded), so always force decoder-side freerun.
       */
      packet_pts = -1;
      packet_dur = 0;
   }
   memset(&header, 0, sizeof(header));
   header.pts = packet_pts;
   header.dur = packet_dur;
   header.size = (uint32_t)packet->size;
   header.flag = AV_PACKET_ES_DATA;
   if (h264) {
      if (media_h264_packet_mode == MEDIA_H264_MODE_AVCC) {
         if (nal_length_size <= 0)
            nal_length_size = media_h264_guess_avcc_length_size(packet->data,
               (size_t)packet->size);
         if (nal_length_size > 0) {
            packet_mode = MEDIA_H264_MODE_AVCC;
            nal_mask = media_h264_avcc_nal_mask(packet->data,
               (size_t)packet->size, nal_length_size, &nal_count,
               &first_nal, &truncated);
         }
      }
      if (packet_mode == MEDIA_H264_MODE_UNKNOWN &&
          media_h264_has_annexb_start(packet->data, (size_t)packet->size)) {
         packet_mode = MEDIA_H264_MODE_ANNEXB;
         first_nal = media_h264_first_nal_type(packet->data,
            (size_t)packet->size);
         nal_mask = media_h264_nal_mask(packet->data, (size_t)packet->size,
            &nal_count);
      } else if (packet_mode == MEDIA_H264_MODE_UNKNOWN) {
         if (nal_length_size <= 0)
            nal_length_size = media_h264_guess_avcc_length_size(packet->data,
               (size_t)packet->size);
         if (nal_length_size > 0) {
            packet_mode = MEDIA_H264_MODE_AVCC;
            nal_mask = media_h264_avcc_nal_mask(packet->data,
               (size_t)packet->size, nal_length_size, &nal_count,
               &first_nal, &truncated);
         }
      }
   }
   log_packet = packet_index < 16u || (packet_index % 120u) == 0 ||
      (packet->flags & AV_PKT_FLAG_KEY) || (nal_mask & 0x1eu);
   if (log_packet) {
      const uint8_t *d = packet->data;

      printf("unifrog media native video packet idx=%u size=%d send=%lu pts=%ld dur=%ld src_pts=%ld src_dts=%ld src_dur=%ld prefer_dts=%d key=%d aud=0 mode=%s cfg_mode=%s nal_len=%d nal=%d nals=%u mask=0x%x trunc=%d bytes=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x hdr=%u extra=%s\n",
         packet_index, packet->size, (unsigned long)header.size,
         (long)header.pts, (long)header.dur,
         (long)packet->pts, (long)packet->dts, (long)packet->duration,
         prefer_dts,
         (packet->flags & AV_PKT_FLAG_KEY) ? 1 : 0,
         media_h264_mode_name(packet_mode),
         media_h264_mode_name(media_h264_packet_mode),
         nal_length_size, first_nal, nal_count, nal_mask, truncated,
         packet->size > 0 ? d[0] : 0, packet->size > 1 ? d[1] : 0,
         packet->size > 2 ? d[2] : 0, packet->size > 3 ? d[3] : 0,
         packet->size > 4 ? d[4] : 0, packet->size > 5 ? d[5] : 0,
         packet->size > 6 ? d[6] : 0, packet->size > 7 ? d[7] : 0,
         packet->size > 8 ? d[8] : 0, packet->size > 9 ? d[9] : 0,
         packet->size > 10 ? d[10] : 0, packet->size > 11 ? d[11] : 0,
         (unsigned)sizeof(header),
         media_h264_extra_delivery_name(media_h264_extra_delivery));
   }
   if (media_video_wait_write_space(fd,
       (uint32_t)sizeof(header) + header.size, packet_index) != 0)
      return -1;
   if (media_write_all(fd, &header, sizeof(header)) != 0 ||
       media_write_all(fd, packet->data, (size_t)packet->size) != 0) {
      printf("unifrog media native video packet_write_failed idx=%u errno=%d size=%d\n",
         packet_index, errno, packet->size);
      return -1;
   }
   if (log_packet) {
      struct vdec_decore_status status;

      memset(&status, 0, sizeof(status));
      if (ioctl(fd, VIDDEC_GET_STATUS, &status) == 0)
         printf("unifrog media native video status_after_packet idx=%u decoded=%lu displayed=%lu hdr=%d pic=%d show=%d eos=%u err=%lu underrun=%lu used=%lu/%lu\n",
            packet_index,
            (unsigned long)status.frames_decoded,
            (unsigned long)status.frames_displayed,
            status.first_header_got,
            status.first_pic_decoded,
            status.first_pic_showed,
            (unsigned)status.get_pkt_eos,
            (unsigned long)status.decode_error,
            (unsigned long)status.under_run_cnt,
            (unsigned long)status.buffer_used,
            (unsigned long)status.buffer_size);
   }
   return 0;
}

static int media_video_wait_write_space(int fd, uint32_t need,
   unsigned packet_index)
{
   uint32_t start_ms;
   unsigned polls = 0;

   if (fd < 0 || need == 0)
      return -1;
   start_ms = unifrog_perf_time_ms();
   for (;;) {
      struct vdec_decore_status status;
      uint32_t elapsed;

      memset(&status, 0, sizeof(status));
      errno = 0;
      if (ioctl(fd, VIDDEC_GET_STATUS, &status) != 0) {
         printf("unifrog media native video queue_status failed idx=%u errno=%d need=%lu\n",
            packet_index, errno, (unsigned long)need);
         return 0;
      }
      if (status.buffer_size == 0 || need > status.buffer_size ||
          status.buffer_used + need <= status.buffer_size)
         return 0;
      elapsed = unifrog_perf_time_ms() - start_ms;
      if (elapsed >= VIDEO_WRITE_SPACE_TIMEOUT_MS) {
         printf("unifrog media native video queue_timeout idx=%u need=%lu used=%lu/%lu decoded=%lu displayed=%lu hdr=%d pic=%d eos=%u err=%lu underrun=%lu waited=%lu\n",
            packet_index, (unsigned long)need,
            (unsigned long)status.buffer_used,
            (unsigned long)status.buffer_size,
            (unsigned long)status.frames_decoded,
            (unsigned long)status.frames_displayed,
            status.first_header_got,
            status.first_pic_decoded,
            (unsigned)status.get_pkt_eos,
            (unsigned long)status.decode_error,
            (unsigned long)status.under_run_cnt,
            (unsigned long)elapsed);
         return -1;
      }
      if (polls == 0 || (polls % 200u) == 0)
         printf("unifrog media native video queue_wait idx=%u need=%lu used=%lu/%lu decoded=%lu displayed=%lu hdr=%d pic=%d waited=%lu\n",
            packet_index, (unsigned long)need,
            (unsigned long)status.buffer_used,
            (unsigned long)status.buffer_size,
            (unsigned long)status.frames_decoded,
            (unsigned long)status.frames_displayed,
            status.first_header_got,
            status.first_pic_decoded,
            (unsigned long)elapsed);
      polls++;
      usleep(VIDEO_WRITE_SPACE_POLL_US);
   }
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

static void media_video_finish_eos(int fd, unsigned timeout_ms)
{
   uint32_t start_ms;
   int eos = 0;

   if (fd < 0)
      return;
   media_video_send_eos(fd);
   start_ms = unifrog_perf_time_ms();
   while (!eos) {
      if (ioctl(fd, VIDDEC_CHECK_EOS, &eos) != 0)
         break;
      if (eos)
         break;
      if (timeout_ms && unifrog_perf_time_ms() - start_ms >= timeout_ms)
         break;
      usleep(20 * 1000);
   }
   printf("unifrog media native video finish eos=%d wait_ms=%lu timeout=%u\n",
      eos, (unsigned long)(unifrog_perf_time_ms() - start_ms),
      timeout_ms);
}

static int media_send_packet_blob(int fd, const uint8_t *data, int size,
   uint32_t flag)
{
   AvPktHd header;

   if (fd < 0 || !data || size <= 0)
      return 0;
   memset(&header, 0, sizeof(header));
   header.pts = 0;
   header.size = (uint32_t)size;
   header.flag = flag;
   if (media_write_all(fd, &header, sizeof(header)) != 0 ||
       media_write_all(fd, data, (size_t)size) != 0)
      return -1;
   return 0;
}

static int media_send_extra_packet(int fd, const uint8_t *data, int size)
{
   return media_send_packet_blob(fd, data, size, AV_PACKET_EXTRA_DATA);
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
   uint32_t packet_index;

   if (!auddec || auddec->fd < 0 || !packet || !packet->data ||
       packet->size <= 0)
      return -1;
   packet_index = auddec->packets;
   memset(&header, 0, sizeof(header));
   header.pts = media_packet_pts_ms(packet, auddec->time_base);
   header.dur = media_packet_duration_ms(packet, auddec->time_base);
   header.size = (uint32_t)packet->size;
   header.flag = AV_PACKET_ES_DATA;
   if (media_auddec_write_all(auddec, &header, sizeof(header)) != 0)
      return -1;
   if (media_auddec_write_all(auddec, packet->data,
       (size_t)packet->size) != 0)
      return -1;
   auddec->packets++;
   media_auddec_log_packet_status(auddec, "compressed", packet_index,
      packet->data, (size_t)packet->size, header.pts, header.dur);
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
   (void)media_auddec_write_all(auddec, &header, sizeof(header));
}

static void media_auddec_close(struct media_auddec *auddec)
{
   int had_output;

   if (!auddec)
      return;
   had_output = auddec->fd >= 0 || auddec->prime_fd >= 0;
   if (auddec->fd >= 0)
      media_auddec_release_fd(&auddec->fd, "auddec_close");
   media_gb300_i2so_prime_close(&auddec->prime_fd, "auddec_close");
   if (had_output) {
      unifrog_audio_set_system_output_enabled(0);
      auddec->output_enabled = 0;
   }
}

static void media_auddec_release_fd(int *fdp, const char *tag)
{
   int fd;
   int close_errno;

   if (!fdp || *fdp < 0)
      return;
   fd = *fdp;
   *fdp = -1;
   errno = 0;
   close(fd);
   close_errno = errno;
   printf("unifrog media auddec release tag=%s fd=%d mode=close_only close_errno=%d\n",
      tag ? tag : "?", fd, close_errno);
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

static void media_auddec_log_caps(int fd, const char *scope,
   const char *label, uint32_t codec_id, int sync_mode)
{
   unsigned int caps = 0;
   int caps_ret;
   int caps_errno;

   if (fd < 0)
      return;
   errno = 0;
   caps_ret = ioctl(fd, AUDDEC_GET_CAPABILITIES, &caps);
   caps_errno = errno;
   printf("unifrog media auddec caps_optional scope=%s label=%s fd=%d ret=%d errno=%d caps=0x%lx codec=%lu sync=%d\n",
      scope ? scope : "?", label ? label : "?", fd, caps_ret, caps_errno,
      (unsigned long)caps, (unsigned long)codec_id, sync_mode);
}

static int media_auddec_status_decode_stalled(
   const struct audio_decore_status *status)
{
   return status &&
      status->frames_decoded == 0 &&
      !status->first_header_got &&
      !status->first_header_parsed;
}

static int media_auddec_clock_has_progress(int time_ok, int64_t cur_time)
{
   return time_ok && cur_time > 0;
}

static int media_auddec_runtime_decode_stalled(
   const struct audio_decore_status *status, int time_ok, int64_t cur_time)
{
   return media_auddec_status_decode_stalled(status) &&
      !media_auddec_clock_has_progress(time_ok, cur_time);
}

static int media_auddec_status_has_progress(
   const struct audio_decore_status *status)
{
   return status &&
      (status->frames_decoded > 0 ||
       status->first_header_got ||
       status->first_header_parsed);
}

static void media_auddec_enable_output_on_progress(
   struct media_auddec *auddec, const struct audio_decore_status *status,
   const char *scope, uint32_t packet_index)
{
   if (!auddec || auddec->fd < 0 || auddec->output_enabled ||
       !unifrog_audio_prefers_stereo_output() ||
       !media_auddec_status_has_progress(status))
      return;
   unifrog_audio_set_system_output_enabled(1);
   auddec->output_enabled = 1;
   printf("unifrog media auddec output_enable reason=decode_progress scope=%s idx=%lu decoded=%lu hdr=%u/%u\n",
      scope ? scope : "?", (unsigned long)packet_index,
      (unsigned long)status->frames_decoded,
      status->first_header_got, status->first_header_parsed);
}

static void media_auddec_enable_output_on_clock_progress(
   struct media_auddec *auddec, int64_t cur_time, const char *scope,
   uint32_t packet_index)
{
   if (!auddec || auddec->fd < 0 || auddec->output_enabled ||
       !unifrog_audio_prefers_stereo_output() || cur_time <= 0)
      return;
   unifrog_audio_set_system_output_enabled(1);
   auddec->output_enabled = 1;
   printf("unifrog media auddec output_enable reason=clock_progress scope=%s idx=%lu atime=%lld\n",
      scope ? scope : "?", (unsigned long)packet_index,
      (long long)cur_time);
}

static void media_auddec_log_packet_status(struct media_auddec *auddec,
   const char *scope, uint32_t packet_index, const uint8_t *data, size_t size,
   int32_t pts, int32_t dur)
{
   struct audio_decore_status status;
   int should_log;
   int should_probe_progress;
   int status_ret;
   int status_errno;
   int64_t cur_time = -1;
   int time_ret;
   int time_errno;

   if (!auddec || auddec->fd < 0)
      return;
   should_log = packet_index < 4u || (packet_index % 512u) == 0;
   should_probe_progress = unifrog_audio_prefers_stereo_output() &&
      !auddec->output_enabled &&
      packet_index < MEDIA_GB300_AUDDEC_STALL_PACKETS;
   if (!should_log && !should_probe_progress)
      return;
   memset(&status, 0, sizeof(status));
   errno = 0;
   status_ret = ioctl(auddec->fd, AUDDEC_GET_STATUS, &status);
   status_errno = errno;
   if (status_ret == 0)
      media_auddec_enable_output_on_progress(auddec, &status, scope,
         packet_index);
   if (!should_log)
      return;
   errno = 0;
   time_ret = ioctl(auddec->fd, AUDDEC_GET_CUR_TIME, &cur_time);
   time_errno = errno;
   if (time_ret == 0)
      media_auddec_enable_output_on_clock_progress(auddec, cur_time, scope,
         packet_index);
   printf("unifrog media auddec packet_status scope=%s idx=%lu size=%lu pts=%ld dur=%ld first=%02x %02x %02x %02x status=%d status_errno=%d decoded=%lu rate=%lu ch=%u bits=%u hdr=%u/%u time=%lld time_ret=%d time_errno=%d freerun=%d\n",
      scope ? scope : "?", (unsigned long)packet_index,
      (unsigned long)size, (long)pts, (long)dur,
      size > 0 && data ? data[0] : 0, size > 1 && data ? data[1] : 0,
      size > 2 && data ? data[2] : 0, size > 3 && data ? data[3] : 0,
      status_ret, status_errno, (unsigned long)status.frames_decoded,
      (unsigned long)status.sample_rate, status.channels,
      status.bits_per_sample, status.first_header_got,
      status.first_header_parsed, (long long)cur_time, time_ret,
      time_errno, auddec->freerun);
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
   header.pts = pts;
   header.dur = dur;
   header.size = (uint32_t)size;
   header.flag = AV_PACKET_ES_DATA;
   if (media_auddec_write_all(auddec, &header, sizeof(header)) != 0 ||
       media_auddec_write_all(auddec, data, size) != 0)
      return -1;
   auddec->packets++;
   media_auddec_log_packet_status(auddec, "raw", packet_index, data, size,
      header.pts, header.dur);
   return 0;
}

struct media_gb300_auddec_probe_variant {
   const char *label;
   unsigned sample_rate;
   uint32_t snd_devs;
   int enable_audsink;
   int audio_flush_thres;
   int kshm_size;
   int gate_before_init;
   int controls_before_start;
   int prime_stage;
   uint32_t prime_dest;
};

static int16_t
   media_gb300_auddec_probe_pcm[MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES *
      MEDIA_GB300_AUDDEC_PROBE_CHANNELS];
static int16_t
   media_gb300_production_tone_pcm[MEDIA_GB300_PRODUCTION_TONE_FRAMES * 2u];

static void media_audio_diag_progress(unifrog_media_progress_cb progress,
   void *userdata, const char *stage, unsigned done, unsigned total)
{
   if (progress)
      progress(userdata, stage ? stage : "", done, total);
}

static void media_fill_gb300_production_tone(unsigned packet,
   unsigned channels)
{
   unsigned half_period = 22u + (packet % 4u) * 11u;
   int amplitude = 9000;

   if (channels == 0 || channels > 2u)
      channels = 2u;
   for (unsigned i = 0; i < MEDIA_GB300_PRODUCTION_TONE_FRAMES; i++) {
      int16_t sample = (((packet * MEDIA_GB300_PRODUCTION_TONE_FRAMES + i) /
         half_period) & 1u) ? (int16_t)amplitude : (int16_t)-amplitude;

      media_gb300_production_tone_pcm[i * channels] = sample;
      if (channels > 1u)
         media_gb300_production_tone_pcm[i * channels + 1u] = sample;
   }
}

static int media_run_gb300_production_tone_probe(const char *tag)
{
   struct unifrog_audio audio;
   unsigned channels = media_audio_output_channels();
   int ret = -1;
   unsigned writes = 0;

   if (!unifrog_audio_prefers_stereo_output())
      return -1;
   if (channels == 0 || channels > 2u)
      channels = 2u;
   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   printf("unifrog media gb300_prod_tone begin tag=%s rate=%u ch=%u packets=%u frames=%u\n",
      tag ? tag : "?", MEDIA_GB300_AUDDEC_PROBE_RATE, channels,
      MEDIA_GB300_PRODUCTION_TONE_PACKETS, MEDIA_GB300_PRODUCTION_TONE_FRAMES);
   if (unifrog_audio_open(&audio, MEDIA_GB300_AUDDEC_PROBE_RATE, channels,
       512, 8) != 0) {
      printf("unifrog media gb300_prod_tone open_failed tag=%s ch=%u\n",
         tag ? tag : "?", channels);
      return -1;
   }
   (void)unifrog_audio_set_volume(&audio, media_audio_runtime_volume());
   (void)unifrog_audio_set_mute(&audio, 1);
   (void)unifrog_audio_start(&audio);
   (void)unifrog_audio_set_output_enabled(&audio, 1);
   unifrog_audio_debug_dump(&audio, "gb300_prod_tone_after_start");
   for (unsigned packet = 0; packet < MEDIA_GB300_PRODUCTION_TONE_PACKETS;
        packet++) {
      media_fill_gb300_production_tone(packet, channels);
      media_log_pcm_stats("gb300_prod_tone", &audio,
         media_gb300_production_tone_pcm, MEDIA_GB300_PRODUCTION_TONE_FRAMES,
         tag);
      if (unifrog_audio_write(&audio, media_gb300_production_tone_pcm,
          MEDIA_GB300_PRODUCTION_TONE_FRAMES) != 0)
         break;
      writes++;
      usleep(10000);
   }
   ret = writes == MEDIA_GB300_PRODUCTION_TONE_PACKETS ? 0 : -1;
   printf("unifrog media gb300_prod_tone end tag=%s ret=%d writes=%u/%u\n",
      tag ? tag : "?", ret, writes, MEDIA_GB300_PRODUCTION_TONE_PACKETS);
   usleep(250000);
   unifrog_audio_close(&audio);
   return ret;
}

static void media_fill_gb300_auddec_probe_pcm(unsigned variant,
   unsigned packet)
{
   unsigned half_period = 18u + (variant % 7u) * 5u;
   int amplitude = 5200 + (int)(variant % 5u) * 900;
   unsigned base = packet * MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES;

   for (unsigned i = 0; i < MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES; i++) {
      int16_t sample = (((base + i) / half_period) & 1u) ?
         (int16_t)amplitude : (int16_t)-amplitude;

      for (unsigned ch = 0; ch < MEDIA_GB300_AUDDEC_PROBE_CHANNELS; ch++)
         media_gb300_auddec_probe_pcm[
            i * MEDIA_GB300_AUDDEC_PROBE_CHANNELS + ch] = sample;
   }
}

static void media_gb300_auddec_probe_controls(int fd, const char *label,
   const char *stage)
{
   audio_channel_select_t channel = media_audio_channel_select();
   uint8_t volume = 90u;
   unsigned int mute = 0;
   int channel_ret;
   int channel_errno;
   int volume_ret;
   int volume_errno;
   int mute_ret;
   int mute_errno;

   errno = 0;
   channel_ret = ioctl(fd, AUDIO_CHANNEL_SELECT, &channel);
   channel_errno = errno;
   errno = 0;
   volume_ret = ioctl(fd, AUDIO_SET_VOLUME, &volume);
   volume_errno = errno;
   errno = 0;
   mute_ret = ioctl(fd, AUDIO_SET_MUTE, &mute);
   mute_errno = errno;
   printf("unifrog media gb300_auddec_probe controls label=%s stage=%s fd=%d channel=%u channel_ret=%d channel_errno=%d volume=%u volume_ret=%d volume_errno=%d mute=%u mute_ret=%d mute_errno=%d\n",
      label ? label : "?", stage ? stage : "?", fd, (unsigned)channel,
      channel_ret, channel_errno, (unsigned)volume, volume_ret, volume_errno,
      mute, mute_ret, mute_errno);
}

static int media_gb300_auddec_probe_status(int fd, const char *label,
   const char *stage)
{
   struct audio_decore_status dec_status;
   audio_status_t audio_status;
   int64_t cur_time = -1;
   unsigned int underruns = 0;
   int dec_ret;
   int dec_errno;
   int audio_ret;
   int audio_errno;
   int time_ret;
   int time_errno;
   int underrun_ret;
   int underrun_errno;

   memset(&dec_status, 0, sizeof(dec_status));
   memset(&audio_status, 0, sizeof(audio_status));
   errno = 0;
   dec_ret = ioctl(fd, AUDDEC_GET_STATUS, &dec_status);
   dec_errno = errno;
   errno = 0;
   audio_ret = ioctl(fd, AUDIO_GET_STATUS, &audio_status);
   audio_errno = errno;
   errno = 0;
   time_ret = ioctl(fd, AUDDEC_GET_CUR_TIME, &cur_time);
   time_errno = errno;
   errno = 0;
   underrun_ret = ioctl(fd, AUDIO_GET_UNDERRUN_TIMES, &underruns);
   underrun_errno = errno;
   printf("unifrog media gb300_auddec_probe status label=%s stage=%s fd=%d dec=%d dec_errno=%d decoded=%lu rate=%lu ch=%u bits=%u hdr=%u/%u audio=%d audio_errno=%d play=%u mute=%u chsel=%u sync=%u bypass=%u time=%lld time_ret=%d time_errno=%d underruns=%lu underrun_ret=%d underrun_errno=%d\n",
      label ? label : "?", stage ? stage : "?", fd, dec_ret, dec_errno,
      (unsigned long)dec_status.frames_decoded,
      (unsigned long)dec_status.sample_rate, dec_status.channels,
      dec_status.bits_per_sample, dec_status.first_header_got,
      dec_status.first_header_parsed, audio_ret, audio_errno,
      (unsigned)audio_status.play_state, (unsigned)audio_status.mute_state,
      (unsigned)audio_status.channel_select,
      (unsigned)audio_status.AV_sync_state,
      (unsigned)audio_status.bypass_mode, (long long)cur_time, time_ret,
      time_errno, (unsigned long)underruns, underrun_ret, underrun_errno);
   return dec_ret == 0 && media_auddec_status_has_progress(&dec_status);
}

static int media_gb300_auddec_probe_variant(unsigned index,
   const struct media_gb300_auddec_probe_variant *variant)
{
   struct media_auddec auddec;
   struct audio_config cfg;
   int fd;
   int init_ret = -1;
   int init_errno = 0;
   int start_ret = -1;
   int start_errno = 0;
   int send_failed = 0;
   int decode_progress = 0;
   int prime_fd = -1;
   int prime_attempted = 0;
   unsigned sample_rate = variant && variant->sample_rate ?
      variant->sample_rate : MEDIA_GB300_AUDDEC_PROBE_RATE;
   int32_t chunk_ms = (int32_t)((MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES *
      1000u) / sample_rate);

   if (!variant)
      return -1;
   if (variant->gate_before_init)
      unifrog_audio_set_system_output_enabled(1);
   fd = open("/dev/auddec", O_RDWR);
   if (fd < 0) {
      printf("unifrog media gb300_auddec_probe open label=%s idx=%u fd=-1 errno=%d\n",
         variant->label, index, errno);
      return -1;
   }
   if (variant->prime_stage == MEDIA_GB300_I2SO_PRIME_BEFORE_INIT) {
      prime_attempted = 1;
      prime_fd = media_gb300_i2so_prime_open(variant->label, sample_rate,
         MEDIA_GB300_AUDDEC_PROBE_CHANNELS, 16u, AVSYNC_TYPE_FREERUN,
         variant->prime_dest);
   }

   memset(&cfg, 0, sizeof(cfg));
   cfg.codec_id = HC_AVCODEC_ID_PCM_S16LE;
   cfg.sync_mode = AVSYNC_TYPE_FREERUN;
   cfg.bits_per_coded_sample = 16u;
   cfg.channels = MEDIA_GB300_AUDDEC_PROBE_CHANNELS;
   cfg.sample_rate = sample_rate;
   cfg.bit_rate = sample_rate *
      MEDIA_GB300_AUDDEC_PROBE_CHANNELS * 16u;
   cfg.block_align = MEDIA_GB300_AUDDEC_PROBE_CHANNELS *
      (16u / 8u);
   cfg.channel_layout =
      media_audio_output_layout(MEDIA_GB300_AUDDEC_PROBE_CHANNELS);
   cfg.snd_devs = variant->snd_devs;
   cfg.enable_audsink = variant->enable_audsink;
   cfg.audio_flush_thres = variant->audio_flush_thres;
   cfg.kshm_size = variant->kshm_size;
   cfg.buffering_start = MEDIA_AUDIO_BUFFERING_START_MS;
   cfg.buffering_end = MEDIA_AUDIO_BUFFERING_END_MS;

   errno = 0;
   init_ret = ioctl(fd, AUDDEC_INIT, &cfg);
   init_errno = errno;
   if (init_ret == 0 && variant->controls_before_start)
      media_gb300_auddec_probe_controls(fd, variant->label, "before_start");
   errno = 0;
   start_ret = init_ret == 0 ? ioctl(fd, AUDDEC_START, 0) : -1;
   start_errno = errno;
   if (init_ret == 0 && start_ret == 0 &&
       variant->prime_stage == MEDIA_GB300_I2SO_PRIME_AFTER_START) {
      prime_attempted = 1;
      prime_fd = media_gb300_i2so_prime_open(variant->label, sample_rate,
         MEDIA_GB300_AUDDEC_PROBE_CHANNELS, 16u, AVSYNC_TYPE_FREERUN,
         variant->prime_dest);
   }
   printf("unifrog media gb300_auddec_probe init label=%s idx=%u fd=%d init=%d init_errno=%d start=%d start_errno=%d snd=0x%lx audsink=%d flush=%d kshm=%d gate_pre=%d controls_pre=%d prime_stage=%d prime_fd=%d prime_dest=%lu rate=%u ch=%u bits=%u block=%u bitrate=%lu\n",
      variant->label, index, fd, init_ret, init_errno, start_ret,
      start_errno, (unsigned long)cfg.snd_devs, cfg.enable_audsink,
      cfg.audio_flush_thres, cfg.kshm_size, variant->gate_before_init,
      variant->controls_before_start, variant->prime_stage, prime_fd,
      (unsigned long)variant->prime_dest, cfg.sample_rate, cfg.channels,
      cfg.bits_per_coded_sample, cfg.block_align, (unsigned long)cfg.bit_rate);
   if (init_ret == 0 && start_ret == 0 &&
       !variant->controls_before_start)
      media_gb300_auddec_probe_controls(fd, variant->label, "after_start");
   if (!variant->gate_before_init && (!prime_attempted || prime_fd < 0))
      unifrog_audio_set_system_output_enabled(1);
   unifrog_audio_debug_dump(NULL, "gb300_auddec_probe");
   decode_progress |= media_gb300_auddec_probe_status(fd, variant->label,
      "after_start");

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = fd;
   auddec.prime_fd = -1;
   auddec.stream = 0;
   auddec.time_base = (AVRational){ 1, 1000 };
   auddec.freerun = 1;
   auddec.write_timeout_ms = 1500u;
   if (init_ret == 0 && start_ret == 0) {
      for (unsigned packet = 0;
           packet < MEDIA_GB300_AUDDEC_PROBE_PACKETS; packet++) {
         int32_t pts = (int32_t)(packet * (unsigned)chunk_ms);

         media_fill_gb300_auddec_probe_pcm(index, packet);
         if (media_auddec_send_raw(&auddec,
             (const uint8_t *)media_gb300_auddec_probe_pcm,
             sizeof(media_gb300_auddec_probe_pcm), pts, chunk_ms) != 0) {
            send_failed = 1;
            break;
         }
         usleep(12000);
      }
      decode_progress |= media_gb300_auddec_probe_status(fd, variant->label,
         "after_feed");
      usleep(MEDIA_GB300_AUDDEC_PROBE_PAUSE_US);
      decode_progress |= media_gb300_auddec_probe_status(fd, variant->label,
         "after_pause");
      media_auddec_send_eos(&auddec);
   }

   close(fd);
   media_gb300_i2so_prime_close(&prime_fd, variant->label);
   unifrog_audio_set_system_output_enabled(0);
   usleep(80000);
   printf("unifrog media gb300_auddec_probe done label=%s idx=%u fd=%d packets=%lu send_failed=%d progress=%d\n",
      variant->label, index, fd, (unsigned long)auddec.packets,
      send_failed, decode_progress);
   return init_ret == 0 && start_ret == 0 && !send_failed &&
      decode_progress ? 0 : -1;
}

static int media_run_gb300_auddec_probe(const char *tag)
{
   static int running;
   static const struct media_gb300_auddec_probe_variant variants[] = {
      { "i2so_prime_after_dma", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_AFTER_START,
         SND_PCM_DEST_DMA },
      { "i2so_prime_before_dma", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_BEFORE_INIT,
         SND_PCM_DEST_DMA },
      { "default_prime_after_dma", 44100u, AUDDEV_DEFAULT, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_AFTER_START,
         SND_PCM_DEST_DMA },
      { "i2so_prime_after_bypass", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_AFTER_START,
         SND_PCM_DEST_BYPASS },
      { "i2so_audsink_prime_after", 44100u, AUDDEV_I2SO, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_AFTER_START,
         SND_PCM_DEST_DMA },
      { "i2so_current_after", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_gate_before", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 1, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_controls_before", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 1, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_audsink_after", 44100u, AUDDEV_I2SO, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "default_current_after", 44100u, AUDDEV_DEFAULT, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_flush200_after", 44100u, AUDDEV_I2SO, 0, 200,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_48k_after", 48000u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_spo_after", 44100u, AUDDEV_I2SO | AUDDEV_SPO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
   };
   unsigned ok = 0;

   if (!unifrog_audio_prefers_stereo_output() || running)
      return -1;
   running = 1;
   media_init_drivers_once();
   printf("unifrog media gb300_auddec_probe begin tag=%s variants=%lu rate=%u chunk_frames=%u packets=%u note=auddec_pcm_plus_i2so_prime\n",
      tag ? tag : "?", (unsigned long)ARRAY_SIZE(variants),
      MEDIA_GB300_AUDDEC_PROBE_RATE,
      MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES,
      MEDIA_GB300_AUDDEC_PROBE_PACKETS);
   unifrog_audio_debug_dump(NULL, "gb300_auddec_probe_begin");
   for (unsigned i = 0; i < ARRAY_SIZE(variants); i++) {
      if (media_gb300_auddec_probe_variant(i, &variants[i]) == 0)
         ok++;
   }
   unifrog_audio_debug_dump(NULL, "gb300_auddec_probe_end");
   printf("unifrog media gb300_auddec_probe end tag=%s ok=%u variants=%lu\n",
      tag ? tag : "?", ok, (unsigned long)ARRAY_SIZE(variants));
   running = 0;
   return ok > 0 ? 0 : -1;
}

static void media_run_gb300_auddec_probe_once(const char *tag)
{
   static int done;

   if (!UNIFROG_MEDIA_GB300_AUDDEC_PROBE_ONCE || done)
      return;
   done = 1;
   (void)media_run_gb300_auddec_probe(tag);
}

int unifrog_media_run_audio_diagnostics_ex(char *summary, size_t summary_size,
   unifrog_media_progress_cb progress, void *userdata)
{
   uint32_t start_ms = unifrog_perf_time_ms();
   int prod_ret;
   int direct_ret;
   int auddec_ret;
   int ret;

   printf("unifrog media audio_diag begin gb300=%d\n",
      unifrog_audio_prefers_stereo_output() ? 1 : 0);
   media_audio_diag_progress(progress, userdata, "production tone", 8, 100);
   unifrog_audio_debug_dump(NULL, "audio_diag_begin");
   prod_ret = media_run_gb300_production_tone_probe("audio_diag");
   media_audio_diag_progress(progress, userdata, "direct routes", 30, 100);
   direct_ret = unifrog_audio_run_gb300_route_probe("audio_diag");
   media_audio_diag_progress(progress, userdata, "auddec routes", 62, 100);
   auddec_ret = media_run_gb300_auddec_probe("audio_diag");
   unifrog_audio_set_system_output_enabled(0);
   unifrog_audio_debug_dump(NULL, "audio_diag_end");
   ret = prod_ret == 0 || direct_ret == 0 || auddec_ret == 0 ? 0 : -1;
   if (summary && summary_size > 0) {
      snprintf(summary, summary_size, "prod=%d direct=%d auddec=%d %lums",
         prod_ret, direct_ret, auddec_ret,
         (unsigned long)(unifrog_perf_time_ms() - start_ms));
   }
   media_audio_diag_progress(progress, userdata, "done", 100, 100);
   printf("unifrog media audio_diag end ret=%d prod=%d direct=%d auddec=%d ms=%lu summary=%s\n",
      ret, prod_ret, direct_ret, auddec_ret,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      summary ? summary : "");
   return ret;
}

int unifrog_media_run_audio_diagnostics(char *summary, size_t summary_size)
{
   return unifrog_media_run_audio_diagnostics_ex(summary, summary_size, NULL,
      NULL);
}

static int media_auddec_open_raw(const char *label, uint32_t codec_id,
   unsigned sample_rate, unsigned channels, unsigned bits,
   const uint8_t *extradata, unsigned extradata_size, int sync_mode,
   struct media_auddec *auddec)
{
   struct audio_config cfg;
   struct audio_config base_cfg;
   static const struct media_raw_auddec_variant variants[] = {
      /*
       * The linked libauddrv.a treats enable_audsink == 0 as the internal
       * audsink render path. Stock direct-decoder examples leave it zero.
       *
       * GB300 runtime diagnostics only expose /dev/sndC0i2so. Keep I2SO-only
       * first and non-rotated; SPO/PCMO routes can init successfully while
       * still producing no audible output on that board.
       */
      { "gb300_raw_i2so_prime_kshm", AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 1, SND_PCM_DEST_DMA },
      { "gb300_raw_i2so_kshm", AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_raw_i2so_audsink_kshm", AUDDEV_I2SO, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_raw_i2so_spo_kshm", AUDDEV_I2SO | AUDDEV_SPO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_raw_i2so_spo_audsink_kshm", AUDDEV_I2SO | AUDDEV_SPO, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_raw_spo_kshm", AUDDEV_SPO, 0, 0, MEDIA_AUDIO_KSHM_SIZE,
         0, SND_PCM_DEST_DMA },
      { "gb300_raw_spo_audsink_kshm", AUDDEV_SPO, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_raw_pcmo_kshm", AUDDEV_PCMO, 0, 0, MEDIA_AUDIO_KSHM_SIZE,
         0, SND_PCM_DEST_DMA },
      { "gb300_raw_i2so_pcmo_kshm", AUDDEV_I2SO | AUDDEV_PCMO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "raw_hcrtos_i2so_kshm", AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "raw_hcrtos_default_kshm", AUDDEV_DEFAULT, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "raw_compat_audsink_i2so_kshm", AUDDEV_I2SO, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "raw_minimal_i2so", AUDDEV_I2SO, 0, 0, 0, 0,
         SND_PCM_DEST_DMA },
      { "raw_minimal", AUDDEV_DEFAULT, 0, 0, 0, 0, SND_PCM_DEST_DMA },
   };
   int fd;
   audio_channel_select_t channel = media_audio_channel_select();
   uint8_t volume = media_audio_runtime_volume();
   int caps_logged = 0;
   unsigned gb300_route_offset = 0;

   if (!auddec || !codec_id)
      return -1;
   media_init_drivers_once();
   media_run_gb300_auddec_probe_once("raw_auddec_open");
   memset(auddec, 0, sizeof(*auddec));
   auddec->fd = -1;
   auddec->prime_fd = -1;
   auddec->output_enabled = 0;
   auddec->stream = 0;
   auddec->time_base = (AVRational){ 1, 1000 };
   auddec->freerun = sync_mode == 0;
   auddec->write_timeout_ms = MEDIA_AUDIO_WRITE_SPACE_TIMEOUT_MS;
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
   if (unifrog_audio_prefers_stereo_output())
      gb300_route_offset = media_gb300_raw_auddec_route_counter++ %
         MEDIA_GB300_RAW_AUDDEC_ROUTE_VARIANTS;
   printf("unifrog media raw auddec route_policy label=%s gb300=%d preferred_snd=0x%lx output_ch=%u variants=%lu gb_routes=%u gb_offset=%u\n",
      label ? label : "?", unifrog_audio_prefers_stereo_output(),
      (unsigned long)media_audio_preferred_snd_devs(),
      media_audio_output_channels(), (unsigned long)ARRAY_SIZE(variants),
      MEDIA_GB300_RAW_AUDDEC_ROUTE_VARIANTS, gb300_route_offset);

   for (unsigned order = 0; order < ARRAY_SIZE(variants); order++) {
      unsigned i = media_auddec_rotated_variant_index(order,
         MEDIA_GB300_RAW_AUDDEC_ROUTE_VARIANTS, gb300_route_offset);
      const struct media_raw_auddec_variant *variant = &variants[i];
      int extra_ret = 0;
      int init_ret;
      int init_errno;
      int start_ret;
      int start_errno;
      int prime_fd = -1;

      if (!media_auddec_variant_allowed(variant->label)) {
         printf("unifrog media raw auddec variant skip label=%s idx=%u order=%u gb300=%d\n",
            variant->label, i, order, unifrog_audio_prefers_stereo_output());
         continue;
      }
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
      if (!caps_logged) {
         media_auddec_log_caps(fd, "raw", label, codec_id, sync_mode);
         caps_logged = 1;
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
      printf("unifrog media raw auddec init label=%s try=%s idx=%u order=%u fd=%d extra=%d init=%d init_errno=%d start=%d start_errno=%d codec=%lu rate=%u ch=%u bits=%u x=%u mode=%u sync=%d snd=0x%lx audsink=%d kshm=%d prime=%d prime_dest=%lu\n",
         label ? label : "?", variant->label, i, order, fd, extra_ret,
         init_ret, init_errno, start_ret, start_errno, (unsigned long)codec_id,
         cfg.sample_rate, cfg.channels, cfg.bits_per_coded_sample,
         cfg.extradata_size, cfg.extradata_mode, sync_mode,
         (unsigned long)cfg.snd_devs, cfg.enable_audsink, cfg.kshm_size,
         variant->gb300_i2so_prime, (unsigned long)variant->prime_dest);
      if (extra_ret == 0 && init_ret == 0 && start_ret == 0) {
         int channel_ret;
         int channel_errno;
         int volume_ret;
         int volume_errno;

         errno = 0;
         channel_ret = ioctl(fd, AUDIO_CHANNEL_SELECT, &channel);
         channel_errno = errno;
         errno = 0;
         volume_ret = ioctl(fd, AUDIO_SET_VOLUME, &volume);
         volume_errno = errno;
         if (variant->gb300_i2so_prime) {
            prime_fd = media_gb300_i2so_prime_open(variant->label,
               cfg.sample_rate, media_audio_output_channels(),
               cfg.bits_per_coded_sample, cfg.sync_mode,
               variant->prime_dest);
            if (prime_fd < 0) {
               printf("unifrog media raw auddec prime failed label=%s try=%s fd=%d sync=%d snd=0x%lx dest=%lu\n",
                  label ? label : "?", variant->label, fd, sync_mode,
                  (unsigned long)cfg.snd_devs,
                  (unsigned long)variant->prime_dest);
               media_auddec_release_fd(&fd, "raw_prime_failed");
               continue;
            }
         } else {
            if (!unifrog_audio_prefers_stereo_output())
               unifrog_audio_set_system_output_enabled(1);
         }
         unifrog_audio_debug_dump(NULL, "raw_auddec_open");
         auddec->fd = fd;
         auddec->prime_fd = prime_fd;
         auddec->output_enabled = !unifrog_audio_prefers_stereo_output();
         printf("unifrog media raw auddec open ok label=%s try=%s fd=%d sync=%d snd=0x%lx audsink=%d flush=%d kshm=%d prime_fd=%d prime_dest=%lu output_ch=%u channel_select=%u channel_ret=%d channel_errno=%d volume_ret=%d volume_errno=%d\n",
            label ? label : "?", variant->label, fd, sync_mode,
            (unsigned long)cfg.snd_devs, cfg.enable_audsink,
            cfg.audio_flush_thres, cfg.kshm_size, prime_fd,
            (unsigned long)variant->prime_dest, media_audio_output_channels(),
            (unsigned)channel, channel_ret, channel_errno, volume_ret,
            volume_errno);
         return 0;
      }
      media_auddec_release_fd(&fd, "raw_open_failed");
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
      /*
       * KSHM-backed profiles must come first. On this driver a no-KSHM
       * profile can init/start cleanly but never consume compressed packets.
       *
       * GB300 only has a working I2SO SND node in current diagnostics. Do not
       * rotate into SPO/PCMO as the first route: those masks can initialize
       * while leaving the decoder clock stuck and the speaker silent.
       */
      { "gb300_i2so_prime_kshm", 0, AUDDEV_I2SO, 0, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 1, SND_PCM_DEST_DMA },
      { "gb300_i2so_kshm", 0, AUDDEV_I2SO, 0, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_i2so_audsink_kshm", 0, AUDDEV_I2SO, 1, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_i2so_spo_kshm", 0, AUDDEV_I2SO | AUDDEV_SPO, 0, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_i2so_spo_audsink_kshm", 0, AUDDEV_I2SO | AUDDEV_SPO, 1, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_spo_kshm", 0, AUDDEV_SPO, 0, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_spo_audsink_kshm", 0, AUDDEV_SPO, 1, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_pcmo_kshm", 0, AUDDEV_PCMO, 0, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "gb300_i2so_pcmo_kshm", 0, AUDDEV_I2SO | AUDDEV_PCMO, 0, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "hcrtos_i2so_kshm", 0, AUDDEV_I2SO, 0, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "hcrtos_default_kshm", 0, AUDDEV_DEFAULT, 0, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "hcrtos_i2so_min_kshm", 0, AUDDEV_I2SO, 0, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "compat_audsink_i2so_kshm", 0, AUDDEV_I2SO, 1, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "compat_audsink_default_kshm", 0, AUDDEV_DEFAULT, 1, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, SND_PCM_DEST_DMA },
      { "ffp_minimal_i2so", 0, AUDDEV_I2SO, 0, 0, 0, 0, 0,
         SND_PCM_DEST_DMA },
      { "ffp_minimal", 0, AUDDEV_DEFAULT, 0, 0, 0, 0, 0,
         SND_PCM_DEST_DMA },
      { "hcplayer_i2so_nokshm", 0, AUDDEV_I2SO, 1, 1, 0, 0, 0,
         SND_PCM_DEST_DMA },
      { "minimal_48k_nokshm", 48000, AUDDEV_DEFAULT, 0, 0, 0, 0, 0,
         SND_PCM_DEST_DMA },
   };
   int hc_codec;
   audio_channel_select_t channel = media_audio_channel_select();
   uint8_t volume = media_audio_runtime_volume();
   unsigned bits;
   uint32_t last_stage = 0;
   int caps_logged = 0;
   unsigned gb300_route_offset = 0;

   media_audio_activity_stage(1u,
      (((uint32_t)stream_index & 0xffffu) << 8) |
      ((uint32_t)sync_mode & 0xffu),
      fmt ? fmt->nb_streams : 0u);
   if (!auddec)
      return -1;
   auddec->fd = -1;
   auddec->prime_fd = -1;
   auddec->stream = -1;
   auddec->time_base = (AVRational){ 1, 1000 };
   auddec->packets = 0;
   auddec->freerun = sync_mode == 0;
   auddec->output_enabled = 0;
   auddec->write_timeout_ms = VIDEO_WRITE_SPACE_TIMEOUT_MS;
   auddec->aac_profile = 1;
   auddec->aac_sample_rate_index = 4;
   auddec->aac_channels = 2;
   if (!fmt || stream_index < 0 || stream_index >= (int)fmt->nb_streams) {
      printf("unifrog media auddec invalid_args fmt=0x%08lx stream=%d nb_streams=%u sync=%d\n",
         (unsigned long)(uintptr_t)fmt, stream_index,
         fmt ? fmt->nb_streams : 0u, sync_mode);
      media_audio_activity_stage(255u, 1u, 0u);
      return -1;
   }
   stream = fmt->streams[stream_index];
   if (!stream) {
      printf("unifrog media auddec invalid_stream stream_ptr=0x%08lx par_ptr=0x%08lx stream=%d\n",
         (unsigned long)(uintptr_t)stream,
         0ul, stream_index);
      media_audio_activity_stage(255u, 2u, 0u);
      return -1;
   }
   par = stream->codecpar;
   if (!par) {
      printf("unifrog media auddec invalid_stream stream_ptr=0x%08lx par_ptr=0x%08lx stream=%d\n",
         (unsigned long)(uintptr_t)stream,
         (unsigned long)(uintptr_t)par, stream_index);
      media_audio_activity_stage(255u, 2u, 0u);
      return -1;
   }
   hc_codec = media_hc_audio_codec_from_av(par->codec_id);
   printf("unifrog media auddec begin stream=%d fmt=0x%08lx stream_ptr=0x%08lx par_ptr=0x%08lx codec=%d name=%s tag=0x%lx rate=%d ch=%d bits=%d block=%d extra=%d sync=%d\n",
      stream_index, (unsigned long)(uintptr_t)fmt,
      (unsigned long)(uintptr_t)stream, (unsigned long)(uintptr_t)par,
      par->codec_id, media_avcodec_name(par->codec_id),
      (unsigned long)par->codec_tag, par->sample_rate, par->channels,
      par->bits_per_coded_sample, par->block_align, par->extradata_size,
      sync_mode);
   media_audio_activity_stage(2u,
      (((uint32_t)(par->codec_id & 0xffffu)) << 8) |
      ((uint32_t)sync_mode & 0xffu),
      (uint32_t)(par->extradata_size & 0xffffu));
   if (!hc_codec) {
      printf("unifrog media auddec unsupported codec=%d stream=%d\n",
         par->codec_id, stream_index);
      media_audio_activity_stage(255u, 3u, (uint32_t)par->codec_id);
      return -1;
   }
   media_run_gb300_auddec_probe_once("auddec_open");

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
       * HCPlayer passes MP4/M4A AAC as raw access units with
       * AudioSpecificConfig in audio_config. The 0153 GB300 ADTS-wrapped path
       * initialized cleanly but left the decoder clock stuck at zero, while
       * the earlier raw ASC path advanced the auddec clock.
       */
      media_aac_parse_asc(par, &auddec->aac_profile,
         &auddec->aac_sample_rate_index, &auddec->aac_channels);
      if (par->extradata && par->extradata_size > 0) {
         base_cfg.extradata_size = (uint32_t)par->extradata_size;
         if (par->extradata_size <= (int)sizeof(base_cfg.extra_data)) {
            base_cfg.extradata_mode = 0;
            memcpy(base_cfg.extra_data, par->extradata,
               (size_t)par->extradata_size);
         } else {
            base_cfg.extradata_mode = 1;
         }
      }
   } else if (par->extradata && par->extradata_size > 0) {
      base_cfg.extradata_size = (uint32_t)par->extradata_size;
      if (par->extradata_size <= (int)sizeof(base_cfg.extra_data)) {
         base_cfg.extradata_mode = 0;
         memcpy(base_cfg.extra_data, par->extradata,
            (size_t)par->extradata_size);
      } else {
         base_cfg.extradata_mode = 1;
      }
   }
   printf("unifrog media auddec base stream=%d codec=%u sync=%u rate=%u ch=%u bits=%u block=%u extra=%u mode=%u aac_packet=%s prof=%u sridx=%u\n",
      stream_index, base_cfg.codec_id, base_cfg.sync_mode,
      base_cfg.sample_rate, base_cfg.channels,
      base_cfg.bits_per_coded_sample, base_cfg.block_align,
      base_cfg.extradata_size, base_cfg.extradata_mode,
      par->codec_id == AV_CODEC_ID_AAC ?
      (base_cfg.extradata_size ? "raw_asc" : "raw_es") : "na",
      auddec->aac_profile, auddec->aac_sample_rate_index);
   if (unifrog_audio_prefers_stereo_output())
      gb300_route_offset = media_gb300_auddec_route_counter++ %
         MEDIA_GB300_AUDDEC_ROUTE_VARIANTS;
   printf("unifrog media auddec route_policy stream=%d gb300=%d preferred_snd=0x%lx output_ch=%u variants=%lu gb_routes=%u gb_offset=%u\n",
      stream_index, unifrog_audio_prefers_stereo_output(),
      (unsigned long)media_audio_preferred_snd_devs(),
      media_audio_output_channels(), (unsigned long)ARRAY_SIZE(variants),
      MEDIA_GB300_AUDDEC_ROUTE_VARIANTS, gb300_route_offset);

   for (unsigned order = 0; order < ARRAY_SIZE(variants); order++) {
      unsigned i = media_auddec_rotated_variant_index(order,
         MEDIA_GB300_AUDDEC_ROUTE_VARIANTS, gb300_route_offset);
      const struct media_auddec_variant *variant = &variants[i];
      int fd;
      int extra_ret = 0;
      int init_ret;
      int init_errno;
      int start_ret;
      int start_errno;
      int prime_fd = -1;

      if (!media_auddec_variant_allowed(variant->label)) {
         printf("unifrog media auddec variant skip label=%s idx=%u order=%u gb300=%d\n",
            variant->label, i, order, unifrog_audio_prefers_stereo_output());
         continue;
      }
      cfg = base_cfg;
      cfg.snd_devs = variant->snd_devs;
      cfg.enable_audsink = variant->enable_audsink;
      cfg.audio_flush_thres = variant->audio_flush_thres;
      cfg.buffering_start = MEDIA_AUDIO_BUFFERING_START_MS;
      cfg.buffering_end = MEDIA_AUDIO_BUFFERING_END_MS;
      cfg.kshm_size = variant->kshm_size;
      if (variant->force_rate)
         cfg.sample_rate = (uint32_t)variant->force_rate;
      if (variant->full_stream_fields) {
         cfg.codec_tag = par->codec_tag;
         cfg.bit_rate = par->bit_rate > 0 ? (uint32_t)par->bit_rate : 0u;
         cfg.block_align = par->block_align > 0 ?
            (uint32_t)par->block_align : 0u;
         cfg.channel_layout = par->channel_layout;
      }
      last_stage = 3u;
      media_audio_activity_stage(3u,
         (((uint32_t)i & 0xffu) << 16) |
         (((uint32_t)cfg.sync_mode & 0xffu) << 8) |
         ((uint32_t)cfg.extradata_mode & 0xffu),
         (uint32_t)(cfg.extradata_size & 0xffffu));
      printf("unifrog media auddec variant begin label=%s idx=%u order=%u stream=%d codec=%u av=%d name=%s rate=%u ch=%u bits=%u block=%u extra=%u mode=%u sync=%u snd=0x%lx audsink=%d flush=%d kshm=%d prime=%d prime_dest=%lu\n",
         variant->label, i, order, stream_index, cfg.codec_id, par->codec_id,
         media_avcodec_name(par->codec_id), cfg.sample_rate, cfg.channels,
         cfg.bits_per_coded_sample, cfg.block_align, cfg.extradata_size,
         cfg.extradata_mode, cfg.sync_mode, (unsigned long)cfg.snd_devs,
         cfg.enable_audsink, cfg.audio_flush_thres, cfg.kshm_size,
         variant->gb300_i2so_prime, (unsigned long)variant->prime_dest);

      fd = open("/dev/auddec", O_RDWR);
      if (fd < 0) {
         printf("unifrog media auddec open failed errno=%d stream=%d codec=%d try=%s\n",
            errno, stream_index, par->codec_id, variant->label);
         media_audio_activity_stage(4u,
            (((uint32_t)i & 0xffu) << 16) | 0xffffu,
            (uint32_t)(errno & 0xffffu));
         continue;
      }
      last_stage = 4u;
      media_audio_activity_stage(4u,
         (((uint32_t)i & 0xffu) << 16) | ((uint32_t)fd & 0xffffu),
         0u);
      if (!caps_logged) {
         media_auddec_log_caps(fd, "compressed", variant->label,
            cfg.codec_id, cfg.sync_mode);
         caps_logged = 1;
      }
      if (cfg.extradata_mode == 1) {
         last_stage = 5u;
         media_audio_activity_stage(5u,
            (((uint32_t)i & 0xffu) << 16) |
            ((uint32_t)(par->extradata_size & 0xffffu)),
            0u);
         extra_ret = media_write_extra_before_init(fd, "auddec",
            par->extradata,
            par->extradata_size);
         media_audio_activity_stage(6u,
            (((uint32_t)i & 0xffu) << 16) |
            ((uint32_t)(extra_ret & 0xffffu)),
            (uint32_t)(errno & 0xffffu));
      }
      errno = 0;
      last_stage = 7u;
      media_audio_activity_stage(7u,
         (((uint32_t)i & 0xffu) << 16) |
         ((uint32_t)(AUDDEC_INIT & 0xffffu)),
         (uint32_t)(cfg.kshm_size & 0xffffu));
      init_ret = extra_ret == 0 ? ioctl(fd, AUDDEC_INIT, &cfg) : -1;
      init_errno = errno;
      media_audio_activity_stage(8u,
         (((uint32_t)i & 0xffu) << 16) |
         ((uint32_t)(init_ret & 0xffffu)),
         (uint32_t)(init_errno & 0xffffu));
      errno = 0;
      last_stage = 9u;
      media_audio_activity_stage(9u,
         (((uint32_t)i & 0xffu) << 16) |
         ((uint32_t)(AUDDEC_START & 0xffffu)),
         (uint32_t)(cfg.sync_mode & 0xffffu));
      start_ret = init_ret == 0 ? ioctl(fd, AUDDEC_START, 0) : -1;
      start_errno = errno;
      media_audio_activity_stage(10u,
         (((uint32_t)i & 0xffu) << 16) |
         ((uint32_t)(start_ret & 0xffffu)),
         (uint32_t)(start_errno & 0xffffu));
      printf("unifrog media auddec init_try label=%s fd=%d extra=%d init=%d init_errno=%d start=%d start_errno=%d stream=%d codec=%u av=%d name=%s tag=0x%lx rate=%u ch=%u bits=%u block=%u extra_size=%u mode=%u sync=%u snd=0x%lx audsink=%d flush=%d buffering=%d/%d prime=%d prime_dest=%lu prof=%u sridx=%u\n",
         variant->label, fd, extra_ret, init_ret, init_errno, start_ret,
         start_errno, stream_index, cfg.codec_id, par->codec_id,
         media_avcodec_name(par->codec_id), (unsigned long)par->codec_tag,
         cfg.sample_rate, cfg.channels, cfg.bits_per_coded_sample,
         cfg.block_align, cfg.extradata_size, cfg.extradata_mode,
         cfg.sync_mode, (unsigned long)cfg.snd_devs, cfg.enable_audsink,
         cfg.audio_flush_thres, cfg.buffering_start, cfg.buffering_end,
         variant->gb300_i2so_prime, (unsigned long)variant->prime_dest,
         auddec->aac_profile, auddec->aac_sample_rate_index);
      if (extra_ret == 0 && init_ret == 0 && start_ret == 0) {
         int channel_ret;
         int channel_errno;
         int volume_ret;
         int volume_errno;

         errno = 0;
         channel_ret = ioctl(fd, AUDIO_CHANNEL_SELECT, &channel);
         channel_errno = errno;
         errno = 0;
         volume_ret = ioctl(fd, AUDIO_SET_VOLUME, &volume);
         volume_errno = errno;
         if (variant->gb300_i2so_prime) {
            prime_fd = media_gb300_i2so_prime_open(variant->label,
               cfg.sample_rate, media_audio_output_channels(),
               cfg.bits_per_coded_sample, cfg.sync_mode,
               variant->prime_dest);
            if (prime_fd < 0) {
               printf("unifrog media auddec prime failed label=%s fd=%d stream=%d sync=%u snd=0x%lx dest=%lu\n",
                  variant->label, fd, stream_index, cfg.sync_mode,
                  (unsigned long)cfg.snd_devs,
                  (unsigned long)variant->prime_dest);
               media_auddec_release_fd(&fd, "prime_failed");
               continue;
            }
         } else {
            if (!unifrog_audio_prefers_stereo_output())
               unifrog_audio_set_system_output_enabled(1);
         }
         unifrog_audio_debug_dump(NULL, "auddec_open");
         auddec->fd = fd;
         auddec->prime_fd = prime_fd;
         auddec->stream = stream_index;
         auddec->time_base = stream->time_base;
         auddec->freerun = cfg.sync_mode == 0;
         auddec->output_enabled = !unifrog_audio_prefers_stereo_output();
         printf("unifrog media auddec open ok label=%s fd=%d stream=%d freerun=%d sync=%u snd=0x%lx audsink=%d flush=%d kshm=%d prime_fd=%d prime_dest=%lu output_ch=%u channel_select=%u channel_ret=%d channel_errno=%d volume_ret=%d volume_errno=%d\n",
            variant->label, fd, stream_index, auddec->freerun,
            cfg.sync_mode, (unsigned long)cfg.snd_devs,
            cfg.enable_audsink, cfg.audio_flush_thres, cfg.kshm_size,
            prime_fd, (unsigned long)variant->prime_dest,
            media_audio_output_channels(), (unsigned)channel, channel_ret,
            channel_errno, volume_ret, volume_errno);
         media_audio_activity_stage(12u,
            (((uint32_t)i & 0xffu) << 16) | ((uint32_t)fd & 0xffffu),
            ((uint32_t)auddec->freerun & 0xffffu));
         return 0;
      }
      media_audio_activity_stage(11u,
         (((uint32_t)i & 0xffu) << 16),
         ((uint32_t)(init_errno & 0xffffu) << 16) |
         ((uint32_t)(start_errno & 0xffffu)));
      media_auddec_release_fd(&fd, "open_failed");
   }
   printf("unifrog media auddec open failed stream=%d codec=%d name=%s rate=%d ch=%d extra=%d\n",
      stream_index, par->codec_id, media_avcodec_name(par->codec_id),
      par->sample_rate, par->channels, par->extradata_size);
   media_audio_activity_stage(255u, 4u, last_stage);
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

static int media_video_is_lowres_stream(const AVCodecParameters *par)
{
   unsigned pixels;

   if (!par || par->width <= 0 || par->height <= 0)
      return 0;
   if (par->width > INT_MAX / par->height)
      return 0;
   pixels = (unsigned)(par->width * par->height);
   return pixels <= MEDIA_VIDEO_LOWRES_MAX_PIXELS;
}

static unsigned media_video_kshm_size_for_stream(
   const AVCodecParameters *par, const char **policy_out)
{
   unsigned lowres_size = MEDIA_VIDEO_LOWRES_KSHM_SIZE;

   if (policy_out)
      *policy_out = "default";
   if (!media_video_is_lowres_stream(par))
      return MEDIA_VIDEO_KSHM_SIZE;
   if (lowres_size == 0 || lowres_size > MEDIA_VIDEO_KSHM_SIZE)
      lowres_size = MEDIA_VIDEO_KSHM_SIZE;
   if (policy_out)
      *policy_out = lowres_size < MEDIA_VIDEO_KSHM_SIZE ?
         "lowres" : "default";
   return lowres_size;
}

static int media_h264_extradata_annexb(const uint8_t *src, int src_size,
   uint8_t *dst, size_t dst_size, size_t *out_size)
{
   static const uint8_t start_code[] = { 0x00, 0x00, 0x00, 0x01 };
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
   int sync_mode, const char *path)
{
   AVStream *stream;
   AVCodecParameters *par;
   struct video_config cfg;
   struct vdec_dis_rect rect;
   int fd;
   int hc_codec;
   int init_ret;
   int init_errno;
   uint32_t init_elapsed_ms = 0;
   int start_ret;
   int start_errno;
   int rect_ret;
   int pre_extra_ret = 0;
   int post_extra_ret = 0;
   int post_extra_size = 0;
   int pre_extra_size = 0;
   int write_extra_before_init = 0;
   const uint8_t *pre_extra_data = NULL;
   const char *kshm_policy = "default";
   uint8_t post_extra[1024];
   size_t post_extra_used = 0;
   int win_ret = -1;
   int win_errno = 0;
   int mirror_ret = -1;

   if (!fmt || stream_index < 0 || stream_index >= (int)fmt->nb_streams)
      return -1;
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
   cfg.codec_tag = par->codec_tag;
   cfg.independent_url = 0;
   cfg.combine_enable = 0;
   cfg.sync_mode = (uint8_t)sync_mode;
   cfg.decode_mode = VDEC_WORK_MODE_KSHM;
   cfg.decoder_flag = 0;
   cfg.rotate_by_cfg = 1;
   cfg.rotate_enable = 0;
   cfg.pic_width = par->width > 0 ? par->width : VIDEO_SOURCE_W;
   cfg.pic_height = par->height > 0 ? par->height : VIDEO_SOURCE_H;
   cfg.pixel_aspect_x = 1;
   cfg.pixel_aspect_y = 1;
   cfg.preview = 0;
   cfg.b_aux_layer = 0;
   cfg.extradata_mode = 0;
   cfg.frame_rate = media_video_frame_rate_milli(stream);
   cfg.codec_frame_size = (int)(((int64_t)cfg.pic_width *
      (int64_t)cfg.pic_height * 3) / 2);
   cfg.src_area.x = 0;
   cfg.src_area.y = 0;
   /*
    * The decoded frame is placed on the HD video plane. Smaller source rects
    * crop that plane before scaling, which makes 240p/360p/480p/720p look
    * zoomed. Keep pic_width/pic_height as codec dimensions, but display the
    * full hardware canvas just like hcplayer's working path.
    */
   cfg.src_area.w = VIDEO_SOURCE_W;
   cfg.src_area.h = VIDEO_SOURCE_H;
   cfg.dst_area.x = 0;
   cfg.dst_area.y = 0;
   cfg.dst_area.w = VIDEO_OUTPUT_W;
   cfg.dst_area.h = VIDEO_OUTPUT_H;
   cfg.quick_mode = 0;
   cfg.img_dis_mode = IMG_DIS_FULLSCREEN;
   cfg.mirror_type = MIRROR_TYPE_NONE;
   cfg.rotate_type = ROTATE_TYPE_0;
   cfg.bit_rate = par->bit_rate > 0 && par->bit_rate < INT32_MAX ?
      (int)par->bit_rate : 0;
   cfg.kshm_size = (int)media_video_kshm_size_for_stream(par,
      &kshm_policy);
   cfg.buffering_start = MEDIA_VIDEO_BUFFERING_START_MS;
   cfg.buffering_end = MEDIA_VIDEO_BUFFERING_END_MS;
   cfg.scan_type = YUV420_YH1V2;
   media_h264_packet_mode = MEDIA_H264_MODE_UNKNOWN;
   media_h264_nal_length_size = 0;
   media_h264_extra_delivery = MEDIA_H264_EXTRA_NONE;
   if (par->codec_id == AV_CODEC_ID_H264) {
      media_h264_nal_length_size = media_h264_avcc_length_size(
         par->extradata, (size_t)(par->extradata_size > 0 ?
         par->extradata_size : 0));
      if (media_h264_nal_length_size > 0)
         media_h264_packet_mode = MEDIA_H264_MODE_AVCC;
      else if (media_h264_has_annexb_start(par->extradata,
          (size_t)(par->extradata_size > 0 ? par->extradata_size : 0)))
         media_h264_packet_mode = MEDIA_H264_MODE_ANNEXB;

      if (par->extradata && par->extradata_size > 0 &&
          media_h264_packet_mode == MEDIA_H264_MODE_ANNEXB &&
          media_h264_extradata_annexb(par->extradata, par->extradata_size,
             post_extra, sizeof(post_extra), &post_extra_used) == 0 &&
          post_extra_used > 0) {
         post_extra_size = (int)post_extra_used;
         media_h264_extra_delivery = MEDIA_H264_EXTRA_POST_ES;
      } else if (par->extradata && par->extradata_size > 0) {
         if (par->extradata_size <= (int)sizeof(cfg.extra_data)) {
            cfg.extradata_size = par->extradata_size;
            cfg.extradata_mode = 0;
            memcpy(cfg.extra_data, par->extradata, (size_t)par->extradata_size);
            cfg.extradata = cfg.extra_data;
            media_h264_extra_delivery =
               media_h264_packet_mode == MEDIA_H264_MODE_AVCC ?
               MEDIA_H264_EXTRA_CFG_AVCC : MEDIA_H264_EXTRA_CFG_RAW;
         } else {
            cfg.extradata_size = par->extradata_size;
            cfg.extradata_mode = 1;
            cfg.extradata = par->extradata;
            pre_extra_data = par->extradata;
            pre_extra_size = par->extradata_size;
            write_extra_before_init = 1;
            media_h264_extra_delivery = MEDIA_H264_EXTRA_PRE_EXTRA;
         }
      }
   } else if (par->extradata && par->extradata_size > 0) {
      cfg.extradata_size = par->extradata_size;
      if (par->extradata_size <= (int)sizeof(cfg.extra_data)) {
         cfg.extradata_mode = 0;
         memcpy(cfg.extra_data, par->extradata, (size_t)par->extradata_size);
         cfg.extradata = cfg.extra_data;
      } else {
         cfg.extradata_mode = 1;
         cfg.extradata = par->extradata;
         pre_extra_data = par->extradata;
         pre_extra_size = par->extradata_size;
         write_extra_before_init = 1;
      }
   }

   if (par->codec_id == AV_CODEC_ID_H264) {
      const uint8_t *extra = par->extradata;
      int extra_size = par->extradata_size;

      printf("unifrog media native video h264_config mode=%s nal_len=%d extra=%d delivery=%s codec_tag=0x%lx first=%02x %02x %02x %02x %02x %02x %02x %02x\n",
         media_h264_mode_name(media_h264_packet_mode),
         media_h264_nal_length_size, extra_size,
         media_h264_extra_delivery_name(media_h264_extra_delivery),
         (unsigned long)cfg.codec_tag,
         extra && extra_size > 0 ? extra[0] : 0,
         extra && extra_size > 1 ? extra[1] : 0,
         extra && extra_size > 2 ? extra[2] : 0,
         extra && extra_size > 3 ? extra[3] : 0,
         extra && extra_size > 4 ? extra[4] : 0,
         extra && extra_size > 5 ? extra[5] : 0,
         extra && extra_size > 6 ? extra[6] : 0,
         extra && extra_size > 7 ? extra[7] : 0);
   }
   printf("unifrog media native video open_viddec begin codec=%u av=%d tag=0x%lx cfg_tag=0x%lx ind=%d %dx%d fps_milli=%u frame=%d kshm=%lu kshm_policy=%s mmz0_total=%lu extra=%d extra_mode=%d write_pre=%d pre_extra=%d post_extra=%d decode=%d quick=%d sync=%d buffering=%d/%d\n",
      cfg.codec_id, par->codec_id, (unsigned long)par->codec_tag,
      (unsigned long)cfg.codec_tag, cfg.independent_url,
      cfg.pic_width, cfg.pic_height, cfg.frame_rate, cfg.codec_frame_size,
      (unsigned long)cfg.kshm_size, kshm_policy ? kshm_policy : "",
      (unsigned long)media_mmz_total0(), cfg.extradata_size,
      cfg.extradata_mode, write_extra_before_init, pre_extra_size,
      post_extra_size, cfg.decode_mode, cfg.quick_mode, cfg.sync_mode,
      cfg.buffering_start, cfg.buffering_end);
   media_video_activity_stage(1u,
      (((uint32_t)stream_index & 0xffu) << 16) |
      ((uint32_t)par->codec_id & 0xffffu),
      (((uint32_t)cfg.pic_width & 0xffffu) << 16) |
      ((uint32_t)cfg.pic_height & 0xffffu));
   errno = 0;
   fd = open("/dev/viddec", O_RDWR);
   if (fd < 0) {
      printf("unifrog media native video open viddec failed errno=%d\n", errno);
      media_video_activity_stage(2u, (uint32_t)(errno & 0xffffu), 0u);
      return -1;
   }
   media_video_activity_stage(3u,
      (uint32_t)(fd & 0xffffu),
      (((uint32_t)dis_fd & 0xffffu) << 16) | ((uint32_t)vidsink_fd & 0xffffu));
   printf("unifrog media native video open_viddec done fd=%d\n", fd);
   media_video_activity_stage(4u, (uint32_t)(fd & 0xffffu), 0u);
   media_video_activity_stage(5u,
      (((uint32_t)cfg.decode_mode & 0xffu) << 16) |
      ((uint32_t)cfg.kshm_size & 0xffffu),
      (((uint32_t)cfg.extradata_size & 0xffffu) << 16) |
      ((uint32_t)cfg.extradata_mode & 0xffffu));
   if (write_extra_before_init)
      pre_extra_ret = media_write_extra_before_init(fd, "viddec",
         pre_extra_data, pre_extra_size);
   media_video_activity_stage(9u,
      ((uint32_t)(pre_extra_ret & 0xffffu) << 16) |
      ((uint32_t)(errno & 0xffffu)),
      (uint32_t)(pre_extra_size & 0xffffu));
   printf("unifrog media native video init begin fd=%d req=0x%lx dis=%d vidsink=%d mode=%d kshm=%d mmz0_total=%lu extra=%d extra_mode=%d write_pre=%d pre_extra=%d pre_ret=%d post_extra=%d sync=%d\n",
      fd, (unsigned long)VIDDEC_INIT, dis_fd, vidsink_fd, cfg.decode_mode,
      cfg.kshm_size, (unsigned long)media_mmz_total0(), cfg.extradata_size,
      cfg.extradata_mode, write_extra_before_init, pre_extra_size,
      pre_extra_ret, post_extra_size, cfg.sync_mode);
   (void)unifrog_log_flush();
   media_video_activity_stage(6u,
      (((uint32_t)cfg.decode_mode & 0xffu) << 16) |
      ((uint32_t)VIDDEC_INIT & 0xffffu),
      (uint32_t)cfg.kshm_size);
   errno = 0;
   init_elapsed_ms = unifrog_perf_time_ms();
   init_ret = pre_extra_ret == 0 ? ioctl(fd, VIDDEC_INIT, &cfg) : -1;
   init_elapsed_ms = unifrog_perf_time_ms() - init_elapsed_ms;
   init_errno = errno;
   media_video_activity_stage(7u,
      ((uint32_t)(init_ret & 0xffffu) << 16) | ((uint32_t)init_errno & 0xffffu),
      (uint32_t)(fd & 0xffffu));
   printf("unifrog media native video init done fd=%d ret=%d errno=%d ms=%lu pre_ret=%d\n",
      fd, init_ret, init_errno, (unsigned long)init_elapsed_ms, pre_extra_ret);
   if (init_ret == 0 && post_extra_size > 0) {
      printf("unifrog media native video post_extra begin fd=%d size=%d\n",
         fd, post_extra_size);
      post_extra_ret = media_send_packet_blob(fd, post_extra, post_extra_size,
         par->codec_id == AV_CODEC_ID_H264 ? AV_PACKET_ES_DATA :
         AV_PACKET_EXTRA_DATA);
      media_video_activity_stage(11u,
         ((uint32_t)(post_extra_ret & 0xffffu) << 16) |
         ((uint32_t)(errno & 0xffffu)),
         (uint32_t)post_extra_size);
      printf("unifrog media native video post_extra done fd=%d size=%d ret=%d errno=%d\n",
         fd, post_extra_size, post_extra_ret, errno);
   }
   if (init_ret == 0 && post_extra_ret == 0) {
      printf("unifrog media native video vidsink begin fd=%d\n", fd);
      open_video_sink();
      printf("unifrog media native video vidsink done fd=%d vidsink=%d\n",
         fd, vidsink_fd);
   }
   memset(&rect, 0, sizeof(rect));
   rect.src_rect = cfg.src_area;
   rect.dst_rect = cfg.dst_area;
   printf("unifrog media native video rect begin fd=%d src=%ux%u dst=%ux%u\n",
      fd, rect.src_rect.w, rect.src_rect.h, rect.dst_rect.w,
      rect.dst_rect.h);
   rect_ret = init_ret == 0 && post_extra_ret == 0 ?
      ioctl(fd, VIDDEC_SET_DISPLAY_RECT, &rect) : -1;
   printf("unifrog media native video rect done fd=%d ret=%d errno=%d\n",
      fd, rect_ret, errno);
   mirror_ret = init_ret == 0 && post_extra_ret == 0 ?
      ioctl(fd, VIDDEC_SET_MIRROR_MODE, (unsigned long)cfg.mirror_type) : -1;
   printf("unifrog media native video start begin fd=%d win=%d mirror=%d\n",
      fd, win_ret, mirror_ret);
   errno = 0;
   start_ret = init_ret == 0 && post_extra_ret == 0 ?
      ioctl(fd, VIDDEC_START, 0) : -1;
   start_errno = errno;
   media_video_activity_stage(8u,
      ((uint32_t)(start_ret & 0xffffu) << 16) |
      ((uint32_t)start_errno & 0xffffu),
      ((uint32_t)(win_ret & 0xffffu) << 16) | ((uint32_t)mirror_ret & 0xffffu));
   printf("unifrog media native video start done fd=%d ret=%d errno=%d\n",
      fd, start_ret, start_errno);
   if (start_ret == 0) {
      open_display_controller();
      media_video_activity_stage(10u,
         (((uint32_t)dis_fd & 0xffffu) << 16) | ((uint32_t)fd & 0xffffu), 0u);
   }
   if (start_ret == 0 && dis_fd >= 0) {
      dis_win_onoff_t win;

      memset(&win, 0, sizeof(win));
      win.distype = DIS_TYPE_HD;
      win.layer = DIS_LAYER_MAIN;
      win.on = true;
      errno = 0;
      win_ret = ioctl(dis_fd, DIS_SET_WIN_ONOFF, &win);
      win_errno = errno;
      printf("unifrog media native video win done fd=%d ret=%d errno=%d\n",
         fd, win_ret, win_errno);
   }
   if (start_ret == 0) {
      int mosaic = 2;

      (void)ioctl(fd, VIDDEC_SET_SHOW_MASAIC_ON_ERR, mosaic);
   }
   printf("unifrog media native video open fd=%d init=%d init_errno=%d pre_ret=%d post_ret=%d rect=%d win=%d win_errno=%d mirror=%d start=%d start_errno=%d codec=%u av=%d tag=0x%lx cfg_tag=0x%lx ind=%d %dx%d fps_milli=%u frame=%d kshm=%lu mmz0_total=%lu extra=%d extra_mode=%d write_pre=%d pre_extra=%d post_extra=%d decode=%d quick=%d sync=%d buffering=%d/%d\n",
      fd, init_ret, init_errno, pre_extra_ret, post_extra_ret, rect_ret, win_ret,
      win_errno, mirror_ret, start_ret, start_errno,
      cfg.codec_id, par->codec_id, (unsigned long)par->codec_tag,
      (unsigned long)cfg.codec_tag, cfg.independent_url,
      cfg.pic_width, cfg.pic_height, cfg.frame_rate, cfg.codec_frame_size,
      (unsigned long)cfg.kshm_size, (unsigned long)media_mmz_total0(),
      cfg.extradata_size, cfg.extradata_mode, write_extra_before_init,
      pre_extra_size, post_extra_size, cfg.decode_mode, cfg.quick_mode,
      cfg.sync_mode, cfg.buffering_start, cfg.buffering_end);
   if (init_ret != 0 || pre_extra_ret != 0 || post_extra_ret != 0 ||
       start_ret != 0) {
      media_video_release_decoder(fd, 1, 0, "open_failed", path);
      close(fd);
      if (init_ret != 0 && init_errno == EPERM)
         media_video_reset_modules("open_failed", path);
      return -1;
   }
   {
      struct vdec_decore_status status;

      memset(&status, 0, sizeof(status));
      if (ioctl(fd, VIDDEC_GET_STATUS, &status) == 0)
         printf("unifrog media native video status_after_open decoded=%lu displayed=%lu hdr=%d pic=%d show=%d eos=%u err=%lu underrun=%lu used=%lu/%lu\n",
            (unsigned long)status.frames_decoded,
            (unsigned long)status.frames_displayed,
            status.first_header_got,
            status.first_pic_decoded,
            status.first_pic_showed,
            (unsigned)status.get_pkt_eos,
            (unsigned long)status.decode_error,
            (unsigned long)status.under_run_cnt,
            (unsigned long)status.buffer_used,
            (unsigned long)status.buffer_size);
   }
   printf("unifrog media native video layer deferred fd=%d reason=wait_first_frame\n",
      fd);
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
   if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
      printf("unifrog media native bsf disabled name=h264_mp4toannexb reason=direct_viddec_h264 mode=%s nal_len=%d extra=%s\n",
         media_h264_mode_name(media_h264_packet_mode),
         media_h264_nal_length_size,
         media_h264_extra_delivery_name(media_h264_extra_delivery));
      return 0;
   }
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

static uint32_t media_audio_frames_to_ms(uint32_t frames, int rate)
{
   if (rate <= 0)
      return 0;
   return (uint32_t)(((uint64_t)frames * 1000ull) / (uint64_t)rate);
}

static uint32_t media_sw_audio_clock_ms(struct unifrog_audio *audio,
   uint32_t frames, int rate, uint32_t start_ms)
{
   unsigned long delay_frames = 0;
   uint32_t written_ms;

   if (frames == 0 || rate <= 0)
      return 0;
   if (audio && audio->fd >= 0 &&
       unifrog_audio_delay(audio, &delay_frames) == 0) {
      uint32_t played_frames = 0;

      if (delay_frames < (unsigned long)frames)
         played_frames = frames - (uint32_t)delay_frames;
      return media_audio_frames_to_ms(played_frames, rate);
   }
   written_ms = media_audio_frames_to_ms(frames, rate);
   if (start_ms) {
      uint32_t elapsed_ms = unifrog_perf_time_ms() - start_ms;

      if (elapsed_ms < written_ms)
         return elapsed_ms;
   }
   return written_ms;
}

static int media_video_relative_packet_ms(const AVPacket *packet,
   AVRational time_base, int *base_ms)
{
   int packet_ms;
   int64_t relative_ms;

   if (!base_ms)
      return -1;
   packet_ms = media_video_packet_time_ms(packet, time_base, 1);
   if (packet_ms == -1)
      return -1;
   if (*base_ms == MEDIA_TIME_UNSET)
      *base_ms = packet_ms;
   relative_ms = (int64_t)packet_ms - (int64_t)*base_ms;
   if (relative_ms < 0)
      return 0;
   if (relative_ms > INT32_MAX)
      return INT32_MAX;
   return (int)relative_ms;
}

static void media_video_pacer_wait(struct media_audio_pacer *pacer,
   const AVPacket *packet, AVRational time_base, unsigned feed_lead_ms)
{
   int32_t pts_ms;
   int32_t dur_ms;

   if (!pacer || !packet)
      return;
   pts_ms = media_video_packet_time_ms(packet, time_base, 1);
   dur_ms = media_packet_duration_ms(packet, time_base);
   media_audio_pacer_wait_ms(pacer, pts_ms, dur_ms, feed_lead_ms);
}

static void media_video_wait_for_sw_audio(struct unifrog_audio *audio,
   const AVPacket *packet, AVRational time_base, int *video_base_ms,
   uint32_t audio_frames, int audio_rate, uint32_t audio_start_ms,
   const char *path)
{
   uint32_t start_ms;
   unsigned polls = 0;
   int packet_ms;

   if (!audio || audio->fd < 0 || audio_frames == 0 || audio_rate <= 0)
      return;
   packet_ms = media_video_relative_packet_ms(packet, time_base,
      video_base_ms);
   if (packet_ms < 0)
      return;
   start_ms = unifrog_perf_time_ms();
   while (!media_exit_down()) {
      uint32_t audio_ms = media_sw_audio_clock_ms(audio, audio_frames,
         audio_rate, audio_start_ms);
      uint32_t lead_ms = (uint32_t)packet_ms > audio_ms ?
         (uint32_t)packet_ms - audio_ms : 0;
      uint32_t elapsed_ms;

      if ((uint32_t)packet_ms <= audio_ms + MEDIA_SW_AUDIO_VIDEO_LEAD_MS)
         break;
      elapsed_ms = unifrog_perf_time_ms() - start_ms;
      if (polls == 0 || elapsed_ms >= MEDIA_SW_AUDIO_MAX_WAIT_MS)
         printf("unifrog media native swsync wait pkt_ms=%d audio_ms=%lu lead=%lu frames=%lu rate=%d waited=%lu path=%s\n",
            packet_ms, (unsigned long)audio_ms, (unsigned long)lead_ms,
            (unsigned long)audio_frames, audio_rate,
            (unsigned long)elapsed_ms, path ? path : "");
      if (elapsed_ms >= MEDIA_SW_AUDIO_MAX_WAIT_MS)
         break;
      polls++;
      usleep(MEDIA_SW_AUDIO_WAIT_POLL_US);
   }
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

static int media_native_open_sw_audio(AVFormatContext *fmt, int audio_stream,
   unsigned output_channels, AVCodec **audio_decoder_out,
   AVCodecContext **audio_ctx_out, struct unifrog_audio *audio,
   struct media_ffmpeg_audio_converter *audio_converter, int16_t **pcm_out,
   int *audio_enabled_out, const char *reason, const char *path)
{
   AVCodec *audio_decoder;
   AVCodecContext *audio_ctx;
   int16_t *pcm;
   int open_ret;

   if (!fmt || audio_stream < 0 || !audio_ctx_out || !audio || !pcm_out ||
       !audio_enabled_out || audio_stream >= (int)fmt->nb_streams ||
       !fmt->streams[audio_stream] || !fmt->streams[audio_stream]->codecpar) {
      printf("unifrog media native software_audio invalid reason=%s stream=%d fmt=0x%08lx\n",
         reason ? reason : "?", audio_stream,
         (unsigned long)(uintptr_t)fmt);
      return -1;
   }
   if (*audio_enabled_out && *audio_ctx_out && *pcm_out && audio->fd >= 0)
      return 0;

   audio_decoder = audio_decoder_out && *audio_decoder_out ? *audio_decoder_out :
      avcodec_find_decoder(fmt->streams[audio_stream]->codecpar->codec_id);
   if (!audio_decoder) {
      printf("unifrog media native software_audio decoder missing reason=%s stream=%d codec=%d path=%s\n",
         reason ? reason : "?", audio_stream,
         fmt->streams[audio_stream]->codecpar->codec_id, path ? path : "");
      return -1;
   }

   audio_ctx = avcodec_alloc_context3(audio_decoder);
   if (!audio_ctx)
      return -1;
   if (avcodec_parameters_to_context(audio_ctx,
       fmt->streams[audio_stream]->codecpar) != 0)
      goto fail;
   audio_ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;
   audio_ctx->request_channel_layout = media_audio_output_layout(
      media_audio_mix_channels(output_channels));
   if (avcodec_open2(audio_ctx, audio_decoder, NULL) != 0)
      goto fail;
   if (audio_ctx->sample_rate < 8000 || audio_ctx->sample_rate > 48000)
      goto fail;

   open_ret = unifrog_audio_open_backend(audio,
      (unsigned)audio_ctx->sample_rate, output_channels,
      MEDIA_VIDEO_AUDIO_PERIOD_BYTES, MEDIA_VIDEO_AUDIO_PERIODS,
      media_gb300_auddec_fallback_backend(reason));
   if (open_ret != 0)
      goto fail;

   pcm = malloc(sizeof(*pcm) * MEDIA_FFMPEG_CHUNK_FRAMES * output_channels);
   if (!pcm)
      goto fail;

   (void)unifrog_audio_set_volume(audio, media_audio_runtime_volume());
   (void)unifrog_audio_set_mute(audio, 1);
   (void)unifrog_audio_start(audio);
   (void)unifrog_audio_set_output_enabled(audio, 1);
   if (audio_decoder_out)
      *audio_decoder_out = audio_decoder;
   *audio_ctx_out = audio_ctx;
   *pcm_out = pcm;
   *audio_enabled_out = 1;
   if (audio_converter)
      memset(audio_converter, 0, sizeof(*audio_converter));
   printf("unifrog media native software_audio open reason=%s rate=%d ch=%u backend=%d period_bytes=%u periods=%u buffer_ms=%lu\n",
      reason ? reason : "?", audio_ctx->sample_rate, output_channels,
      audio->backend, MEDIA_VIDEO_AUDIO_PERIOD_BYTES,
      MEDIA_VIDEO_AUDIO_PERIODS,
      (unsigned long)(((uint64_t)MEDIA_VIDEO_AUDIO_PERIOD_BYTES *
      (uint64_t)MEDIA_VIDEO_AUDIO_PERIODS * 1000ull) /
      ((uint64_t)audio_ctx->sample_rate * sizeof(int16_t) *
      (uint64_t)output_channels)));
   unifrog_audio_debug_dump(audio, "native_video_after_start");
   return 0;

fail:
   if (audio->fd >= 0)
      unifrog_audio_close(audio);
   if (audio_ctx)
      avcodec_free_context(&audio_ctx);
   printf("unifrog media native software_audio open failed reason=%s stream=%d path=%s\n",
      reason ? reason : "?", audio_stream, path ? path : "");
   return -1;
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
   struct media_audio_pacer hw_audio_pacer;
   struct media_audio_pacer hw_video_pacer;
   struct media_buffered_input input;
   struct media_ffmpeg_audio_converter audio_converter;
   struct media_progress_overlay overlay;
   int16_t *pcm = NULL;
   int video_stream = -1;
   int audio_stream = -1;
   int video_fd = -1;
   int ret = -1;
   int audio_enabled = 0;
   uint32_t video_packets = 0;
   uint32_t audio_frames = 0;
   uint32_t sw_audio_start_ms = 0;
   uint32_t loop_polls = 0;
   uint32_t start_ms = unifrog_perf_time_ms();
   int sw_video_base_ms = MEDIA_TIME_UNSET;
   unsigned long frames_decoded = 0;
   unsigned long frames_displayed = 0;
   int disable_audio = options && options->disable_audio;
   int video_freerun = 0;
   int video_sync_mode = AVSYNC_TYPE_FREERUN;
   int video_layer_revealed = 0;
   int seek_video_catchup_hidden = 0;
   int sd_read_active = 0;
   int auddec_write_failed = 0;
   int auddec_sw_fallback_attempted = 0;
   int native_video_failed = 0;
   const char *sw_audio_reason = "auddec_open_failed";
   uint32_t seek_video_dropped_packets = 0;
   uint32_t seek_video_drop_last_log_ms = 0;
   unsigned video_feed_lead_ms = MEDIA_VIDEO_FEED_LEAD_MS;
   unsigned audio_feed_lead_ms = MEDIA_AUDIO_FEED_LEAD_MS;
   unsigned audio_output_channels = media_audio_output_channels();
   int64_t duration_ms = -1;
   int64_t seek_video_catchup_until_ms = MEDIA_TIME_UNSET;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = -1;
   auddec.prime_fd = -1;
   memset(&hw_audio_pacer, 0, sizeof(hw_audio_pacer));
   memset(&hw_video_pacer, 0, sizeof(hw_video_pacer));
   memset(&input, 0, sizeof(input));
   input.fd = -1;
   input.file_size = -1;
   memset(&audio_converter, 0, sizeof(audio_converter));
   memset(&overlay, 0, sizeof(overlay));
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_MEDIA_VIDEO,
      unifrog_exception_activity_hash(path ? path : "native_video"), 0, 1);
   media_video_activity_marker =
      unifrog_exception_activity_hash(path ? path : "native_video");
   media_ffmpeg_register_once();
   media_video_progress(options, "opening", 2, 100);
   media_sd_read_begin("native_video_open", path);
   sd_read_active = 1;
   printf("unifrog media native open_input begin path=%s\n",
      path ? path : "");
   int open_ret = media_buffered_input_open(&fmt, &input, path,
      "native_video");
   printf("unifrog media native open_input done ret=%d fmt=0x%08lx path=%s\n",
      open_ret, (unsigned long)(uintptr_t)fmt, path ? path : "");
   printf("unifrog media native stream_info begin path=%s\n",
      path ? path : "");
   media_video_progress(options, "scanning", 10, 100);
   int info_ret = open_ret == 0 ? avformat_find_stream_info(fmt, NULL) : 0;
   printf("unifrog media native stream_info done ret=%d streams=%u path=%s\n",
      info_ret, fmt ? fmt->nb_streams : 0, path ? path : "");
   if (open_ret == 0 && info_ret == 0) {
      media_buffered_input_log_coverage(&input, fmt, "native_video", path);
      media_video_progress(options, "decoders", 75, 100);
   }

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
   duration_ms = media_format_duration_ms(fmt);
   printf("unifrog media native streams selected video=%d audio=%d disable_audio=%d path=%s\n",
      video_stream, audio_stream, disable_audio, path ? path : "");
   (void)unifrog_log_flush();
   if (video_stream < 0) {
      printf("unifrog media native no video audio=%d path=%s\n",
         audio_stream, path);
      if (!disable_audio && audio_stream >= 0) {
         ret = media_play_native_audio_compressed(path);
         if (ret != 0) {
            printf("unifrog media native no_video fallback ffmpeg ret=%d path=%s\n",
               ret, path ? path : "");
            ret = media_play_ffmpeg_audio_backend(path,
               media_gb300_auddec_fallback_backend("auddec_no_video"),
               "auddec_no_video");
         }
      }
      goto out;
   }
   media_clear_graphics_black("native_video_prepare", path);
   printf("unifrog media native init_drivers begin\n");
   media_video_progress(options, "drivers", 80, 100);
   (void)unifrog_log_flush();
   media_init_drivers_once();
   printf("unifrog media native init_drivers done\n");
   (void)unifrog_log_flush();
   (void)set_video_layer_visible(0, 0, 0, 0, 0);
   if (!disable_audio && audio_stream >= 0) {
      int auddec_ret;

      printf("unifrog media native auddec_open begin stream=%d\n",
         audio_stream);
      (void)unifrog_log_flush();
      auddec_ret = media_auddec_open(fmt, audio_stream, AVSYNC_TYPE_UPDATESTC,
         &auddec);
      if (auddec_ret != 0)
         auddec_ret = media_auddec_open(fmt, audio_stream,
            AVSYNC_TYPE_FREERUN, &auddec);
      printf("unifrog media native auddec_open done ret=%d fd=%d freerun=%d\n",
         auddec_ret, auddec.fd, auddec.freerun);
      (void)unifrog_log_flush();
   }
   media_video_debug_packets = 0;
   if (!disable_audio && auddec.fd >= 0 && !auddec.freerun)
      video_sync_mode = AVSYNC_TYPE_SYNCSTC;
   printf("unifrog media native video_open begin stream=%d audio_fd=%d sync=%d\n",
      video_stream, auddec.fd, video_sync_mode);
   (void)unifrog_log_flush();
   video_fd = media_video_open_decoder(fmt, video_stream, video_sync_mode, path);
   printf("unifrog media native video_open done fd=%d\n", video_fd);
   (void)unifrog_log_flush();
   if (video_fd < 0 && auddec.fd >= 0 &&
       !unifrog_audio_prefers_stereo_output()) {
      printf("unifrog media native sf2000 video_retry close_auddec fd=%d sync=%d path=%s\n",
         auddec.fd, video_sync_mode, path ? path : "");
      media_auddec_close(&auddec);
      media_video_reset_modules("sf2000_auddec_closed", path);
      sw_audio_reason = "sf2000_viddec_retry";
      video_sync_mode = AVSYNC_TYPE_FREERUN;
      video_fd = media_video_open_decoder(fmt, video_stream, video_sync_mode,
         path);
      printf("unifrog media native sf2000 video_retry done fd=%d sync=%d\n",
         video_fd, video_sync_mode);
      (void)unifrog_log_flush();
   }
   if (video_fd < 0)
      goto out;
   media_buffered_input_enable_video_readahead(&input, fmt, options, path);
   printf("unifrog media native bsf begin stream=%d\n", video_stream);
   (void)unifrog_log_flush();
   (void)media_video_bsf_init(fmt->streams[video_stream], &video_bsf);
   printf("unifrog media native bsf done enabled=%d\n", video_bsf ? 1 : 0);
   (void)unifrog_log_flush();
   if (audio_stream >= 0 && auddec.fd < 0)
      (void)media_native_open_sw_audio(fmt, audio_stream,
         audio_output_channels, &audio_decoder, &audio_ctx, &audio,
         &audio_converter, &pcm, &audio_enabled,
         sw_audio_reason, path);
   video_freerun = video_sync_mode == AVSYNC_TYPE_FREERUN;
   if (auddec.fd >= 0 && audio_feed_lead_ms > video_feed_lead_ms)
      video_feed_lead_ms = audio_feed_lead_ms;
   if (video_freerun && audio_enabled)
      printf("unifrog media native video forcing freerun due to software audio path\n");
   printf("unifrog media native video clock freerun=%d disable_audio=%d auddec=%d auddec_freerun=%d audio_enabled=%d audio_output_ch=%u video_feed_lead_ms=%u audio_feed_lead_ms=%u duration=%lld overlay=1 overlay_hide=A video_max_hw_ahead_ms=%u audio_max_hw_ahead_ms=%u hw_ahead_max_wait_ms=%u video_stuck_behind_ms=%u seek_catchup=%s\n",
      video_freerun, disable_audio, auddec.fd >= 0, auddec.freerun,
      audio_enabled, audio_output_channels, video_feed_lead_ms, audio_feed_lead_ms,
      (long long)duration_ms, MEDIA_VIDEO_MAX_HW_AHEAD_MS,
      MEDIA_AUDIO_MAX_HW_AHEAD_MS, MEDIA_HW_AHEAD_MAX_WAIT_MS,
      MEDIA_VIDEO_STUCK_BEHIND_MS,
      MEDIA_SEEK_ACCELERATE_FRAMES ? "accelerate" : "skip");
   packet = av_packet_alloc();
   frame = av_frame_alloc();
   if (!packet || !frame)
      goto out;
   printf("unifrog media native play video=%d audio=%d audio_enabled=%d auddec=%d path=%s\n",
      video_stream, audio_stream, audio_enabled, auddec.fd >= 0, path);
   media_video_progress(options, "playing", 100, 100);
   media_clear_graphics_black("native_video_play", path);
   media_controls_reset_for_playback("native_video", path);
   media_draw_progress_overlay(&overlay, "video_start", 0, duration_ms, 1,
      path);

   for (;;) {
      struct media_controls controls;
      int read_ret;

      media_poll_controls(&controls);
      if (controls.exit_down)
         break;
      if (controls.overlay_toggle) {
         int64_t video_time = -1;
         int64_t audio_time = -1;

         if (auddec.fd >= 0)
            (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &audio_time);
         if (video_fd >= 0)
            (void)ioctl(video_fd, VIDDEC_GET_CUR_TIME, &video_time);
         media_toggle_progress_overlay(&overlay, "video_toggle",
            audio_time >= 0 ? audio_time : video_time, duration_ms, path);
      }
      if (controls.seek_delta_ms && duration_ms > 0) {
         int64_t video_time = -1;
         int64_t audio_time = -1;
         int64_t cur_time;
         int64_t target_ms;

         if (auddec.fd >= 0)
            (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &audio_time);
         if (video_fd >= 0)
            (void)ioctl(video_fd, VIDDEC_GET_CUR_TIME, &video_time);
         cur_time = audio_time >= 0 ? audio_time : video_time;
         if (cur_time < 0 && hw_video_pacer.started)
            cur_time = hw_video_pacer.next_ms;
         target_ms = media_seek_target_ms(cur_time, controls.seek_delta_ms,
            duration_ms);
         printf("unifrog media seek video request cur=%lld video=%lld audio=%lld dur=%lld delta=%d target=%lld path=%s\n",
            (long long)cur_time, (long long)video_time,
            (long long)audio_time, (long long)duration_ms,
            controls.seek_delta_ms, (long long)target_ms, path ? path : "");
         media_flush_viddec_for_seek(video_fd, "video", path);
         media_flush_auddec_for_seek(&auddec, "video", path);
         if (media_seek_format_ms(fmt, target_ms, "video", path) == 0) {
            media_set_avsync_timebase(target_ms, "video", path);
            if (video_bsf)
               av_bsf_flush(video_bsf);
            if (audio_ctx)
               avcodec_flush_buffers(audio_ctx);
            media_audio_pacer_seek_reset(&hw_video_pacer, target_ms);
            media_audio_pacer_seek_reset(&hw_audio_pacer, target_ms);
            sw_video_base_ms = MEDIA_TIME_UNSET;
            sw_audio_start_ms = 0;
            audio_frames = 0;
            seek_video_catchup_until_ms = target_ms;
            seek_video_catchup_hidden = 0;
            seek_video_dropped_packets = 0;
            seek_video_drop_last_log_ms = 0;
            if (!MEDIA_SEEK_ACCELERATE_FRAMES) {
               (void)set_video_layer_visible(0, 0, 0, 0, 0);
               video_layer_revealed = 0;
               seek_video_catchup_hidden = 1;
            }
            printf("unifrog media seek video catchup mode=%s until=%lld hidden=%d path=%s\n",
               MEDIA_SEEK_ACCELERATE_FRAMES ? "accelerate" : "skip",
               (long long)seek_video_catchup_until_ms,
               seek_video_catchup_hidden, path ? path : "");
            media_draw_progress_overlay(&overlay, "video_seek", target_ms,
               duration_ms, 1, path);
         }
         continue;
      }

      read_ret = av_read_frame(fmt, packet);

      if (read_ret < 0)
         break;
      if (packet->stream_index == video_stream) {
         AVStream *video_st = fmt->streams[video_stream];
         int32_t video_packet_ms = media_video_packet_time_ms(packet,
            video_st->time_base, 1);
         int32_t video_packet_dur_ms = media_packet_duration_ms(packet,
            video_st->time_base);
         int seek_catchup_packet = 0;

         if (seek_video_catchup_until_ms != MEDIA_TIME_UNSET &&
             video_packet_ms >= 0) {
            int64_t packet_end_ms = (int64_t)video_packet_ms +
               (video_packet_dur_ms > 0 ? (int64_t)video_packet_dur_ms : 0);

            seek_catchup_packet = packet_end_ms < seek_video_catchup_until_ms;
         }
         media_video_activity_stage(20u, video_packets & 0x00ffffffu,
            auddec.fd >= 0 ? auddec.packets : audio_frames);
         if (seek_catchup_packet && !MEDIA_SEEK_ACCELERATE_FRAMES) {
            uint32_t now_ms = unifrog_perf_time_ms();

            seek_video_dropped_packets++;
            if (seek_video_dropped_packets <= 4u ||
                now_ms - seek_video_drop_last_log_ms >= 500u) {
               printf("unifrog media seek video drop mode=skip packet_ms=%ld dur=%ld until=%lld dropped=%lu key=%d size=%d path=%s\n",
                  (long)video_packet_ms, (long)video_packet_dur_ms,
                  (long long)seek_video_catchup_until_ms,
                  (unsigned long)seek_video_dropped_packets,
                  (packet->flags & AV_PKT_FLAG_KEY) ? 1 : 0, packet->size,
                  path ? path : "");
               seek_video_drop_last_log_ms = now_ms;
            }
            av_packet_unref(packet);
            continue;
         }
         if (audio_enabled && auddec.fd < 0) {
            if (!seek_catchup_packet)
               media_video_wait_for_sw_audio(&audio, packet,
                  video_st->time_base, &sw_video_base_ms,
                  audio_frames, audio_ctx ? audio_ctx->sample_rate : 0,
                  sw_audio_start_ms, path);
         } else {
            int hw_wait_ret = 0;

            media_video_pacer_wait(&hw_video_pacer, packet,
               video_st->time_base, video_feed_lead_ms);
            if (!seek_catchup_packet)
               hw_wait_ret = media_wait_hardware_ahead("viddec", video_fd, 1,
                  &hw_video_pacer, MEDIA_VIDEO_MAX_HW_AHEAD_MS, path);
            if (hw_wait_ret > 0 && auddec.fd >= 0) {
               int64_t video_time = -1;
               int64_t audio_time = -1;

               (void)ioctl(video_fd, VIDDEC_GET_CUR_TIME, &video_time);
               (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &audio_time);
               if (video_time >= 0 && audio_time >= 0 &&
                   audio_time - video_time >
                   (int64_t)MEDIA_VIDEO_STUCK_BEHIND_MS) {
                  printf("unifrog media seek video recover reason=viddec_timeout video=%lld audio=%lld diff=%lld feed=%lld threshold=%u path=%s\n",
                     (long long)video_time, (long long)audio_time,
                     (long long)(audio_time - video_time),
                     (long long)hw_video_pacer.next_ms,
                     MEDIA_VIDEO_STUCK_BEHIND_MS, path ? path : "");
                  media_flush_viddec_for_seek(video_fd, "video_recover", path);
                  media_set_avsync_timebase(audio_time, "video_recover", path);
                  media_audio_pacer_seek_reset(&hw_video_pacer, audio_time);
                  seek_video_catchup_until_ms = audio_time;
                  seek_video_catchup_hidden = 0;
                  seek_video_dropped_packets = 0;
                  seek_video_drop_last_log_ms = 0;
                  if (!MEDIA_SEEK_ACCELERATE_FRAMES) {
                     (void)set_video_layer_visible(0, 0, 0, 0, 0);
                     video_layer_revealed = 0;
                     seek_video_catchup_hidden = 1;
                  }
               }
            }
         }
         int write_ret = media_video_send_filtered(video_fd, video_bsf,
            packet, video_st->time_base,
            video_freerun,
            video_st->codecpar->codec_id == AV_CODEC_ID_H264,
            &video_packets);

         if (write_ret < 0) {
            native_video_failed = 1;
            printf("unifrog media native video write failed ret=%d packets=%lu path=%s\n",
               write_ret, (unsigned long)video_packets, path);
            av_packet_unref(packet);
            break;
         }
         if (seek_video_catchup_until_ms != MEDIA_TIME_UNSET &&
             !seek_catchup_packet) {
            printf("unifrog media seek video catchup_done mode=%s packet_ms=%ld until=%lld hidden=%d dropped=%lu path=%s\n",
               MEDIA_SEEK_ACCELERATE_FRAMES ? "accelerate" : "skip",
               (long)video_packet_ms, (long long)seek_video_catchup_until_ms,
               seek_video_catchup_hidden,
               (unsigned long)seek_video_dropped_packets, path ? path : "");
            seek_video_catchup_until_ms = MEDIA_TIME_UNSET;
            seek_video_catchup_hidden = 0;
            seek_video_dropped_packets = 0;
            seek_video_drop_last_log_ms = 0;
         }
         if (!seek_catchup_packet || MEDIA_SEEK_ACCELERATE_FRAMES)
            (void)media_native_video_reveal_if_ready(video_fd,
               &video_layer_revealed, &frames_decoded, &frames_displayed, path);
      } else if (auddec.fd >= 0 && packet->stream_index == audio_stream) {
         if (!auddec_write_failed) {
            media_video_activity_stage(21u, auddec.packets & 0x00ffffffu,
               video_packets);
            media_audio_pacer_wait_lead(&hw_audio_pacer, packet,
               fmt->streams[audio_stream]->time_base, audio_feed_lead_ms);
            (void)media_wait_hardware_ahead("native_auddec", auddec.fd, 0,
               &hw_audio_pacer, MEDIA_AUDIO_MAX_HW_AHEAD_MS, path);
            if (media_auddec_send_packet(&auddec, packet) != 0) {
               struct audio_decore_status status;
               int64_t audio_time = -1;

               memset(&status, 0, sizeof(status));
               (void)ioctl(auddec.fd, AUDDEC_GET_STATUS, &status);
               (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &audio_time);
               printf("unifrog media native auddec write failed packets=%lu errno=%d decoded=%lu rate=%lu ch=%u bits=%u atime=%lld path=%s\n",
                  (unsigned long)auddec.packets, errno,
                  (unsigned long)status.frames_decoded,
                  (unsigned long)status.sample_rate, status.channels,
                  status.bits_per_sample, (long long)audio_time, path);
               auddec_write_failed = 1;
               media_auddec_close(&auddec);
               if (!auddec_sw_fallback_attempted && audio_stream >= 0) {
                  auddec_sw_fallback_attempted = 1;
                  if (media_native_open_sw_audio(fmt, audio_stream,
                      audio_output_channels, &audio_decoder, &audio_ctx,
                      &audio, &audio_converter, &pcm, &audio_enabled,
                      "auddec_write_failed", path) == 0) {
                     auddec_write_failed = 0;
                     sw_audio_start_ms = unifrog_perf_time_ms();
                     printf("unifrog media native auddec fallback active reason=write_failed stream=%d path=%s\n",
                        audio_stream, path ? path : "");
                  }
               }
            }
         }
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
               {
                  uint32_t before_audio_frames = audio_frames;

                  write_ret = media_ffmpeg_write_frame(&audio, audio_ctx,
                     &audio_converter, frame, pcm, MEDIA_FFMPEG_CHUNK_FRAMES,
                     &audio_frames, path);
                  if (before_audio_frames == 0 && audio_frames > 0)
                     sw_audio_start_ms = unifrog_perf_time_ms();
               }
               if (write_ret != 0)
                  break;
            }
         }
      }
      av_packet_unref(packet);
      if ((++loop_polls % 180u) == 0) {
         struct vdec_decore_status status;
         struct audio_decore_status aud_status;
         int64_t video_time = -1;
         int64_t audio_time = -1;
         int aud_status_ok = 0;
         uint32_t elapsed_ms = unifrog_perf_time_ms() - start_ms;
         uint32_t sw_audio_ms = media_sw_audio_clock_ms(
            audio_enabled ? &audio : NULL, audio_frames,
            audio_ctx ? audio_ctx->sample_rate : 0, sw_audio_start_ms);

         memset(&status, 0, sizeof(status));
         memset(&aud_status, 0, sizeof(aud_status));
         if (video_fd >= 0)
            (void)ioctl(video_fd, VIDDEC_GET_CUR_TIME, &video_time);
         if (auddec.fd >= 0)
            (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &audio_time);
         if (auddec.fd >= 0 && !audio_enabled)
            aud_status_ok = ioctl(auddec.fd, AUDDEC_GET_STATUS,
               &aud_status) == 0;
         if (ioctl(video_fd, VIDDEC_GET_STATUS, &status) == 0)
            printf("unifrog media native monitor packets=%lu audio=%lu decoded=%lu displayed=%lu hdr=%d pic=%d show=%d eos=%u err=%lu underrun=%lu used=%lu/%lu ms=%lu vtime=%lld atime=%lld sw_audio_ms=%lu feed_v=%lld feed_a=%lld lead_v=%lld lead_a=%lld ahead_v=%lld ahead_a=%lld\n",
               (unsigned long)video_packets,
               (unsigned long)(audio_frames + auddec.packets),
               (unsigned long)status.frames_decoded,
               (unsigned long)status.frames_displayed,
               status.first_header_got,
               status.first_pic_decoded,
               status.first_pic_showed,
               (unsigned)status.get_pkt_eos,
               (unsigned long)status.decode_error,
               (unsigned long)status.under_run_cnt,
               (unsigned long)status.buffer_used,
               (unsigned long)status.buffer_size,
               (unsigned long)elapsed_ms,
               (long long)video_time, (long long)audio_time,
               (unsigned long)sw_audio_ms,
               (long long)hw_video_pacer.next_ms,
               (long long)hw_audio_pacer.next_ms,
               (long long)(hw_video_pacer.next_ms - (int64_t)elapsed_ms),
               (long long)(hw_audio_pacer.next_ms - (int64_t)elapsed_ms),
               video_time >= 0 ?
               (long long)(hw_video_pacer.next_ms - video_time) : -1ll,
               audio_time >= 0 ?
               (long long)(hw_audio_pacer.next_ms - audio_time) : -1ll);
         if (auddec.fd >= 0 &&
             unifrog_audio_prefers_stereo_output() &&
             !audio_enabled &&
             !auddec_sw_fallback_attempted &&
             audio_stream >= 0 &&
             elapsed_ms >= MEDIA_GB300_AUDDEC_STALL_MS &&
             auddec.packets >= MEDIA_GB300_AUDDEC_STALL_PACKETS &&
             aud_status_ok &&
             media_auddec_runtime_decode_stalled(&aud_status,
                audio_time >= 0, audio_time)) {
            printf("unifrog media native auddec fallback trigger reason=decode_stall packets=%lu ms=%lu decoded=%lu hdr=%u/%u atime=%lld path=%s\n",
               (unsigned long)auddec.packets, (unsigned long)elapsed_ms,
               (unsigned long)aud_status.frames_decoded,
               aud_status.first_header_got, aud_status.first_header_parsed,
               (long long)audio_time, path ? path : "");
            media_auddec_close(&auddec);
            auddec_sw_fallback_attempted = 1;
            if (media_native_open_sw_audio(fmt, audio_stream,
                audio_output_channels, &audio_decoder, &audio_ctx, &audio,
                &audio_converter, &pcm, &audio_enabled,
                "auddec_decode_stall", path) == 0) {
               sw_audio_start_ms = unifrog_perf_time_ms();
               printf("unifrog media native auddec fallback active reason=decode_stall stream=%d path=%s\n",
                  audio_stream, path ? path : "");
            }
         }
         if (auddec.fd >= 0 && !audio_enabled && audio_time >= 0)
            media_auddec_enable_output_on_clock_progress(&auddec, audio_time,
               "video_monitor", auddec.packets);
         if (auddec.fd >= 0 && !audio_enabled && aud_status_ok &&
             media_auddec_status_has_progress(&aud_status))
            media_auddec_enable_output_on_progress(&auddec, &aud_status,
               "video_monitor", auddec.packets);
         media_draw_progress_overlay(&overlay, "video",
            audio_time >= 0 ? audio_time : video_time, duration_ms, 0, path);
      }
   }
   if (!native_video_failed) {
      media_video_finish_eos(video_fd, VIDEO_EOS_TIMEOUT_MS);
   } else {
      printf("unifrog media native video skip_eos reason=write_failed packets=%lu path=%s\n",
         (unsigned long)video_packets, path ? path : "");
   }
   if (video_fd >= 0) {
      struct vdec_decore_status status;
      int64_t video_time = -1;
      int64_t audio_time = -1;
      uint32_t sw_audio_ms = media_sw_audio_clock_ms(
         audio_enabled ? &audio : NULL, audio_frames,
         audio_ctx ? audio_ctx->sample_rate : 0, sw_audio_start_ms);

      memset(&status, 0, sizeof(status));
      (void)ioctl(video_fd, VIDDEC_GET_CUR_TIME, &video_time);
      if (auddec.fd >= 0)
         (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &audio_time);
      if (ioctl(video_fd, VIDDEC_GET_STATUS, &status) == 0) {
         frames_decoded = (unsigned long)status.frames_decoded;
         frames_displayed = (unsigned long)status.frames_displayed;
         printf("unifrog media native final_status decoded=%lu displayed=%lu hdr=%d pic=%d show=%d eos=%u err=%lu underrun=%lu used=%lu/%lu packets=%lu vtime=%lld atime=%lld sw_audio_ms=%lu feed_v=%lld feed_a=%lld\n",
            frames_decoded, frames_displayed,
            status.first_header_got,
            status.first_pic_decoded,
            status.first_pic_showed,
            (unsigned)status.get_pkt_eos,
            (unsigned long)status.decode_error,
            (unsigned long)status.under_run_cnt,
            (unsigned long)status.buffer_used,
            (unsigned long)status.buffer_size,
            (unsigned long)video_packets, (long long)video_time,
            (long long)audio_time, (unsigned long)sw_audio_ms,
            (long long)hw_video_pacer.next_ms,
            (long long)hw_audio_pacer.next_ms);
      }
   }
   if (auddec.fd >= 0)
      media_auddec_finish(&auddec, 1000);
   ret = !native_video_failed && video_packets &&
      (frames_decoded || frames_displayed) ? 0 : -1;

out:
   printf("unifrog media native end ret=%d video_packets=%lu audio_frames=%lu ms=%lu path=%s\n",
      ret, (unsigned long)video_packets,
      (unsigned long)(audio_frames + auddec.packets),
      (unsigned long)(unifrog_perf_time_ms() - start_ms), path ? path : "");
   media_video_activity_marker = 0;
   unifrog_exception_activity_clear();
   if (audio.fd >= 0)
      unifrog_audio_close(&audio);
   media_auddec_close(&auddec);
   if (video_fd >= 0) {
      int decoder_failed = native_video_failed ||
         (ret < 0 && video_packets > 0);

      media_video_release_decoder(video_fd, decoder_failed ? 1 : 0, 0,
         decoder_failed ? "native_video_error" : "native_video_close", path);
      close(video_fd);
      if (decoder_failed)
         media_video_reset_modules("native_video_error", path);
   }
   close_display();
   swr_free(&audio_converter.swr);
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
   media_buffered_input_close(&input, "native_video_close", path);
   if (sd_read_active)
      media_sd_read_end("native_video_close", path);
   return ret;
}

static int media_play_ffmpeg_video(const char *path,
   const struct unifrog_media_video_options *options)
{
   AVFormatContext *fmt = NULL;
   AVCodecContext *video_ctx = NULL;
   AVCodecContext *audio_ctx = NULL;
   AVCodec *video_decoder = NULL;
   AVCodec *audio_decoder = NULL;
   AVPacket *packet = NULL;
   AVFrame *frame = NULL;
   struct unifrog_audio audio;
   struct media_ffmpeg_audio_converter audio_converter;
   struct media_sw_video sw_video;
   struct media_buffered_input input;
   int16_t *pcm = NULL;
   int video_stream = -1;
   int audio_stream = -1;
   uint32_t audio_frames = 0;
   uint32_t video_frames = 0;
   uint32_t loop_polls = 0;
   uint32_t start_ms = unifrog_perf_time_ms();
   uint32_t swvideo_failures = 0;
   int audio_enabled = 0;
   int disable_audio = options && options->disable_audio;
   unsigned audio_output_channels = media_audio_output_channels();
   int ret = -1;
   int sd_read_active = 0;
   int abort_video = 0;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   memset(&audio_converter, 0, sizeof(audio_converter));
   memset(&sw_video, 0, sizeof(sw_video));
   memset(&input, 0, sizeof(input));
   input.fd = -1;
   input.file_size = -1;
   media_ffmpeg_register_once();
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_MEDIA_VIDEO,
      unifrog_exception_activity_hash(path ? path : "ffmpeg_video"), 0, 1);
   media_sd_read_begin("ffmpeg_video_open", path);
   sd_read_active = 1;
   printf("unifrog media ffmpeg video open_input begin path=%s\n",
      path ? path : "");
   if (media_buffered_input_open(&fmt, &input, path, "ffmpeg_video") < 0) {
      printf("unifrog media ffmpeg video open_input failed path=%s\n",
         path ? path : "");
      goto out;
   }
   if (avformat_find_stream_info(fmt, NULL) < 0) {
      printf("unifrog media ffmpeg video stream_info failed path=%s\n",
         path ? path : "");
      goto out;
   }
   media_buffered_input_log_coverage(&input, fmt, "ffmpeg_video", path);
   (void)media_buffered_input_enable_readahead(&input, fmt, "ffmpeg_video",
      path);
   media_log_format_streams(fmt, path, "ffmpeg_video_open");
   video_stream = media_find_stream_type(fmt, AVMEDIA_TYPE_VIDEO);
   if (!disable_audio)
      audio_stream = media_find_stream_type(fmt, AVMEDIA_TYPE_AUDIO);
   if (video_stream < 0) {
      printf("unifrog media ffmpeg video no_video audio=%d path=%s\n",
         audio_stream, path ? path : "");
      goto out;
   }
   media_clear_graphics_black("ffmpeg_video_prepare", path);
   video_decoder = avcodec_find_decoder(
      fmt->streams[video_stream]->codecpar->codec_id);
   if (!video_decoder) {
      printf("unifrog media ffmpeg video decoder missing codec=%d path=%s\n",
         fmt->streams[video_stream]->codecpar->codec_id, path ? path : "");
      goto out;
   }
   video_ctx = avcodec_alloc_context3(video_decoder);
   if (!video_ctx ||
       avcodec_parameters_to_context(video_ctx,
          fmt->streams[video_stream]->codecpar) < 0 ||
       avcodec_open2(video_ctx, video_decoder, NULL) < 0) {
      printf("unifrog media ffmpeg video decoder open failed codec=%s path=%s\n",
         video_decoder && video_decoder->name ? video_decoder->name : "?",
         path ? path : "");
      goto out;
   }
   printf("unifrog media ffmpeg video decoder codec=%s stream=%d %dx%d pix=%s path=%s\n",
      video_decoder->name ? video_decoder->name : "?",
      video_stream, video_ctx->width, video_ctx->height,
      media_pixel_format_name(video_ctx->pix_fmt), path ? path : "");

   if (audio_stream >= 0) {
      audio_decoder = avcodec_find_decoder(
         fmt->streams[audio_stream]->codecpar->codec_id);
      if (audio_decoder) {
         audio_ctx = avcodec_alloc_context3(audio_decoder);
         if (audio_ctx &&
             avcodec_parameters_to_context(audio_ctx,
                fmt->streams[audio_stream]->codecpar) == 0) {
            audio_ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;
            audio_ctx->request_channel_layout = media_audio_output_layout(
               media_audio_mix_channels(audio_output_channels));
            if (avcodec_open2(audio_ctx, audio_decoder, NULL) == 0 &&
                audio_ctx->sample_rate >= 8000 &&
                audio_ctx->sample_rate <= 48000 &&
                unifrog_audio_open(&audio, (unsigned)audio_ctx->sample_rate,
                   audio_output_channels,
                   512, 8) == 0) {
               pcm = malloc(sizeof(*pcm) * MEDIA_FFMPEG_CHUNK_FRAMES *
                  audio_output_channels);
               if (pcm) {
                  (void)unifrog_audio_set_volume(&audio,
                     media_audio_runtime_volume());
                  (void)unifrog_audio_set_mute(&audio, 1);
                  (void)unifrog_audio_start(&audio);
                  (void)unifrog_audio_set_output_enabled(&audio, 1);
                  audio_enabled = 1;
                  printf("unifrog media ffmpeg video audio enabled codec=%s stream=%d rate=%d src_ch=%d out_ch=%u fmt=%s\n",
                     audio_decoder->name ? audio_decoder->name : "?",
                     audio_stream, audio_ctx->sample_rate, audio_ctx->channels,
                     audio_output_channels, media_sample_format_name(audio_ctx->sample_fmt));
               }
            }
         }
      }
      if (!audio_enabled)
         printf("unifrog media ffmpeg video audio disabled stream=%d decoder=%s path=%s\n",
            audio_stream, audio_decoder && audio_decoder->name ?
            audio_decoder->name : "missing", path ? path : "");
   }

   packet = av_packet_alloc();
   frame = av_frame_alloc();
   if (!packet || !frame)
      goto out;
   open_display();
   if (fb_fd >= 0)
      (void)ioctl(fb_fd, FBIOBLANK, FB_BLANK_NORMAL);

   while (!media_exit_down() && !abort_video) {
      int read_ret = av_read_frame(fmt, packet);

      if (read_ret < 0)
         break;
      if (packet->stream_index == video_stream) {
         int send_ret = avcodec_send_packet(video_ctx, packet);

         if (send_ret < 0 && send_ret != AVERROR(EAGAIN)) {
            printf("unifrog media ffmpeg video send failed ret=%d path=%s\n",
               send_ret, path ? path : "");
            av_packet_unref(packet);
            break;
         }
         for (;;) {
            int recv_ret = avcodec_receive_frame(video_ctx, frame);

            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
               break;
            if (recv_ret < 0) {
               printf("unifrog media ffmpeg video receive failed ret=%d path=%s\n",
                  recv_ret, path ? path : "");
               break;
            }
            if (frame->format != AV_PIX_FMT_YUV420P &&
                frame->format != AV_PIX_FMT_YUVJ420P) {
               printf("unifrog media ffmpeg video unsupported_pix fmt=%s(%d) %dx%d path=%s\n",
                  media_pixel_format_name((enum AVPixelFormat)frame->format),
                  frame->format, frame->width, frame->height,
                  path ? path : "");
               av_frame_unref(frame);
               continue;
            }
            if (sw_video.frames == 0) {
               media_set_aspect_mode(DIS_TV_16_9, DIS_PILLBOX);
               (void)set_video_layer_visible(1, frame->width, frame->height,
                  VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
               printf("unifrog media ffmpeg video first_frame %dx%d fmt=%s path=%s\n",
                  frame->width, frame->height,
                  media_pixel_format_name((enum AVPixelFormat)frame->format),
                  path ? path : "");
            }
            if (media_swvideo_present(&sw_video, frame) == 0) {
               video_frames = sw_video.frames;
               swvideo_failures = 0;
            } else {
               swvideo_failures++;
               if (swvideo_failures >= MEDIA_SWVIDEO_DISPLAY_FAIL_LIMIT) {
                  printf("unifrog media ffmpeg video swvideo abort failures=%lu frame=%lu %dx%d path=%s\n",
                     (unsigned long)swvideo_failures,
                     (unsigned long)sw_video.frames, frame->width,
                     frame->height, path ? path : "");
                  abort_video = 1;
                  av_frame_unref(frame);
                  break;
               }
            }
            av_frame_unref(frame);
         }
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
               write_ret = media_ffmpeg_write_frame(&audio, audio_ctx,
                  &audio_converter, frame, pcm, MEDIA_FFMPEG_CHUNK_FRAMES,
                  &audio_frames, path);
               av_frame_unref(frame);
               if (write_ret != 0)
                  break;
            }
         }
      }
      av_packet_unref(packet);
      if (abort_video)
         break;
      if ((++loop_polls % 240u) == 0)
         printf("unifrog media ffmpeg video monitor video=%lu audio=%lu ms=%lu path=%s\n",
            (unsigned long)video_frames, (unsigned long)audio_frames,
            (unsigned long)(unifrog_perf_time_ms() - start_ms),
            path ? path : "");
   }

   if (!abort_video) {
      (void)avcodec_send_packet(video_ctx, NULL);
      for (;;) {
         int recv_ret = avcodec_receive_frame(video_ctx, frame);

         if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
            break;
         if (recv_ret < 0)
            break;
         if (frame->format == AV_PIX_FMT_YUV420P ||
             frame->format == AV_PIX_FMT_YUVJ420P) {
            if (media_swvideo_present(&sw_video, frame) == 0) {
               video_frames = sw_video.frames;
               swvideo_failures = 0;
            } else {
               swvideo_failures++;
               if (swvideo_failures >= MEDIA_SWVIDEO_DISPLAY_FAIL_LIMIT) {
                  printf("unifrog media ffmpeg video swvideo flush abort failures=%lu frame=%lu %dx%d path=%s\n",
                     (unsigned long)swvideo_failures,
                     (unsigned long)sw_video.frames, frame->width,
                     frame->height, path ? path : "");
                  abort_video = 1;
                  av_frame_unref(frame);
                  break;
               }
            }
         }
         av_frame_unref(frame);
      }
   }
   ret = video_frames && !abort_video ? 0 : -1;

out:
   printf("unifrog media ffmpeg video end ret=%d abort=%d video_frames=%lu audio_frames=%lu ms=%lu path=%s\n",
      ret, abort_video, (unsigned long)video_frames,
      (unsigned long)audio_frames,
      (unsigned long)(unifrog_perf_time_ms() - start_ms), path ? path : "");
   unifrog_exception_activity_clear();
   if (audio.fd >= 0)
      unifrog_audio_close(&audio);
   swr_free(&audio_converter.swr);
   media_swvideo_close(&sw_video);
   close_display();
   free(pcm);
   if (frame)
      av_frame_free(&frame);
   if (packet)
      av_packet_free(&packet);
   if (audio_ctx)
      avcodec_free_context(&audio_ctx);
   if (video_ctx)
      avcodec_free_context(&video_ctx);
   if (fmt)
      avformat_close_input(&fmt);
   media_buffered_input_close(&input, "ffmpeg_video_close", path);
   if (sd_read_active)
      media_sd_read_end("ffmpeg_video_close", path);
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
   auddec.prime_fd = -1;
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
   auddec.prime_fd = -1;
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
   auddec.prime_fd = -1;
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
   st.auddec.prime_fd = -1;
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
   auddec.prime_fd = -1;
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
   uint32_t loop_polls = 0;
   unsigned output_channels = media_audio_output_channels();
   struct media_progress_overlay overlay;
   int16_t mono[MEDIA_WAV_CHUNK_FRAMES];
   int16_t pcm[MEDIA_WAV_CHUNK_FRAMES * 2u];
   int ret = -1;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   memset(&overlay, 0, sizeof(overlay));
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
   if (unifrog_audio_open(&audio, rate, output_channels, 512, 8) != 0) {
      printf("unifrog media wav audio_open failed rate=%u ch=%u path=%s\n",
         rate, output_channels, path);
      goto out;
   }
   (void)unifrog_audio_set_volume(&audio, media_audio_runtime_volume());
   (void)unifrog_audio_set_mute(&audio, 0);
   (void)unifrog_audio_start(&audio);
   (void)unifrog_audio_set_output_enabled(&audio, 1);
   unifrog_audio_debug_dump(&audio, "wav_after_start");
   printf("unifrog media wav audio start rate=%u src_ch=%u out_ch=%u frames=%lu duration=%lu overlay=1 overlay_hide=A path=%s\n",
      rate, channels, output_channels, (unsigned long)frames,
      (unsigned long)media_audio_frames_to_ms(frames, (int)rate),
      path ? path : "");
   media_controls_reset_for_playback("wav_audio", path);
   media_draw_progress_overlay(&overlay, "audio_start", 0,
      media_audio_frames_to_ms(frames, (int)rate), 1, path);
   if (format == 2u) {
      uint8_t *block = malloc(block_align);

      if (!block)
         goto out;
      while (played < frames &&
          fread(block, 1, block_align, file) == block_align) {
         struct media_controls controls;
         int got = media_wav_decode_ms_adpcm_block(block, block_align,
            channels, coeffs, coeff_count, mono,
            frames - played > MEDIA_WAV_CHUNK_FRAMES ?
            MEDIA_WAV_CHUNK_FRAMES : frames - played);

         if (got <= 0)
            break;
         (void)media_audio_write_mono_output(&audio, mono, pcm,
            (unsigned)got, output_channels);
         played += (uint32_t)got;
         media_poll_controls(&controls);
         if (controls.overlay_toggle)
            media_toggle_progress_overlay(&overlay, "audio_toggle",
               media_audio_frames_to_ms(played, (int)rate),
               media_audio_frames_to_ms(frames, (int)rate), path);
         if ((++loop_polls % 16u) == 0)
            media_draw_progress_overlay(&overlay, "audio",
               media_audio_frames_to_ms(played, (int)rate),
               media_audio_frames_to_ms(frames, (int)rate), 0, path);
         if (controls.exit_down)
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
         if (output_channels > 1u) {
            int16_t sample = media_wav_clip_sample((left + right) >> 1);

            pcm[got * output_channels] = sample;
            pcm[got * output_channels + 1u] = sample;
            got++;
         } else {
            pcm[got++] = media_wav_clip_sample((left + right) >> 1);
         }
      }
      if (!got)
         break;
      (void)unifrog_audio_write(&audio, pcm, got);
      played += got;
      {
         struct media_controls controls;

         media_poll_controls(&controls);
         if (controls.overlay_toggle)
            media_toggle_progress_overlay(&overlay, "audio_toggle",
               media_audio_frames_to_ms(played, (int)rate),
               media_audio_frames_to_ms(frames, (int)rate), path);
         if ((++loop_polls % 16u) == 0)
            media_draw_progress_overlay(&overlay, "audio",
               media_audio_frames_to_ms(played, (int)rate),
               media_audio_frames_to_ms(frames, (int)rate), 0, path);
         if (controls.exit_down)
            break;
      }
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

   if (initialized) {
      if (unifrog_audio_prefers_stereo_output())
         unifrog_audio_prepare_output_route();
      return;
   }

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
   printf("unifrog media abi audio_cfg=%lu audio_status=%lu video_cfg=%lu pkt=%lu cmd_auddec_init=0x%lx cmd_viddec_init=0x%lx off_audio_extradata=%lu off_audio_frame=%lu off_audio_extrasz=%lu off_audio_extramode=%lu off_audio_bypass=%lu off_audio_kshm=%lu off_audio_chlayout=%lu off_audio_buf_start=%lu off_audio_buf_end=%lu off_audio_audsink=%lu off_audio_dma=%lu\n",
      (unsigned long)sizeof(struct audio_config),
      (unsigned long)sizeof(struct audio_decore_status),
      (unsigned long)sizeof(struct video_config),
      (unsigned long)sizeof(AvPktHd),
      (unsigned long)AUDDEC_INIT,
      (unsigned long)VIDDEC_INIT,
      (unsigned long)offsetof(struct audio_config, extradata),
      (unsigned long)offsetof(struct audio_config, codec_frame_size),
      (unsigned long)offsetof(struct audio_config, extradata_size),
      (unsigned long)offsetof(struct audio_config, extradata_mode),
      (unsigned long)offsetof(struct audio_config, bypass),
      (unsigned long)offsetof(struct audio_config, kshm_size),
      (unsigned long)offsetof(struct audio_config, channel_layout),
      (unsigned long)offsetof(struct audio_config, buffering_start),
      (unsigned long)offsetof(struct audio_config, buffering_end),
      (unsigned long)offsetof(struct audio_config, enable_audsink),
      (unsigned long)offsetof(struct audio_config, dma_buffer_time));
   (void)unifrog_log_flush();
   initialized = 1;
   if (unifrog_audio_prefers_stereo_output())
      unifrog_audio_prepare_output_route();
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
      ret = media_play_ffmpeg_audio_backend(path,
         media_gb300_auddec_fallback_backend("auddec_audio_fallback"),
         "auddec_audio_fallback");
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
   int force_ffmpeg = options && options->force_ffmpeg;
   int ret;
   size_t old_log_auto_flush;

   if (!path || !path[0])
      return -1;
   media_sd_read_recover_stale("play_start");
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   media_disk_suspend_begin("media_session", path);
   unifrog_log_set_auto_flush_bytes(VIDEO_LOG_AUTO_FLUSH_BYTES);
   printf("unifrog media start stack=native path=%s audio_only=%d image=%d force_native=%d force_ffmpeg=%d\n",
      path, audio_only, image_file, force_native, force_ffmpeg);
   (void)unifrog_log_flush();
   if (audio_only) {
      if (force_ffmpeg) {
         ret = media_play_ffmpeg_audio(path);
      } else if (media_is_wav_path(path)) {
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
            ret = media_play_ffmpeg_audio_backend(path,
               media_gb300_auddec_fallback_backend("auddec_audio_fallback"),
               "auddec_audio_fallback");
         }
      }
   } else if (image_file) {
      printf("unifrog media native image unsupported_needs_hcplayer path=%s\n",
         path);
      ret = -1;
   } else {
      ret = media_play_native_video(path, options);
      if (ret != 0) {
         printf("unifrog media native video fallback ffmpeg_swvideo ret=%d path=%s\n",
            ret, path ? path : "");
         ret = media_play_ffmpeg_video(path, options);
      }
   }
   if (ret != 0 && !force_native) {
      printf("unifrog media native fallback_unavailable ret=%d path=%s\n",
         ret, path);
      (void)unifrog_log_flush();
   }
   printf("unifrog media end stack=native ret=%d path=%s\n", ret, path);
   unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
   media_disk_suspend_end("media_session", path);
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
   media_disk_suspend_begin("media_session", path);
   unifrog_log_set_auto_flush_bytes(VIDEO_LOG_AUTO_FLUSH_BYTES);
   printf("unifrog media start path=%s audio_only=%d image=%d\n",
      path, audio_only, image_file);
   (void)unifrog_log_flush();
   media_log_file_probe(path, "play_start");
   media_log_ffmpeg_caps_once();
   if (options && options->preset >= 0 &&
      (unsigned)options->preset < sizeof(playback_presets) / sizeof(playback_presets[0]))
      preset = &playback_presets[options->preset];
   if (unifrog_audio_prefers_stereo_output() &&
       !(options && options->force_hcplayer)) {
      if (audio_only && !force_no_audio) {
         printf("unifrog media audio route=direct reason=gb300_hcplayer_native_audio path=%s\n",
            path ? path : "");
         ret = media_play_direct_audio(path);
         unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
         media_disk_suspend_end("media_session", path);
         (void)unifrog_log_flush();
         return ret;
      }
      if (!audio_only && !image_file && !force_no_audio) {
         printf("unifrog media video route=native reason=gb300_hcplayer_native_video path=%s\n",
            path ? path : "");
         ret = media_play_native_video(path, options);
         unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
         media_disk_suspend_end("media_session", path);
         (void)unifrog_log_flush();
         return ret;
      }
   }
   if (audio_only && options && options->force_native) {
      printf("unifrog media audio route=direct reason=explicit_native "
             "force_native=1 force_audio=%d path=%s\n",
         force_audio, path ? path : "");
      ret = media_play_direct_audio(path);
      unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
      media_disk_suspend_end("media_session", path);
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
      media_disk_suspend_end("media_session", path);
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
   init_args.buffering_start = MEDIA_VIDEO_BUFFERING_START_MS;
   init_args.buffering_end = MEDIA_VIDEO_BUFFERING_END_MS;
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
   (void)unifrog_audio_set_system_volume(media_audio_runtime_volume());
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
      (void)unifrog_audio_set_system_volume(media_audio_runtime_volume());
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
   media_disk_suspend_end("media_session", path);
   (void)unifrog_log_flush();
   return ret;
#endif
}

int unifrog_media_play_video(const char *path)
{
   return unifrog_media_play_video_ex(path, NULL);
}
