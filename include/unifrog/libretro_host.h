#ifndef UNIFROG_LIBRETRO_HOST_H
#define UNIFROG_LIBRETRO_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

enum unifrog_libretro_frameskip {
   UNIFROG_LIBRETRO_FRAMESKIP_OFF = 0,
   UNIFROG_LIBRETRO_FRAMESKIP_AUTO = 1,
   UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1 = 2,
   UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2 = 3,
};

enum unifrog_libretro_display_mode {
   UNIFROG_LIBRETRO_DISPLAY_FIT = 0,
   UNIFROG_LIBRETRO_DISPLAY_STRETCH = 1,
   UNIFROG_LIBRETRO_DISPLAY_ORIGINAL = 2,
};

struct unifrog_libretro_run_options {
   int audio_enabled;
   unsigned audio_gain;
   unsigned scpu_mhz;
   int ge_clock;
   int backlight_level;
   int frameskip;
   int display_mode;
   char sd_read_profile[16];
   char core_id[24];
   char core_path[256];
};

void unifrog_libretro_run_options_init(
   struct unifrog_libretro_run_options *options);
int unifrog_libretro_run_gambatte_ex(const char *path,
   const struct unifrog_libretro_run_options *options);
int unifrog_libretro_run_gpsp_ex(const char *path,
   const struct unifrog_libretro_run_options *options);
int unifrog_libretro_run_path_ex(const char *path,
   const struct unifrog_libretro_run_options *options);
int unifrog_libretro_run_gambatte(const char *path);
int unifrog_libretro_run_gpsp(const char *path);
int unifrog_libretro_run_path(const char *path);

#ifdef __cplusplus
}
#endif

#endif
