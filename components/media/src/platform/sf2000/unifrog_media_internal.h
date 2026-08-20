#ifndef UNIFROG_MEDIA_INTERNAL_H
#define UNIFROG_MEDIA_INTERNAL_H

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
#include <unifrog/media_policy.h>
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
#define MEDIA_READAHEAD_MAX_SLOTS 16u
#define MEDIA_PROGRESS_OVERLAY_MIN_MS 500u
#define MEDIA_PROGRESS_OVERLAY_STRIP_PX 24u
#define MEDIA_PROGRESS_OVERLAY_BAR_H 10u
#define MEDIA_PROGRESS_OVERLAY_FB_H 240u
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MEDIA_SEEK_STEP_MS 10000
#define MEDIA_SWVIDEO_MMZ_ID 0
#define MEDIA_TIME_UNSET INT32_MIN
#define MEDIA_TIME_HOLD INT64_MAX
#define MEDIA_GB300_AUDDEC_STALL_PACKETS 32u
#define MEDIA_GB300_AUDDEC_STALL_MS 2000u

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
   struct unifrog_media_readahead_slot
      readahead_slots[MEDIA_READAHEAD_MAX_SLOTS];
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

extern struct unifrog_media_tuning media_runtime_tuning;
extern unsigned media_gb300_raw_auddec_route_counter;
extern unsigned media_gb300_auddec_route_counter;
extern unsigned media_sd_read_depth;
extern unsigned media_disk_suspend_depth;
extern uint32_t media_disk_suspend_start_ms;
extern uint32_t media_video_activity_marker;
extern uint32_t media_audio_activity_marker;
extern int media_pending_seek_delta_ms;
extern int media_pending_overlay_toggle;
extern int media_controls_wait_release;
extern int media_controls_wait_logged;
extern struct media_audio_screen media_audio_screen;

int media_auddec_variant_allowed(const char *label);
uint32_t media_audio_preferred_snd_devs(void);
unsigned media_auddec_rotated_variant_index(unsigned order,
   unsigned gb300_count, unsigned offset);
uint16_t media_read_le16(const uint8_t *p);
uint32_t media_read_le32(const uint8_t *p);
uint32_t media_read_be32(const uint8_t *p);
unsigned media_audio_output_channels(void);
unsigned media_audio_mix_channels(unsigned output_channels);
uint64_t media_audio_output_layout(unsigned channels);
audio_channel_select_t media_audio_channel_select(void);
uint8_t media_audio_runtime_volume(void);
int media_gb300_i2so_prime_open(const char *tag, unsigned sample_rate,
   unsigned channels, unsigned bits, int sync_mode, uint32_t pcm_dest);
void media_gb300_i2so_prime_close(int *fdp, const char *tag);
void media_expand_mono_to_output(const int16_t *mono, int16_t *output,
   unsigned frames, unsigned channels);
void media_expand_mono_to_output_inplace(int16_t *buffer, unsigned frames,
   unsigned channels);
int media_audio_write_mono_output(struct unifrog_audio *audio,
   const int16_t *mono, int16_t *scratch, unsigned frames,
   unsigned channels);
void media_log_pcm_stats(const char *scope, const struct unifrog_audio *audio,
   const int16_t *pcm, unsigned frames, const char *path);
uint32_t media_video_activity_mark_value(void);
void media_video_activity_stage(uint32_t stage, uint32_t detail0,
   uint32_t detail1);
uint32_t media_audio_activity_mark_value(void);
void media_audio_activity_stage(uint32_t stage, uint32_t detail0,
   uint32_t detail1);
void media_disk_suspend_begin(const char *tag, const char *path);
void media_disk_suspend_end(const char *tag, const char *path);
void media_sd_read_begin(const char *tag, const char *path);
void media_sd_read_end(const char *tag, const char *path);
void media_sd_read_recover_stale(const char *tag);
void media_video_progress(const struct unifrog_media_video_options *options,
   const char *stage, uint64_t done, uint64_t total);
int media_exit_down(void);
void media_controls_reset_for_playback(const char *tag, const char *path);
void media_poll_controls(struct media_controls *controls);
int media_controls_pending_action(void);
void media_audio_pacer_wait_ms(struct media_audio_pacer *pacer,
   int64_t pts_ms, int64_t dur_ms, unsigned feed_lead_ms);
void media_audio_pacer_seek_reset(struct media_audio_pacer *pacer,
   int64_t target_ms);
int64_t media_seek_target_ms(int64_t current_ms, int delta_ms,
   int64_t duration_ms);
int64_t media_seek_current_ms(int64_t video_time, int64_t audio_time,
   struct media_audio_pacer *video_pacer,
   struct media_audio_pacer *audio_pacer, int64_t pending_target_ms);
void media_flush_auddec_for_seek(struct media_auddec *auddec,
   const char *tag, const char *path);
int media_wait_hardware_ahead(const char *kind, int fd, int video,
   struct media_audio_pacer *pacer, unsigned max_ahead_ms,
   const char *path);
uint32_t media_audio_frames_to_ms(uint32_t frames, int rate);
uint32_t media_audio_ms_to_frames(int64_t ms, unsigned rate);
uint32_t media_audio_bytes_to_ms(uint32_t bytes, unsigned bytes_per_frame,
   unsigned rate);
uint32_t media_sw_audio_clock_ms(struct unifrog_audio *audio,
   uint32_t frames, int rate, uint32_t start_ms);
int media_sw_audio_reset_output(struct unifrog_audio *audio,
   unsigned rate, unsigned channels, int backend, int defer_stereo_output,
   const char *tag, const char *path);
void media_audio_screen_draw(const char *tag, const char *path,
   int64_t current_ms, int64_t duration_ms, int force);
int media_wav_read_pcm_sample(FILE *file, unsigned bits, int32_t *sample);
int16_t media_wav_clip_sample(int32_t sample);
int media_auddec_open_raw(const char *label, uint32_t codec_id,
   unsigned sample_rate, unsigned channels, unsigned bits,
   const uint8_t *extradata, unsigned extradata_size, int sync_mode,
   struct media_auddec *auddec);
int media_auddec_send_raw(struct media_auddec *auddec, const uint8_t *data,
   size_t size, int32_t pts, int32_t dur);
void media_auddec_send_eos(struct media_auddec *auddec);
void media_auddec_close(struct media_auddec *auddec);
void media_auddec_release_fd(int *fdp, const char *tag);
void media_auddec_finish(struct media_auddec *auddec, unsigned timeout_ms);
int media_auddec_open(AVFormatContext *fmt, int stream_index, int sync_mode,
   struct media_auddec *auddec);
int media_auddec_send_packet(struct media_auddec *auddec,
   const AVPacket *packet);
int media_auddec_status_decode_stalled(
   const struct audio_decore_status *status);
int media_auddec_clock_has_progress(int time_ok, int64_t cur_time);
int media_auddec_runtime_decode_stalled(
   const struct audio_decore_status *status, int time_ok, int64_t cur_time);
int media_auddec_status_has_progress(const struct audio_decore_status *status);
void media_auddec_enable_output_on_progress(struct media_auddec *auddec,
   const struct audio_decore_status *status, const char *scope,
   uint32_t packet_index);
void media_auddec_enable_output_on_clock_progress(
   struct media_auddec *auddec, int64_t cur_time, const char *scope,
   uint32_t packet_index);
void media_auddec_log_packet_status(struct media_auddec *auddec,
   const char *scope, uint32_t packet_index, const uint8_t *data,
   size_t size, int32_t pts, int32_t dur);
int media_run_gb300_auddec_probe(const char *tag);
void media_run_gb300_auddec_probe_once(const char *tag);
void media_init_drivers_once(void);
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

#endif
