#ifndef UNIFROG_LIBRETRO_SESSION_H
#define UNIFROG_LIBRETRO_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include <unifrog/surface_alloc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_LIBRETRO_SESSION_STATE_SLOTS 10u
#define UNIFROG_LIBRETRO_SESSION_OPTION_MAX 32u
#define UNIFROG_LIBRETRO_SESSION_OPTION_LABEL_MAX 64u
#define UNIFROG_LIBRETRO_SESSION_OPTION_VALUE_MAX 64u
#define UNIFROG_LIBRETRO_SESSION_STATUS_MAX 96u

enum unifrog_libretro_session_adjustment {
   UNIFROG_LIBRETRO_SESSION_FAST_FORWARD = 0,
   UNIFROG_LIBRETRO_SESSION_FRAMESKIP,
   UNIFROG_LIBRETRO_SESSION_AUDIO,
   UNIFROG_LIBRETRO_SESSION_DISPLAY,
   UNIFROG_LIBRETRO_SESSION_INPUT_PROFILE,
   UNIFROG_LIBRETRO_SESSION_CPU,
   UNIFROG_LIBRETRO_SESSION_GE,
   UNIFROG_LIBRETRO_SESSION_BACKLIGHT,
   UNIFROG_LIBRETRO_SESSION_RTC_OFFSET,
};

enum unifrog_libretro_session_state_action {
   UNIFROG_LIBRETRO_SESSION_STATE_SAVE = 0,
   UNIFROG_LIBRETRO_SESSION_STATE_LOAD,
};

enum unifrog_libretro_session_scope {
   UNIFROG_LIBRETRO_SESSION_SCOPE_CORE = 0,
   UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT,
};

struct unifrog_libretro_session_snapshot {
   unsigned state_slot;
   unsigned fast_forward_multiplier;
   unsigned scpu_mhz;
   unsigned backlight;
   int frameskip;
   int audio_enabled;
   int display_mode;
   int input_profile;
   int ge_clock;
   int rtc_offset_minutes;
   char frameskip_label[16];
   char display_label[16];
   char input_profile_label[24];
   char ge_label[16];
   char status[UNIFROG_LIBRETRO_SESSION_STATUS_MAX];
};

struct unifrog_libretro_session_option {
   char label[UNIFROG_LIBRETRO_SESSION_OPTION_LABEL_MAX];
   char value[UNIFROG_LIBRETRO_SESSION_OPTION_VALUE_MAX];
};

int unifrog_libretro_session_menu_begin(const void *core,
   const char *content_path);
int unifrog_libretro_session_menu_finish(int return_to_frontend);
void unifrog_libretro_session_snapshot(
   struct unifrog_libretro_session_snapshot *snapshot);
int unifrog_libretro_session_adjust(
   enum unifrog_libretro_session_adjustment adjustment, int delta);
unsigned unifrog_libretro_session_option_count(void);
int unifrog_libretro_session_option_get(unsigned index,
   struct unifrog_libretro_session_option *option);
int unifrog_libretro_session_option_adjust(unsigned index, int delta);
int unifrog_libretro_session_state(
   enum unifrog_libretro_session_state_action action, unsigned slot);
int unifrog_libretro_session_save_settings(
   enum unifrog_libretro_session_scope scope);
int unifrog_libretro_session_clear_settings(
   enum unifrog_libretro_session_scope scope);
int unifrog_libretro_session_save_core_options(
   enum unifrog_libretro_session_scope scope);
int unifrog_libretro_session_clear_core_options(
   enum unifrog_libretro_session_scope scope);
void unifrog_libretro_session_status(const char *status);
int unifrog_libretro_session_surface(struct unifrog_surface *surface,
   unsigned *buffer);
void unifrog_libretro_session_present(unsigned buffer);
void unifrog_libretro_session_note_buttons(uint32_t buttons);

#ifdef __cplusplus
}
#endif

#endif
