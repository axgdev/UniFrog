#ifndef UNIFROG_MEDIA_H
#define UNIFROG_MEDIA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*unifrog_media_progress_cb)(void *userdata,
   const char *stage, unsigned done, unsigned total);

struct unifrog_media_video_options {
   int preset;
   int disable_audio;
   int force_audio;
   int force_native;
   int force_hcplayer;
   int force_ffmpeg;
   unifrog_media_progress_cb progress;
   void *progress_userdata;
};

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
