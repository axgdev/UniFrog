#ifndef UNIFROG_RUNTIME_H
#define UNIFROG_RUNTIME_H

#define UNIFROG_API_VERSION 1u
#define UNIFROG_SCREEN_WIDTH 320u
#define UNIFROG_SCREEN_HEIGHT 240u
#define UNIFROG_VIDEO_WIDTH 640u
#define UNIFROG_VIDEO_HEIGHT 480u
#define UNIFROG_SD_ROOT "/media/mmcblk0"

#ifdef __cplusplus
extern "C" {
#endif

const char *unifrog_runtime_name(void);
unsigned unifrog_runtime_api_version(void);

#ifdef __cplusplus
}
#endif

#endif
