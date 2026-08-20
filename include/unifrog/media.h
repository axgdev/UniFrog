#ifndef UNIFROG_MEDIA_H
#define UNIFROG_MEDIA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*unifrog_media_progress_cb)(void *userdata,
   const char *stage, unsigned done, unsigned total);

struct unifrog_media_tuning {
   unsigned video_feed_lead_ms;
   unsigned audio_feed_lead_ms;
   unsigned video_kshm_size;
   unsigned video_lowres_kshm_size;
   size_t file_buffer_size;
   size_t file_buffer_min_size;
   size_t file_readahead_size;
   size_t file_readahead_min_size;
   unsigned file_readahead_slots;
   size_t video_readahead_size;
   size_t video_readahead_min_size;
   unsigned video_readahead_slots;
   unsigned video_prefill_target_ms;
   size_t video_prefill_min_bytes;
   size_t video_prefill_max_bytes;
   size_t video_preload_max_bytes;
   unsigned audio_max_hw_ahead_ms;
   unsigned video_max_hw_ahead_ms;
   unsigned seek_warmup_packets;
   unsigned seek_video_warmup_packets;
   unsigned seek_video_recover_warmup_packets;
   unsigned hw_ahead_max_wait_ms;
   unsigned seek_settle_ms;
   int seek_accelerate_frames;
   unsigned seek_keyframe_drop_limit;
   int seek_preroll_decode_ms;
   int seek_preroll_hd_decode_ms;
   int seek_preroll_keyframe_max_bytes;
   unsigned video_stuck_behind_ms;
   unsigned video_stall_recover_ms;
   unsigned video_recover_gap_ms;
   unsigned video_write_recover_max;
   unsigned video_write_eperm_recover_ms;
   unsigned file_slow_read_log_ms;
   int audio_buffering_start_ms;
   int audio_buffering_end_ms;
   int video_buffering_start_ms;
   int video_buffering_end_ms;
   int reset_viddec_on_fail;
   int gb300_auddec_probe_once;
};

enum unifrog_media_route {
   UNIFROG_MEDIA_ROUTE_AUTO = 0,
   UNIFROG_MEDIA_ROUTE_NATIVE,
   UNIFROG_MEDIA_ROUTE_FFMPEG,
   UNIFROG_MEDIA_ROUTE_WAV_AUDDEC,
   UNIFROG_MEDIA_ROUTE_HCPLAYER,
   UNIFROG_MEDIA_ROUTE_HCPLAYER_AUDIO,
   UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED,
};

int unifrog_media_route_parse(const char *name,
   enum unifrog_media_route *route);
const char *unifrog_media_route_name(enum unifrog_media_route route);
int unifrog_media_route_available(const char *path,
   enum unifrog_media_route route, int hcplayer_enabled);

struct unifrog_media_video_options {
   int preset;
   enum unifrog_media_route route;
   int disable_video;
   int disable_audio;
   unsigned max_play_ms;
   const struct unifrog_media_tuning *tuning;
   unifrog_media_progress_cb progress;
   void *progress_userdata;
};

void unifrog_media_tuning_defaults(struct unifrog_media_tuning *tuning);
void unifrog_media_set_tuning(const struct unifrog_media_tuning *tuning);
int unifrog_media_play_video_ex(const char *path,
   const struct unifrog_media_video_options *options);
int unifrog_media_play_video(const char *path);
int unifrog_media_run_audio_diagnostics_ex(char *summary, size_t summary_size,
   unifrog_media_progress_cb progress, void *userdata);
int unifrog_media_run_audio_diagnostics(char *summary, size_t summary_size);

#ifdef __cplusplus
}
#endif

#endif
