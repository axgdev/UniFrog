#ifndef UNIFROG_FRONTEND_CONFIG_H
#define UNIFROG_FRONTEND_CONFIG_H

#include <stddef.h>

#include <unifrog/libretro_host.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Scoped config is RAM-resident; raise this only with a size check. */
#define UNIFROG_FRONTEND_SCOPED_CONFIG_MAX 56u
#define UNIFROG_FRONTEND_CONFIG_TARGET_MAX 256u

enum unifrog_frontend_config_scope {
   UNIFROG_FRONTEND_CONFIG_CORE = 0,
   UNIFROG_FRONTEND_CONFIG_ROM,
};

struct unifrog_frontend_scoped_options {
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
   int rtc_offset_minutes;
   unsigned state_slot;
   char core_id[sizeof(((struct unifrog_libretro_run_options *)0)->core_id)];
};

struct unifrog_frontend_scoped_config {
   enum unifrog_frontend_config_scope scope;
   char target[UNIFROG_FRONTEND_CONFIG_TARGET_MAX];
   struct unifrog_frontend_scoped_options options;
   unsigned option_mask;
};

struct unifrog_frontend_config {
   struct unifrog_frontend_scoped_config
      entries[UNIFROG_FRONTEND_SCOPED_CONFIG_MAX];
   unsigned count;
   unsigned overflowed;
};

void unifrog_frontend_config_init(struct unifrog_frontend_config *config);
int unifrog_frontend_config_parse_entry(
   struct unifrog_frontend_config *config, const char *section,
   const char *key, const char *value);
int unifrog_frontend_config_load(struct unifrog_frontend_config *config,
   const char *path, unsigned *error_count);
void unifrog_frontend_config_apply(const struct unifrog_frontend_config *config,
   const char *initial_core, const char *path,
   struct unifrog_libretro_run_options *options);

#ifdef __cplusplus
}
#endif

#endif
