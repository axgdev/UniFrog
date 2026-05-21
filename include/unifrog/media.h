#ifndef UNIFROG_MEDIA_H
#define UNIFROG_MEDIA_H

#ifdef __cplusplus
extern "C" {
#endif

struct unifrog_media_video_options {
   int preset;
   int disable_audio;
   int force_audio;
   int force_native;
   int force_hcplayer;
};

int unifrog_media_play_video_ex(const char *path,
   const struct unifrog_media_video_options *options);
int unifrog_media_play_video(const char *path);

#ifdef __cplusplus
}
#endif

#endif
