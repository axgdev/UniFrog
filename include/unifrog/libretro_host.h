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

enum unifrog_libretro_framebuffer_format {
   UNIFROG_LIBRETRO_FB_RGB565 = 0,
   UNIFROG_LIBRETRO_FB_XRGB8888 = 1,
};

enum unifrog_libretro_input_profile {
   UNIFROG_LIBRETRO_INPUT_DEFAULT = 0,
   UNIFROG_LIBRETRO_INPUT_RETROARCH = 1,
   UNIFROG_LIBRETRO_INPUT_GENESIS = 2,
   UNIFROG_LIBRETRO_INPUT_SWAP_AB = 3,
   UNIFROG_LIBRETRO_INPUT_SWAP_XY = 4,
};

struct unifrog_libretro_run_options {
   int audio_enabled;
   unsigned audio_gain;
   unsigned scpu_mhz;
   int ge_clock;
   int backlight_level;
   int frameskip;
   int display_mode;
   int framebuffer_format;
   int input_profile;
   int state_auto_load;
   int state_auto_save;
   unsigned state_slot;
   unsigned max_frames;
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
int unifrog_libretro_recover_saved_state(void);

#ifdef __cplusplus
}
#endif

#endif
