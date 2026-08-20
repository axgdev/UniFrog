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
#include <unifrog/hcrtos_media_abi.h>
#include <unifrog/input.h>
#include <unifrog/log.h>
#include <unifrog/media_content.h>
#include <unifrog/perf.h>
#include <unifrog/runtime.h>
#include <unifrog/storage_io.h>
#include <unifrog/text.h>
#include <unifrog/ui.h>

#include "unifrog_media_tuning_defaults.h"

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
#define MEDIA_AUDDEC_WRITE_EPERM_RECOVER_MS 250u
#define MEDIA_AUDIO_PROGRESS_POLL_MS 100u
#define MEDIA_GB300_AUDDEC_ROUTE_VARIANTS 1u
#define MEDIA_GB300_RAW_AUDDEC_ROUTE_VARIANTS 1u
/* Decoder rings are live after START; large leads play as startup bursts. */
#define MEDIA_VIDEO_FEED_LEAD_MS (media_runtime_tuning.video_feed_lead_ms)
#define MEDIA_AUDIO_FEED_LEAD_MS (media_runtime_tuning.audio_feed_lead_ms)
#define MEDIA_VIDEO_MAX_HW_AHEAD_MS \
   (media_runtime_tuning.video_max_hw_ahead_ms)
#define MEDIA_AUDIO_MAX_HW_AHEAD_MS \
   (media_runtime_tuning.audio_max_hw_ahead_ms)
#define MEDIA_HW_AHEAD_POLL_US 10000u
#define MEDIA_HW_AHEAD_LOG_MS 500u
#define MEDIA_HW_AHEAD_LOG_MIN_MS 100u
#define MEDIA_HW_AHEAD_MAX_WAIT_MS \
   (media_runtime_tuning.hw_ahead_max_wait_ms)
#define MEDIA_HW_AHEAD_OK 0
#define MEDIA_HW_AHEAD_TIMEOUT 1
#define MEDIA_HW_AHEAD_INTERRUPTED 2
#define MEDIA_SEEK_WARMUP_PACKETS \
   (media_runtime_tuning.seek_warmup_packets)
#define MEDIA_SEEK_VIDEO_WARMUP_PACKETS \
   (media_runtime_tuning.seek_video_warmup_packets)
#define MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS \
   (media_runtime_tuning.seek_video_recover_warmup_packets)
#define MEDIA_SEEK_SETTLE_MS (media_runtime_tuning.seek_settle_ms)
#define MEDIA_SEEK_ACCELERATE_FRAMES \
   (media_runtime_tuning.seek_accelerate_frames)
#define MEDIA_SEEK_KEYFRAME_DROP_LIMIT \
   (media_runtime_tuning.seek_keyframe_drop_limit)
#define MEDIA_SEEK_PREROLL_DECODE_MS \
   ((int64_t)media_runtime_tuning.seek_preroll_decode_ms)
#define MEDIA_SEEK_PREROLL_HD_DECODE_MS \
   ((int64_t)media_runtime_tuning.seek_preroll_hd_decode_ms)
#define MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES \
   (media_runtime_tuning.seek_preroll_keyframe_max_bytes)
#define MEDIA_VIDEO_STUCK_BEHIND_MS \
   (media_runtime_tuning.video_stuck_behind_ms)
#define MEDIA_VIDEO_STALL_RECOVER_MS \
   (media_runtime_tuning.video_stall_recover_ms)
#define MEDIA_VIDEO_RECOVER_GAP_MS \
   (media_runtime_tuning.video_recover_gap_ms)
#define MEDIA_VIDEO_WRITE_RECOVER_MAX \
   (media_runtime_tuning.video_write_recover_max)
#define MEDIA_VIDEO_WRITE_EPERM_RECOVER_MS \
   (media_runtime_tuning.video_write_eperm_recover_ms)
#define MEDIA_AUDIO_PACE_MAX_SLEEP_MS 100u
#define MEDIA_AUDIO_VOLUME 75u
#define MEDIA_WAV_CHUNK_FRAMES 512u
#define MEDIA_FFMPEG_CHUNK_FRAMES 512u
#define MEDIA_VIDEO_AUDIO_PERIOD_BYTES 2048u
#define MEDIA_VIDEO_AUDIO_PERIODS 16u
#define MEDIA_AUDIO_KSHM_SIZE 0x000a0000u
#define MEDIA_WAV_AUDDEC_CHUNK_BYTES 4096u
#define MEDIA_WAV_AUDDEC_MIN_FEED_MS 250u
#define MEDIA_WAV_AUDDEC_FINISH_PAD_MS 1000u
#define MEDIA_GB300_AUDDEC_PROBE_RATE 44100u
#define MEDIA_GB300_AUDDEC_PROBE_CHANNELS 2u
#define MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES 2048u
#define MEDIA_GB300_AUDDEC_PROBE_PACKETS 4u
#define MEDIA_GB300_AUDDEC_PROBE_PAUSE_US 280000u
#define MEDIA_GB300_PRODUCTION_TONE_FRAMES 512u
#define MEDIA_GB300_PRODUCTION_TONE_PACKETS 48u
#define MEDIA_GB300_I2SO_PRIME_PERIOD_BYTES 3072u
#define MEDIA_GB300_I2SO_PRIME_PERIODS 40u
#define MEDIA_GB300_I2SO_PRIME_START_THRESHOLD 2u
#define MEDIA_GB300_I2SO_PRIME_NONE 0
#define MEDIA_GB300_I2SO_PRIME_BEFORE_INIT 1
#define MEDIA_GB300_I2SO_PRIME_AFTER_START 2
#define MEDIA_VIDEO_KSHM_SIZE (media_runtime_tuning.video_kshm_size)
#define MEDIA_VIDEO_LOWRES_KSHM_SIZE \
   (media_runtime_tuning.video_lowres_kshm_size)
#define MEDIA_VIDEO_LOWRES_MAX_PIXELS (640u * 360u)
#define MEDIA_FILE_BUFFER_SIZE (media_runtime_tuning.file_buffer_size)
#define MEDIA_FILE_BUFFER_MIN_SIZE \
   (media_runtime_tuning.file_buffer_min_size)
#define MEDIA_FILE_READAHEAD_SIZE \
   (media_runtime_tuning.file_readahead_size)
#define MEDIA_FILE_READAHEAD_MIN_SIZE \
   (media_runtime_tuning.file_readahead_min_size)
#define MEDIA_FILE_READAHEAD_SLOTS \
   (media_runtime_tuning.file_readahead_slots)
#define MEDIA_VIDEO_READAHEAD_SIZE \
   (media_runtime_tuning.video_readahead_size)
#define MEDIA_VIDEO_READAHEAD_MIN_SIZE \
   (media_runtime_tuning.video_readahead_min_size)
#define MEDIA_VIDEO_READAHEAD_SLOTS \
   (media_runtime_tuning.video_readahead_slots)
#define MEDIA_VIDEO_PREFILL_TARGET_MS \
   (media_runtime_tuning.video_prefill_target_ms)
#define MEDIA_VIDEO_PREFILL_MIN_BYTES \
   (media_runtime_tuning.video_prefill_min_bytes)
#define MEDIA_VIDEO_PREFILL_MAX_BYTES \
   (media_runtime_tuning.video_prefill_max_bytes)
#define MEDIA_VIDEO_PRELOAD_MAX_BYTES \
   (media_runtime_tuning.video_preload_max_bytes)
#define MEDIA_FILE_SLOW_READ_LOG_MS \
   (media_runtime_tuning.file_slow_read_log_ms)
#define MEDIA_AUDIO_BUFFERING_START_MS \
   (media_runtime_tuning.audio_buffering_start_ms)
#define MEDIA_AUDIO_BUFFERING_END_MS \
   (media_runtime_tuning.audio_buffering_end_ms)
#define MEDIA_VIDEO_BUFFERING_START_MS \
   (media_runtime_tuning.video_buffering_start_ms)
#define MEDIA_VIDEO_BUFFERING_END_MS \
   (media_runtime_tuning.video_buffering_end_ms)
#define MEDIA_RESET_VIDDEC_ON_FAIL \
   (media_runtime_tuning.reset_viddec_on_fail)
#define MEDIA_GB300_AUDDEC_PROBE_ONCE \
   (media_runtime_tuning.gb300_auddec_probe_once)
#define MEDIA_SW_AUDIO_VIDEO_LEAD_MS 60u
#define MEDIA_SW_AUDIO_MAX_WAIT_MS 160u
#define MEDIA_SW_AUDIO_WAIT_POLL_US 2000u
#define MEDIA_SWVIDEO_DISPLAY_FAIL_LIMIT 3u
#define MEDIA_SWVIDEO_SEND_FAIL_LOG_LIMIT 16u
#define MEDIA_SWVIDEO_MIN_AUDIO_MS 500u
#define MEDIA_READAHEAD_MAX_SLOTS 16u
#define MEDIA_PROGRESS_OVERLAY_MIN_MS 500u
#define MEDIA_PROGRESS_OVERLAY_STRIP_PX 24u
#define MEDIA_PROGRESS_OVERLAY_BAR_H 10u
#define MEDIA_PROGRESS_OVERLAY_FB_H 240u
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MEDIA_SEEK_STEP_MS 10000
#define MEDIA_SWVIDEO_MMZ_ID 0
#define MEDIA_DYNAMIC_MMZ_ALIGN 4096u
#define MEDIA_DYNAMIC_MMZ_MIN_BYTES (8u * 1024u * 1024u)
#define MEDIA_TIME_UNSET INT32_MIN
#define MEDIA_TIME_HOLD INT64_MAX
#define MEDIA_GB300_AUDDEC_STALL_PACKETS 32u
#define MEDIA_GB300_AUDDEC_STALL_MS 2000u

extern struct unifrog_media_tuning media_runtime_tuning;

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
   int seek_clock_floor_active;
   int64_t seek_clock_floor_ms;
   uint32_t seek_clock_floor_wall_ms;
};

struct media_controls {
   int exit_down;
   int seek_delta_ms;
   int overlay_toggle;
};

struct media_progress_overlay {
   int hidden;
   int video_layer_active;
   int video_strip_active;
   int video_strip_dst_h;
   int fb_open;
   struct unifrog_fb fb;
   uint32_t last_draw_ms;
};

struct media_audio_screen {
   struct unifrog_ui ui;
   int open;
   uint32_t last_draw_ms;
};

static void media_audio_pacer_wait(struct media_audio_pacer *pacer,
   const AVPacket *packet, AVRational time_base);
static void media_audio_pacer_wait_lead(struct media_audio_pacer *pacer,
   const AVPacket *packet, AVRational time_base, unsigned feed_lead_ms);
void media_audio_pacer_seek_reset(struct media_audio_pacer *pacer,
   int64_t target_ms);
static void media_audio_pacer_seek_reset_warmup(
   struct media_audio_pacer *pacer, int64_t target_ms,
   unsigned warmup_packets);
int media_wait_hardware_ahead(const char *kind, int fd, int video,
   struct media_audio_pacer *pacer, unsigned max_ahead_ms,
   const char *path);
static int64_t media_format_duration_ms(AVFormatContext *fmt);
int64_t media_seek_target_ms(int64_t current_ms, int delta_ms,
   int64_t duration_ms);
int64_t media_seek_current_ms(int64_t video_time, int64_t audio_time,
   struct media_audio_pacer *video_pacer,
   struct media_audio_pacer *audio_pacer, int64_t pending_target_ms);
static int32_t media_packet_pts_ms(const AVPacket *packet,
   AVRational time_base);
static int32_t media_packet_duration_ms(const AVPacket *packet,
   AVRational time_base);
static int media_seek_format_ms(AVFormatContext *fmt, int64_t target_ms,
   const char *tag, const char *path);
static int64_t media_seek_video_preroll_limit_ms(
   const AVCodecParameters *codecpar);
static void media_video_note_progress(int64_t video_time,
   const struct vdec_decore_status *status, int64_t *progress_time_ms,
   unsigned long *progress_decoded, unsigned long *progress_displayed,
   uint32_t *progress_wall_ms);
void media_flush_auddec_for_seek(struct media_auddec *auddec,
   const char *tag, const char *path);
static void media_flush_viddec_for_seek(int video_fd, const char *tag,
   const char *path);
uint32_t media_audio_frames_to_ms(uint32_t frames, int rate);
uint32_t media_sw_audio_clock_ms(struct unifrog_audio *audio,
   uint32_t frames, int rate, uint32_t start_ms);
unsigned media_auddec_rotated_variant_index(unsigned order,
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

int media_auddec_variant_allowed(const char *label)
{
   if (!label)
      return 1;
   if (strncmp(label, "gb300_", 6) == 0)
      return unifrog_audio_prefers_stereo_output();
   if (strncmp(label, "sf2000_", 7) == 0)
      return !unifrog_audio_prefers_stereo_output();
   return 1;
}

uint32_t media_audio_preferred_snd_devs(void)
{
   if (unifrog_audio_prefers_stereo_output())
      return AUDDEV_I2SO;
   return AUDDEV_I2SO;
}

unsigned media_gb300_raw_auddec_route_counter;
unsigned media_gb300_auddec_route_counter;

unsigned media_auddec_rotated_variant_index(unsigned order,
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
   uint32_t storage_recoveries;
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
extern int mmz_create(void *start, size_t size) __attribute__((weak));
extern int mmz_delete(int id) __attribute__((weak));

int media_buffered_input_open(AVFormatContext **fmt_out,
   struct media_buffered_input *input, const char *path, const char *tag);
int media_buffered_input_enable_readahead(struct media_buffered_input *input,
   const AVFormatContext *fmt, const char *tag, const char *path);
void media_buffered_input_enable_video_readahead(
   struct media_buffered_input *input, const AVFormatContext *fmt,
   const struct unifrog_media_video_options *options, const char *path);
void media_buffered_input_log_coverage(
   const struct media_buffered_input *input, const AVFormatContext *fmt,
   const char *tag, const char *path);
void media_buffered_input_close(struct media_buffered_input *input,
   const char *tag, const char *path);
int media_play_wav_pcm(const char *path);
int media_play_wav_auddec(const char *path);
int media_play_mp3_auddec(const char *path);
int media_play_aac_adts_auddec(const char *path);
int media_play_flac_auddec(const char *path);
int media_play_ogg_auddec(const char *path);
static const char *media_pixel_format_name(enum AVPixelFormat fmt);

struct media_sw_video {
   uint32_t frames;
   struct SwsContext *fb_sws;
   struct unifrog_fb fb;
   int fb_src_w;
   int fb_src_h;
   enum AVPixelFormat fb_src_fmt;
   int fb_dst_w;
   int fb_dst_h;
};

uint16_t media_read_le16(const uint8_t *p)
{
   return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t media_read_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t media_read_be32(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int dis_fd = -1;
static int fb_fd = -1;
static int vidsink_fd = -1;
static unsigned media_video_debug_packets;
static int media_caps_logged;
unsigned media_sd_read_depth;
unsigned media_disk_suspend_depth;
uint32_t media_disk_suspend_start_ms;
uint32_t media_video_activity_marker;
uint32_t media_audio_activity_marker;
static int media_h264_packet_mode;
static int media_h264_nal_length_size;
static int media_h264_extra_delivery;
static int media_native_video_hardware_error;
int media_pending_seek_delta_ms;
int media_pending_overlay_toggle;
int media_controls_wait_release;
int media_controls_wait_logged;
struct media_audio_screen media_audio_screen;

extern unsigned long _padec_start;
extern unsigned long _padec_end;
extern unsigned long _pvdec_start;
extern unsigned long _pvdec_end;
extern unsigned long _deca_audio_stream_struct_start;
extern unsigned long _deca_audio_stream_struct_end;

void media_init_drivers_once(void);
int media_auddec_open(AVFormatContext *fmt, int stream_index,
   int sync_mode, struct media_auddec *auddec);
static const char *media_avcodec_name(enum AVCodecID codec_id);
int media_exit_down(void);
int media_auddec_send_packet(struct media_auddec *auddec,
   const AVPacket *packet);
int media_auddec_status_decode_stalled(
   const struct audio_decore_status *status);
int media_auddec_clock_has_progress(int time_ok, int64_t cur_time);
int media_auddec_runtime_decode_stalled(
   const struct audio_decore_status *status, int time_ok, int64_t cur_time);
int media_auddec_status_has_progress(
   const struct audio_decore_status *status);
void media_auddec_enable_output_on_progress(
   struct media_auddec *auddec, const struct audio_decore_status *status,
   const char *scope, uint32_t packet_index);
void media_auddec_enable_output_on_clock_progress(
   struct media_auddec *auddec, int64_t cur_time, const char *scope,
   uint32_t packet_index);
void media_auddec_log_packet_status(struct media_auddec *auddec,
   const char *scope, uint32_t packet_index, const uint8_t *data, size_t size,
   int32_t pts, int32_t dur);
void media_auddec_finish(struct media_auddec *auddec,
   unsigned timeout_ms);
void media_auddec_release_fd(int *fdp, const char *tag);
void media_auddec_close(struct media_auddec *auddec);
int media_run_gb300_auddec_probe(const char *tag);
void media_run_gb300_auddec_probe_once(const char *tag);
static int media_video_wait_write_space(int fd, uint32_t need,
   unsigned packet_index);

unsigned media_audio_output_channels(void)
{
   return unifrog_audio_output_channels();
}

unsigned media_audio_mix_channels(unsigned output_channels)
{
   if (unifrog_audio_prefers_stereo_output() && output_channels > 1u)
      return 1u;
   return output_channels;
}

uint64_t media_audio_output_layout(unsigned channels)
{
   return channels > 1u ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
}

static int media_channel_count(const AVChannelLayout *layout)
{
   return layout && layout->nb_channels > 0 ? layout->nb_channels : 0;
}

static int media_codecpar_channels(const AVCodecParameters *par)
{
   return par ? media_channel_count(&par->ch_layout) : 0;
}

static int media_codec_channels(const AVCodecContext *codec)
{
   return codec ? media_channel_count(&codec->ch_layout) : 0;
}

static int media_frame_channels(const AVFrame *frame)
{
   return frame ? media_channel_count(&frame->ch_layout) : 0;
}

static uint64_t media_channel_mask(const AVChannelLayout *layout)
{
   AVChannelLayout fallback = { 0 };
   uint64_t mask = 0;

   if (!layout || layout->nb_channels <= 0)
      return 0;
   if (layout->order == AV_CHANNEL_ORDER_NATIVE)
      return layout->u.mask;
   av_channel_layout_default(&fallback, layout->nb_channels);
   if (fallback.order == AV_CHANNEL_ORDER_NATIVE)
      mask = fallback.u.mask;
   av_channel_layout_uninit(&fallback);
   return mask;
}

audio_channel_select_t media_audio_channel_select(void)
{
   return media_audio_output_channels() > 1u ? AUDIO_STEREO :
      AUDIO_MONO_LEFT;
}

uint8_t media_audio_runtime_volume(void)
{
   return unifrog_audio_prefers_stereo_output() ? 90u :
      (uint8_t)MEDIA_AUDIO_VOLUME;
}

int media_gb300_i2so_prime_open(const char *tag,
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

void media_gb300_i2so_prime_close(int *fdp, const char *tag)
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

void media_expand_mono_to_output(const int16_t *mono, int16_t *output,
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

void media_expand_mono_to_output_inplace(int16_t *buffer,
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

int media_audio_write_mono_output(struct unifrog_audio *audio,
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

void media_log_pcm_stats(const char *scope,
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

static uintptr_t media_align_up_uintptr(uintptr_t value, size_t alignment)
{
   uintptr_t rem;
   uintptr_t add;

   if (alignment <= 1u)
      return value;
   rem = value % (uintptr_t)alignment;
   if (!rem)
      return value;
   add = (uintptr_t)alignment - rem;
   if (value > (uintptr_t)-1 - add)
      return 0;
   return value + add;
}

static uintptr_t media_align_down_uintptr(uintptr_t value, size_t alignment)
{
   if (alignment <= 1u)
      return value;
   return value - value % (uintptr_t)alignment;
}

static uintptr_t media_mips_physical_addr(uintptr_t addr)
{
   return addr & 0x1fffffffu;
}

static int media_dynamic_mmz_acquire(const char *tag, const char *path)
{
   struct unifrog_abi_memory_slot slot;
   uintptr_t start;
   uintptr_t end;
   uintptr_t phys;
   size_t bytes;
   size_t existing;
   int id;

   existing = media_mmz_total0();
   if (existing > 0u) {
      printf("unifrog media mmz lease tag=%s source=existing id=%d bytes=%lu path=%s\n",
         tag ? tag : "?", MEDIA_SWVIDEO_MMZ_ID, (unsigned long)existing,
         path ? path : "");
      return 0;
   }

   if (!mmz_total || !mmz_create || !mmz_delete) {
      printf("unifrog media mmz lease unavailable tag=%s reason=symbols path=%s\n",
         tag ? tag : "?", path ? path : "");
      return -1;
   }

   memset(&slot, 0, sizeof(slot));
   if (unifrog_abi_application_memory_slot(&slot) != 0 || slot.bytes == 0) {
      printf("unifrog media mmz lease unavailable tag=%s reason=no_appmem path=%s\n",
         tag ? tag : "?", path ? path : "");
      return -1;
   }
   if (slot.base > (uintptr_t)-1 - slot.bytes) {
      printf("unifrog media mmz lease unavailable tag=%s reason=appmem_overflow base=0x%08lx bytes=%lu path=%s\n",
         tag ? tag : "?", (unsigned long)slot.base,
         (unsigned long)slot.bytes, path ? path : "");
      return -1;
   }

   start = media_align_up_uintptr(slot.base, MEDIA_DYNAMIC_MMZ_ALIGN);
   end = media_align_down_uintptr(slot.base + slot.bytes,
      MEDIA_DYNAMIC_MMZ_ALIGN);
   if (!start || end <= start || end - start < MEDIA_DYNAMIC_MMZ_MIN_BYTES) {
      printf("unifrog media mmz lease unavailable tag=%s reason=too_small base=0x%08lx bytes=%lu aligned=%lu path=%s\n",
         tag ? tag : "?", (unsigned long)slot.base,
         (unsigned long)slot.bytes,
         (unsigned long)(end > start ? end - start : 0u),
         path ? path : "");
      return -1;
   }

   bytes = (size_t)(end - start);
   phys = media_mips_physical_addr(start);
   id = mmz_create((void *)phys, bytes);
   if (id != MEDIA_SWVIDEO_MMZ_ID) {
      if (id >= 0)
         (void)mmz_delete(id);
      printf("unifrog media mmz lease failed tag=%s id=%d expected=%d cached=0x%08lx phys=0x%08lx bytes=%lu path=%s\n",
         tag ? tag : "?", id, MEDIA_SWVIDEO_MMZ_ID,
         (unsigned long)start, (unsigned long)phys, (unsigned long)bytes,
         path ? path : "");
      return -1;
   }

   existing = media_mmz_total0();
   if (existing == 0u) {
      (void)mmz_delete(MEDIA_SWVIDEO_MMZ_ID);
      printf("unifrog media mmz lease failed tag=%s reason=empty_after_create cached=0x%08lx phys=0x%08lx bytes=%lu path=%s\n",
         tag ? tag : "?", (unsigned long)start, (unsigned long)phys,
         (unsigned long)bytes, path ? path : "");
      return -1;
   }

   printf("unifrog media mmz lease tag=%s source=appmem id=%d cached=0x%08lx phys=0x%08lx bytes=%lu total=%lu path=%s\n",
      tag ? tag : "?", id, (unsigned long)start, (unsigned long)phys,
      (unsigned long)bytes, (unsigned long)existing, path ? path : "");
   return 1;
}

static void media_dynamic_mmz_release(int lease, const char *tag,
   const char *path)
{
   size_t before;
   int ret;
   size_t after;

   if (lease != 1)
      return;
   if (!mmz_delete)
      return;

   before = media_mmz_total0();
   ret = mmz_delete(MEDIA_SWVIDEO_MMZ_ID);
   after = media_mmz_total0();
   printf("unifrog media mmz release tag=%s ret=%d before=%lu after=%lu path=%s\n",
      tag ? tag : "?", ret, (unsigned long)before, (unsigned long)after,
      path ? path : "");
}

uint32_t media_video_activity_mark_value(void)
{
   if (media_video_activity_marker == 0)
      media_video_activity_marker =
         unifrog_exception_activity_hash("native_video");
   return media_video_activity_marker;
}

void media_video_activity_stage(uint32_t stage, uint32_t detail0,
   uint32_t detail1)
{
   uint32_t packed = ((stage & 0xffu) << 24) | (detail0 & 0x00ffffffu);

   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_MEDIA_VIDEO,
      media_video_activity_mark_value(), packed, detail1);
}

uint32_t media_audio_activity_mark_value(void)
{
   if (media_audio_activity_marker == 0)
      media_audio_activity_marker =
         unifrog_exception_activity_hash("native_audio");
   return media_audio_activity_marker;
}

void media_audio_activity_stage(uint32_t stage, uint32_t detail0,
   uint32_t detail1)
{
   uint32_t packed = ((stage & 0xffu) << 24) | (detail0 & 0x00ffffffu);

   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_MEDIA_AUDIO,
      media_audio_activity_mark_value(), packed, detail1);
}

void media_video_progress(
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

void media_disk_suspend_begin(const char *tag, const char *path)
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

void media_disk_suspend_end(const char *tag, const char *path)
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

void media_sd_read_begin(const char *tag, const char *path)
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

void media_sd_read_end(const char *tag, const char *path)
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

void media_sd_read_recover_stale(const char *tag)
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
         par->sample_rate, media_codecpar_channels(par),
         par->bits_per_coded_sample,
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

static int media_progress_overlay_open_fb(
   struct media_progress_overlay *overlay, struct unifrog_fb *scratch,
   unsigned flags, const char *tag, int64_t pos_ms, int64_t dur_ms,
   const char *path, int *scratch_open)
{
   struct unifrog_fb *fb;
   int ret;

   if (scratch_open)
      *scratch_open = 0;
   if (overlay && overlay->fb_open) {
      if (overlay->fb.bpp == 16)
         return 0;
      printf("unifrog media overlay tag=%s cached_bpp=%u pos=%lld dur=%lld path=%s\n",
         tag ? tag : "", overlay->fb.bpp, (long long)pos_ms,
         (long long)dur_ms, path ? path : "");
      return -1;
   }

   fb = overlay ? &overlay->fb : scratch;
   if (!fb)
      return -1;
   memset(fb, 0, sizeof(*fb));
   fb->fd = -1;
   ret = unifrog_fb_open(fb, flags);
   if (ret != 0 || fb->bpp != 16) {
      printf("unifrog media overlay tag=%s ret=%d bpp=%u pos=%lld dur=%lld path=%s\n",
         tag ? tag : "", ret, fb->bpp, (long long)pos_ms,
         (long long)dur_ms, path ? path : "");
      if (ret == 0)
         unifrog_fb_close(fb);
      return -1;
   }

   if (overlay)
      overlay->fb_open = 1;
   else if (scratch_open)
      *scratch_open = 1;
   return 0;
}

static struct unifrog_fb *media_progress_overlay_fb(
   struct media_progress_overlay *overlay, struct unifrog_fb *scratch)
{
   if (overlay && overlay->fb_open)
      return &overlay->fb;
   return scratch;
}

static void media_progress_overlay_release_fb(
   struct media_progress_overlay *overlay, struct unifrog_fb *scratch,
   int scratch_open)
{
   if (!overlay && scratch_open && scratch)
      unifrog_fb_close(scratch);
}

static void media_progress_overlay_present_fb(struct unifrog_fb *fb)
{
   if (!fb)
      return;
   unifrog_fb_flush(fb);
   (void)unifrog_fb_pan(fb, fb->current_buffer);
}

static unsigned media_overlay_strip_y(const struct unifrog_fb *fb)
{
   unsigned strip_h;

   if (!fb || fb->height == 0)
      return 0;
   strip_h = MEDIA_PROGRESS_OVERLAY_STRIP_PX;
   if (strip_h >= fb->height)
      return 0;
   return fb->height - strip_h;
}

static unsigned media_overlay_strip_h(const struct unifrog_fb *fb)
{
   unsigned strip_y;

   if (!fb || fb->height == 0)
      return 0;
   strip_y = media_overlay_strip_y(fb);
   return fb->height > strip_y ? fb->height - strip_y : 0;
}

static int media_overlay_video_dst_h(unsigned fb_height, unsigned strip_px)
{
   unsigned hd_strip;

   if (fb_height == 0)
      fb_height = MEDIA_PROGRESS_OVERLAY_FB_H;
   if (strip_px == 0 || strip_px >= fb_height)
      return VIDEO_OUTPUT_H;
   hd_strip = (strip_px * VIDEO_OUTPUT_H + fb_height - 1u) / fb_height;
   if (hd_strip == 0)
      hd_strip = 1;
   if (hd_strip >= VIDEO_OUTPUT_H)
      hd_strip = VIDEO_OUTPUT_H - 1u;
   return (int)(VIDEO_OUTPUT_H - hd_strip);
}

static int media_progress_overlay_target_dst_h(
   const struct media_progress_overlay *overlay)
{
   int dst_h = overlay ? overlay->video_strip_dst_h : 0;

   if (dst_h <= 0 || dst_h >= VIDEO_OUTPUT_H)
      dst_h = media_overlay_video_dst_h(MEDIA_PROGRESS_OVERLAY_FB_H,
         MEDIA_PROGRESS_OVERLAY_STRIP_PX);
   return dst_h;
}

static void media_progress_overlay_set_video_window(
   struct media_progress_overlay *overlay, int strip, int force,
   const char *tag, const char *path)
{
   int want_strip;
   int dst_h;
   int ret;

   if (!overlay || !overlay->video_layer_active)
      return;
   want_strip = strip && !overlay->hidden;
   dst_h = want_strip ? media_progress_overlay_target_dst_h(overlay) :
      VIDEO_OUTPUT_H;
   if (!force && overlay->video_strip_active == want_strip &&
       (!want_strip || overlay->video_strip_dst_h == dst_h))
      return;

   ret = set_video_layer_visible(1, VIDEO_SOURCE_W, VIDEO_SOURCE_H,
      VIDEO_OUTPUT_W, dst_h);
   if (ret == 0) {
      overlay->video_strip_active = want_strip && dst_h < VIDEO_OUTPUT_H;
      if (overlay->video_strip_active)
         overlay->video_strip_dst_h = dst_h;
   }
   printf("unifrog media overlay video_window tag=%s strip=%d dst=%dx%d ret=%d active=%d path=%s\n",
      tag ? tag : "", want_strip, VIDEO_OUTPUT_W, dst_h, ret,
      overlay->video_strip_active, path ? path : "");
}

static void media_progress_overlay_video_layer_inactive(
   struct media_progress_overlay *overlay)
{
   if (!overlay)
      return;
   overlay->video_layer_active = 0;
   overlay->video_strip_active = 0;
}

static void media_progress_overlay_clear_all(
   struct media_progress_overlay *overlay, const char *tag, const char *path)
{
   struct unifrog_fb scratch;
   struct unifrog_fb *fb;
   int scratch_open = 0;

   if (media_progress_overlay_open_fb(overlay, &scratch,
       UNIFROG_FB_OPEN_DEFAULT, tag, 0, 0, path, &scratch_open) != 0)
      return;
   fb = media_progress_overlay_fb(overlay, &scratch);
   media_fb_blend_rect(fb, 0, 0, fb->width, fb->height, 0x0000, 255u);
   media_progress_overlay_present_fb(fb);
   media_progress_overlay_release_fb(overlay, &scratch, scratch_open);
   printf("unifrog media overlay clear_all tag=%s path=%s\n",
      tag ? tag : "", path ? path : "");
}

static void media_overlay_geometry(const struct unifrog_fb *fb,
   unsigned *bar_x, unsigned *bar_y, unsigned *bar_w)
{
   unsigned x = 0;
   unsigned y = 0;
   unsigned w = 0;
   unsigned strip_y;
   unsigned strip_h;

   if (fb) {
      x = fb->width > 300u ? (fb->width - 300u) / 2u : 8u;
      w = fb->width > 300u ? 300u :
         (fb->width > 16u ? fb->width - 16u : fb->width);
      strip_y = media_overlay_strip_y(fb);
      strip_h = media_overlay_strip_h(fb);
      y = strip_y;
      if (strip_h > MEDIA_PROGRESS_OVERLAY_BAR_H + 2u)
         y += (strip_h - (MEDIA_PROGRESS_OVERLAY_BAR_H + 2u)) / 2u;
   }
   if (bar_x)
      *bar_x = x;
   if (bar_y)
      *bar_y = y;
   if (bar_w)
      *bar_w = w;
}

static void media_clear_progress_overlay_pixels(
   struct media_progress_overlay *overlay, const char *tag, const char *path)
{
   struct unifrog_fb scratch;
   struct unifrog_fb *fb;
   unsigned strip_y;
   unsigned strip_h;
   int scratch_open = 0;

   if (media_progress_overlay_open_fb(overlay, &scratch,
       UNIFROG_FB_OPEN_PRESERVE, tag, 0, 0, path, &scratch_open) != 0)
      return;
   fb = media_progress_overlay_fb(overlay, &scratch);
   strip_y = media_overlay_strip_y(fb);
   strip_h = media_overlay_strip_h(fb);
   if (strip_h)
      media_fb_blend_rect(fb, 0, strip_y, fb->width, strip_h, 0x0000, 255u);
   media_progress_overlay_present_fb(fb);
   media_progress_overlay_release_fb(overlay, &scratch, scratch_open);
}

static void media_clear_progress_overlay(struct media_progress_overlay *overlay,
   const char *tag, const char *path)
{
   if (overlay) {
      overlay->last_draw_ms = 0;
      media_progress_overlay_set_video_window(overlay, 0, 0, tag, path);
   }
   media_clear_progress_overlay_pixels(overlay, tag, path);
   printf("unifrog media overlay hidden tag=%s path=%s\n",
      tag ? tag : "", path ? path : "");
}

static void media_draw_progress_overlay(struct media_progress_overlay *overlay,
   const char *tag, int64_t pos_ms, int64_t dur_ms, int force,
   const char *path)
{
   struct unifrog_fb scratch;
   struct unifrog_fb *fb;
   uint32_t now;
   unsigned bar_x;
   unsigned bar_y;
   unsigned bar_w;
   unsigned fill_w;
   unsigned strip_y;
   unsigned strip_h;
   int scratch_open = 0;

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
   if (media_progress_overlay_open_fb(overlay, &scratch,
       UNIFROG_FB_OPEN_PRESERVE, tag, pos_ms, dur_ms, path,
       &scratch_open) != 0)
      return;
   fb = media_progress_overlay_fb(overlay, &scratch);
   media_overlay_geometry(fb, &bar_x, &bar_y, &bar_w);
   strip_y = media_overlay_strip_y(fb);
   strip_h = media_overlay_strip_h(fb);
   overlay->video_strip_dst_h =
      media_overlay_video_dst_h(fb->height, strip_h);
   fill_w = (unsigned)(((uint64_t)bar_w * (uint64_t)pos_ms) /
      (uint64_t)dur_ms);
   if (fill_w > bar_w)
      fill_w = bar_w;
   if (strip_h)
      media_fb_blend_rect(fb, 0, strip_y, fb->width, strip_h, 0x0000, 255u);
   media_fb_blend_rect(fb, bar_x, bar_y, bar_w,
      MEDIA_PROGRESS_OVERLAY_BAR_H, UNIFROG_RGB565(230, 236, 242), 255u);
   if (bar_w > 4u)
      media_fb_blend_rect(fb, bar_x + 2u, bar_y + 2u, bar_w - 4u,
         MEDIA_PROGRESS_OVERLAY_BAR_H - 4u, UNIFROG_RGB565(26, 32, 38),
         255u);
   if (fill_w > 4u)
      media_fb_blend_rect(fb, bar_x + 2u, bar_y + 2u, fill_w - 4u,
         MEDIA_PROGRESS_OVERLAY_BAR_H - 4u, UNIFROG_RGB565(64, 255, 120),
         255u);
   media_progress_overlay_present_fb(fb);
   media_progress_overlay_release_fb(overlay, &scratch, scratch_open);
   overlay->last_draw_ms = now;
   media_progress_overlay_set_video_window(overlay, 1, 0, tag, path);
   if (force)
      printf("unifrog media overlay tag=%s pos=%lld dur=%lld fill=%u/%u path=%s\n",
         tag ? tag : "", (long long)pos_ms, (long long)dur_ms, fill_w,
         bar_w, path ? path : "");
}

static void media_progress_overlay_close(struct media_progress_overlay *overlay,
   const char *tag, const char *path)
{
   if (!overlay)
      return;
   if (!overlay->last_draw_ms && !overlay->video_strip_active &&
       overlay->video_strip_dst_h == 0 && !overlay->fb_open)
      return;
   media_clear_progress_overlay_pixels(overlay, tag, path);
   overlay->last_draw_ms = 0;
   overlay->video_layer_active = 0;
   overlay->video_strip_active = 0;
   overlay->video_strip_dst_h = 0;
   if (overlay->fb_open) {
      unifrog_fb_close(&overlay->fb);
      overlay->fb_open = 0;
   }
   printf("unifrog media overlay close tag=%s path=%s\n",
      tag ? tag : "", path ? path : "");
}

static const char *media_path_basename(const char *path)
{
   const char *slash;

   if (!path || !*path)
      return "Unknown";
   slash = strrchr(path, '/');
   return slash && slash[1] ? slash + 1 : path;
}

static void media_format_time_text(int64_t ms, char *text, size_t text_size)
{
   unsigned total;
   unsigned hours;
   unsigned minutes;
   unsigned seconds;

   if (!text || text_size == 0)
      return;
   if (ms < 0) {
      snprintf(text, text_size, "--:--");
      return;
   }
   total = (unsigned)(ms / 1000);
   hours = total / 3600u;
   minutes = (total / 60u) % 60u;
   seconds = total % 60u;
   if (hours)
      snprintf(text, text_size, "%u:%02u:%02u", hours, minutes, seconds);
   else
      snprintf(text, text_size, "%u:%02u", minutes, seconds);
}

static int media_audio_screen_open(const char *tag, const char *path)
{
   int ret;

   if (media_audio_screen.open)
      return 0;
   memset(&media_audio_screen.ui, 0, sizeof(media_audio_screen.ui));
   ret = unifrog_ui_open(&media_audio_screen.ui, 0);
   if (ret != 0) {
      printf("unifrog media audio_screen_open failed ret=%d tag=%s path=%s\n",
         ret, tag ? tag : "", path ? path : "");
      return -1;
   }
   media_audio_screen.open = 1;
   media_audio_screen.last_draw_ms = 0;
   return 0;
}

void media_audio_screen_draw(const char *tag, const char *path,
   int64_t pos_ms, int64_t dur_ms, int force)
{
   struct unifrog_surface surface;
   const char *name = media_path_basename(path);
   uint32_t now;
   int bar_x;
   int bar_y;
   int bar_w;
   int bar_h;
   int fill_w = 0;
   char pos_text[16];
   char dur_text[16];
   char time_text[40];

   now = unifrog_perf_time_ms();
   if (!force && media_audio_screen.last_draw_ms &&
       now - media_audio_screen.last_draw_ms < MEDIA_PROGRESS_OVERLAY_MIN_MS)
      return;
   if (media_audio_screen_open(tag, path) != 0)
      return;
   media_audio_screen.last_draw_ms = now;
   if (pos_ms < 0)
      pos_ms = 0;
   if (dur_ms > 0 && pos_ms > dur_ms)
      pos_ms = dur_ms;

   unifrog_ui_begin(&media_audio_screen.ui, UNIFROG_RGB565(0, 0, 0));
   surface = unifrog_ui_surface(&media_audio_screen.ui);
   unifrog_gfx_draw_text(&surface, 18, 54, "Now Playing",
      UNIFROG_RGB565(236, 241, 246), 2);
   unifrog_ui_text_clipped(&media_audio_screen.ui, 18, 86, 38, name,
      UNIFROG_RGB565(160, 174, 188), 1);
   unifrog_ui_text_clipped(&media_audio_screen.ui, 18, 104, 46,
      path ? path : "", UNIFROG_RGB565(112, 126, 140), 1);

   bar_x = 18;
   bar_y = (int)surface.height - 82;
   bar_w = (int)surface.width - 36;
   bar_h = 14;
   if (dur_ms > 0)
      fill_w = (int)(((int64_t)(bar_w - 4) * pos_ms) / dur_ms);
   if (fill_w < 0)
      fill_w = 0;
   if (fill_w > bar_w - 4)
      fill_w = bar_w - 4;
   unifrog_gfx_fill_rect(&surface, bar_x, bar_y, bar_w, bar_h,
      UNIFROG_RGB565(42, 50, 60));
   if (fill_w > 0)
      unifrog_gfx_fill_rect(&surface, bar_x + 2, bar_y + 2, fill_w,
         bar_h - 4, UNIFROG_RGB565(68, 188, 136));
   media_format_time_text(pos_ms, pos_text, sizeof(pos_text));
   media_format_time_text(dur_ms, dur_text, sizeof(dur_text));
   snprintf(time_text, sizeof(time_text), "%s / %s", pos_text, dur_text);
   unifrog_gfx_draw_text(&surface, 18, bar_y + 24, time_text,
      UNIFROG_RGB565(236, 241, 246), 1);
   unifrog_gfx_draw_text(&surface, 18, (int)surface.height - 22, "B stop",
      UNIFROG_RGB565(160, 174, 188), 1);
   unifrog_gfx_draw_text(&surface, 172, (int)surface.height - 22,
      "Left/Right seek", UNIFROG_RGB565(160, 174, 188), 1);
   unifrog_ui_present(&media_audio_screen.ui);
   if (force)
      printf("unifrog media audio_screen tag=%s pos=%lld dur=%lld fill=%d/%d path=%s\n",
         tag ? tag : "", (long long)pos_ms, (long long)dur_ms, fill_w,
         bar_w - 4, path ? path : "");
}

static void media_audio_screen_close(void)
{
   if (!media_audio_screen.open)
      return;
   unifrog_ui_close(&media_audio_screen.ui);
   memset(&media_audio_screen, 0, sizeof(media_audio_screen));
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
   struct media_progress_overlay *overlay, const char *path)
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
       status.frames_displayed == 0)
      return 0;
   media_set_aspect_mode(DIS_TV_16_9, DIS_PILLBOX);
   if (overlay) {
      overlay->video_layer_active = 1;
      media_progress_overlay_set_video_window(overlay,
         overlay->last_draw_ms != 0, 1, "video_reveal", path);
   } else {
      (void)set_video_layer_visible(1, VIDEO_SOURCE_W, VIDEO_SOURCE_H,
         VIDEO_OUTPUT_W, VIDEO_OUTPUT_H);
   }
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
   sws_freeContext(video->fb_sws);
   video->fb_sws = NULL;
   if (video->fb.fd >= 0)
      unifrog_fb_close(&video->fb);
   video->frames = 0;
   video->fb.fd = -1;
   video->fb_src_w = 0;
   video->fb_src_h = 0;
   video->fb_src_fmt = AV_PIX_FMT_NONE;
   video->fb_dst_w = 0;
   video->fb_dst_h = 0;
}

static void media_swvideo_fit_rect(unsigned src_w, unsigned src_h,
   unsigned dst_w, unsigned dst_h, unsigned *x, unsigned *y, unsigned *w,
   unsigned *h)
{
   uint64_t scaled_w;
   uint64_t scaled_h;

   if (!src_w || !src_h || !dst_w || !dst_h) {
      *x = 0;
      *y = 0;
      *w = dst_w;
      *h = dst_h;
      return;
   }
   scaled_w = dst_w;
   scaled_h = (uint64_t)dst_w * src_h / src_w;
   if (scaled_h > dst_h) {
      scaled_h = dst_h;
      scaled_w = (uint64_t)dst_h * src_w / src_h;
   }
   if (!scaled_w)
      scaled_w = 1;
   if (!scaled_h)
      scaled_h = 1;
   if (scaled_w > dst_w)
      scaled_w = dst_w;
   if (scaled_h > dst_h)
      scaled_h = dst_h;
   *w = (unsigned)scaled_w;
   *h = (unsigned)scaled_h;
   *x = (dst_w - *w) / 2u;
   *y = (dst_h - *h) / 2u;
}

static int media_swvideo_present_fb(struct media_sw_video *video,
   const AVFrame *frame)
{
   unsigned dst_x;
   unsigned dst_y;
   unsigned dst_w;
   unsigned dst_h;
   uint8_t *dst_data[4];
   int dst_linesize[4];
   enum AVPixelFormat src_fmt;
   int scale_ret;

   if (!video || !frame || frame->width <= 0 || frame->height <= 0)
      return -1;
   /*
    * This path is a diagnostic/fallback renderer for FFmpeg software video.
    * Native media playback should keep using the hardware video pipeline, but
    * the matrix must still exercise software decode without depending on the
    * video sink accepting large decoded YUV frames.  Scale into the visible
    * RGB565 framebuffer so every decoded format swscale supports can render.
    */
   src_fmt = (enum AVPixelFormat)frame->format;
   if (video->fb.fd < 0) {
      video->fb.fd = -1;
      if (unifrog_fb_open(&video->fb, UNIFROG_FB_OPEN_DEFAULT) != 0) {
         printf("unifrog media swvideo fb open failed frame=%lu %dx%d fmt=%s\n",
            (unsigned long)video->frames, frame->width, frame->height,
            media_pixel_format_name(src_fmt));
         return -1;
      }
   }
   if (!video->fb.pixels || video->fb.bpp != 16 ||
       !video->fb.width || !video->fb.height)
      return -1;
   media_swvideo_fit_rect((unsigned)frame->width, (unsigned)frame->height,
      video->fb.width, video->fb.height, &dst_x, &dst_y, &dst_w, &dst_h);
   if (!video->fb_sws || video->fb_src_w != frame->width ||
       video->fb_src_h != frame->height || video->fb_src_fmt != src_fmt ||
       video->fb_dst_w != (int)dst_w || video->fb_dst_h != (int)dst_h) {
      sws_freeContext(video->fb_sws);
      video->fb_sws = sws_getContext(frame->width, frame->height, src_fmt,
         (int)dst_w, (int)dst_h, AV_PIX_FMT_RGB565LE, SWS_FAST_BILINEAR,
         NULL, NULL, NULL);
      if (!video->fb_sws) {
         printf("unifrog media swvideo fb sws failed frame=%lu src=%dx%d dst=%ux%u fmt=%s\n",
            (unsigned long)video->frames, frame->width, frame->height,
            dst_w, dst_h, media_pixel_format_name(src_fmt));
         return -1;
      }
      video->fb_src_w = frame->width;
      video->fb_src_h = frame->height;
      video->fb_src_fmt = src_fmt;
      video->fb_dst_w = (int)dst_w;
      video->fb_dst_h = (int)dst_h;
   }
   memset(video->fb.pixels, 0, video->fb.visible_bytes);
   dst_data[0] = (uint8_t *)video->fb.pixels +
      (size_t)dst_y * video->fb.pitch_bytes + (size_t)dst_x * 2u;
   dst_data[1] = NULL;
   dst_data[2] = NULL;
   dst_data[3] = NULL;
   dst_linesize[0] = (int)video->fb.pitch_bytes;
   dst_linesize[1] = 0;
   dst_linesize[2] = 0;
   dst_linesize[3] = 0;
   scale_ret = sws_scale(video->fb_sws,
      (const uint8_t * const *)frame->data, frame->linesize, 0,
      frame->height, dst_data, dst_linesize);
   if (scale_ret != (int)dst_h) {
      printf("unifrog media swvideo fb scale failed ret=%d want=%u frame=%lu src=%dx%d dst=%ux%u fmt=%s\n",
         scale_ret, dst_h, (unsigned long)video->frames, frame->width,
         frame->height, dst_w, dst_h, media_pixel_format_name(src_fmt));
      return -1;
   }
   unifrog_fb_flush(&video->fb);
   if (video->frames <= 3u || (video->frames % 120u) == 0)
      printf("unifrog media swvideo fb presented frame=%lu src=%dx%d dst=%u,%u %ux%u fmt=%s fb=%ux%u pitch=%u\n",
         (unsigned long)(video->frames + 1u), frame->width, frame->height,
         dst_x, dst_y, dst_w, dst_h, media_pixel_format_name(src_fmt),
         video->fb.width, video->fb.height, video->fb.pitch_bytes);
   video->frames++;
   return 0;
}

void media_controls_reset_for_playback(const char *tag,
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

void media_poll_controls(struct media_controls *controls)
{
   media_poll_controls_internal(controls, 1);
}

int media_exit_down(void)
{
   struct media_controls controls;

   media_poll_controls_internal(&controls, 0);
   return controls.exit_down;
}

int media_controls_pending_action(void)
{
   return media_pending_seek_delta_ms || media_pending_overlay_toggle;
}

int media_wav_read_pcm_sample(FILE *file, unsigned bits,
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

int16_t media_wav_clip_sample(int32_t sample)
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

static void media_ffmpeg_converter_reset(
   struct media_ffmpeg_audio_converter *converter)
{
   if (!converter)
      return;
   swr_free(&converter->swr);
   memset(converter, 0, sizeof(*converter));
}

static void media_ffmpeg_register_once(void)
{
   static int registered;

   if (registered)
      return;
   registered = 1;
   printf("unifrog media ffmpeg registered\n");
   media_log_ffmpeg_caps_once();
}

static int media_ffmpeg_frame_layout(const AVCodecContext *codec_ctx,
   const AVFrame *frame, AVChannelLayout *layout)
{
   const AVChannelLayout *source = NULL;

   if (!layout)
      return -1;
   memset(layout, 0, sizeof(*layout));
   if (frame && frame->ch_layout.nb_channels > 0)
      source = &frame->ch_layout;
   else if (codec_ctx && codec_ctx->ch_layout.nb_channels > 0)
      source = &codec_ctx->ch_layout;
   return source ? av_channel_layout_copy(layout, source) : -1;
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
   uint64_t layout_mask;
   uint64_t dst_layout_mask;
   AVChannelLayout layout = { 0 };
   AVChannelLayout dst_layout = { 0 };
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
   if (media_ffmpeg_frame_layout(codec_ctx, frame, &layout) != 0)
      return -1;
   channels = layout.nb_channels;
   layout_mask = media_channel_mask(&layout);
   av_channel_layout_default(&dst_layout, dst_channels);
   dst_layout_mask = media_channel_mask(&dst_layout);
   if (channels <= 0 || src_rate <= 0 || bytes <= 0) {
      av_channel_layout_uninit(&layout);
      av_channel_layout_uninit(&dst_layout);
      return -1;
   }
   if (converter->swr && converter->src_rate == src_rate &&
       converter->src_channels == channels &&
       converter->src_layout == layout_mask && converter->src_fmt == src_fmt &&
       converter->dst_rate == dst_rate &&
       converter->dst_channels == dst_channels &&
       converter->dst_layout == dst_layout_mask) {
      av_channel_layout_uninit(&layout);
      av_channel_layout_uninit(&dst_layout);
      return 0;
   }

   swr_free(&converter->swr);
   swr = NULL;
   ret = swr_alloc_set_opts2(&swr, &dst_layout, AV_SAMPLE_FMT_S16,
      dst_rate, &layout, src_fmt, src_rate, 0, NULL);
   av_channel_layout_uninit(&layout);
   av_channel_layout_uninit(&dst_layout);
   if (ret < 0 || !swr) {
      printf("unifrog media ffmpeg swr_alloc failed src_rate=%d ch=%d layout=0x%lx fmt=%s dst_rate=%d dst_ch=%d dst_layout=0x%lx path=%s\n",
         src_rate, channels, (unsigned long)layout_mask,
         media_sample_format_name(src_fmt), dst_rate, dst_channels,
         (unsigned long)dst_layout_mask, path ? path : "");
      swr_free(&swr);
      return -1;
   }
   ret = swr_init(swr);
   if (ret < 0) {
      printf("unifrog media ffmpeg swr_init failed ret=%d src_rate=%d ch=%d layout=0x%lx fmt=%s dst_rate=%d dst_ch=%d dst_layout=0x%lx path=%s\n",
         ret, src_rate, channels, (unsigned long)layout_mask,
         media_sample_format_name(src_fmt), dst_rate, dst_channels,
         (unsigned long)dst_layout_mask, path ? path : "");
      swr_free(&swr);
      return -1;
   }

   converter->swr = swr;
   converter->src_rate = src_rate;
   converter->src_channels = channels;
   converter->src_layout = layout_mask;
   converter->src_fmt = src_fmt;
   converter->dst_rate = dst_rate;
   converter->dst_channels = dst_channels;
   converter->dst_layout = dst_layout_mask;
   printf("unifrog media ffmpeg swr config src_rate=%d ch=%d layout=0x%lx fmt=%s dst_rate=%d dst_ch=%d dst_layout=0x%lx path=%s\n",
      src_rate, channels, (unsigned long)layout_mask,
      media_sample_format_name(src_fmt), dst_rate, dst_channels,
      (unsigned long)dst_layout_mask, path ? path : "");
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

int media_sw_audio_reset_output(struct unifrog_audio *audio,
   unsigned rate, unsigned channels, int backend, int defer_stereo_output,
   const char *tag, const char *path)
{
   int ret;

   if (!audio || rate == 0 || channels == 0)
      return -1;
   printf("unifrog media sw_audio reset begin tag=%s fd=%d rate=%u ch=%u backend=%d path=%s\n",
      tag ? tag : "", audio->fd, rate, channels, backend,
      path ? path : "");
   unifrog_audio_close(audio);
   ret = unifrog_audio_open_backend(audio, rate, channels, 512, 8, backend);
   if (ret != 0) {
      printf("unifrog media sw_audio reset open_failed tag=%s ret=%d rate=%u ch=%u backend=%d path=%s\n",
         tag ? tag : "", ret, rate, channels, backend, path ? path : "");
      return -1;
   }
   (void)unifrog_audio_set_volume(audio, media_audio_runtime_volume());
   (void)unifrog_audio_set_mute(audio, 1);
   (void)unifrog_audio_start(audio);
   if (!defer_stereo_output || !unifrog_audio_prefers_stereo_output())
      (void)unifrog_audio_set_output_enabled(audio, 1);
   unifrog_audio_debug_dump(audio, tag ? tag : "sw_audio_reset");
   printf("unifrog media sw_audio reset done tag=%s fd=%d backend=%d path=%s\n",
      tag ? tag : "", audio->fd, audio->backend, path ? path : "");
   return 0;
}

static int media_ffmpeg_open_audio(const char *path, unsigned output_channels,
   AVFormatContext **fmt_out, AVCodecContext **codec_out, int *stream_out,
   const AVCodec **decoder_out)
{
   AVFormatContext *fmt = NULL;
   AVCodecContext *codec_ctx = NULL;
   const AVCodec *decoder = NULL;
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
      media_codecpar_channels(fmt->streams[stream]->codecpar),
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
   if (strncmp(reason, "auddec", 6) != 0 && strncmp(reason, "ffmpeg", 6) != 0)
      return UNIFROG_AUDIO_BACKEND_AUTO;
   return UNIFROG_AUDIO_BACKEND_SND;
}

static int media_play_ffmpeg_audio_backend(const char *path, int backend,
   const char *reason)
{
   struct unifrog_audio audio;
   AVFormatContext *fmt = NULL;
   AVCodecContext *codec_ctx = NULL;
   const AVCodec *decoder = NULL;
   AVPacket *packet = NULL;
   AVFrame *frame = NULL;
   struct media_ffmpeg_audio_converter converter;
   int16_t *pcm = NULL;
   int stream = -1;
   uint32_t played = 0;
   uint32_t loop_polls = 0;
   int64_t playback_base_ms = 0;
   int64_t duration_ms = -1;
   unsigned output_channels = media_audio_output_channels();
   int effective_backend = backend;
   int saw_frame = 0;
   int ret = -1;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   memset(&converter, 0, sizeof(converter));
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
   if (backend == UNIFROG_AUDIO_BACKEND_AUTO &&
       unifrog_audio_prefers_stereo_output())
      effective_backend = media_gb300_auddec_fallback_backend("ffmpeg_audio");
   if (unifrog_audio_open_backend(&audio, (unsigned)codec_ctx->sample_rate,
       output_channels, 512, 8, effective_backend) != 0) {
      printf("unifrog media ffmpeg audio_open failed rate=%d ch=%u backend=%d reason=%s path=%s\n",
         codec_ctx->sample_rate, output_channels, effective_backend,
         reason ? reason : "?", path);
      goto out;
   }
   (void)unifrog_audio_set_volume(&audio, media_audio_runtime_volume());
   (void)unifrog_audio_set_mute(&audio, 1);
   (void)unifrog_audio_start(&audio);
   if (!unifrog_audio_prefers_stereo_output())
      (void)unifrog_audio_set_output_enabled(&audio, 1);
   unifrog_audio_debug_dump(&audio, "ffmpeg_after_start");
   printf("unifrog media ffmpeg audio start codec=%s stream=%d rate=%d src_ch=%d out_ch=%u backend=%d reason=%s fmt=%s duration=%lld overlay=1 overlay_hide=A path=%s\n",
      decoder && decoder->name ? decoder->name : "?",
      stream, codec_ctx->sample_rate, media_codec_channels(codec_ctx),
      output_channels,
      effective_backend, reason ? reason : "?",
      media_sample_format_name(codec_ctx->sample_fmt), (long long)duration_ms,
      path);
   media_controls_reset_for_playback("ffmpeg_audio", path);
   media_audio_screen_draw("ffmpeg_audio_start", path, 0, duration_ms, 1);

   for (;;) {
      struct media_controls controls;
      int read_ret;

      media_poll_controls(&controls);
      if (controls.exit_down)
         break;
      if (controls.overlay_toggle) {
         media_audio_screen_draw("ffmpeg_audio_toggle", path,
            playback_base_ms + (int64_t)media_sw_audio_clock_ms(&audio,
               played, audio.rate, 0), duration_ms, 1);
      }
      if (controls.seek_delta_ms && duration_ms > 0) {
         int64_t cur_ms = playback_base_ms +
            (int64_t)media_sw_audio_clock_ms(&audio, played, audio.rate, 0);
         int64_t target_ms = media_seek_target_ms(cur_ms,
            controls.seek_delta_ms, duration_ms);

         printf("unifrog media seek ffmpeg_audio request cur=%lld base=%lld frames=%lu dur=%lld delta=%d target=%lld path=%s\n",
            (long long)cur_ms, (long long)playback_base_ms,
            (unsigned long)played, (long long)duration_ms,
            controls.seek_delta_ms, (long long)target_ms,
            path ? path : "");
         if (media_seek_format_ms(fmt, target_ms, "ffmpeg_audio", path) == 0) {
            avcodec_flush_buffers(codec_ctx);
            media_ffmpeg_converter_reset(&converter);
            if (media_sw_audio_reset_output(&audio,
                (unsigned)codec_ctx->sample_rate, output_channels,
                effective_backend, 1, "ffmpeg_audio_seek", path) != 0)
               goto out;
            played = 0;
            loop_polls = 0;
            playback_base_ms = target_ms;
            saw_frame = 0;
            media_audio_screen_draw("ffmpeg_audio_seek", path, target_ms,
               duration_ms, 1);
         }
         continue;
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
                  frame->sample_rate, media_frame_channels(frame),
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
                  media_frame_channels(frame), frame->nb_samples, path);
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
         media_audio_screen_draw("ffmpeg_audio", path,
            playback_base_ms + (int64_t)media_sw_audio_clock_ms(&audio,
               played, audio.rate, 0), duration_ms, 0);
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
   printf("unifrog media ffmpeg audio end ret=%d base=%lld frames=%lu path=%s\n",
      ret, (long long)playback_base_ms, (unsigned long)played,
      path ? path : "");
   unifrog_exception_activity_clear();
   if (audio.fd >= 0)
      unifrog_audio_close(&audio);
   media_ffmpeg_converter_reset(&converter);
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
   int stream = -1;
   int ret = -1;
   unsigned finish_timeout_ms = 600000u;
   uint32_t loop_polls = 0;
   uint32_t last_progress_poll_ms = 0;
   uint32_t last_stall_poll_ms = 0;
   uint32_t start_ms = unifrog_perf_time_ms();
   uint32_t stall_epoch_ms = start_ms;
   int64_t duration_ms = -1;
   int64_t seek_audio_pending_ms = MEDIA_TIME_UNSET;
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
   media_audio_screen_draw("auddec_audio_start", path, 0, duration_ms, 1);
   for (;;) {
      struct media_controls controls;
      int read_ret;

      media_poll_controls(&controls);
      if (controls.exit_down)
         break;
      if (controls.overlay_toggle) {
         int64_t cur_time = -1;

         (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &cur_time);
         cur_time = media_seek_current_ms(-1, cur_time, NULL, &pacer,
            seek_audio_pending_ms);
         media_audio_screen_draw("auddec_audio_toggle", path,
            cur_time >= 0 ? cur_time : pacer.next_ms, duration_ms, 1);
      }
      if (controls.seek_delta_ms && duration_ms > 0) {
         int64_t cur_time = -1;
         int64_t target_ms;

         (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &cur_time);
         cur_time = media_seek_current_ms(-1, cur_time, NULL, &pacer,
            seek_audio_pending_ms);
         target_ms = media_seek_target_ms(cur_time, controls.seek_delta_ms,
            duration_ms);
         printf("unifrog media seek audio request cur=%lld pending=%lld dur=%lld delta=%d target=%lld path=%s\n",
            (long long)cur_time, (long long)seek_audio_pending_ms,
            (long long)duration_ms, controls.seek_delta_ms,
            (long long)target_ms, path ? path : "");
         media_flush_auddec_for_seek(&auddec, "audio", path);
         if (media_seek_format_ms(fmt, target_ms, "audio", path) == 0) {
            media_audio_pacer_seek_reset(&pacer, target_ms);
            seek_audio_pending_ms = target_ms;
            stall_epoch_ms = unifrog_perf_time_ms();
            last_stall_poll_ms = 0;
            last_status_ok = 0;
            last_time_ok = 0;
            last_decoded = 0;
            last_header_seen = 0;
            last_audio_time = -1;
            eof_seen = 0;
            media_audio_screen_draw("auddec_audio_seek", path, target_ms,
               duration_ms, 1);
         }
         continue;
      }

      read_ret = av_read_frame(fmt, packet);
      if (read_ret < 0) {
         eof_seen = 1;
         break;
      }
      if (packet->stream_index == stream) {
         if (seek_audio_pending_ms != MEDIA_TIME_UNSET) {
            int32_t packet_ms = media_packet_pts_ms(packet,
               fmt->streams[stream]->time_base);
            int32_t packet_dur_ms = media_packet_duration_ms(packet,
               fmt->streams[stream]->time_base);
            int64_t packet_end_ms = (int64_t)packet_ms +
               (packet_dur_ms > 0 ? (int64_t)packet_dur_ms : 0);

            if (packet_ms < 0 || packet_end_ms >= seek_audio_pending_ms)
               seek_audio_pending_ms = MEDIA_TIME_UNSET;
         }
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
          seek_audio_pending_ms == MEDIA_TIME_UNSET &&
          !decode_stalled) {
         uint32_t now_ms = unifrog_perf_time_ms();
         uint32_t elapsed_ms = now_ms - stall_epoch_ms;

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
         media_audio_screen_draw("auddec_audio", path,
            cur_time >= 0 ? cur_time : pacer.next_ms, duration_ms, 0);
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
         media_audio_screen_draw("auddec_audio", path,
            cur_time >= 0 ? cur_time : pacer.next_ms, duration_ms, 0);
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

static int media_video_seek_requires_keyframe(enum AVCodecID codec_id)
{
   switch (codec_id) {
   case AV_CODEC_ID_MPEG1VIDEO:
   case AV_CODEC_ID_MPEG2VIDEO:
   case AV_CODEC_ID_H263:
   case AV_CODEC_ID_H264:
   case AV_CODEC_ID_MPEG4:
   case AV_CODEC_ID_VC1:
   case AV_CODEC_ID_WMV3:
   case AV_CODEC_ID_VP8:
      return 1;
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

static int media_seek_drop_audio_packet_until(const AVPacket *packet,
   AVStream *stream, int64_t *until_ms, uint32_t *dropped_packets,
   const char *label, const char *path)
{
   int32_t packet_ms;
   int32_t packet_dur_ms;
   int64_t packet_end_ms;
   int hold;

   if (!packet || !stream || !until_ms || *until_ms == MEDIA_TIME_UNSET)
      return 0;
   hold = *until_ms == MEDIA_TIME_HOLD;
   packet_ms = media_packet_pts_ms(packet, stream->time_base);
   packet_dur_ms = media_packet_duration_ms(packet, stream->time_base);
   if (packet_ms < 0) {
      if (hold) {
         uint32_t dropped = dropped_packets ? ++(*dropped_packets) : 0u;

         if (dropped <= 4u) {
            printf("unifrog media seek %s drop mode=hold packet_ms=%ld dur=%ld until=-1 dropped=%lu size=%d path=%s\n",
               label ? label : "audio", (long)packet_ms,
               (long)packet_dur_ms, (unsigned long)dropped, packet->size,
               path ? path : "");
         }
         return 1;
      }
      *until_ms = MEDIA_TIME_UNSET;
      if (dropped_packets)
         *dropped_packets = 0;
      return 0;
   }
   packet_end_ms = (int64_t)packet_ms +
      (packet_dur_ms > 0 ? (int64_t)packet_dur_ms : 0);
   if (hold || packet_end_ms < *until_ms) {
      uint32_t dropped = dropped_packets ? ++(*dropped_packets) : 0u;

      if (dropped <= 4u) {
         printf("unifrog media seek %s drop mode=%s packet_ms=%ld dur=%ld until=%lld dropped=%lu size=%d path=%s\n",
            label ? label : "audio", hold ? "hold" : "skip",
            (long)packet_ms, (long)packet_dur_ms,
            hold ? -1ll : (long long)*until_ms,
            (unsigned long)dropped, packet->size, path ? path : "");
      }
      return 1;
   }
   if (dropped_packets && *dropped_packets) {
      printf("unifrog media seek %s catchup_done packet_ms=%ld until=%lld dropped=%lu path=%s\n",
         label ? label : "audio", (long)packet_ms, (long long)*until_ms,
         (unsigned long)*dropped_packets, path ? path : "");
   }
   *until_ms = MEDIA_TIME_UNSET;
   if (dropped_packets)
      *dropped_packets = 0;
   return 0;
}

void media_audio_pacer_wait_ms(struct media_audio_pacer *pacer,
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
      if (media_controls_pending_action())
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

void media_audio_pacer_seek_reset(struct media_audio_pacer *pacer,
   int64_t target_ms)
{
   media_audio_pacer_seek_reset_warmup(pacer, target_ms,
      MEDIA_SEEK_WARMUP_PACKETS);
}

static void media_audio_pacer_seek_reset_warmup(
   struct media_audio_pacer *pacer, int64_t target_ms,
   unsigned warmup_packets)
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
   /* Align wall time to the seek target. Some decoders report the pre-seek
    * clock briefly, so floor it and let a bounded packet burst through. */
   pacer->next_ms = 0;
   pacer->wall_start_ms = now - (uint32_t)target_ms;
   pacer->seek_warmup_packets = warmup_packets;
   pacer->seek_warmup_total = warmup_packets;
   pacer->seek_clock_floor_active = 1;
   pacer->seek_clock_floor_ms = target_ms;
   pacer->seek_clock_floor_wall_ms = now;
}

static int64_t media_audio_pacer_effective_clock_ms(
   struct media_audio_pacer *pacer, int64_t decoder_ms, uint32_t now_ms)
{
   int64_t effective_ms = decoder_ms;
   int64_t elapsed_ms;
   int64_t floor_ms;

   if (!pacer || !pacer->seek_clock_floor_active)
      return effective_ms;
   if (decoder_ms >= pacer->seek_clock_floor_ms) {
      pacer->seek_clock_floor_active = 0;
      return decoder_ms;
   }
   elapsed_ms = (int64_t)(now_ms - pacer->seek_clock_floor_wall_ms);
   floor_ms = pacer->seek_clock_floor_ms + elapsed_ms;
   if (floor_ms > INT32_MAX)
      floor_ms = INT32_MAX;
   if (effective_ms < floor_ms)
      effective_ms = floor_ms;
   return effective_ms;
}

static void media_audio_pacer_wait(struct media_audio_pacer *pacer,
   const AVPacket *packet, AVRational time_base)
{
   media_audio_pacer_wait_lead(pacer, packet, time_base,
      MEDIA_AUDIO_FEED_LEAD_MS);
}

int media_wait_hardware_ahead(const char *kind, int fd, int video,
   struct media_audio_pacer *pacer, unsigned max_ahead_ms,
   const char *path)
{
   uint32_t start_ms;
   uint32_t last_log_ms = 0;
   int logged = 0;

   if (fd < 0 || !pacer || !pacer->started || max_ahead_ms == 0)
      return MEDIA_HW_AHEAD_OK;
   if (pacer->seek_warmup_packets > 0) {
      if (pacer->seek_warmup_packets == pacer->seek_warmup_total) {
         printf("unifrog media hw_ahead seek_warmup kind=%s packets=%u feed=%lld max=%u path=%s\n",
            kind ? kind : "?", pacer->seek_warmup_total,
            (long long)pacer->next_ms, max_ahead_ms, path ? path : "");
      }
      pacer->seek_warmup_packets--;
      return MEDIA_HW_AHEAD_OK;
   }
   start_ms = unifrog_perf_time_ms();
   for (;;) {
      int64_t cur_time = -1;
      int64_t effective_time;
      int64_t ahead_ms;
      uint32_t now_ms;
      uint32_t waited_ms;

      errno = 0;
      if (ioctl(fd, video ? VIDDEC_GET_CUR_TIME : AUDDEC_GET_CUR_TIME,
            &cur_time) != 0 || cur_time < 0)
         return MEDIA_HW_AHEAD_OK;
      now_ms = unifrog_perf_time_ms();
      effective_time = media_audio_pacer_effective_clock_ms(pacer, cur_time,
         now_ms);
      ahead_ms = pacer->next_ms - effective_time;
      if (ahead_ms <= (int64_t)max_ahead_ms)
         break;
      if (media_exit_down())
         break;
      if (media_controls_pending_action()) {
         waited_ms = unifrog_perf_time_ms() - start_ms;
         printf("unifrog media hw_ahead interrupt kind=%s reason=controls ahead=%lld max=%u clock=%lld eff_clock=%lld feed=%lld waited=%lu path=%s\n",
            kind ? kind : "?", (long long)ahead_ms, max_ahead_ms,
            (long long)cur_time, (long long)effective_time,
            (long long)pacer->next_ms, (unsigned long)waited_ms,
            path ? path : "");
         return MEDIA_HW_AHEAD_INTERRUPTED;
      }
      now_ms = unifrog_perf_time_ms();
      waited_ms = now_ms - start_ms;
      if (waited_ms >= MEDIA_HW_AHEAD_LOG_MIN_MS &&
          (!logged || now_ms - last_log_ms >= MEDIA_HW_AHEAD_LOG_MS)) {
         printf("unifrog media hw_ahead wait kind=%s ahead=%lld max=%u clock=%lld eff_clock=%lld feed=%lld waited=%lu path=%s\n",
            kind ? kind : "?", (long long)ahead_ms, max_ahead_ms,
            (long long)cur_time, (long long)effective_time,
            (long long)pacer->next_ms,
            (unsigned long)waited_ms, path ? path : "");
         logged = 1;
         last_log_ms = now_ms;
      }
      if (MEDIA_HW_AHEAD_MAX_WAIT_MS &&
          waited_ms >= MEDIA_HW_AHEAD_MAX_WAIT_MS) {
         int64_t cap_ms = effective_time + (int64_t)max_ahead_ms;
         int64_t feed_ms = pacer->next_ms;

         if (effective_time >= 0 && pacer->next_ms > cap_ms)
            pacer->next_ms = cap_ms;
         printf("unifrog media hw_ahead timeout kind=%s ahead=%lld max=%u clock=%lld eff_clock=%lld feed=%lld capped_feed=%lld waited=%lu limit=%u path=%s\n",
            kind ? kind : "?", (long long)ahead_ms, max_ahead_ms,
            (long long)cur_time, (long long)effective_time,
            (long long)feed_ms,
            (long long)pacer->next_ms,
            (unsigned long)waited_ms, MEDIA_HW_AHEAD_MAX_WAIT_MS,
            path ? path : "");
         return MEDIA_HW_AHEAD_TIMEOUT;
      }
      usleep(MEDIA_HW_AHEAD_POLL_US);
   }
   if (logged) {
      int64_t cur_time = -1;
      int64_t effective_time = -1;
      uint32_t now_ms = unifrog_perf_time_ms();
      uint32_t waited_ms = now_ms - start_ms;

      (void)ioctl(fd, video ? VIDDEC_GET_CUR_TIME : AUDDEC_GET_CUR_TIME,
         &cur_time);
      if (cur_time >= 0)
         effective_time = media_audio_pacer_effective_clock_ms(pacer,
            cur_time, now_ms);
      printf("unifrog media hw_ahead done kind=%s clock=%lld eff_clock=%lld feed=%lld ahead=%lld waited=%lu path=%s\n",
         kind ? kind : "?", (long long)cur_time,
         (long long)effective_time, (long long)pacer->next_ms,
         effective_time >= 0 ?
         (long long)(pacer->next_ms - effective_time) : -1ll,
         (unsigned long)waited_ms, path ? path : "");
   }
   return MEDIA_HW_AHEAD_OK;
}

static int64_t media_format_duration_ms(AVFormatContext *fmt)
{
   if (!fmt || fmt->duration <= 0)
      return -1;
   return fmt->duration / (AV_TIME_BASE / 1000);
}

int64_t media_seek_target_ms(int64_t current_ms, int delta_ms,
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

int64_t media_seek_current_ms(int64_t video_time, int64_t audio_time,
   struct media_audio_pacer *video_pacer,
   struct media_audio_pacer *audio_pacer, int64_t pending_target_ms)
{
   uint32_t now_ms;

   if (pending_target_ms != MEDIA_TIME_UNSET)
      return pending_target_ms;
   now_ms = unifrog_perf_time_ms();
   if (audio_pacer && audio_pacer->started)
      audio_time = media_audio_pacer_effective_clock_ms(audio_pacer,
         audio_time, now_ms);
   if (video_pacer && video_pacer->started)
      video_time = media_audio_pacer_effective_clock_ms(video_pacer,
         video_time, now_ms);
   if (audio_time > 0)
      return audio_time;
   if (video_time > 0)
      return video_time;
   if (audio_pacer && audio_pacer->started && audio_pacer->next_ms > 0)
      return audio_pacer->next_ms;
   if (video_pacer && video_pacer->started && video_pacer->next_ms > 0)
      return video_pacer->next_ms;
   if (audio_time >= 0)
      return audio_time;
   if (video_time >= 0)
      return video_time;
   return -1;
}

static int64_t media_seek_active_target_ms(int64_t video_target_ms,
   int64_t audio_target_ms)
{
   if (video_target_ms != MEDIA_TIME_UNSET)
      return video_target_ms;
   if (audio_target_ms == MEDIA_TIME_HOLD)
      return MEDIA_TIME_UNSET;
   if (audio_target_ms != MEDIA_TIME_UNSET)
      return audio_target_ms;
   return MEDIA_TIME_UNSET;
}

static int media_seek_format_ms_flags(AVFormatContext *fmt, int64_t target_ms,
   int forward, const char *tag, const char *path)
{
   int64_t target_us;
   int64_t min_us;
   int64_t max_us;
   int flags;
   int ret;
   int fallback_ret = 0;

   if (!fmt || target_ms < 0)
      return -1;
   target_us = target_ms * (AV_TIME_BASE / 1000);
   flags = forward ? 0 : AVSEEK_FLAG_BACKWARD;
   min_us = forward ? target_us : INT64_MIN;
   max_us = INT64_MAX;
   errno = 0;
   ret = avformat_seek_file(fmt, -1, min_us, target_us, max_us, flags);
   if (ret < 0) {
      fallback_ret = av_seek_frame(fmt, -1, target_us, flags);
      if (fallback_ret >= 0)
         ret = fallback_ret;
   }
   printf("unifrog media seek demux tag=%s target=%lld direction=%s ret=%d fallback=%d errno=%d path=%s\n",
      tag ? tag : "", (long long)target_ms, forward ? "forward" : "backward",
      ret, fallback_ret, errno, path ? path : "");
   return ret;
}

static int media_seek_format_ms(AVFormatContext *fmt, int64_t target_ms,
   const char *tag, const char *path)
{
   return media_seek_format_ms_flags(fmt, target_ms, 0, tag, path);
}

static int media_seek_format_next_ms(AVFormatContext *fmt, int64_t target_ms,
   const char *tag, const char *path)
{
   return media_seek_format_ms_flags(fmt, target_ms, 1, tag, path);
}

static int media_seek_format_key_ms(AVFormatContext *fmt, int64_t target_ms,
   int prefer_forward, const char *tag, const char *path)
{
   int ret;

   ret = media_seek_format_ms_flags(fmt, target_ms, prefer_forward, tag, path);
   if (ret == 0)
      return 0;
   printf("unifrog media seek demux_key_fallback tag=%s target=%lld first_direction=%s ret=%d path=%s\n",
      tag ? tag : "", (long long)target_ms,
      prefer_forward ? "forward" : "backward", ret, path ? path : "");
   return media_seek_format_ms_flags(fmt, target_ms, !prefer_forward,
      tag, path);
}

static int64_t media_seek_video_preroll_limit_ms(
   const AVCodecParameters *codecpar)
{
   int64_t limit_ms = MEDIA_SEEK_PREROLL_DECODE_MS;
   int64_t pixels = 0;

   if (codecpar && codecpar->width > 0 && codecpar->height > 0)
      pixels = (int64_t)codecpar->width * (int64_t)codecpar->height;
   if (pixels >= (int64_t)1280 * 720 &&
       MEDIA_SEEK_PREROLL_HD_DECODE_MS >= 0 &&
       limit_ms > MEDIA_SEEK_PREROLL_HD_DECODE_MS)
      limit_ms = MEDIA_SEEK_PREROLL_HD_DECODE_MS;
   return limit_ms;
}

static const char *media_seek_video_preroll_block_reason(int64_t gap_ms,
   int packet_size, int64_t limit_ms)
{
   int gap_blocked = gap_ms < 0 ||
      (limit_ms >= 0 && gap_ms > limit_ms);
   int size_blocked = MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES > 0 &&
      packet_size > MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES;

   if (gap_blocked && size_blocked)
      return "gap_size";
   if (gap_blocked)
      return "gap";
   if (size_blocked)
      return "size";
   return "";
}

static void media_video_note_progress(int64_t video_time,
   const struct vdec_decore_status *status, int64_t *progress_time_ms,
   unsigned long *progress_decoded, unsigned long *progress_displayed,
   uint32_t *progress_wall_ms)
{
   int progressed = 0;

   if (progress_time_ms && video_time >= 0 &&
       video_time > *progress_time_ms) {
      *progress_time_ms = video_time;
      progressed = 1;
   }
   if (status && progress_decoded &&
       (unsigned long)status->frames_decoded > *progress_decoded) {
      *progress_decoded = (unsigned long)status->frames_decoded;
      progressed = 1;
   }
   if (status && progress_displayed &&
       (unsigned long)status->frames_displayed > *progress_displayed) {
      *progress_displayed = (unsigned long)status->frames_displayed;
      progressed = 1;
   }
   if (progressed && progress_wall_ms)
      *progress_wall_ms = unifrog_perf_time_ms();
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

void media_flush_auddec_for_seek(struct media_auddec *auddec,
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
   int rate_milli = 1000;
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
   printf("unifrog media seek viddec_flush tag=%s fd=%d pause=%d flush=%d start=%d errno=%d rate_milli=%d path=%s\n",
      tag ? tag : "", video_fd, pause_ret, flush_ret, start_ret, errno,
      rate_milli, path ? path : "");
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
   int close_vidsink = 0;
   int exit_viddec_ret;
   int exit_viddec_errno;
   int exit_vidsink_ret;
   int exit_vidsink_errno;
   int exit_llav_ret;
   int exit_llav_errno;
   int init_llav_ret;
   int init_llav_errno;
   int init_vidsink_ret;
   int init_vidsink_errno;
   int init_viddec_ret;
   int init_viddec_errno;

   if (!MEDIA_RESET_VIDDEC_ON_FAIL)
      return;

   if (vidsink_fd >= 0) {
      close(vidsink_fd);
      vidsink_fd = -1;
      close_vidsink = 1;
   }
   errno = 0;
   exit_viddec_ret = module_exit("viddec");
   exit_viddec_errno = errno;
   errno = 0;
   exit_vidsink_ret = module_exit("vidsink");
   exit_vidsink_errno = errno;
   errno = 0;
   exit_llav_ret = module_exit("llav_vdec");
   exit_llav_errno = errno;
   msleep(60);
   errno = 0;
   init_llav_ret = module_init("llav_vdec");
   init_llav_errno = errno;
   errno = 0;
   init_vidsink_ret = module_init("vidsink");
   init_vidsink_errno = errno;
   errno = 0;
   init_viddec_ret = module_init("viddec");
   init_viddec_errno = errno;
   printf("unifrog media native video module_reset tag=%s close_vidsink=%d exit_viddec=%d exit_viddec_errno=%d exit_vidsink=%d exit_vidsink_errno=%d exit_llav=%d exit_llav_errno=%d init_llav=%d init_llav_errno=%d init_vidsink=%d init_vidsink_errno=%d init_viddec=%d init_viddec_errno=%d path=%s\n",
      tag ? tag : "", close_vidsink, exit_viddec_ret, exit_viddec_errno,
      exit_vidsink_ret, exit_vidsink_errno, exit_llav_ret, exit_llav_errno,
      init_llav_ret, init_llav_errno, init_vidsink_ret, init_vidsink_errno,
      init_viddec_ret, init_viddec_errno, path ? path : "");
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
      int par_channels = media_codecpar_channels(par);

      if (idx >= 0)
         sr_index = (unsigned)idx;
      if (par_channels > 0 && par_channels < 8)
         channel_config = (unsigned)par_channels;
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
            channel_config = par_channels > 0 && par_channels < 8 ?
               (unsigned)par_channels : 2u;
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
   int video_scope = scope && strcmp(scope, "video") == 0;
   int auddec_scope = scope && strcmp(scope, "auddec") == 0;

   while (written < size) {
      ssize_t ret = write(fd, p + written, size - written);

      if (ret < 0) {
         int saved_errno = errno;
         int retry_write = saved_errno == EAGAIN ||
            saved_errno == EWOULDBLOCK || saved_errno == EBUSY ||
            (saved_errno == EPERM && (video_scope || auddec_scope));

         if (saved_errno == EINTR)
            continue;
         if (retry_write) {
            uint32_t elapsed = unifrog_perf_time_ms() - start_ms;
            unsigned limit_ms = timeout_ms;

            if (saved_errno == EPERM && video_scope &&
                MEDIA_VIDEO_WRITE_EPERM_RECOVER_MS < limit_ms)
               limit_ms = MEDIA_VIDEO_WRITE_EPERM_RECOVER_MS;
            if (saved_errno == EPERM && auddec_scope &&
                MEDIA_AUDDEC_WRITE_EPERM_RECOVER_MS < limit_ms)
               limit_ms = MEDIA_AUDDEC_WRITE_EPERM_RECOVER_MS;
            if (elapsed >= limit_ms) {
               printf("unifrog media write timeout scope=%s fd=%d size=%lu written=%lu errno=%d retries=%u waited=%lu limit=%u\n",
                  scope ? scope : "?",
                  fd, (unsigned long)size, (unsigned long)written,
                  saved_errno, retries, (unsigned long)elapsed, limit_ms);
               errno = saved_errno;
               return -1;
            }
            if (retries == 0 || (retries % 200u) == 0)
               printf("unifrog media write retry scope=%s fd=%d size=%lu written=%lu errno=%d retries=%u waited=%lu limit=%u\n",
                  scope ? scope : "?",
                  fd, (unsigned long)size, (unsigned long)written,
                  saved_errno, retries, (unsigned long)elapsed, limit_ms);
            retries++;
            usleep(VIDEO_WRITE_SPACE_POLL_US);
            continue;
         }
         printf("unifrog media write fatal scope=%s fd=%d size=%lu written=%lu errno=%d retries=%u waited=%lu\n",
            scope ? scope : "?", fd, (unsigned long)size,
            (unsigned long)written, saved_errno, retries,
            (unsigned long)(unifrog_perf_time_ms() - start_ms));
         errno = saved_errno;
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

int media_auddec_send_packet(struct media_auddec *auddec,
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

void media_auddec_send_eos(struct media_auddec *auddec)
{
   AvPktHd header;

   if (!auddec || auddec->fd < 0)
      return;
   memset(&header, 0, sizeof(header));
   header.pts = -1;
   header.flag = AV_PACKET_EOS;
   (void)media_auddec_write_all(auddec, &header, sizeof(header));
}

void media_auddec_close(struct media_auddec *auddec)
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

void media_auddec_release_fd(int *fdp, const char *tag)
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

void media_auddec_finish(struct media_auddec *auddec,
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

int media_auddec_status_decode_stalled(
   const struct audio_decore_status *status)
{
   return status &&
      status->frames_decoded == 0 &&
      !status->first_header_got &&
      !status->first_header_parsed;
}

int media_auddec_clock_has_progress(int time_ok, int64_t cur_time)
{
   return time_ok && cur_time > 0;
}

int media_auddec_runtime_decode_stalled(
   const struct audio_decore_status *status, int time_ok, int64_t cur_time)
{
   return media_auddec_status_decode_stalled(status) &&
      !media_auddec_clock_has_progress(time_ok, cur_time);
}

int media_auddec_status_has_progress(
   const struct audio_decore_status *status)
{
   return status &&
      (status->frames_decoded > 0 ||
       status->first_header_got ||
       status->first_header_parsed);
}

void media_auddec_enable_output_on_progress(
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

void media_auddec_enable_output_on_clock_progress(
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

void media_auddec_log_packet_status(struct media_auddec *auddec,
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

int media_auddec_send_raw(struct media_auddec *auddec,
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

int media_auddec_open_raw(const char *label, uint32_t codec_id,
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

int media_auddec_open(AVFormatContext *fmt, int stream_index,
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
      (unsigned long)par->codec_tag, par->sample_rate,
      media_codecpar_channels(par),
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
   base_cfg.channels = media_codecpar_channels(par) > 0 ?
      (uint8_t)media_codecpar_channels(par) : 2u;
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
         cfg.channel_layout = media_channel_mask(&par->ch_layout);
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
      par->sample_rate, media_codecpar_channels(par), par->extradata_size);
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
   unsigned size = MEDIA_VIDEO_KSHM_SIZE;
   unsigned lowres_size = MEDIA_VIDEO_LOWRES_KSHM_SIZE;

   if (policy_out)
      *policy_out = "default";
   if (size == 0)
      size = 0x00800000u;
   if (!media_video_is_lowres_stream(par))
      return size;
   if (lowres_size == 0 || lowres_size > size)
      lowres_size = size;
   if (policy_out && lowres_size < size)
      *policy_out = "lowres";
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
      media_native_video_hardware_error = 1;
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

static int media_video_reopen_decoder(int *video_fd, AVFormatContext *fmt,
   int video_stream, int video_sync_mode, const char *reason,
   const char *path)
{
   int old_fd;
   int new_fd;

   if (!video_fd || *video_fd < 0)
      return -1;

   old_fd = *video_fd;
   printf("unifrog media native video reopen begin reason=%s old_fd=%d sync=%d path=%s\n",
      reason ? reason : "", old_fd, video_sync_mode, path ? path : "");
   media_video_release_decoder(old_fd, 1, 0, reason, path);
   close(old_fd);
   *video_fd = -1;
   close_display();

   new_fd = media_video_open_decoder(fmt, video_stream, video_sync_mode, path);
   if (new_fd < 0) {
      printf("unifrog media native video reopen retry reason=%s old_fd=%d sync=%d path=%s\n",
         reason ? reason : "", old_fd, video_sync_mode, path ? path : "");
      media_video_reset_modules(reason, path);
      new_fd = media_video_open_decoder(fmt, video_stream, video_sync_mode,
         path);
   }
   *video_fd = new_fd;
   printf("unifrog media native video reopen done reason=%s old_fd=%d new_fd=%d sync=%d path=%s\n",
      reason ? reason : "", old_fd, new_fd, video_sync_mode,
      path ? path : "");
   return new_fd >= 0 ? 0 : -1;
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

static int media_ffmpeg_swvideo_bsf_init(AVStream *stream,
   AVBSFContext **bsf_out)
{
   const AVBitStreamFilter *filter;
   AVBSFContext *bsf = NULL;
   AVCodecParameters *par;
   int nal_len;
   int ret;

   if (!stream || !bsf_out)
      return -1;
   *bsf_out = NULL;
   par = stream->codecpar;
   if (!par || par->codec_id != AV_CODEC_ID_H264)
      return 0;
   nal_len = media_h264_avcc_length_size(par->extradata,
      (size_t)(par->extradata_size > 0 ? par->extradata_size : 0));
   if (nal_len <= 0)
      return 0;
   filter = av_bsf_get_by_name("h264_mp4toannexb");
   if (!filter) {
      printf("unifrog media ffmpeg video bsf missing name=h264_mp4toannexb nal_len=%d\n",
         nal_len);
      return 0;
   }
   ret = av_bsf_alloc(filter, &bsf);
   if (ret < 0 || !bsf)
      return -1;
   ret = avcodec_parameters_copy(bsf->par_in, par);
   if (ret >= 0) {
      bsf->time_base_in = stream->time_base;
      ret = av_bsf_init(bsf);
   }
   if (ret < 0) {
      printf("unifrog media ffmpeg video bsf init failed name=h264_mp4toannexb ret=%d nal_len=%d\n",
         ret, nal_len);
      av_bsf_free(&bsf);
      return -1;
   }
   /*
    * The software H.264 decoder expects Annex B packets on the target FFmpeg
    * build. MP4/AVCC packets were previously logged as invalid until a final
    * decoder flush emitted one frame, which made the diagnostic look like it
    * passed while the user saw no video during playback.
    */
   printf("unifrog media ffmpeg video bsf enabled name=h264_mp4toannexb nal_len=%d extra=%d\n",
      nal_len, par->extradata_size);
   *bsf_out = bsf;
   return 0;
}

static int media_ffmpeg_swvideo_decode_present(AVCodecContext *video_ctx,
   AVPacket *packet, AVFrame *frame, struct media_sw_video *sw_video,
   uint32_t *video_frames, uint32_t *swvideo_failures,
   uint32_t *video_send_failures, uint32_t audio_frames, int *abort_video,
   const char *path)
{
   int send_ret;

   if (!video_ctx || !packet || !frame || !sw_video || !video_frames ||
       !swvideo_failures || !video_send_failures || !abort_video)
      return -1;
   send_ret = avcodec_send_packet(video_ctx, packet);
   if (send_ret < 0 && send_ret != AVERROR(EAGAIN)) {
      (*video_send_failures)++;
      /*
       * Keep feeding the audio leg even when the software decoder rejects a
       * bad startup packet. The final success check now requires a real frame
       * before flush, so this logging cannot mask a no-video diagnostic.
       */
      if (*video_send_failures <= MEDIA_SWVIDEO_SEND_FAIL_LOG_LIMIT ||
          (*video_send_failures % 64u) == 0)
         printf("unifrog media ffmpeg video send failed ret=%d failures=%lu audio=%lu video=%lu path=%s\n",
            send_ret, (unsigned long)*video_send_failures,
            (unsigned long)audio_frames, (unsigned long)*video_frames,
            path ? path : "");
      return 0;
   }
   *video_send_failures = 0;
   for (;;) {
      int recv_ret = avcodec_receive_frame(video_ctx, frame);

      if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
         break;
      if (recv_ret < 0) {
         printf("unifrog media ffmpeg video receive failed ret=%d path=%s\n",
            recv_ret, path ? path : "");
         break;
      }
      if (sw_video->frames == 0) {
         printf("unifrog media ffmpeg video first_frame %dx%d fmt=%s path=%s\n",
            frame->width, frame->height,
            media_pixel_format_name((enum AVPixelFormat)frame->format),
            path ? path : "");
      }
      if (media_swvideo_present_fb(sw_video, frame) == 0) {
         *video_frames = sw_video->frames;
         *swvideo_failures = 0;
      } else {
         (*swvideo_failures)++;
         if (*swvideo_failures >= MEDIA_SWVIDEO_DISPLAY_FAIL_LIMIT) {
            printf("unifrog media ffmpeg video swvideo abort failures=%lu frame=%lu %dx%d path=%s\n",
               (unsigned long)*swvideo_failures,
               (unsigned long)sw_video->frames, frame->width,
               frame->height, path ? path : "");
            *abort_video = 1;
            av_frame_unref(frame);
            break;
         }
      }
      av_frame_unref(frame);
   }
   return 0;
}

uint32_t media_audio_frames_to_ms(uint32_t frames, int rate)
{
   if (rate <= 0)
      return 0;
   return (uint32_t)(((uint64_t)frames * 1000ull) / (uint64_t)rate);
}

uint32_t media_audio_ms_to_frames(int64_t ms, unsigned rate)
{
   uint64_t frames;

   if (ms <= 0 || rate == 0)
      return 0;
   frames = ((uint64_t)ms * (uint64_t)rate) / 1000ull;
   if (frames > UINT32_MAX)
      return UINT32_MAX;
   return (uint32_t)frames;
}

uint32_t media_audio_bytes_to_ms(uint32_t bytes,
   unsigned bytes_per_frame, unsigned rate)
{
   if (bytes_per_frame == 0 || rate == 0)
      return 0;
   return media_audio_frames_to_ms(bytes / bytes_per_frame, (int)rate);
}

uint32_t media_sw_audio_clock_ms(struct unifrog_audio *audio,
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
   unsigned output_channels, const AVCodec **audio_decoder_out,
   AVCodecContext **audio_ctx_out, struct unifrog_audio *audio,
   struct media_ffmpeg_audio_converter *audio_converter, int16_t **pcm_out,
   int *audio_enabled_out, const char *reason, const char *path)
{
   const AVCodec *audio_decoder;
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
   const AVCodec *audio_decoder = NULL;
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
   uint32_t auddec_stall_epoch_ms = start_ms;
   int sw_video_base_ms = MEDIA_TIME_UNSET;
   unsigned long frames_decoded = 0;
   unsigned long frames_displayed = 0;
   int disable_audio = options && options->disable_audio;
   int video_freerun = 0;
   int video_sync_mode = AVSYNC_TYPE_FREERUN;
   int video_layer_revealed = 0;
   int sd_read_active = 0;
   int auddec_write_failed = 0;
   int auddec_sw_fallback_attempted = 0;
   int native_video_failed = 0;
   int timed_stop = 0;
   const char *sw_audio_reason = "auddec_open_failed";
   int seek_video_require_keyframe = 0;
   int seek_video_wait_keyframe = 0;
   int seek_video_keyframe_mode = 0;
   int seek_video_anchor_pending = 0;
   int seek_video_decode_preroll = 0;
   int seek_video_forward_key_seek_tried = 0;
   uint32_t seek_video_dropped_packets = 0;
   uint32_t seek_video_keyframe_drops = 0;
   uint32_t seek_video_preroll_packets = 0;
   uint32_t seek_video_drop_last_log_ms = 0;
   int64_t seek_audio_catchup_until_ms = MEDIA_TIME_UNSET;
   uint32_t seek_audio_dropped_packets = 0;
   unsigned video_feed_lead_ms = MEDIA_VIDEO_FEED_LEAD_MS;
   unsigned audio_feed_lead_ms = MEDIA_AUDIO_FEED_LEAD_MS;
   unsigned audio_output_channels = media_audio_output_channels();
   int64_t duration_ms = -1;
   int64_t seek_video_catchup_until_ms = MEDIA_TIME_UNSET;
   int64_t seek_video_preroll_limit_ms = MEDIA_SEEK_PREROLL_DECODE_MS;
   uint32_t seek_video_settle_until_ms = 0;
   int64_t video_progress_time_ms = MEDIA_TIME_UNSET;
   unsigned long video_progress_decoded = 0;
   unsigned long video_progress_displayed = 0;
   uint32_t video_progress_wall_ms = start_ms;
   uint32_t video_recover_last_ms = 0;
   uint32_t video_recover_count = 0;
   uint32_t video_write_recover_streak = 0;
   uint32_t video_write_recover_total = 0;

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
   media_native_video_hardware_error = 0;
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
   seek_video_require_keyframe =
      media_video_seek_requires_keyframe(
         fmt->streams[video_stream]->codecpar->codec_id);
   seek_video_preroll_limit_ms =
      media_seek_video_preroll_limit_ms(fmt->streams[video_stream]->codecpar);
   printf("unifrog media native init_drivers begin\n");
   media_video_progress(options, "drivers", 80, 100);
   (void)unifrog_log_flush();
   media_init_drivers_once();
   printf("unifrog media native init_drivers done\n");
   (void)unifrog_log_flush();
   (void)set_video_layer_visible(0, 0, 0, 0, 0);
   media_progress_overlay_video_layer_inactive(&overlay);
   media_progress_overlay_clear_all(&overlay, "native_video_black_prepare",
      path);
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
   printf("unifrog media native video clock freerun=%d disable_audio=%d auddec=%d auddec_freerun=%d audio_enabled=%d audio_output_ch=%u video_feed_lead_ms=%u audio_feed_lead_ms=%u duration=%lld overlay=strip overlay_hide=A video_max_hw_ahead_ms=%u audio_max_hw_ahead_ms=%u hw_ahead_max_wait_ms=%u video_stuck_behind_ms=%u video_stall_recover_ms=%u video_recover_gap_ms=%u video_write_recover_streak_max=%u seek_packet_policy=%s seek_keyframe=%d seek_settle_ms=0 seek_video_warmup=%u seek_recover_warmup=%u preroll_limit=%lld preroll_key_max=%d seek_supersede=1 seek_avsync=keyframe\n",
      video_freerun, disable_audio, auddec.fd >= 0, auddec.freerun,
      audio_enabled, audio_output_channels, video_feed_lead_ms, audio_feed_lead_ms,
      (long long)duration_ms, MEDIA_VIDEO_MAX_HW_AHEAD_MS,
      MEDIA_AUDIO_MAX_HW_AHEAD_MS, MEDIA_HW_AHEAD_MAX_WAIT_MS,
      MEDIA_VIDEO_STUCK_BEHIND_MS, MEDIA_VIDEO_STALL_RECOVER_MS,
      MEDIA_VIDEO_RECOVER_GAP_MS,
      MEDIA_VIDEO_WRITE_RECOVER_MAX,
      MEDIA_SEEK_ACCELERATE_FRAMES ? "accelerate" : "drop_to_anchor",
      seek_video_require_keyframe,
      MEDIA_SEEK_VIDEO_WARMUP_PACKETS,
      MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS,
      (long long)seek_video_preroll_limit_ms,
      MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES);
   packet = av_packet_alloc();
   frame = av_frame_alloc();
   if (!packet || !frame)
      goto out;
   printf("unifrog media native play video=%d audio=%d audio_enabled=%d auddec=%d path=%s\n",
      video_stream, audio_stream, audio_enabled, auddec.fd >= 0, path);
   media_video_progress(options, "playing", 100, 100);
   media_progress_overlay_clear_all(&overlay, "native_video_black_start",
      path);
   media_controls_reset_for_playback("native_video", path);

   for (;;) {
      struct media_controls controls;
      int read_ret;

      if (options && options->max_play_ms &&
          unifrog_perf_time_ms() - start_ms >= options->max_play_ms) {
         printf("unifrog media native video timed_stop ms=%lu limit=%u path=%s\n",
            (unsigned long)(unifrog_perf_time_ms() - start_ms),
            options->max_play_ms, path ? path : "");
         timed_stop = 1;
         break;
      }
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
      {
         int64_t video_time = -1;
         int64_t audio_time = -1;
         int64_t pending_seek_time = MEDIA_TIME_UNSET;
         int64_t cur_time;
         int64_t target_ms = MEDIA_TIME_UNSET;
         uint32_t now_ms = unifrog_perf_time_ms();
         int apply_seek = 0;
         int seek_delta_ms = controls.seek_delta_ms;
         const char *seek_source = "input";
         int seek_to_keyframe =
            seek_video_require_keyframe && !MEDIA_SEEK_ACCELERATE_FRAMES;

         if (seek_video_settle_until_ms &&
             (int32_t)(seek_video_settle_until_ms - now_ms) <= 0) {
            printf("unifrog media seek video settle_done wall=%lu path=%s\n",
               (unsigned long)now_ms, path ? path : "");
            seek_video_settle_until_ms = 0;
         }
         if (seek_delta_ms && duration_ms > 0) {
            if (auddec.fd >= 0)
               (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &audio_time);
            if (video_fd >= 0)
               (void)ioctl(video_fd, VIDDEC_GET_CUR_TIME, &video_time);
            pending_seek_time = media_seek_active_target_ms(
               seek_video_catchup_until_ms, seek_audio_catchup_until_ms);
            cur_time = media_seek_current_ms(video_time, audio_time,
               &hw_video_pacer, &hw_audio_pacer, pending_seek_time);
            target_ms = media_seek_target_ms(cur_time, seek_delta_ms,
               duration_ms);
            printf("unifrog media seek video request cur=%lld video=%lld audio=%lld pending=%lld dur=%lld delta=%d target=%lld active_video=%lld active_audio=%lld source=supersede path=%s\n",
               (long long)cur_time, (long long)video_time,
               (long long)audio_time, (long long)pending_seek_time,
               (long long)duration_ms, seek_delta_ms,
               (long long)target_ms,
               (long long)seek_video_catchup_until_ms,
               (long long)seek_audio_catchup_until_ms, path ? path : "");
            apply_seek = 1;
         }

         if (!apply_seek)
            goto no_video_seek;

         int seek_ret;

         media_flush_viddec_for_seek(video_fd, "video", path);
         media_flush_auddec_for_seek(&auddec, "video", path);
         seek_ret = seek_to_keyframe ?
            media_seek_format_key_ms(fmt, target_ms, seek_delta_ms >= 0,
               "video", path) :
            media_seek_format_ms(fmt, target_ms, "video", path);
         if (seek_ret == 0) {
            if (audio_enabled && audio.fd >= 0) {
               int drop_ret = unifrog_audio_drop(&audio);

               printf("unifrog media seek video drop_sw_audio ret=%d target=%lld path=%s\n",
                  drop_ret, (long long)target_ms, path ? path : "");
            }
            if (seek_to_keyframe)
               printf("unifrog media seek avsync_defer tag=video reason=wait_keyframe target=%lld path=%s\n",
                  (long long)target_ms, path ? path : "");
            else
               media_set_avsync_timebase(target_ms, "video", path);
            if (video_bsf)
               av_bsf_flush(video_bsf);
            if (audio_ctx)
               avcodec_flush_buffers(audio_ctx);
            media_ffmpeg_converter_reset(&audio_converter);
            media_audio_pacer_seek_reset_warmup(&hw_video_pacer, target_ms,
               MEDIA_SEEK_VIDEO_WARMUP_PACKETS);
            media_audio_pacer_seek_reset(&hw_audio_pacer, target_ms);
            sw_video_base_ms = MEDIA_TIME_UNSET;
            sw_audio_start_ms = 0;
            audio_frames = 0;
            auddec_stall_epoch_ms = unifrog_perf_time_ms();
            seek_video_catchup_until_ms = target_ms;
            seek_video_wait_keyframe = seek_to_keyframe;
            seek_video_keyframe_mode = seek_to_keyframe;
            seek_video_anchor_pending = 0;
            seek_video_decode_preroll = 0;
            seek_video_forward_key_seek_tried = 0;
            seek_video_dropped_packets = 0;
            seek_video_keyframe_drops = 0;
            seek_video_preroll_packets = 0;
            seek_video_drop_last_log_ms = 0;
            seek_audio_catchup_until_ms =
               seek_to_keyframe ? MEDIA_TIME_HOLD : target_ms;
            seek_audio_dropped_packets = 0;
            seek_video_settle_until_ms = 0;
            video_progress_time_ms = MEDIA_TIME_UNSET;
            video_progress_decoded = 0;
            video_progress_displayed = 0;
            video_progress_wall_ms = unifrog_perf_time_ms();
            video_layer_revealed = 0;
            printf("unifrog media seek video barrier source=%s delta=%d mode=%s until=%lld keyframe=%d anchor=%d settle_ms=0 warmup=%u preroll_limit=%lld hidden=0 preserve_frame=1 path=%s\n",
               seek_source, seek_delta_ms,
               MEDIA_SEEK_ACCELERATE_FRAMES ? "accelerate" :
               (seek_to_keyframe ? "keyframe" : "skip"),
               (long long)seek_video_catchup_until_ms,
               seek_video_wait_keyframe, seek_video_anchor_pending,
               MEDIA_SEEK_VIDEO_WARMUP_PACKETS,
               (long long)seek_video_preroll_limit_ms, path ? path : "");
            media_draw_progress_overlay(&overlay, "video_seek", target_ms,
               duration_ms, 1, path);
         }
         continue;
      }
no_video_seek:

      if (seek_video_settle_until_ms) {
         uint32_t now_ms = unifrog_perf_time_ms();

         if ((int32_t)(seek_video_settle_until_ms - now_ms) > 0) {
            usleep(MEDIA_HW_AHEAD_POLL_US);
            continue;
         }
         printf("unifrog media seek video settle_done wall=%lu path=%s\n",
            (unsigned long)now_ms, path ? path : "");
         seek_video_settle_until_ms = 0;
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
         int packet_is_key = (packet->flags & AV_PKT_FLAG_KEY) ? 1 : 0;
         int seek_catchup_active = 0;
         int seek_before_target_packet = 0;
         int seek_drop_packet = 0;
         int seek_wait_key_drop = 0;
         int seek_anchor_packet = 0;
         int seek_preroll_packet = 0;
         int seek_done_after_send = 0;
         int seek_recover_drop_current = 0;
         const char *seek_drop_reason = "";

         if (seek_video_catchup_until_ms != MEDIA_TIME_UNSET &&
             video_packet_ms >= 0) {
            int64_t packet_end_ms = (int64_t)video_packet_ms +
               (video_packet_dur_ms > 0 ? (int64_t)video_packet_dur_ms : 0);

            seek_catchup_active = 1;
            seek_before_target_packet =
               packet_end_ms < seek_video_catchup_until_ms;
            if (!MEDIA_SEEK_ACCELERATE_FRAMES) {
               if (seek_before_target_packet) {
                  if (seek_video_wait_keyframe && packet_is_key) {
                     int64_t requested_ms = seek_video_catchup_until_ms;
                     int64_t align_ms = video_packet_ms;

                     if (align_ms < 0)
                        align_ms = requested_ms;
                     printf("unifrog media seek video keyframe_align requested=%lld actual=%lld early=1 wait_key=%d audio_hold=%d path=%s\n",
                        (long long)requested_ms, (long long)align_ms,
                        seek_video_wait_keyframe,
                        seek_audio_catchup_until_ms == MEDIA_TIME_HOLD,
                        path ? path : "");
                     seek_video_catchup_until_ms = align_ms;
                     media_set_avsync_timebase(align_ms,
                        "video_keyframe", path);
                     media_audio_pacer_seek_reset_warmup(
                        &hw_video_pacer, align_ms,
                        MEDIA_SEEK_VIDEO_WARMUP_PACKETS);
                     media_audio_pacer_seek_reset(&hw_audio_pacer,
                        align_ms);
                     seek_audio_catchup_until_ms = align_ms;
                     seek_audio_dropped_packets = 0;
                     seek_video_anchor_pending = 0;
                     seek_video_wait_keyframe = 0;
                     seek_video_decode_preroll = 0;
                     seek_done_after_send = 1;
                  } else if (seek_video_anchor_pending && packet_is_key) {
                     int64_t preroll_ms =
                        seek_video_catchup_until_ms - video_packet_ms;
                     const char *block_reason =
                        media_seek_video_preroll_block_reason(preroll_ms,
                           packet->size, seek_video_preroll_limit_ms);

                     if (!block_reason[0]) {
                        if (seek_audio_catchup_until_ms == MEDIA_TIME_HOLD)
                           seek_audio_catchup_until_ms =
                              seek_video_catchup_until_ms;
                        seek_anchor_packet = 1;
                        seek_video_anchor_pending = 0;
                        seek_video_decode_preroll = 1;
                        seek_video_wait_keyframe = 0;
                        seek_video_preroll_packets++;
                        printf("unifrog media seek video preroll_decode begin packet_ms=%ld until=%lld gap=%lld limit=%lld key_max=%d size=%d path=%s\n",
                           (long)video_packet_ms,
                           (long long)seek_video_catchup_until_ms,
                           (long long)preroll_ms,
                           (long long)seek_video_preroll_limit_ms,
                           MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES,
                           packet->size, path ? path : "");
                     } else {
                        int forward_ret = -1;

                        seek_video_wait_keyframe =
                           seek_video_require_keyframe;
                        seek_audio_catchup_until_ms = MEDIA_TIME_HOLD;
                        seek_video_keyframe_drops++;
                        printf("unifrog media seek video preroll_skip reason=%s packet_ms=%ld until=%lld gap=%lld limit=%lld key_max=%d size=%d forward_tried=%d path=%s\n",
                           block_reason, (long)video_packet_ms,
                           (long long)seek_video_catchup_until_ms,
                           (long long)preroll_ms,
                           (long long)seek_video_preroll_limit_ms,
                           MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES,
                           packet->size, seek_video_forward_key_seek_tried,
                           path ? path : "");
                        if (!seek_video_forward_key_seek_tried) {
                           seek_video_forward_key_seek_tried = 1;
                           forward_ret = media_seek_format_next_ms(fmt,
                              seek_video_catchup_until_ms,
                              "video_preroll_next", path);
                           printf("unifrog media seek video preroll_forward ret=%d target=%lld anchor=%ld reason=%s path=%s\n",
                              forward_ret,
                              (long long)seek_video_catchup_until_ms,
                              (long)video_packet_ms, block_reason,
                              path ? path : "");
                           if (forward_ret == 0) {
                              media_flush_viddec_for_seek(video_fd,
                                 "video_preroll_next", path);
                              media_flush_auddec_for_seek(&auddec,
                                 "video_preroll_next", path);
                              if (audio_enabled && audio.fd >= 0) {
                                 int drop_ret = unifrog_audio_drop(&audio);

                                 printf("unifrog media seek video drop_sw_audio ret=%d target=%lld reason=preroll_next path=%s\n",
                                    drop_ret,
                                    (long long)seek_video_catchup_until_ms,
                                    path ? path : "");
                              }
                              if (video_bsf)
                                 av_bsf_flush(video_bsf);
                              if (audio_ctx)
                                 avcodec_flush_buffers(audio_ctx);
                              media_ffmpeg_converter_reset(
                                 &audio_converter);
                              media_audio_pacer_seek_reset_warmup(
                                 &hw_video_pacer,
                                 seek_video_catchup_until_ms,
                                 MEDIA_SEEK_VIDEO_WARMUP_PACKETS);
                              media_audio_pacer_seek_reset(&hw_audio_pacer,
                                 seek_video_catchup_until_ms);
                              sw_video_base_ms = MEDIA_TIME_UNSET;
                              sw_audio_start_ms = 0;
                              audio_frames = 0;
                              auddec_stall_epoch_ms =
                                 unifrog_perf_time_ms();
                              seek_video_anchor_pending = 0;
                              seek_video_decode_preroll = 0;
                              seek_video_keyframe_mode =
                                 seek_video_require_keyframe;
                              seek_video_dropped_packets = 0;
                              seek_video_keyframe_drops = 0;
                              seek_video_preroll_packets = 0;
                              seek_video_drop_last_log_ms = 0;
                              seek_audio_dropped_packets = 0;
                              seek_video_settle_until_ms = 0;
                              video_progress_time_ms = MEDIA_TIME_UNSET;
                              video_progress_decoded = 0;
                              video_progress_displayed = 0;
                              video_progress_wall_ms =
                                 unifrog_perf_time_ms();
                              video_layer_revealed = 0;
                              av_packet_unref(packet);
                              continue;
                           }
                        }
                        seek_drop_packet = 1;
                        seek_drop_reason = "preroll";
                     }
                  } else if (seek_video_decode_preroll) {
                     seek_preroll_packet = 1;
                     seek_video_preroll_packets++;
                  } else {
                     seek_drop_packet = 1;
                     seek_drop_reason = "before";
                  }
               } else {
                  if (seek_video_require_keyframe &&
                      !seek_video_decode_preroll && !packet_is_key) {
                     seek_drop_packet = 1;
                     seek_wait_key_drop = 1;
                     seek_video_wait_keyframe = 1;
                     seek_audio_catchup_until_ms = MEDIA_TIME_HOLD;
                     seek_video_keyframe_drops++;
                     seek_drop_reason = "wait_key";
                  } else {
                     if (seek_video_require_keyframe &&
                         !seek_video_decode_preroll && packet_is_key &&
                         (seek_video_wait_keyframe ||
                          seek_audio_catchup_until_ms == MEDIA_TIME_HOLD)) {
                        int64_t requested_ms = seek_video_catchup_until_ms;
                        int64_t align_ms = video_packet_ms;

                        if (align_ms < 0)
                           align_ms = requested_ms;
                        printf("unifrog media seek video keyframe_align requested=%lld actual=%lld early=0 wait_key=%d audio_hold=%d path=%s\n",
                           (long long)requested_ms, (long long)align_ms,
                           seek_video_wait_keyframe,
                           seek_audio_catchup_until_ms == MEDIA_TIME_HOLD,
                           path ? path : "");
                        seek_video_catchup_until_ms = align_ms;
                        media_set_avsync_timebase(align_ms,
                           "video_keyframe", path);
                        media_audio_pacer_seek_reset_warmup(
                           &hw_video_pacer, align_ms,
                           MEDIA_SEEK_VIDEO_WARMUP_PACKETS);
                        media_audio_pacer_seek_reset(&hw_audio_pacer,
                           align_ms);
                        seek_audio_catchup_until_ms = align_ms;
                        seek_audio_dropped_packets = 0;
                     }
                     seek_video_anchor_pending = 0;
                     seek_video_wait_keyframe = 0;
                     seek_done_after_send = 1;
                  }
               }
            } else if (!seek_before_target_packet &&
                       (!seek_video_wait_keyframe || packet_is_key)) {
               seek_done_after_send = 1;
            }
         }
         media_video_activity_stage(20u, video_packets & 0x00ffffffu,
            auddec.fd >= 0 ? auddec.packets : audio_frames);
         if (seek_anchor_packet) {
            printf("unifrog media seek video anchor keyframe packet_ms=%ld dur=%ld until=%lld dropped=%lu size=%d path=%s\n",
               (long)video_packet_ms, (long)video_packet_dur_ms,
               (long long)seek_video_catchup_until_ms,
               (unsigned long)seek_video_dropped_packets, packet->size,
               path ? path : "");
         }
         if (seek_preroll_packet &&
             (seek_video_preroll_packets <= 4u ||
              (seek_video_preroll_packets % 60u) == 0u)) {
            printf("unifrog media seek video preroll_decode packet_ms=%ld dur=%ld until=%lld packets=%lu size=%d path=%s\n",
               (long)video_packet_ms, (long)video_packet_dur_ms,
               (long long)seek_video_catchup_until_ms,
               (unsigned long)seek_video_preroll_packets, packet->size,
               path ? path : "");
         }
         if (seek_drop_packet) {
            uint32_t now_ms = unifrog_perf_time_ms();

            seek_video_dropped_packets++;
            if (seek_video_dropped_packets <= 4u ||
                now_ms - seek_video_drop_last_log_ms >= 500u) {
               printf("unifrog media seek video drop mode=skip reason=%s packet_ms=%ld dur=%ld until=%lld dropped=%lu key=%d key_wait=%d size=%d path=%s\n",
                  seek_drop_reason,
                  (long)video_packet_ms, (long)video_packet_dur_ms,
                  (long long)seek_video_catchup_until_ms,
                  (unsigned long)seek_video_dropped_packets,
                  packet_is_key, seek_wait_key_drop, packet->size, path ? path : "");
               seek_video_drop_last_log_ms = now_ms;
            }
            av_packet_unref(packet);
            continue;
         }
         if (audio_enabled && auddec.fd < 0) {
            if (!seek_catchup_active || seek_done_after_send)
               media_video_wait_for_sw_audio(&audio, packet,
                  video_st->time_base, &sw_video_base_ms,
                  audio_frames, audio_ctx ? audio_ctx->sample_rate : 0,
                  sw_audio_start_ms, path);
         } else {
            int hw_wait_ret = 0;

            if (!seek_catchup_active || seek_done_after_send) {
               media_video_pacer_wait(&hw_video_pacer, packet,
                  video_st->time_base, video_feed_lead_ms);
               hw_wait_ret = media_wait_hardware_ahead("viddec", video_fd, 1,
                  &hw_video_pacer, MEDIA_VIDEO_MAX_HW_AHEAD_MS, path);
            }
            if (hw_wait_ret == MEDIA_HW_AHEAD_TIMEOUT && auddec.fd >= 0) {
               struct vdec_decore_status status;
               int64_t video_time = -1;
               int64_t audio_time = -1;
               int status_ok;

               memset(&status, 0, sizeof(status));
               (void)ioctl(video_fd, VIDDEC_GET_CUR_TIME, &video_time);
               (void)ioctl(auddec.fd, AUDDEC_GET_CUR_TIME, &audio_time);
               status_ok = ioctl(video_fd, VIDDEC_GET_STATUS, &status) == 0;
               media_video_note_progress(video_time, status_ok ? &status : NULL,
                  &video_progress_time_ms, &video_progress_decoded,
                  &video_progress_displayed, &video_progress_wall_ms);
               if (video_time >= 0 && audio_time >= 0 &&
                   audio_time - video_time >
                   (int64_t)MEDIA_VIDEO_STUCK_BEHIND_MS) {
                  uint32_t now_ms = unifrog_perf_time_ms();
                  uint32_t since_progress_ms =
                     video_progress_wall_ms ? now_ms - video_progress_wall_ms :
                     UINT_MAX;
                  uint32_t since_recover_ms =
                     video_recover_last_ms ? now_ms - video_recover_last_ms :
                     UINT_MAX;

                  if (since_progress_ms < MEDIA_VIDEO_STALL_RECOVER_MS ||
                      since_recover_ms < MEDIA_VIDEO_RECOVER_GAP_MS) {
                     printf("unifrog media seek video recover_skip reason=%s video=%lld audio=%lld diff=%lld feed=%lld decoded=%lu displayed=%lu since_progress=%lu stall=%u since_recover=%lu gap=%u threshold=%u count=%lu path=%s\n",
                        since_progress_ms < MEDIA_VIDEO_STALL_RECOVER_MS ?
                        "progressing" : "recent",
                        (long long)video_time, (long long)audio_time,
                        (long long)(audio_time - video_time),
                        (long long)hw_video_pacer.next_ms,
                        status_ok ? (unsigned long)status.frames_decoded : 0ul,
                        status_ok ? (unsigned long)status.frames_displayed : 0ul,
                        (unsigned long)since_progress_ms,
                        MEDIA_VIDEO_STALL_RECOVER_MS,
                        (unsigned long)since_recover_ms,
                        MEDIA_VIDEO_RECOVER_GAP_MS,
                        MEDIA_VIDEO_STUCK_BEHIND_MS,
                        (unsigned long)video_recover_count,
                        path ? path : "");
                  } else {
                     int64_t recover_target_ms = audio_time;
                     int64_t audio_drop_until_ms = recover_target_ms;
                     int recover_to_keyframe =
                        seek_video_require_keyframe &&
                        !MEDIA_SEEK_ACCELERATE_FRAMES;
                     int recover_seek_ret;

                     recover_seek_ret = recover_to_keyframe ?
                        media_seek_format_key_ms(fmt, recover_target_ms, 1,
                           "video_recover", path) :
                        media_seek_format_ms(fmt, recover_target_ms,
                           "video_recover", path);
                     if (recover_seek_ret == 0) {
                        if (hw_audio_pacer.started &&
                            hw_audio_pacer.next_ms > audio_drop_until_ms)
                           audio_drop_until_ms = hw_audio_pacer.next_ms;
                        printf("unifrog media seek video recover reason=viddec_timeout video=%lld audio=%lld diff=%lld feed=%lld target=%lld audio_drop_until=%lld decoded=%lu displayed=%lu since_progress=%lu threshold=%u count=%lu path=%s\n",
                           (long long)video_time, (long long)audio_time,
                           (long long)(audio_time - video_time),
                           (long long)hw_video_pacer.next_ms,
                           (long long)recover_target_ms,
                           (long long)audio_drop_until_ms,
                           status_ok ?
                           (unsigned long)status.frames_decoded : 0ul,
                           status_ok ?
                           (unsigned long)status.frames_displayed : 0ul,
                           (unsigned long)since_progress_ms,
                           MEDIA_VIDEO_STUCK_BEHIND_MS,
                           (unsigned long)video_recover_count,
                           path ? path : "");
                        if (video_bsf)
                           av_bsf_flush(video_bsf);
                        media_flush_viddec_for_seek(video_fd,
                           "video_recover", path);
                        if (recover_to_keyframe)
                           printf("unifrog media seek avsync_defer tag=video_recover reason=wait_keyframe target=%lld path=%s\n",
                              (long long)recover_target_ms,
                              path ? path : "");
                        else
                           media_set_avsync_timebase(recover_target_ms,
                              "video_recover", path);
                        media_audio_pacer_seek_reset_warmup(&hw_video_pacer,
                           recover_target_ms,
                           MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS);
                        seek_video_catchup_until_ms = recover_target_ms;
                        seek_video_wait_keyframe = recover_to_keyframe;
                        seek_video_keyframe_mode = recover_to_keyframe;
                        seek_video_anchor_pending = 0;
                        seek_video_decode_preroll = 0;
                        seek_video_forward_key_seek_tried = 0;
                        seek_video_dropped_packets = 0;
                        seek_video_keyframe_drops = 0;
                        seek_video_preroll_packets = 0;
                        seek_video_drop_last_log_ms = 0;
                        seek_audio_catchup_until_ms =
                           recover_to_keyframe ? MEDIA_TIME_HOLD :
                           audio_drop_until_ms;
                        seek_audio_dropped_packets = 0;
                        seek_video_settle_until_ms = 0;
                        video_progress_time_ms = MEDIA_TIME_UNSET;
                        video_progress_decoded = 0;
                        video_progress_displayed = 0;
                        video_progress_wall_ms = now_ms;
                        auddec_stall_epoch_ms = now_ms;
                        video_recover_last_ms = now_ms;
                        video_recover_count++;
                        video_layer_revealed = 0;
                        seek_recover_drop_current = 1;
                        printf("unifrog media seek video barrier mode=%s until=%lld keyframe=%d anchor=%d settle_ms=0 warmup=%u preroll_limit=%lld hidden=0 preserve_frame=1 path=%s\n",
                           MEDIA_SEEK_ACCELERATE_FRAMES ? "accelerate" :
                           (recover_to_keyframe ? "keyframe" : "skip"),
                           (long long)seek_video_catchup_until_ms,
                           seek_video_wait_keyframe,
                           seek_video_anchor_pending,
                           MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS,
                           (long long)seek_video_preroll_limit_ms,
                           path ? path : "");
                     } else {
                        printf("unifrog media seek video recover_skip reason=demux_failed ret=%d video=%lld audio=%lld diff=%lld target=%lld path=%s\n",
                           recover_seek_ret, (long long)video_time,
                           (long long)audio_time,
                           (long long)(audio_time - video_time),
                           (long long)recover_target_ms,
                           path ? path : "");
                     }
                  }
               }
            }
         }
         if (seek_recover_drop_current) {
            av_packet_unref(packet);
            continue;
         }
         int write_ret = media_video_send_filtered(video_fd, video_bsf,
            packet, video_st->time_base,
            video_freerun,
            video_st->codecpar->codec_id == AV_CODEC_ID_H264,
            &video_packets);

         if (write_ret < 0) {
            int saved_errno = errno;
            int64_t recover_target_ms = seek_video_catchup_until_ms;

            if (recover_target_ms == MEDIA_TIME_UNSET ||
                recover_target_ms == MEDIA_TIME_HOLD) {
               if (video_packet_ms >= 0)
                  recover_target_ms = video_packet_ms;
               else
                  recover_target_ms = hw_video_pacer.next_ms;
            }
            if (recover_target_ms < 0)
               recover_target_ms = 0;
            if (video_write_recover_streak < MEDIA_VIDEO_WRITE_RECOVER_MAX &&
                recover_target_ms != MEDIA_TIME_HOLD) {
               uint32_t now_ms = unifrog_perf_time_ms();
               int recover_to_keyframe =
                  seek_video_require_keyframe &&
                  !MEDIA_SEEK_ACCELERATE_FRAMES;
               int reopen_ret;
               int seek_ret = -1;

               printf("unifrog media native video write_recover begin ret=%d errno=%d packets=%lu target=%lld packet_ms=%ld key=%d streak=%lu total=%lu max=%u path=%s\n",
                  write_ret, saved_errno, (unsigned long)video_packets,
                  (long long)recover_target_ms, (long)video_packet_ms,
                  packet_is_key, (unsigned long)video_write_recover_streak,
                  (unsigned long)video_write_recover_total,
                  MEDIA_VIDEO_WRITE_RECOVER_MAX, path ? path : "");
               if (video_bsf)
                  av_bsf_flush(video_bsf);
               if (audio_ctx)
                  avcodec_flush_buffers(audio_ctx);
               media_ffmpeg_converter_reset(&audio_converter);
               media_flush_auddec_for_seek(&auddec, "video_write_recover",
                  path);
               if (audio_enabled && audio.fd >= 0) {
                  int drop_ret = unifrog_audio_drop(&audio);

                  printf("unifrog media seek video drop_sw_audio ret=%d target=%lld reason=write_recover path=%s\n",
                     drop_ret, (long long)recover_target_ms,
                     path ? path : "");
               }
               reopen_ret = media_video_reopen_decoder(&video_fd, fmt,
                  video_stream, video_sync_mode, "video_write_recover", path);
               media_progress_overlay_video_layer_inactive(&overlay);
               if (reopen_ret == 0)
                  seek_ret = recover_to_keyframe ?
                     media_seek_format_key_ms(fmt, recover_target_ms, 1,
                        "video_write_recover", path) :
                     media_seek_format_ms(fmt, recover_target_ms,
                        "video_write_recover", path);
               if (seek_ret == 0) {
                  if (recover_to_keyframe)
                     printf("unifrog media seek avsync_defer tag=video_write_recover reason=wait_keyframe target=%lld path=%s\n",
                        (long long)recover_target_ms, path ? path : "");
                  else
                     media_set_avsync_timebase(recover_target_ms,
                        "video_write_recover", path);
                  media_audio_pacer_seek_reset_warmup(&hw_video_pacer,
                     recover_target_ms,
                     MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS);
                  media_audio_pacer_seek_reset(&hw_audio_pacer,
                     recover_target_ms);
                  sw_video_base_ms = MEDIA_TIME_UNSET;
                  sw_audio_start_ms = 0;
                  audio_frames = 0;
                  auddec_stall_epoch_ms = now_ms;
                  seek_video_catchup_until_ms = recover_target_ms;
                  seek_video_wait_keyframe = recover_to_keyframe;
                  seek_video_keyframe_mode = recover_to_keyframe;
                  seek_video_anchor_pending = 0;
                  seek_video_decode_preroll = 0;
                  seek_video_forward_key_seek_tried = 0;
                  seek_video_dropped_packets = 0;
                  seek_video_keyframe_drops = 0;
                  seek_video_preroll_packets = 0;
                  seek_video_drop_last_log_ms = 0;
                  seek_audio_catchup_until_ms =
                     recover_to_keyframe ? MEDIA_TIME_HOLD :
                     recover_target_ms;
                  seek_audio_dropped_packets = 0;
                  seek_video_settle_until_ms = 0;
                  video_progress_time_ms = MEDIA_TIME_UNSET;
                  video_progress_decoded = 0;
                  video_progress_displayed = 0;
                  video_progress_wall_ms = now_ms;
                  video_recover_last_ms = now_ms;
                  video_recover_count++;
                  video_write_recover_streak++;
                  video_write_recover_total++;
                  video_layer_revealed = 0;
                  printf("unifrog media seek video barrier mode=%s until=%lld keyframe=%d anchor=%d settle_ms=0 warmup=%u preroll_limit=%lld hidden=0 preserve_frame=0 reason=write_recover path=%s\n",
                     MEDIA_SEEK_ACCELERATE_FRAMES ? "accelerate" :
                     (recover_to_keyframe ? "keyframe" : "skip"),
                     (long long)seek_video_catchup_until_ms,
                     seek_video_wait_keyframe, seek_video_anchor_pending,
                     MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS,
                     (long long)seek_video_preroll_limit_ms,
                     path ? path : "");
                  av_packet_unref(packet);
                  continue;
               }
               printf("unifrog media native video write_recover failed reopen=%d seek=%d errno=%d packets=%lu target=%lld streak=%lu total=%lu path=%s\n",
                  reopen_ret, seek_ret, errno, (unsigned long)video_packets,
                  (long long)recover_target_ms,
                  (unsigned long)video_write_recover_streak,
                  (unsigned long)video_write_recover_total,
                  path ? path : "");
            }
            native_video_failed = 1;
            media_native_video_hardware_error = 1;
            printf("unifrog media native video write failed ret=%d errno=%d packets=%lu recoveries=%lu streak=%lu path=%s\n",
               write_ret, saved_errno, (unsigned long)video_packets,
               (unsigned long)video_write_recover_total,
               (unsigned long)video_write_recover_streak, path ? path : "");
            av_packet_unref(packet);
            break;
         }
         if (video_write_recover_streak) {
            printf("unifrog media native video write_recover reset streak=%lu total=%lu packet_ms=%ld path=%s\n",
               (unsigned long)video_write_recover_streak,
               (unsigned long)video_write_recover_total,
               (long)video_packet_ms, path ? path : "");
            video_write_recover_streak = 0;
         }
         if (seek_video_catchup_until_ms != MEDIA_TIME_UNSET &&
             seek_done_after_send) {
            printf("unifrog media seek video catchup_done mode=%s packet_ms=%ld until=%lld key=%d preroll=%d preroll_packets=%lu hidden=%d dropped=%lu key_drops=%lu path=%s\n",
               seek_video_keyframe_mode ? "keyframe" :
               (MEDIA_SEEK_ACCELERATE_FRAMES ? "accelerate" : "skip"),
               (long)video_packet_ms, (long long)seek_video_catchup_until_ms,
               packet_is_key, seek_video_decode_preroll,
               (unsigned long)seek_video_preroll_packets, 0,
               (unsigned long)seek_video_dropped_packets,
               (unsigned long)seek_video_keyframe_drops,
               path ? path : "");
            seek_video_catchup_until_ms = MEDIA_TIME_UNSET;
            seek_video_wait_keyframe = 0;
            seek_video_keyframe_mode = 0;
            seek_video_anchor_pending = 0;
            seek_video_decode_preroll = 0;
            seek_video_forward_key_seek_tried = 0;
            seek_video_dropped_packets = 0;
            seek_video_keyframe_drops = 0;
            seek_video_preroll_packets = 0;
            seek_video_drop_last_log_ms = 0;
            video_progress_wall_ms = unifrog_perf_time_ms();
         }
         if (!seek_catchup_active || seek_done_after_send ||
             seek_anchor_packet || MEDIA_SEEK_ACCELERATE_FRAMES)
            (void)media_native_video_reveal_if_ready(video_fd,
               &video_layer_revealed, &frames_decoded, &frames_displayed,
               &overlay, path);
      } else if (auddec.fd >= 0 && packet->stream_index == audio_stream) {
         if (media_seek_drop_audio_packet_until(packet,
             fmt->streams[audio_stream], &seek_audio_catchup_until_ms,
             &seek_audio_dropped_packets, "audio", path)) {
            av_packet_unref(packet);
            continue;
         }
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
         if (media_seek_drop_audio_packet_until(packet,
             fmt->streams[audio_stream], &seek_audio_catchup_until_ms,
             &seek_audio_dropped_packets, "sw_audio", path)) {
            av_packet_unref(packet);
            continue;
         }
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
         uint32_t now_ms = unifrog_perf_time_ms();
         uint32_t elapsed_ms = now_ms - start_ms;
         uint32_t auddec_stall_ms = now_ms - auddec_stall_epoch_ms;
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
         if (ioctl(video_fd, VIDDEC_GET_STATUS, &status) == 0) {
            media_video_note_progress(video_time, &status,
               &video_progress_time_ms, &video_progress_decoded,
               &video_progress_displayed, &video_progress_wall_ms);
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
         }
         if (auddec.fd >= 0 &&
             unifrog_audio_prefers_stereo_output() &&
             !audio_enabled &&
             !auddec_sw_fallback_attempted &&
             audio_stream >= 0 &&
             seek_audio_catchup_until_ms == MEDIA_TIME_UNSET &&
             seek_video_catchup_until_ms == MEDIA_TIME_UNSET &&
             auddec_stall_ms >= MEDIA_GB300_AUDDEC_STALL_MS &&
             auddec.packets >= MEDIA_GB300_AUDDEC_STALL_PACKETS &&
             aud_status_ok &&
             media_auddec_runtime_decode_stalled(&aud_status,
                audio_time >= 0, audio_time)) {
            printf("unifrog media native auddec fallback trigger reason=decode_stall packets=%lu ms=%lu session_ms=%lu decoded=%lu hdr=%u/%u atime=%lld path=%s\n",
               (unsigned long)auddec.packets, (unsigned long)auddec_stall_ms,
               (unsigned long)elapsed_ms,
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
   if (!native_video_failed && !timed_stop) {
      media_video_finish_eos(video_fd, VIDEO_EOS_TIMEOUT_MS);
   } else {
      printf("unifrog media native video skip_eos reason=%s packets=%lu path=%s\n",
         timed_stop ? "timed_stop" : "write_failed",
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
   media_progress_overlay_close(&overlay,
      ret < 0 ? "native_video_error" : "native_video_close", path);
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
      if (decoder_failed) {
         close_display();
         media_video_reset_modules("native_video_error", path);
      }
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
   media_audio_screen_close();
   return ret;
}

static int media_play_ffmpeg_video(const char *path,
   const struct unifrog_media_video_options *options)
{
   AVFormatContext *fmt = NULL;
   AVCodecContext *video_ctx = NULL;
   AVCodecContext *audio_ctx = NULL;
   const AVCodec *video_decoder = NULL;
   const AVCodec *audio_decoder = NULL;
   AVPacket *packet = NULL;
   AVPacket *video_filter_packet = NULL;
   AVBSFContext *video_bsf = NULL;
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
   uint32_t video_send_failures = 0;
   uint32_t audio_send_failures = 0;
   uint32_t audio_write_successes = 0;
   uint32_t audio_write_failures = 0;
   uint32_t video_frames_before_flush = 0;
   int audio_enabled = 0;
   int disable_audio = options && options->disable_audio;
   int disable_video = options && options->disable_video;
   unsigned audio_output_channels = media_audio_output_channels();
   int audio_backend = media_gb300_auddec_fallback_backend("ffmpeg_video");
   int ret = -1;
   int sd_read_active = 0;
   int abort_video = 0;

   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   memset(&audio_converter, 0, sizeof(audio_converter));
   memset(&sw_video, 0, sizeof(sw_video));
   sw_video.fb.fd = -1;
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
   if (!disable_video)
      video_stream = media_find_stream_type(fmt, AVMEDIA_TYPE_VIDEO);
   if (!disable_audio)
      audio_stream = media_find_stream_type(fmt, AVMEDIA_TYPE_AUDIO);
   if (video_stream < 0) {
      if (!disable_video || audio_stream < 0) {
         printf("unifrog media ffmpeg video no_video audio=%d disable_video=%d path=%s\n",
            audio_stream, disable_video, path ? path : "");
         goto out;
      }
   }
   if (video_stream >= 0) {
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
      (void)media_ffmpeg_swvideo_bsf_init(fmt->streams[video_stream],
         &video_bsf);
   } else {
      printf("unifrog media ffmpeg video disabled audio_stream=%d path=%s\n",
         audio_stream, path ? path : "");
   }

   if (audio_stream >= 0) {
      audio_decoder = avcodec_find_decoder(
         fmt->streams[audio_stream]->codecpar->codec_id);
      if (audio_decoder) {
         audio_ctx = avcodec_alloc_context3(audio_decoder);
         if (audio_ctx &&
            avcodec_parameters_to_context(audio_ctx,
                fmt->streams[audio_stream]->codecpar) == 0) {
            audio_ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;
            if (avcodec_open2(audio_ctx, audio_decoder, NULL) == 0 &&
                audio_ctx->sample_rate >= 8000 &&
                audio_ctx->sample_rate <= 48000 &&
                unifrog_audio_open_backend(&audio,
                   (unsigned)audio_ctx->sample_rate,
                   audio_output_channels,
                   512, 8, audio_backend) == 0) {
               pcm = malloc(sizeof(*pcm) * MEDIA_FFMPEG_CHUNK_FRAMES *
                  audio_output_channels);
               if (pcm) {
                  (void)unifrog_audio_set_volume(&audio,
                     media_audio_runtime_volume());
                  (void)unifrog_audio_set_mute(&audio, 1);
                  (void)unifrog_audio_start(&audio);
                  (void)unifrog_audio_set_output_enabled(&audio, 1);
                  audio_enabled = 1;
                  printf("unifrog media ffmpeg video audio enabled codec=%s stream=%d rate=%d src_ch=%d out_ch=%u backend=%d fmt=%s\n",
                     audio_decoder->name ? audio_decoder->name : "?",
                     audio_stream, audio_ctx->sample_rate,
                     media_codec_channels(audio_ctx),
                     audio_output_channels, audio_backend,
                     media_sample_format_name(audio_ctx->sample_fmt));
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
   video_filter_packet = av_packet_alloc();
   frame = av_frame_alloc();
   if (!packet || !video_filter_packet || !frame)
      goto out;
   /*
    * avformat_find_stream_info may leave the custom AVIO position deep into an
    * MP4. Native playback feeds the hardware decoder from the beginning, but
    * the software decoder otherwise starts mid-GOP and reports invalid H.264
    * packets before audio gets a chance to run. Rewind explicitly so this
    * diagnostic path exercises the same start-of-file playback shape.
    */
   (void)media_seek_format_ms(fmt, 0, "ffmpeg_video_start", path);
   while (!media_exit_down() && !abort_video) {
      int read_ret;

      if (options && options->max_play_ms &&
          unifrog_perf_time_ms() - start_ms >= options->max_play_ms) {
         printf("unifrog media ffmpeg video timed_stop ms=%lu limit=%u path=%s\n",
            (unsigned long)(unifrog_perf_time_ms() - start_ms),
            options->max_play_ms, path ? path : "");
         break;
      }
      read_ret = av_read_frame(fmt, packet);
      if (read_ret < 0)
         break;
      if (video_ctx && packet->stream_index == video_stream) {
         if (video_bsf) {
            int bsf_ret = av_bsf_send_packet(video_bsf, packet);

            if (bsf_ret < 0) {
               video_send_failures++;
               printf("unifrog media ffmpeg video bsf_send failed ret=%d failures=%lu path=%s\n",
                  bsf_ret, (unsigned long)video_send_failures,
                  path ? path : "");
            }
            while (bsf_ret >= 0 && !abort_video) {
               bsf_ret = av_bsf_receive_packet(video_bsf,
                  video_filter_packet);
               if (bsf_ret == AVERROR(EAGAIN) || bsf_ret == AVERROR_EOF)
                  break;
               if (bsf_ret < 0) {
                  video_send_failures++;
                  printf("unifrog media ffmpeg video bsf_receive failed ret=%d failures=%lu path=%s\n",
                     bsf_ret, (unsigned long)video_send_failures,
                     path ? path : "");
                  break;
               }
               (void)media_ffmpeg_swvideo_decode_present(video_ctx,
                  video_filter_packet, frame, &sw_video, &video_frames,
                  &swvideo_failures, &video_send_failures, audio_frames,
                  &abort_video, path);
               av_packet_unref(video_filter_packet);
            }
         } else {
            (void)media_ffmpeg_swvideo_decode_present(video_ctx, packet,
               frame, &sw_video, &video_frames, &swvideo_failures,
               &video_send_failures, audio_frames, &abort_video, path);
         }
      } else if (audio_enabled && packet->stream_index == audio_stream) {
         int send_ret = avcodec_send_packet(audio_ctx, packet);

         if (send_ret >= 0 || send_ret == AVERROR(EAGAIN)) {
            audio_send_failures = 0;
            for (;;) {
               int recv_ret = avcodec_receive_frame(audio_ctx, frame);
               int write_ret;
               uint32_t before_frames;

               if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
                  break;
               if (recv_ret < 0)
                  break;
               before_frames = audio_frames;
               write_ret = media_ffmpeg_write_frame(&audio, audio_ctx,
                  &audio_converter, frame, pcm, MEDIA_FFMPEG_CHUNK_FRAMES,
                  &audio_frames, path);
               if (write_ret == 0 && audio_frames > before_frames)
                  audio_write_successes++;
               else
                  audio_write_failures++;
               av_frame_unref(frame);
               if (write_ret != 0)
                  break;
            }
         } else {
            audio_send_failures++;
            printf("unifrog media ffmpeg video audio_send failed ret=%d failures=%lu path=%s\n",
               send_ret, (unsigned long)audio_send_failures,
               path ? path : "");
            if (audio_send_failures >= MEDIA_SWVIDEO_DISPLAY_FAIL_LIMIT)
               abort_video = 1;
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

   video_frames_before_flush = video_frames;
   if (!abort_video && video_ctx) {
      if (video_bsf) {
         int bsf_ret = av_bsf_send_packet(video_bsf, NULL);

         while (bsf_ret >= 0) {
            bsf_ret = av_bsf_receive_packet(video_bsf, video_filter_packet);
            if (bsf_ret == AVERROR(EAGAIN) || bsf_ret == AVERROR_EOF)
               break;
            if (bsf_ret < 0)
               break;
            (void)media_ffmpeg_swvideo_decode_present(video_ctx,
               video_filter_packet, frame, &sw_video, &video_frames,
               &swvideo_failures, &video_send_failures, audio_frames,
               &abort_video, path);
            av_packet_unref(video_filter_packet);
            if (abort_video)
               break;
         }
      }
      (void)avcodec_send_packet(video_ctx, NULL);
      for (;;) {
         int recv_ret = avcodec_receive_frame(video_ctx, frame);

         if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF)
            break;
         if (recv_ret < 0)
            break;
         if (media_swvideo_present_fb(&sw_video, frame) == 0) {
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
         av_frame_unref(frame);
      }
   }
   {
      uint32_t min_audio_frames = 0;
      int video_ok = !video_ctx || video_frames_before_flush > 0;
      int audio_ok = !audio_enabled;

      if (audio_enabled && audio_ctx && audio_ctx->sample_rate > 0) {
         min_audio_frames = media_audio_ms_to_frames(
            MEDIA_SWVIDEO_MIN_AUDIO_MS, (unsigned)audio_ctx->sample_rate);
         audio_ok = audio_frames >= min_audio_frames &&
            audio_write_successes > 0 && audio_write_failures == 0;
      }
      printf("unifrog media ffmpeg video quality video_ok=%d audio_ok=%d preflush_video=%lu video=%lu audio=%lu min_audio=%lu audio_writes=%lu audio_failures=%lu bsf=%d backend=%d path=%s\n",
         video_ok, audio_ok, (unsigned long)video_frames_before_flush,
         (unsigned long)video_frames, (unsigned long)audio_frames,
         (unsigned long)min_audio_frames,
         (unsigned long)audio_write_successes,
         (unsigned long)audio_write_failures, video_bsf ? 1 : 0,
         audio_backend, path ? path : "");
      ret = video_ok && audio_ok && !abort_video ? 0 : -1;
   }

out:
   printf("unifrog media ffmpeg video end ret=%d abort=%d video_frames=%lu preflush_video=%lu audio_frames=%lu audio_writes=%lu audio_failures=%lu ms=%lu path=%s\n",
      ret, abort_video, (unsigned long)video_frames,
      (unsigned long)video_frames_before_flush, (unsigned long)audio_frames,
      (unsigned long)audio_write_successes,
      (unsigned long)audio_write_failures,
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
   if (video_bsf)
      av_bsf_free(&video_bsf);
   if (video_filter_packet)
      av_packet_free(&video_filter_packet);
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

static int media_play_native_video_with_mmz(const char *path,
   const struct unifrog_media_video_options *options, const char *tag)
{
   int lease;
   int ret;

   lease = media_dynamic_mmz_acquire(tag, path);
   if (lease < 0)
      return -1;
   ret = media_play_native_video(path, options);
   media_dynamic_mmz_release(lease, tag, path);
   return ret;
}

static int media_init_module_logged(const char *name)
{
   int ret = module_init(name);

   printf("unifrog media module_init name=%s ret=%d\n", name, ret);
   return ret;
}

void media_init_drivers_once(void)
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

   if (unifrog_media_path_is_wav(path)) {
      ret = media_play_wav_pcm(path);
      if (ret != 0) {
         printf("unifrog media direct wav fallback auddec path=%s\n", path);
         ret = media_play_wav_auddec(path);
      }
      if (ret != 0) {
         printf("unifrog media direct wav fallback container path=%s\n",
            path);
         ret = media_play_native_audio_compressed(path);
      }
   } else if (unifrog_media_path_is_mp3(path)) {
      ret = media_play_mp3_auddec(path);
      if (ret != 0) {
         printf("unifrog media direct mp3 fallback container path=%s\n",
            path);
         ret = media_play_native_audio_compressed(path);
      }
   } else if (unifrog_media_path_is_aac(path)) {
      ret = media_play_aac_adts_auddec(path);
      if (ret != 0) {
         printf("unifrog media direct aac fallback container path=%s\n",
            path);
         ret = media_play_native_audio_compressed(path);
      }
   } else if (unifrog_media_path_is_flac(path)) {
      ret = media_play_flac_auddec(path);
      if (ret != 0) {
         printf("unifrog media direct flac fallback container path=%s\n",
            path);
         ret = media_play_native_audio_compressed(path);
      }
   } else if (unifrog_media_path_is_ogg(path)) {
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
   int audio_only = unifrog_media_path_is_audio(path);
   int image_file = unifrog_media_path_is_image(path);
   int force_native = options &&
      options->route == UNIFROG_MEDIA_ROUTE_NATIVE;
   int force_ffmpeg = options &&
      options->route == UNIFROG_MEDIA_ROUTE_FFMPEG;
   int force_wav_auddec = options &&
      options->route == UNIFROG_MEDIA_ROUTE_WAV_AUDDEC;
   int disable_video = options && options->disable_video;
   int ret;
   int mmz_lease = 0;
   size_t old_log_auto_flush;

   if (!path || !path[0])
      return -1;
   if (options && !unifrog_media_route_available(path, options->route, 0))
      return -1;
   if (options && options->tuning)
      unifrog_media_set_tuning(options->tuning);
   media_sd_read_recover_stale("play_start");
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   printf("unifrog media launch stack=native path=%s audio_only=%d image=%d force_native=%d force_ffmpeg=%d force_wav_auddec=%d disable_video=%d\n",
      path, audio_only, image_file, force_native, force_ffmpeg,
      force_wav_auddec, disable_video);
   (void)unifrog_log_flush();
   media_disk_suspend_begin("media_session", path);
   unifrog_log_set_auto_flush_bytes(VIDEO_LOG_AUTO_FLUSH_BYTES);
   printf("unifrog media start stack=native path=%s audio_only=%d image=%d force_native=%d force_ffmpeg=%d force_wav_auddec=%d disable_video=%d\n",
      path, audio_only, image_file, force_native, force_ffmpeg,
      force_wav_auddec, disable_video);
   (void)unifrog_log_flush();
   if (audio_only)
      media_audio_screen_draw("audio_session", path, 0, -1, 1);
   if (audio_only) {
      if (force_ffmpeg) {
         ret = media_play_ffmpeg_audio(path);
      } else if (force_wav_auddec && unifrog_media_path_is_wav(path)) {
         ret = media_play_wav_auddec(path);
      } else if (unifrog_media_path_is_wav(path)) {
         ret = media_play_wav_pcm(path);
         if (ret != 0) {
            printf("unifrog media wav fallback auddec path=%s\n", path);
            ret = media_play_wav_auddec(path);
         }
         if (ret != 0) {
            printf("unifrog media wav fallback container path=%s\n", path);
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
   } else if (force_ffmpeg) {
      printf("unifrog media video route=ffmpeg reason=explicit_ffmpeg path=%s\n",
         path);
      ret = media_play_ffmpeg_video(path, options);
   } else {
      mmz_lease = media_dynamic_mmz_acquire("native_video", path);
      if (mmz_lease < 0) {
         ret = -1;
      } else {
         ret = media_play_native_video(path, options);
         if (ret != 0 && media_native_video_hardware_error) {
            printf("unifrog media native video fallback skip reason=hardware_error ret=%d path=%s\n",
               ret, path ? path : "");
         } else if (ret != 0) {
            printf("unifrog media native video fallback ffmpeg_swvideo ret=%d path=%s\n",
               ret, path ? path : "");
            ret = media_play_ffmpeg_video(path, options);
         }
         media_dynamic_mmz_release(mmz_lease, "native_video", path);
      }
   }
   if (ret != 0 && !force_native) {
      printf("unifrog media native fallback_unavailable ret=%d path=%s\n",
         ret, path);
      (void)unifrog_log_flush();
   }
   printf("unifrog media end stack=native ret=%d path=%s\n", ret, path);
   if (audio_only)
      media_audio_screen_close();
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
   uint32_t start_ms = unifrog_perf_time_ms();
   int audio_output_enabled = 0;
   int audio_only = unifrog_media_path_is_audio(path);
   int image_file = unifrog_media_path_is_image(path);
   int force_no_audio = options && (options->disable_audio ||
      options->route == UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED);
   int force_no_video = options && options->disable_video;
   int force_audio = options &&
      options->route == UNIFROG_MEDIA_ROUTE_HCPLAYER_AUDIO;
   int force_wav_auddec = options &&
      options->route == UNIFROG_MEDIA_ROUTE_WAV_AUDDEC;
   int force_native = options &&
      options->route == UNIFROG_MEDIA_ROUTE_NATIVE;
   int force_hcplayer = options &&
      (options->route == UNIFROG_MEDIA_ROUTE_HCPLAYER ||
      options->route == UNIFROG_MEDIA_ROUTE_HCPLAYER_AUDIO ||
      options->route == UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED);
   int direct_audio_fallback = 0;
   int audio_stream_count = -1;
   int video_stream_count = -1;
   int64_t last_pos = -1;
   size_t old_log_auto_flush;
   int mmz_lease = 0;
   int ret = -1;

   if (!path || !path[0])
      return -1;
   if (options && !unifrog_media_route_available(path, options->route, 1))
      return -1;
   if (options && options->tuning)
      unifrog_media_set_tuning(options->tuning);
   media_sd_read_recover_stale("play_start");
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   printf("unifrog media launch stack=hcplayer path=%s audio_only=%d image=%d\n",
      path, audio_only, image_file);
   (void)unifrog_log_flush();
   media_disk_suspend_begin("media_session", path);
   unifrog_log_set_auto_flush_bytes(VIDEO_LOG_AUTO_FLUSH_BYTES);
   printf("unifrog media start path=%s audio_only=%d image=%d\n",
      path, audio_only, image_file);
   (void)unifrog_log_flush();
   if (audio_only)
      media_audio_screen_draw("audio_session", path, 0, -1, 1);
   media_log_file_probe(path, "play_start");
   media_log_ffmpeg_caps_once();
   if (options && options->preset >= 0 &&
      (unsigned)options->preset < sizeof(playback_presets) / sizeof(playback_presets[0]))
      preset = &playback_presets[options->preset];
   if (options && options->route == UNIFROG_MEDIA_ROUTE_FFMPEG) {
      printf("unifrog media route=ffmpeg reason=explicit path=%s\n",
         path ? path : "");
      ret = audio_only ? media_play_ffmpeg_audio(path) :
         media_play_ffmpeg_video(path, options);
      goto out;
   }
   if (audio_only && unifrog_media_path_is_wav(path) && force_wav_auddec) {
      printf("unifrog media audio route=auddec reason=explicit_wav_auddec path=%s\n",
         path ? path : "");
      ret = media_play_wav_auddec(path);
      media_audio_screen_close();
      unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
      media_disk_suspend_end("media_session", path);
      (void)unifrog_log_flush();
      return ret;
   }
   if (unifrog_audio_prefers_stereo_output() &&
       !force_hcplayer) {
      if (audio_only && !force_no_audio) {
         printf("unifrog media audio route=direct reason=gb300_hcplayer_native_audio path=%s\n",
            path ? path : "");
         ret = media_play_direct_audio(path);
         media_audio_screen_close();
         unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
         media_disk_suspend_end("media_session", path);
         (void)unifrog_log_flush();
         return ret;
      }
      if (!audio_only && !image_file && !force_no_audio) {
         printf("unifrog media video route=native reason=gb300_hcplayer_native_video path=%s\n",
            path ? path : "");
         ret = media_play_native_video_with_mmz(path, options,
            "hcplayer_native_video");
         unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
         media_disk_suspend_end("media_session", path);
         (void)unifrog_log_flush();
         return ret;
      }
   }
   if (audio_only && force_native) {
      printf("unifrog media audio route=direct reason=explicit_native "
             "force_native=1 force_audio=%d path=%s\n",
         force_audio, path ? path : "");
      ret = media_play_direct_audio(path);
      media_audio_screen_close();
      unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
      media_disk_suspend_end("media_session", path);
      (void)unifrog_log_flush();
      return ret;
   }
   if (!audio_only && !image_file && force_native) {
      printf("unifrog media video route=native reason=explicit_native "
             "force_native=1 disable_audio=%d path=%s\n",
         force_no_audio, path);
      ret = media_play_native_video_with_mmz(path, options,
         "hcplayer_force_native");
      printf("unifrog media video route=native ret=%d path=%s\n", ret, path);
      unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
      media_disk_suspend_end("media_session", path);
      (void)unifrog_log_flush();
      return ret;
   }
   printf("unifrog media route=hcplayer reason=%s audio_only=%d image=%d "
          "force_audio=%d disable_audio=%d path=%s\n",
      force_hcplayer ? "requested" : "image",
      audio_only, image_file, force_audio, force_no_audio, path ? path : "");
   if (!audio_only) {
      mmz_lease = media_dynamic_mmz_acquire(image_file ?
         "hcplayer_image" : "hcplayer_video", path);
      if (mmz_lease < 0)
         goto out;
   }
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
   init_args.disable_video = (audio_only || force_no_video) ? true : false;
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
      if (options && options->max_play_ms &&
          unifrog_perf_time_ms() - start_ms >= options->max_play_ms) {
         printf("unifrog media hcplayer timed_stop ms=%lu limit=%u path=%s\n",
            (unsigned long)(unifrog_perf_time_ms() - start_ms),
            options->max_play_ms, path ? path : "");
         break;
      }
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
   media_dynamic_mmz_release(mmz_lease, image_file ?
      "hcplayer_image" : "hcplayer_video", path);
   if (direct_audio_fallback)
      ret = media_play_direct_audio(path);
   printf("unifrog media end ret=%d path=%s\n", ret, path ? path : "");
   if (audio_only)
      media_audio_screen_close();
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
