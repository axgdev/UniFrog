#ifndef UNIFROG_FRONTEND_SERVICES_H
#define UNIFROG_FRONTEND_SERVICES_H

#include <stddef.h>

#include <unifrog/libretro_host.h>
#include <unifrog/media.h>

#ifdef __cplusplus
extern "C" {
#endif

struct unifrog_frontend_launch_services {
   int (*run_game)(const char *path,
      const struct unifrog_libretro_run_options *options);
   int (*play_media)(const char *path,
      const struct unifrog_media_video_options *options);
   int (*run_reader)(const char *path);
   int (*run_script)(const char *path);
   int (*run_audio_diagnostics)(char *summary, size_t summary_size,
      unifrog_media_progress_cb progress, void *userdata);
   int (*create_bug_report)(char *output_path, size_t output_path_size,
      char *summary, size_t summary_size);
};

const struct unifrog_frontend_launch_services *
unifrog_frontend_launch_services_default(void);

#ifdef __cplusplus
}
#endif

#endif
