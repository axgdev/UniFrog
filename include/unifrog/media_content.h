#ifndef UNIFROG_MEDIA_CONTENT_H
#define UNIFROG_MEDIA_CONTENT_H

#ifdef __cplusplus
extern "C" {
#endif

int unifrog_media_path_is_supported(const char *path);
int unifrog_media_path_is_audio(const char *path);
int unifrog_media_path_is_image(const char *path);
int unifrog_media_path_is_wav(const char *path);
int unifrog_media_path_is_mp3(const char *path);
int unifrog_media_path_is_aac(const char *path);
int unifrog_media_path_is_flac(const char *path);
int unifrog_media_path_is_ogg(const char *path);

#ifdef __cplusplus
}
#endif

#endif
