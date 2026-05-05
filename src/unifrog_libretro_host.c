#include <unifrog/libretro_host.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <js2300/js2300.h>

#include <kernel/lib/zlib.h>

#include <unifrog/audio.h>
#include <unifrog/abi.h>
#include <unifrog/backlight.h>
#include <unifrog/fb.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/log.h>
#include <unifrog/panic.h>
#include <unifrog/perf.h>
#include <unifrog/presenter.h>
#include <unifrog/scpu.h>
#include <unifrog/text.h>

#include <libretro.h>

#include <third_party/lz4/lz4frame.h>
#include <zstd.h>

#include "unifrog_core_module_loader.h"
#include "unifrog_mips_call.h"

#define printf unifrog_log

#define DEFAULT_SAMPLE_RATE 32000u
#define LIBRETRO_AUDIO_OUTPUT_RATE 32000u
#define LIBRETRO_AUDIO_DEFAULT_GAIN 2u
#define LIBRETRO_AUDIO_MAX_GAIN 12u
#define LIBRETRO_AUDIO_VOLUME 75u
#define LIBRETRO_AUDIO_ROUTE "sf2000_stereo_safe"
#define LIBRETRO_AUDIO_GATE_OPEN_LEVEL 256u
#define LIBRETRO_AUDIO_GATE_CLOSE_LEVEL 96u
#define LIBRETRO_AUDIO_GATE_CLOSE_BATCHES 45u
#define LIBRETRO_AUDIO_CHANNELS 2u
#define LIBRETRO_AUDIO_PERIOD_BYTES 512u
#define LIBRETRO_AUDIO_PERIODS 8u
#define LIBRETRO_AUDIO_WRITE_CHUNK_FRAMES 256u
#define LIBRETRO_AUDIO_WRITE_ATTEMPTS 2u
#define LIBRETRO_AUDIO_WRITE_POLL_MS 1u
#define LIBRETRO_WIRELESS_POLL_DIVISOR 2u
#define LIBRETRO_PERF_REPORT_FRAMES 600u
/* Give slow cores time to catch up before resetting the pacing deadline. */
#define LIBRETRO_PACE_RESET_LATE_FRAMES 12u
#define LIBRETRO_COUNT_CALIBRATE_US 20000u
#define LIBRETRO_LOG_AUTO_FLUSH_BYTES (64u * 1024u)
#define LIBRETRO_LOAD_LOG_PERCENT_STEP 10u
#define LIBRETRO_WATCHDOG_TICKS pdMS_TO_TICKS(100)
#define LIBRETRO_WATCHDOG_LOAD_STALL_POLLS 600u
#define LIBRETRO_WATCHDOG_RUN_STALL_POLLS 100u
#define LIBRETRO_WATCHDOG_PHASE_LOAD 1u
#define LIBRETRO_WATCHDOG_PHASE_RUN 2u
#define LIBRETRO_SAVE_DIR "/media/mmcblk0/unifrog/saves"
#define LIBRETRO_CONTENT_CACHE_DIR "/media/mmcblk0/unifrog/cache"
#define LIBRETRO_QUICK_JS_ROOT "/media/mmcblk0/unifrog"
#define LIBRETRO_QUICK_JS_ENTRY "quick-menu.js"
#define LIBRETRO_QUICK_JS_HEAP_BYTES (2u * 1024u * 1024u)
#define LIBRETRO_QUICK_JS_STACK_BYTES (96u * 1024u)
#define LIBRETRO_QUICK_JS_BYTECODE_BYTES (512u * 1024u)
#define LIBRETRO_MEMORY_AUTOSAVE_FRAMES 600u
#define LIBRETRO_MEMORY_FILE_MAX (16u * 1024u * 1024u)
#define LIBRETRO_STATE_FILE_MAX (16u * 1024u * 1024u)
#define LIBRETRO_STATE_SLOT_COUNT 10u
#define LIBRETRO_ZIP_MAX_UNCOMPRESSED (64u * 1024u * 1024u)
#define LIBRETRO_COMPRESSED_MAX_INPUT (64u * 1024u * 1024u)
#define LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED LIBRETRO_ZIP_MAX_UNCOMPRESSED
#define LIBRETRO_COMPRESSED_GROW_INITIAL (2u * 1024u * 1024u)
#define LIBRETRO_COMPRESSED_STREAM_FIRST_INPUT (4u * 1024u * 1024u)
#define LIBRETRO_CONTENT_READ_CHUNK (512u * 1024u)
#define LIBRETRO_CONTENT_STREAM_IN (64u * 1024u)
#define LIBRETRO_CONTENT_STREAM_OUT (256u * 1024u)
#define LIBRETRO_FS_PROBE_MIN_SIZE (512u * 1024u)
#define LIBRETRO_FS_PROBE_SAMPLE 64u
#define LIBRETRO_FS_PROBE_MAX_LOGS 12u
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#ifndef UNIFROG_GIT_COMMIT
#define UNIFROG_GIT_COMMIT "unknown"
#endif

#ifndef UNIFROG_GIT_DIRTY
#define UNIFROG_GIT_DIRTY 1
#endif

#ifndef UNIFROG_SDK_GIT_COMMIT
#define UNIFROG_SDK_GIT_COMMIT "unknown"
#endif

#ifndef UNIFROG_CORES_GIT_COMMIT
#define UNIFROG_CORES_GIT_COMMIT "unknown"
#endif

#define ROM_ALLOC_MAGIC 0x5546524du

enum rom_alloc_kind {
   ROM_ALLOC_HEAP = 1,
   ROM_ALLOC_APPMEM = 2,
};

struct rom_alloc_header {
   uint32_t magic;
   uint32_t kind;
   void *raw;
   size_t reserved_bytes;
   size_t payload_size;
};

enum quick_memory_slot {
   QUICK_MEMORY_SAVE_RAM = 0,
   QUICK_MEMORY_RTC,
   QUICK_MEMORY_COUNT,
};

enum quick_js_action {
   QUICK_JS_ACTION_RESUME = 0,
   QUICK_JS_ACTION_RETURN_MENU = 1,
};

struct quick_memory_file {
   unsigned id;
   const char *extension;
};

struct zip_rom_entry {
   uint32_t crc32;
   uint32_t compressed_size;
   uint32_t uncompressed_size;
   uint32_t local_offset;
   uint16_t flags;
   uint16_t method;
   char name[256];
};

void *__dso_handle = &__dso_handle;

extern char _gp[];
extern bool unifrog_libretro_environment_trampoline(unsigned cmd, void *data);
extern void unifrog_libretro_video_refresh_trampoline(const void *data,
   unsigned width, unsigned height, size_t pitch);
extern void unifrog_libretro_audio_sample_trampoline(int16_t left,
   int16_t right);
extern size_t unifrog_libretro_audio_batch_trampoline(const int16_t *data,
   size_t frames);
extern void unifrog_libretro_input_poll_trampoline(void);
extern int16_t unifrog_libretro_input_state_trampoline(unsigned port,
   unsigned device, unsigned index, unsigned id);
extern bool unifrog_libretro_rumble_trampoline(unsigned port,
   enum retro_rumble_effect effect, uint16_t strength);

#define UNIFROG_STRONG
#define UNIFROG_WEAK __attribute__((weak))

#define DECLARE_UNPREFIXED_LIBRETRO_API_ATTR(attr) \
extern void retro_set_environment(retro_environment_t cb) attr; \
extern void retro_set_video_refresh(retro_video_refresh_t cb) attr; \
extern void retro_set_audio_sample(retro_audio_sample_t cb) attr; \
extern void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) attr; \
extern void retro_set_input_poll(retro_input_poll_t cb) attr; \
extern void retro_set_input_state(retro_input_state_t cb) attr; \
extern void retro_init(void) attr; \
extern void retro_deinit(void) attr; \
extern unsigned retro_api_version(void) attr; \
extern void retro_get_system_info(struct retro_system_info *info) attr; \
extern void retro_get_system_av_info(struct retro_system_av_info *info) attr; \
extern void retro_set_controller_port_device(unsigned port, unsigned device) attr; \
extern void retro_run(void) attr; \
extern void retro_unload_game(void) attr; \
extern bool retro_load_game(const struct retro_game_info *game) attr; \
extern unsigned retro_get_region(void) attr; \
extern size_t retro_serialize_size(void) attr; \
extern bool retro_serialize(void *data, size_t size) attr; \
extern bool retro_unserialize(const void *data, size_t size) attr; \
extern void *retro_get_memory_data(unsigned id) attr; \
extern size_t retro_get_memory_size(unsigned id) attr; \
extern void retro_cheat_reset(void) attr; \
extern void retro_cheat_set(unsigned index, bool enabled, const char *code) attr

#define DECLARE_PREFIXED_LIBRETRO_API_ATTR(prefix, attr) \
extern void prefix##_retro_set_environment(retro_environment_t cb) attr; \
extern void prefix##_retro_set_video_refresh(retro_video_refresh_t cb) attr; \
extern void prefix##_retro_set_audio_sample(retro_audio_sample_t cb) attr; \
extern void prefix##_retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) attr; \
extern void prefix##_retro_set_input_poll(retro_input_poll_t cb) attr; \
extern void prefix##_retro_set_input_state(retro_input_state_t cb) attr; \
extern void prefix##_retro_init(void) attr; \
extern void prefix##_retro_deinit(void) attr; \
extern unsigned prefix##_retro_api_version(void) attr; \
extern void prefix##_retro_get_system_info(struct retro_system_info *info) attr; \
extern void prefix##_retro_get_system_av_info(struct retro_system_av_info *info) attr; \
extern void prefix##_retro_set_controller_port_device(unsigned port, unsigned device) attr; \
extern void prefix##_retro_run(void) attr; \
extern void prefix##_retro_unload_game(void) attr; \
extern bool prefix##_retro_load_game(const struct retro_game_info *game) attr; \
extern unsigned prefix##_retro_get_region(void) attr; \
extern size_t prefix##_retro_serialize_size(void) attr; \
extern bool prefix##_retro_serialize(void *data, size_t size) attr; \
extern bool prefix##_retro_unserialize(const void *data, size_t size) attr; \
extern void *prefix##_retro_get_memory_data(unsigned id) attr; \
extern size_t prefix##_retro_get_memory_size(unsigned id) attr; \
extern void prefix##_retro_cheat_reset(void) attr; \
extern void prefix##_retro_cheat_set(unsigned index, bool enabled, \
   const char *code) attr

DECLARE_UNPREFIXED_LIBRETRO_API_ATTR(UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(gpsp, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(picodrive, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(snes9x2005, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(snes9x2002, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(quicknes, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(fceumm, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(gearboy, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(pce_fast, UNIFROG_WEAK);

struct libretro_core_api {
   const char *id;
   uintptr_t call_gp;
   int external;
   void (*set_environment)(retro_environment_t cb);
   void (*set_video_refresh)(retro_video_refresh_t cb);
   void (*set_audio_sample)(retro_audio_sample_t cb);
   void (*set_audio_sample_batch)(retro_audio_sample_batch_t cb);
   void (*set_input_poll)(retro_input_poll_t cb);
   void (*set_input_state)(retro_input_state_t cb);
   void (*init)(void);
   void (*deinit)(void);
   unsigned (*api_version)(void);
   void (*get_system_info)(struct retro_system_info *info);
   void (*get_system_av_info)(struct retro_system_av_info *info);
   void (*set_controller_port_device)(unsigned port, unsigned device);
   void (*run)(void);
   void (*unload_game)(void);
   bool (*load_game)(const struct retro_game_info *game);
   unsigned (*get_region)(void);
   size_t (*serialize_size)(void);
   bool (*serialize)(void *data, size_t size);
   bool (*unserialize)(const void *data, size_t size);
   void *(*get_memory_data)(unsigned id);
   size_t (*get_memory_size)(unsigned id);
   void (*cheat_reset)(void);
   void (*cheat_set)(unsigned index, bool enabled, const char *code);
};

#define PREFIXED_LIBRETRO_CORE(id_literal, prefix) { \
   id_literal, 0, 0, \
   prefix##_retro_set_environment, \
   prefix##_retro_set_video_refresh, \
   prefix##_retro_set_audio_sample, \
   prefix##_retro_set_audio_sample_batch, \
   prefix##_retro_set_input_poll, \
   prefix##_retro_set_input_state, \
   prefix##_retro_init, \
   prefix##_retro_deinit, \
   prefix##_retro_api_version, \
   prefix##_retro_get_system_info, \
   prefix##_retro_get_system_av_info, \
   prefix##_retro_set_controller_port_device, \
   prefix##_retro_run, \
   prefix##_retro_unload_game, \
   prefix##_retro_load_game, \
   prefix##_retro_get_region, \
   prefix##_retro_serialize_size, \
   prefix##_retro_serialize, \
   prefix##_retro_unserialize, \
   prefix##_retro_get_memory_data, \
   prefix##_retro_get_memory_size, \
   prefix##_retro_cheat_reset, \
   prefix##_retro_cheat_set, \
}

static const struct libretro_core_api gambatte_core = {
   "gambatte",
   0,
   0,
   retro_set_environment,
   retro_set_video_refresh,
   retro_set_audio_sample,
   retro_set_audio_sample_batch,
   retro_set_input_poll,
   retro_set_input_state,
   retro_init,
   retro_deinit,
   retro_api_version,
   retro_get_system_info,
   retro_get_system_av_info,
   retro_set_controller_port_device,
   retro_run,
   retro_unload_game,
   retro_load_game,
   retro_get_region,
   retro_serialize_size,
   retro_serialize,
   retro_unserialize,
   retro_get_memory_data,
   retro_get_memory_size,
   retro_cheat_reset,
   retro_cheat_set,
};

static const struct libretro_core_api gpsp_core =
   PREFIXED_LIBRETRO_CORE("gpsp", gpsp);
static const struct libretro_core_api picodrive_core =
   PREFIXED_LIBRETRO_CORE("picodrive", picodrive);
static const struct libretro_core_api snes9x2005_core =
   PREFIXED_LIBRETRO_CORE("snes9x2005", snes9x2005);
static const struct libretro_core_api snes9x2002_core =
   PREFIXED_LIBRETRO_CORE("snes9x2002", snes9x2002);
static const struct libretro_core_api quicknes_core =
   PREFIXED_LIBRETRO_CORE("quicknes", quicknes);
static const struct libretro_core_api fceumm_core =
   PREFIXED_LIBRETRO_CORE("fceumm", fceumm);
static const struct libretro_core_api gearboy_core =
   PREFIXED_LIBRETRO_CORE("gearboy", gearboy);
static const struct libretro_core_api pce_fast_core =
   PREFIXED_LIBRETRO_CORE("pce-fast", pce_fast);

static int libretro_core_available(const struct libretro_core_api *core)
{
   return core &&
      core->set_environment &&
      core->set_video_refresh &&
      core->set_audio_sample &&
      core->set_audio_sample_batch &&
      core->set_input_poll &&
      core->set_input_state &&
      core->init &&
      core->deinit &&
      core->api_version &&
      core->get_system_info &&
      core->get_system_av_info &&
      core->set_controller_port_device &&
      core->run &&
      core->unload_game &&
      core->load_game &&
      core->get_region;
}

static const struct libretro_core_api *libretro_core_if_available(
   const struct libretro_core_api *core)
{
   return libretro_core_available(core) ? core : NULL;
}

struct libretro_host {
   struct unifrog_presenter presenter;
   struct unifrog_audio audio;
   struct unifrog_libretro_run_options options;
   struct unifrog_scpu_clock scpu_restore;
   struct retro_audio_buffer_status_callback audio_status;
   const char *core_id;
   uintptr_t core_gp;
   uint32_t buttons;
   enum retro_pixel_format pixel_format;
   enum unifrog_ge_clock ge_clock;
   int display_mode;
   unsigned video_width;
   unsigned video_height;
   unsigned video_pitch;
   uint16_t *video_convert_buffer;
   size_t video_convert_pixels;
   unsigned fps;
   int presenter_open;
   int audio_open;
   int audio_enabled;
   int video_seen;
   unsigned run_frames;
   unsigned video_frames;
   unsigned audio_batches;
   unsigned audio_frames;
   unsigned audio_input_rate;
   unsigned audio_output_rate;
   unsigned audio_resample_accum;
   unsigned audio_failures;
   unsigned audio_quiet_batches;
   unsigned audio_gain;
   unsigned audio_peak_max;
   unsigned audio_clip_count;
   unsigned audio_write_count;
   uint64_t audio_write_total_count;
   unsigned audio_write_max_count;
   unsigned audio_status_count;
   unsigned audio_status_active_count;
   unsigned audio_status_underrun_count;
   unsigned audio_status_occupancy_total;
   unsigned audio_status_occupancy_min;
   unsigned audio_status_occupancy_max;
   uint64_t run_total_count;
   uint64_t active_total_count;
   unsigned run_max_count;
   unsigned active_max_count;
   unsigned run_report_frames;
   unsigned video_report_frames;
   unsigned audio_report_batches;
   unsigned audio_report_frames;
   unsigned audio_report_failures;
   unsigned slow_frames;
   unsigned frame_budget_count;
   unsigned count_hz_est;
   unsigned count_hz_calibrated;
   unsigned scpu_mhz_est;
   unsigned frame_period_us;
   uint64_t frame_deadline_us;
   uint64_t report_start_us;
   unsigned pace_wait_frames;
   uint64_t pace_wait_total_us;
   unsigned pace_wait_max_us;
   unsigned pace_late_frames;
   unsigned pace_reset_frames;
   int audio_gate_open;
   int audio_status_enabled;
   int scpu_restore_valid;
   int scpu_apply_ret;
   unsigned scpu_target_mhz;
   int backlight_restore_valid;
   int backlight_apply_ret;
   unsigned backlight_restore_level;
   struct unifrog_fb loading_fb;
   int loading_open;
   unsigned loading_percent;
   unsigned loading_log_stage_hash;
   unsigned loading_log_percent_bucket;
   uint32_t memory_hash[QUICK_MEMORY_COUNT];
   int memory_hash_valid[QUICK_MEMORY_COUNT];
   unsigned memory_autosave_frame;
   unsigned memory_autosaves;
   int fast_forward;
   int content_alloc_appmem;
   const struct libretro_core_api *quick_core;
   const char *quick_rom_path;
   unsigned quick_state_slot;
   int quick_js_action;
   int quick_js_frame_open;
   unsigned quick_js_draw_buffer;
   char quick_status[96];
};

static struct libretro_host host;

static int16_t audio_mix_buffer[2048 * 2];
static int16_t audio_silence_buffer[384 * 2];
static volatile unsigned watchdog_active;
static volatile unsigned watchdog_phase;
static volatile unsigned watchdog_marker;
static volatile unsigned watchdog_heartbeat;

static void loading_draw(const char *title, const char *detail,
   unsigned percent);
static uint64_t host_time_us(void);
static unsigned host_elapsed_ms(uint64_t start_us, uint64_t end_us);
static unsigned host_compute_frame_budget(unsigned fps, unsigned *scpu_mhz,
   unsigned *count_hz, unsigned *count_hz_calibrated);
static int read_file_aligned(FILE *file, const char *path,
   uint8_t **out_data, size_t *out_size, const char *label);
static int content_cache_file_valid(const char *path, size_t expected_size);
static int content_cache_write_buffer(const char *path, const void *data,
   size_t size);
static int exit_combo_down(void);
static int quick_js_run(const struct libretro_core_api *core,
   const char *rom_path);
static void host_pace_begin(void);

static const struct quick_memory_file quick_memory_files[] = {
   { RETRO_MEMORY_SAVE_RAM, "srm" },
   { RETRO_MEMORY_RTC, "rtc" },
};

static const unsigned quick_backlight_levels[] = {
   1, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
};

static const unsigned quick_scpu_mhz_options[] = {
   198, 297, 396, 594, 702, 756, 810, 864, 918,
};

void unifrog_libretro_run_options_init(
   struct unifrog_libretro_run_options *options)
{
   if (!options)
      return;

   options->audio_enabled = 1;
   options->audio_gain = LIBRETRO_AUDIO_DEFAULT_GAIN;
   options->scpu_mhz = 0;
   options->ge_clock = -1;
   options->backlight_level = -1;
   options->frameskip = UNIFROG_LIBRETRO_FRAMESKIP_OFF;
   options->display_mode = UNIFROG_LIBRETRO_DISPLAY_FIT;
   options->core_id[0] = '\0';
   options->core_path[0] = '\0';
}

static int valid_scpu_mhz(unsigned mhz)
{
   return mhz == 0 || mhz == 198 || mhz == 297 || mhz == 396 ||
      mhz == 594 || mhz == 702 || mhz == 756 || mhz == 808 ||
      mhz == 810 || mhz == 864 || mhz == 918;
}

static enum unifrog_ge_clock sanitize_ge_clock(int clock)
{
   switch (clock) {
   case UNIFROG_GE_CLOCK_198MHZ:
   case UNIFROG_GE_CLOCK_148MHZ:
   case UNIFROG_GE_CLOCK_225MHZ:
   case UNIFROG_GE_CLOCK_238MHZ:
      return (enum unifrog_ge_clock)clock;
   default:
      return UNIFROG_GE_CLOCK_FAST;
   }
}

static int sanitize_frameskip(int frameskip)
{
   switch (frameskip) {
   case UNIFROG_LIBRETRO_FRAMESKIP_OFF:
   case UNIFROG_LIBRETRO_FRAMESKIP_AUTO:
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1:
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2:
      return frameskip;
   default:
      return UNIFROG_LIBRETRO_FRAMESKIP_OFF;
   }
}

static int sanitize_display_mode(int display_mode)
{
   switch (display_mode) {
   case UNIFROG_LIBRETRO_DISPLAY_FIT:
   case UNIFROG_LIBRETRO_DISPLAY_STRETCH:
   case UNIFROG_LIBRETRO_DISPLAY_ORIGINAL:
      return display_mode;
   default:
      return UNIFROG_LIBRETRO_DISPLAY_FIT;
   }
}

static const char *display_mode_label(int display_mode)
{
   switch (display_mode) {
   case UNIFROG_LIBRETRO_DISPLAY_STRETCH:
      return "STRETCH";
   case UNIFROG_LIBRETRO_DISPLAY_ORIGINAL:
      return "ORIGINAL";
   default:
      return "FIT";
   }
}

static unsigned present_flags_for_display_mode(int display_mode)
{
   switch (display_mode) {
   case UNIFROG_LIBRETRO_DISPLAY_STRETCH:
      return 0;
   case UNIFROG_LIBRETRO_DISPLAY_ORIGINAL:
      return UNIFROG_PRESENT_KEEP_SIZE;
   default:
      return UNIFROG_PRESENT_KEEP_ASPECT;
   }
}

static void host_configure_options(
   const struct unifrog_libretro_run_options *options)
{
   unifrog_libretro_run_options_init(&host.options);
   if (options)
      host.options = *options;

   host.options.audio_enabled = host.options.audio_enabled == 0 ? 0 : 1;
   if (host.options.audio_gain > LIBRETRO_AUDIO_MAX_GAIN)
      host.options.audio_gain = LIBRETRO_AUDIO_DEFAULT_GAIN;
   if (!valid_scpu_mhz(host.options.scpu_mhz))
      host.options.scpu_mhz = 0;
   host.ge_clock = sanitize_ge_clock(host.options.ge_clock);
   host.options.ge_clock = (int)host.ge_clock;
   if (host.options.backlight_level > 100)
      host.options.backlight_level = 100;
   host.options.frameskip = sanitize_frameskip(host.options.frameskip);
   host.options.display_mode = sanitize_display_mode(host.options.display_mode);
   host.options.core_id[sizeof(host.options.core_id) - 1] = '\0';

   host.audio_enabled = host.options.audio_enabled;
   host.audio_gain = host.options.audio_gain;
   host.scpu_target_mhz = host.options.scpu_mhz;
   host.display_mode = host.options.display_mode;
}

static uintptr_t host_read_gp(void)
{
   uintptr_t gp;

   __asm__ volatile("move %0, $28" : "=r"(gp));
   return gp;
}

static void host_restore_gp(uintptr_t gp)
{
   __asm__ volatile("move $28, %0" :: "r"(gp) : "memory");
}

static uintptr_t host_expected_gp(void)
{
   return (uintptr_t)_gp;
}

static void host_force_expected_gp(void)
{
   host_restore_gp(host_expected_gp());
}

static uintptr_t core_call_gp(const struct libretro_core_api *core)
{
   return core && core->call_gp ? core->call_gp : host_expected_gp();
}

#define CORE_CALL0_VOID(core, fn) do { \
   (void)unifrog_mips_call0(core_call_gp(core), (uintptr_t)(fn)); \
   host_force_expected_gp(); \
} while (0)

#define CORE_CALL0_RET(core, fn) ({ \
   __typeof__((fn)()) ret__ = (__typeof__((fn)())) \
      unifrog_mips_call0(core_call_gp(core), (uintptr_t)(fn)); \
   host_force_expected_gp(); \
   ret__; \
})

#define CORE_CALL1_VOID(core, fn, a0) do { \
   (void)unifrog_mips_call1(core_call_gp(core), (uintptr_t)(fn), \
      (uintptr_t)(a0)); \
   host_force_expected_gp(); \
} while (0)

#define CORE_CALL1_RET(core, fn, a0) ({ \
   __typeof__((fn)(a0)) ret__ = (__typeof__((fn)(a0))) \
      unifrog_mips_call1(core_call_gp(core), (uintptr_t)(fn), \
         (uintptr_t)(a0)); \
   host_force_expected_gp(); \
   ret__; \
})

#define CORE_CALL2_VOID(core, fn, a0, a1) do { \
   (void)unifrog_mips_call2(core_call_gp(core), (uintptr_t)(fn), \
      (uintptr_t)(a0), (uintptr_t)(a1)); \
   host_force_expected_gp(); \
} while (0)

#define CORE_CALL2_RET(core, fn, a0, a1) ({ \
   __typeof__((fn)(a0, a1)) ret__ = (__typeof__((fn)(a0, a1))) \
      unifrog_mips_call2(core_call_gp(core), (uintptr_t)(fn), \
         (uintptr_t)(a0), (uintptr_t)(a1)); \
   host_force_expected_gp(); \
   ret__; \
})

#define CORE_CALL3_VOID(core, fn, a0, a1, a2) do { \
   (void)unifrog_mips_call3(core_call_gp(core), (uintptr_t)(fn), \
      (uintptr_t)(a0), (uintptr_t)(a1), (uintptr_t)(a2)); \
   host_force_expected_gp(); \
} while (0)

static void host_apply_runtime_options(void)
{
   if (host.options.backlight_level >= 0) {
      host.backlight_restore_valid =
         unifrog_backlight_get(&host.backlight_restore_level) == 0;
      host.backlight_apply_ret =
         unifrog_backlight_set((unsigned)host.options.backlight_level);
      printf("unifrog libretro backlight target=%d ret=%d restore_valid=%d restore=%u\n",
         host.options.backlight_level, host.backlight_apply_ret,
         host.backlight_restore_valid, host.backlight_restore_level);
   }

   if (host.scpu_target_mhz) {
      host.scpu_restore_valid =
         unifrog_scpu_capture(&host.scpu_restore) == 0 &&
         host.scpu_restore.valid;
      printf("unifrog libretro scpu before target=%u restore_valid=%d current=%u selector=%u pll=%u\n",
         host.scpu_target_mhz, host.scpu_restore_valid,
         host.scpu_restore.mhz, host.scpu_restore.selector,
         host.scpu_restore.pll_enabled);
      (void)unifrog_log_flush();
      host.scpu_apply_ret = unifrog_scpu_apply_mhz(host.scpu_target_mhz);
      printf("unifrog libretro scpu after target=%u ret=%d current=%u restore_valid=%d\n",
         host.scpu_target_mhz, host.scpu_apply_ret,
         unifrog_scpu_current_mhz(), host.scpu_restore_valid);
   }
}

static void host_restore_runtime_options(void)
{
   if (host.scpu_restore_valid) {
      int ret = unifrog_scpu_restore(&host.scpu_restore);

      printf("unifrog libretro scpu restore ret=%d current=%u target=%u original=%u\n",
         ret, unifrog_scpu_current_mhz(), host.scpu_target_mhz,
         host.scpu_restore.mhz);
   }

   if (host.backlight_restore_valid) {
      int ret = unifrog_backlight_set(host.backlight_restore_level);

      printf("unifrog libretro backlight restore ret=%d level=%u target=%d\n",
         ret, host.backlight_restore_level, host.options.backlight_level);
   }
}

static void quick_sanitize_component(char *text)
{
   if (!text)
      return;

   for (; *text; text++) {
      char c = *text;

      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')
         continue;
      *text = '_';
   }
}

static void quick_save_component(const char *rom_path, char *out,
   size_t out_size)
{
   const char *base;
   char *dot;

   if (!out || out_size == 0)
      return;

   base = rom_path ? strrchr(rom_path, '/') : NULL;
   base = base ? base + 1 : rom_path;
   if (!base || !base[0])
      base = "game";
   snprintf(out, out_size, "%s", base);
   dot = strrchr(out, '.');
   if (dot && dot != out)
      *dot = '\0';
   quick_sanitize_component(out);
}

static void quick_memory_path(const struct libretro_core_api *core,
   const char *rom_path, const char *extension, char *out, size_t out_size)
{
   char name[80];

   if (!out || out_size == 0)
      return;
   quick_save_component(rom_path, name, sizeof(name));
   snprintf(out, out_size, "%s/%s-%s.%s", LIBRETRO_SAVE_DIR,
      core && core->id ? core->id : "core", name,
      extension && extension[0] ? extension : "sav");
}

static void quick_state_path(const struct libretro_core_api *core,
   const char *rom_path, unsigned slot, char *out, size_t out_size)
{
   char extension[16];

   if (slot >= LIBRETRO_STATE_SLOT_COUNT)
      slot = LIBRETRO_STATE_SLOT_COUNT - 1u;
   snprintf(extension, sizeof(extension), "state%u", slot);
   quick_memory_path(core, rom_path, extension, out, out_size);
}

static int quick_memory_data(const struct libretro_core_api *core,
   unsigned id, void **data, size_t *size)
{
   if (data)
      *data = NULL;
   if (size)
      *size = 0;
   if (!core || !core->get_memory_data || !core->get_memory_size)
      return -1;

   if (size)
      *size = CORE_CALL1_RET(core, core->get_memory_size, id);
   if (data)
      *data = CORE_CALL1_RET(core, core->get_memory_data, id);
   if (!data || !size || !*data || *size == 0 ||
       *size > LIBRETRO_MEMORY_FILE_MAX)
      return -1;
   return 0;
}

static int quick_memory_slot_for_id(unsigned id)
{
   for (unsigned i = 0; i < ARRAY_SIZE(quick_memory_files); i++) {
      if (quick_memory_files[i].id == id)
         return (int)i;
   }
   return -1;
}

static uint32_t quick_memory_hash(const void *data, size_t size)
{
   const uint8_t *bytes = (const uint8_t *)data;
   uint32_t hash = 2166136261u;

   for (size_t i = 0; i < size; i++) {
      hash ^= bytes[i];
      hash *= 16777619u;
   }
   return hash ? hash : 1u;
}

static int quick_note_memory_hash(const struct libretro_core_api *core,
   unsigned id)
{
   void *data;
   size_t size;
   int slot;

   if (quick_memory_data(core, id, &data, &size) != 0)
      return -1;
   slot = quick_memory_slot_for_id(id);
   if (slot < 0)
      return -1;
   host.memory_hash[slot] = quick_memory_hash(data, size);
   host.memory_hash_valid[slot] = 1;
   return 0;
}

static int quick_save_memory_file(const struct libretro_core_api *core,
   const char *rom_path, unsigned id, const char *extension, int manual)
{
   char path[160];
   FILE *file;
   void *data;
   size_t size;
   int ok;

   if (quick_memory_data(core, id, &data, &size) != 0) {
      if (manual)
         snprintf(host.quick_status, sizeof(host.quick_status),
            "SAVE RAM UNSUPPORTED");
      return -1;
   }

   (void)mkdir("/media/mmcblk0/unifrog", 0777);
   (void)mkdir(LIBRETRO_SAVE_DIR, 0777);
   quick_memory_path(core, rom_path, extension, path, sizeof(path));
   file = fopen(path, "wb");
   if (!file) {
      if (manual)
         snprintf(host.quick_status, sizeof(host.quick_status),
            "SAVE RAM OPEN FAILED");
      return -1;
   }
   ok = fwrite(data, 1, size, file) == size;
   fclose(file);
   if (manual)
      snprintf(host.quick_status, sizeof(host.quick_status),
         ok ? "SAVE RAM WRITTEN" : "SAVE RAM FAILED");
   printf("unifrog quick save_memory id=%u path=%s size=%u ok=%d manual=%d\n",
      id, path, (unsigned)size, ok, manual ? 1 : 0);
   if (ok)
      (void)quick_note_memory_hash(core, id);
   if (manual)
      (void)unifrog_log_flush_force();
   return ok ? 0 : -1;
}

static int quick_load_memory_file(const struct libretro_core_api *core,
   const char *rom_path, unsigned id, const char *extension, int manual)
{
   char path[160];
   FILE *file;
   void *data;
   size_t size;
   size_t read_size;
   int ok;

   if (quick_memory_data(core, id, &data, &size) != 0) {
      if (manual)
         snprintf(host.quick_status, sizeof(host.quick_status),
            "SAVE RAM UNSUPPORTED");
      return -1;
   }

   quick_memory_path(core, rom_path, extension, path, sizeof(path));
   file = fopen(path, "rb");
   if (!file) {
      if (manual)
         snprintf(host.quick_status, sizeof(host.quick_status),
            "NO SAVE RAM FILE");
      return -1;
   }
   read_size = fread(data, 1, size, file);
   ok = read_size == size && ferror(file) == 0;
   fclose(file);
   if (manual)
      snprintf(host.quick_status, sizeof(host.quick_status),
         ok ? "SAVE RAM LOADED" : "SAVE RAM READ FAILED");
   printf("unifrog quick load_memory id=%u path=%s size=%u read=%u ok=%d manual=%d\n",
      id, path, (unsigned)size, (unsigned)read_size, ok, manual ? 1 : 0);
   if (ok)
      (void)quick_note_memory_hash(core, id);
   if (manual)
      (void)unifrog_log_flush_force();
   return ok ? 0 : -1;
}

static void quick_load_all_memory_files(const struct libretro_core_api *core,
   const char *rom_path)
{
   (void)quick_load_memory_file(core, rom_path, RETRO_MEMORY_SAVE_RAM,
      "srm", 0);
   (void)quick_load_memory_file(core, rom_path, RETRO_MEMORY_RTC,
      "rtc", 0);
   for (unsigned i = 0; i < ARRAY_SIZE(quick_memory_files); i++)
      (void)quick_note_memory_hash(core, quick_memory_files[i].id);
}

static void quick_save_all_memory_files(const struct libretro_core_api *core,
   const char *rom_path)
{
   (void)quick_save_memory_file(core, rom_path, RETRO_MEMORY_SAVE_RAM,
      "srm", 0);
   (void)quick_save_memory_file(core, rom_path, RETRO_MEMORY_RTC,
      "rtc", 0);
}

static void quick_autosave_memory_files(const struct libretro_core_api *core,
   const char *rom_path)
{
   for (unsigned i = 0; i < ARRAY_SIZE(quick_memory_files); i++) {
      void *data;
      size_t size;
      uint32_t hash;

      if (quick_memory_data(core, quick_memory_files[i].id, &data, &size) != 0)
         continue;
      hash = quick_memory_hash(data, size);
      if (host.memory_hash_valid[i] && host.memory_hash[i] == hash)
         continue;
      if (quick_save_memory_file(core, rom_path, quick_memory_files[i].id,
          quick_memory_files[i].extension, 0) == 0) {
         host.memory_autosaves++;
         printf("unifrog quick autosave_memory id=%u frame=%u count=%u\n",
            quick_memory_files[i].id, host.run_frames,
            host.memory_autosaves);
      }
   }
}

static int quick_save_state_file(void)
{
   const struct libretro_core_api *core = host.quick_core;
   const char *rom_path = host.quick_rom_path;
   unsigned slot = host.quick_state_slot;
   char path[160];
   FILE *file = NULL;
   void *data = NULL;
   size_t size;
   int ok;
   int ret = -1;

   if (!core || !core->serialize_size || !core->serialize) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE UNSUPPORTED");
      return -1;
   }

   size = CORE_CALL0_RET(core, core->serialize_size);
   if (size == 0 || size > LIBRETRO_STATE_FILE_MAX) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         size == 0 ? "STATE UNSUPPORTED" : "STATE TOO LARGE");
      printf("unifrog quick save_state unsupported core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)unifrog_log_flush_force();
      return -1;
   }

   data = malloc(size);
   if (!data) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE OUT OF MEMORY");
      printf("unifrog quick save_state oom core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)unifrog_log_flush_force();
      return -1;
   }

   ok = CORE_CALL2_RET(core, core->serialize, data, size);
   if (!ok) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE SERIALIZE FAILED");
      goto out;
   }

   (void)mkdir("/media/mmcblk0/unifrog", 0777);
   (void)mkdir(LIBRETRO_SAVE_DIR, 0777);
   quick_state_path(core, rom_path, slot, path, sizeof(path));
   file = fopen(path, "wb");
   if (!file) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE OPEN FAILED");
      goto out;
   }

   ok = fwrite(data, 1, size, file) == size && fflush(file) == 0;
   fclose(file);
   file = NULL;
   snprintf(host.quick_status, sizeof(host.quick_status),
      ok ? "STATE SAVED" : "STATE WRITE FAILED");
   ret = ok ? (int)slot : -1;
   printf("unifrog quick save_state core=%s path=%s size=%u slot=%u ok=%d\n",
      core->id, path, (unsigned)size, slot, ok);

out:
   if (file)
      fclose(file);
   free(data);
   (void)unifrog_log_flush_force();
   return ret;
}

static int quick_load_state_file(void)
{
   const struct libretro_core_api *core = host.quick_core;
   const char *rom_path = host.quick_rom_path;
   unsigned slot = host.quick_state_slot;
   char path[160];
   FILE *file = NULL;
   void *data = NULL;
   size_t size;
   size_t read_size;
   int extra;
   int ok;
   int ret = -1;

   if (!core || !core->serialize_size || !core->unserialize) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE UNSUPPORTED");
      return -1;
   }

   size = CORE_CALL0_RET(core, core->serialize_size);
   if (size == 0 || size > LIBRETRO_STATE_FILE_MAX) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         size == 0 ? "STATE UNSUPPORTED" : "STATE TOO LARGE");
      printf("unifrog quick load_state unsupported core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)unifrog_log_flush_force();
      return -1;
   }

   data = malloc(size);
   if (!data) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE OUT OF MEMORY");
      printf("unifrog quick load_state oom core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)unifrog_log_flush_force();
      return -1;
   }

   quick_state_path(core, rom_path, slot, path, sizeof(path));
   file = fopen(path, "rb");
   if (!file) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "NO STATE FILE");
      goto out;
   }

   read_size = fread(data, 1, size, file);
   extra = fgetc(file);
   ok = read_size == size && extra == EOF && ferror(file) == 0;
   fclose(file);
   file = NULL;
   if (!ok) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE SIZE MISMATCH");
      printf("unifrog quick load_state read_failed core=%s path=%s size=%u read=%u slot=%u extra=%d\n",
         core->id, path, (unsigned)size, (unsigned)read_size, slot, extra);
      goto out;
   }

   ok = CORE_CALL2_RET(core, core->unserialize, data, size);
   snprintf(host.quick_status, sizeof(host.quick_status),
      ok ? "STATE LOADED" : "STATE LOAD FAILED");
   if (ok) {
      for (unsigned i = 0; i < ARRAY_SIZE(quick_memory_files); i++)
         (void)quick_note_memory_hash(core, quick_memory_files[i].id);
      ret = (int)slot;
   }
   printf("unifrog quick load_state core=%s path=%s size=%u slot=%u ok=%d\n",
      core->id, path, (unsigned)size, slot, ok);

out:
   if (file)
      fclose(file);
   free(data);
   (void)unifrog_log_flush_force();
   return ret;
}

static void libretro_watchdog_task(void *arg)
{
   unsigned stable_polls = 0;
   unsigned last_phase = 0;
   unsigned last_marker = 0;
   unsigned last_heartbeat = 0;

   (void)arg;

   while (watchdog_active) {
      vTaskDelay(LIBRETRO_WATCHDOG_TICKS);
      if (!watchdog_phase) {
         stable_polls = 0;
         last_phase = 0;
         last_marker = watchdog_marker;
         last_heartbeat = watchdog_heartbeat;
         continue;
      }
      if (watchdog_phase == last_phase &&
          watchdog_marker == last_marker &&
          watchdog_heartbeat == last_heartbeat) {
         stable_polls++;
      } else {
         stable_polls = 0;
         last_phase = watchdog_phase;
         last_marker = watchdog_marker;
         last_heartbeat = watchdog_heartbeat;
      }
      if (watchdog_phase == LIBRETRO_WATCHDOG_PHASE_LOAD &&
          stable_polls >= LIBRETRO_WATCHDOG_LOAD_STALL_POLLS) {
         unifrog_panic_screen_labeled("UNIFROG CORE HANG",
            "PHASE", watchdog_phase,
            "MARK", watchdog_marker,
            "BEAT", watchdog_heartbeat,
            "STABLE", stable_polls);
      } else if (watchdog_phase == LIBRETRO_WATCHDOG_PHASE_RUN &&
                 stable_polls >= LIBRETRO_WATCHDOG_RUN_STALL_POLLS) {
         unifrog_panic_screen_labeled("UNIFROG CORE HANG",
            "PHASE", watchdog_phase,
            "FRAME", watchdog_marker,
            "BEAT", watchdog_heartbeat,
            "STABLE", stable_polls);
      }
   }

   vTaskDelete(NULL);
}

static void libretro_watchdog_start(void)
{
   watchdog_active = 1;
   watchdog_phase = 0;
   watchdog_marker = 0;
   watchdog_heartbeat = 1;
   if (xTaskCreate(libretro_watchdog_task, (const char *)"lr_wdog",
       configTASK_STACK_DEPTH, NULL, portPRI_TASK_HIGH, NULL) != pdPASS) {
      watchdog_active = 0;
      printf("unifrog libretro watchdog_create_failed\n");
   }
}

static void libretro_watchdog_stop(void)
{
   watchdog_phase = 0;
   watchdog_active = 0;
}

static void libretro_watchdog_enter(unsigned phase, unsigned marker)
{
   watchdog_marker = marker;
   watchdog_phase = phase;
   watchdog_heartbeat++;
}

static void libretro_watchdog_leave(void)
{
   watchdog_phase = 0;
   watchdog_heartbeat++;
}

static unsigned load_stage_hash(const char *stage)
{
   unsigned hash = 2166136261u;

   if (stage) {
      while (*stage) {
         hash ^= (unsigned)(uint8_t)*stage++;
         hash *= 16777619u;
      }
   }
   return hash;
}

static unsigned load_progress_marker(const char *stage, unsigned current,
   unsigned total)
{
   unsigned hash = load_stage_hash(stage);
   unsigned percent = total ? (unsigned)(((uint64_t)current * 100u) / total) :
      0xffu;

   if (percent > 100u)
      percent = 100u;

   return ((hash & 0xffu) << 24) | ((percent & 0xffu) << 16) |
      (current & 0xffffu);
}

static void libretro_watchdog_load_progress(const char *stage,
   unsigned current, unsigned total)
{
   if (watchdog_phase != LIBRETRO_WATCHDOG_PHASE_LOAD)
      return;

   watchdog_marker = load_progress_marker(stage, current, total);
   watchdog_heartbeat++;
}

void unifrog_core_load_progress(const char *stage, unsigned current,
   unsigned total)
{
   char detail[64];
   unsigned stage_hash;
   unsigned percent;
   unsigned percent_bucket;
   int log_progress = 0;

   host_force_expected_gp();
   percent = total ? (unsigned)(((uint64_t)current * 100u) / total) :
      host.loading_percent;
   if (percent > 100u)
      percent = 100u;
   host.loading_percent = percent;
   stage_hash = load_stage_hash(stage);
   libretro_watchdog_load_progress(stage, current, total);
   if (stage && stage[0]) {
      if (total)
         snprintf(detail, sizeof(detail), "%s %u%%", stage, percent);
      else
         snprintf(detail, sizeof(detail), "%s", stage);
   } else if (total) {
      snprintf(detail, sizeof(detail), "%u%%", percent);
   } else {
      snprintf(detail, sizeof(detail), "CORE LOAD");
   }
   loading_draw("LOADING GAME", detail, percent);

   percent_bucket = total ? percent / LIBRETRO_LOAD_LOG_PERCENT_STEP : 0;
   if (stage_hash != host.loading_log_stage_hash ||
       !total || current == 0 || current == total ||
       percent_bucket != host.loading_log_percent_bucket) {
      host.loading_log_stage_hash = stage_hash;
      host.loading_log_percent_bucket = percent_bucket;
      log_progress = 1;
   }

   if (log_progress) {
      printf("unifrog libretro load_progress stage=%s current=%u total=%u percent=%u\n",
         stage && stage[0] ? stage : "?", current, total, percent);
      if (watchdog_phase == LIBRETRO_WATCHDOG_PHASE_LOAD)
         (void)unifrog_log_flush_force();
   }
}

static void loading_close(void)
{
   if (host.loading_open) {
      unifrog_fb_close(&host.loading_fb);
      host.loading_open = 0;
   }
}

static void loading_draw(const char *title, const char *detail, unsigned percent)
{
   struct unifrog_surface surface;
   unsigned buffer = 0;
   int bar_x;
   int bar_y;
   int bar_w;
   int bar_h;
   int fill_w;

   if (!host.loading_open) {
      if (unifrog_fb_open(&host.loading_fb, UNIFROG_FB_OPEN_DEFAULT) != 0)
         return;
      if (unifrog_fb_set_buffer_count(&host.loading_fb, 2) != 0)
         (void)unifrog_fb_set_buffer_count(&host.loading_fb, 1);
      host.loading_open = 1;
   }

   if (percent > 100)
      percent = 100;
   host.loading_percent = percent;
   buffer = host.loading_fb.current_buffer;
   if (host.loading_fb.buffer_count > 1)
      buffer = (host.loading_fb.current_buffer + 1) % host.loading_fb.buffer_count;
   surface = unifrog_fb_surface_for_buffer(&host.loading_fb, buffer);
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height,
      UNIFROG_RGB565(8, 10, 14));
   unifrog_gfx_draw_text(&surface, 18, 54, title ? title : "LOADING",
      UNIFROG_RGB565(236, 241, 246), 2);
   if (detail && detail[0])
      unifrog_gfx_draw_text(&surface, 18, 86, detail,
         UNIFROG_RGB565(160, 174, 188), 1);

   bar_x = 18;
   bar_y = (int)surface.height - 54;
   bar_w = (int)surface.width - 36;
   bar_h = 14;
   fill_w = (bar_w - 4) * (int)percent / 100;
   unifrog_gfx_fill_rect(&surface, bar_x, bar_y, bar_w, bar_h,
      UNIFROG_RGB565(42, 50, 60));
   unifrog_gfx_fill_rect(&surface, bar_x + 2, bar_y + 2, fill_w, bar_h - 4,
      UNIFROG_RGB565(68, 188, 136));
   unifrog_fb_flush_buffer(&host.loading_fb, buffer);
   unifrog_fb_pan(&host.loading_fb, buffer);
}

void unifrog_libretro_log_cb(enum retro_log_level level, const char *fmt, ...)
{
   char msg[192];
   va_list ap;
   uintptr_t caller_gp = host_read_gp();

   host_force_expected_gp();
   if (!fmt) {
      host_restore_gp(caller_gp);
      return;
   }

   va_start(ap, fmt);
   vsnprintf(msg, sizeof(msg), fmt, ap);
   va_end(ap);
   printf("unifrog libretro core_log level=%u %s", (unsigned)level, msg);
   host_restore_gp(caller_gp);
}

static bool host_get_variable(struct retro_variable *var)
{
   if (!var || !var->key)
      return false;

   if (strcmp(var->key, "gpsp_drc") == 0) {
      var->value = "enabled";
      printf("unifrog libretro variable %s=%s\n", var->key, var->value);
      return true;
   }
   if (host.core_id && strcmp(host.core_id, "gpsp") == 0) {
      if (strcmp(var->key, "gpsp_frameskip") == 0) {
         if (host.options.frameskip == UNIFROG_LIBRETRO_FRAMESKIP_AUTO)
            var->value = "auto_threshold";
         else if (host.options.frameskip ==
                  UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1 ||
                  host.options.frameskip ==
                  UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2)
            var->value = "fixed_interval";
         else
            var->value = "disabled";
         printf("unifrog libretro variable %s=%s\n", var->key, var->value);
         return true;
      }
      if (strcmp(var->key, "gpsp_frameskip_threshold") == 0) {
         var->value = "45";
         printf("unifrog libretro variable %s=%s\n", var->key, var->value);
         return true;
      }
      if (strcmp(var->key, "gpsp_frameskip_interval") == 0) {
         if (host.options.frameskip ==
             UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2)
            var->value = "2";
         else if (host.options.frameskip ==
                  UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1)
            var->value = "1";
         else
            var->value = "0";
         printf("unifrog libretro variable %s=%s\n", var->key, var->value);
         return true;
      }
   }

   return false;
}

bool unifrog_libretro_rumble_cb(unsigned port, enum retro_rumble_effect effect,
   uint16_t strength)
{
   host_force_expected_gp();
   (void)port;
   (void)effect;
   (void)strength;
   return false;
}

void unifrog_libretro_input_poll_cb(void)
{
   host_force_expected_gp();
   unifrog_input_save_previous();
   unifrog_input_poll_with_wireless_divisor(LIBRETRO_WIRELESS_POLL_DIVISOR);
   host.buttons = unifrog_input_buttons();
}

static int button_down_from_mask(uint32_t buttons, unsigned id)
{
   switch (id) {
   case RETRO_DEVICE_ID_JOYPAD_B:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_Y:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_Y)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_SELECT:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_START:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_UP:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_DOWN:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_LEFT:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_RIGHT:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_A:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_X:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_X)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_L:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_L)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_R:
      return (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_R)) != 0;
   default:
      return 0;
   }
}

static int button_down(unsigned id)
{
   return button_down_from_mask(host.buttons, id);
}

int16_t unifrog_libretro_input_state_cb(unsigned port, unsigned device,
   unsigned index, unsigned id)
{
   uint32_t buttons;

   host_force_expected_gp();
   (void)index;
   if (device != RETRO_DEVICE_JOYPAD)
      return 0;

   if (port < UNIFROG_INPUT_MAX_PORTS) {
      buttons = unifrog_input_local_buttons() |
         unifrog_input_wireless_buttons(port);
   } else {
      return 0;
   }

   if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
      uint16_t mask = 0;

      for (unsigned i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++) {
         if (button_down_from_mask(buttons, i))
            mask |= (uint16_t)(1u << i);
      }
      return (int16_t)mask;
   }

   return button_down_from_mask(buttons, id) ? 1 : 0;
}

static int host_audio_scale_sample(int sample)
{
   switch (host.audio_gain) {
   case 0:
      return 0;
   case 1:
      return sample;
   case 2:
      return sample + sample;
   case 4:
      sample += sample;
      return sample + sample;
   case 8:
      sample += sample;
      sample += sample;
      return sample + sample;
   default:
      return sample * (int)host.audio_gain;
   }
}

static unsigned host_audio_write_attempts(void)
{
   if (host.core_id && strcmp(host.core_id, "qpsx") == 0)
      return 1;
   return LIBRETRO_AUDIO_WRITE_ATTEMPTS;
}

static unsigned host_audio_write_poll_ms(void)
{
   if (host.core_id && strcmp(host.core_id, "qpsx") == 0)
      return 0;
   return LIBRETRO_AUDIO_WRITE_POLL_MS;
}

void unifrog_libretro_audio_sample_cb(int16_t left, int16_t right)
{
   int sample = (left + right) / 2;
   int scaled = host_audio_scale_sample(sample);
   int16_t frame[LIBRETRO_AUDIO_CHANNELS];

   host_force_expected_gp();
   if (!host.audio_enabled || host.audio_gain == 0 || host.fast_forward)
      return;
   if (scaled > 32767) {
      scaled = 32767;
      host.audio_clip_count++;
   } else if (scaled < -32768) {
      scaled = -32768;
      host.audio_clip_count++;
   }
   if ((unsigned)(scaled < 0 ? -scaled : scaled) > host.audio_peak_max)
      host.audio_peak_max = (unsigned)(scaled < 0 ? -scaled : scaled);
   frame[0] = (int16_t)scaled;
   frame[1] = (int16_t)scaled;
   if (host.audio_open) {
      uint32_t start = unifrog_perf_count();
      unsigned count;

      (void)unifrog_audio_write_timeout(&host.audio, frame, 1,
         host_audio_write_attempts(), host_audio_write_poll_ms());
      count = unifrog_perf_elapsed(start, unifrog_perf_count());
      host.audio_write_total_count += count;
      if (count > host.audio_write_max_count)
         host.audio_write_max_count = count;
      host.audio_write_count++;
      host.audio_batches++;
      host.audio_frames++;
   }
}

size_t unifrog_libretro_audio_batch_cb(const int16_t *data, size_t frames)
{
   size_t offset = 0;
   size_t input_offset = 0;
   unsigned peak_out = 0;
   unsigned input_rate;
   unsigned output_rate;

   host_force_expected_gp();
   if (!host.audio_enabled || host.fast_forward) {
      host.audio_batches++;
      host.audio_frames += (unsigned)frames;
      return frames;
   }
   if (!host.audio_open) {
      host.audio_batches++;
      host.audio_frames += (unsigned)frames;
      return frames;
   }

   input_rate = host.audio_input_rate ? host.audio_input_rate :
      DEFAULT_SAMPLE_RATE;
   output_rate = host.audio_output_rate ? host.audio_output_rate :
      input_rate;

   while (input_offset < frames) {
      size_t out_frames = 0;

      while (input_offset < frames &&
             out_frames < LIBRETRO_AUDIO_WRITE_CHUNK_FRAMES) {
         int left = data[input_offset * 2];
         int right = data[input_offset * 2 + 1];
         int sample = (left + right) / 2;
         int scaled = host_audio_scale_sample(sample);
         unsigned abs_out;

         input_offset++;
         host.audio_resample_accum += output_rate;
         if (host.audio_resample_accum < input_rate)
            continue;
         host.audio_resample_accum -= input_rate;
         if (scaled > 32767) {
            scaled = 32767;
            host.audio_clip_count++;
         } else if (scaled < -32768) {
            scaled = -32768;
            host.audio_clip_count++;
         }
         abs_out = scaled < 0 ? (unsigned)-scaled : (unsigned)scaled;
         if (abs_out > host.audio_peak_max)
            host.audio_peak_max = abs_out;
         if (abs_out > peak_out)
            peak_out = abs_out;
         if (abs_out < LIBRETRO_AUDIO_GATE_CLOSE_LEVEL)
            scaled = 0;
         audio_mix_buffer[out_frames * LIBRETRO_AUDIO_CHANNELS] =
            (int16_t)scaled;
         audio_mix_buffer[out_frames * LIBRETRO_AUDIO_CHANNELS + 1u] =
            (int16_t)scaled;
         out_frames++;
      }
      if (out_frames == 0)
         continue;
      if (peak_out >= LIBRETRO_AUDIO_GATE_OPEN_LEVEL) {
         if (!host.audio_gate_open) {
            (void)unifrog_audio_set_output_enabled(&host.audio, 1);
            host.audio_gate_open = 1;
         }
         host.audio_quiet_batches = 0;
      } else if (peak_out <= LIBRETRO_AUDIO_GATE_CLOSE_LEVEL) {
         if (host.audio_quiet_batches < UINT32_MAX)
            host.audio_quiet_batches++;
         if (host.audio_gate_open &&
             host.audio_quiet_batches >= LIBRETRO_AUDIO_GATE_CLOSE_BATCHES) {
            (void)unifrog_audio_set_output_enabled(&host.audio, 0);
            host.audio_gate_open = 0;
         }
      } else {
         host.audio_quiet_batches = 0;
      }
      {
         uint32_t write_start = unifrog_perf_count();
         int write_ret = unifrog_audio_write_timeout(&host.audio,
            audio_mix_buffer, (unsigned)out_frames,
            host_audio_write_attempts(), host_audio_write_poll_ms());
         unsigned write_count = unifrog_perf_elapsed(write_start,
            unifrog_perf_count());

         host.audio_write_total_count += write_count;
         if (write_count > host.audio_write_max_count)
            host.audio_write_max_count = write_count;
         host.audio_write_count++;
         if (write_ret != 0) {
            if (host.audio_failures < 8) {
               printf("unifrog libretro audio_write_fail batch=%u frames=%u count=%u\n",
                  host.audio_batches, (unsigned)out_frames, write_count);
               (void)unifrog_log_flush();
            }
            host.audio_failures++;
            break;
         }
      }
      offset += out_frames;
   }

   host.audio_batches++;
   host.audio_frames += (unsigned)offset;

   return input_offset ? input_offset : frames;
}

static uint16_t *host_convert_xrgb8888_to_rgb565(const void *data,
   unsigned width, unsigned height, size_t pitch)
{
   size_t pixels;

   if (pitch < (size_t)width * 4u)
      return NULL;
   pixels = (size_t)width * (size_t)height;
   if (width != 0 && pixels / width != height)
      return NULL;

   if (pixels > host.video_convert_pixels) {
      uint16_t *buffer = malloc(pixels * sizeof(*buffer));

      if (!buffer)
         return NULL;
      free(host.video_convert_buffer);
      host.video_convert_buffer = buffer;
      host.video_convert_pixels = pixels;
   }

   for (unsigned y = 0; y < height; y++) {
      const uint8_t *src = (const uint8_t *)data + (size_t)y * pitch;
      uint16_t *dst = host.video_convert_buffer + (size_t)y * width;

      for (unsigned x = 0; x < width; x++) {
         unsigned b = src[x * 4u + 0u];
         unsigned g = src[x * 4u + 1u];
         unsigned r = src[x * 4u + 2u];

         dst[x] = (uint16_t)(((r & 0xf8u) << 8) |
            ((g & 0xfcu) << 3) | (b >> 3));
      }
   }

   return host.video_convert_buffer;
}

void unifrog_libretro_video_refresh_cb(const void *data, unsigned width,
   unsigned height, size_t pitch)
{
   const void *present_data = data;
   unsigned present_pitch = (unsigned)pitch;

   host_force_expected_gp();
   if (!data || width == 0 || height == 0)
      return;

   if (host.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
      present_data = host_convert_xrgb8888_to_rgb565(data, width, height,
         pitch);
      if (!present_data)
         return;
      present_pitch = width * 2u;
   } else if (host.pixel_format != RETRO_PIXEL_FORMAT_RGB565) {
      return;
   }

   if (!host.video_seen) {
      printf("unifrog libretro first_video %ux%u pitch=%u fmt=%u\n",
         width, height, (unsigned)pitch, host.pixel_format);
      (void)unifrog_log_flush();
      host.video_seen = 1;
   }
   host.video_width = width;
   host.video_height = height;
   host.video_pitch = present_pitch;
   host.video_frames++;
   if (host.presenter_open)
      (void)unifrog_presenter_present_rgb565(&host.presenter,
         present_data, width, height, present_pitch);
}

bool unifrog_libretro_environment_cb(unsigned cmd, void *data)
{
   host_force_expected_gp();
   switch (cmd) {
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      if (!data)
         return false;
      ((struct retro_log_callback *)data)->log = unifrog_libretro_log_cb;
      printf("unifrog libretro env=get_log_interface\n");
      (void)unifrog_log_flush();
      return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      if (!data)
         return false;
      host.pixel_format = *(const enum retro_pixel_format *)data;
      printf("unifrog libretro set_pixel_format=%u\n", host.pixel_format);
      (void)unifrog_log_flush();
      return host.pixel_format == RETRO_PIXEL_FORMAT_RGB565 ||
         host.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888;
   case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      if (!data)
         return false;
      *(bool *)data = true;
      return true;
   case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
   case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
      if (!data)
         return false;
      *(unsigned *)data = 2;
      return true;
   case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      if (!data)
         return false;
      *(int *)data = RETRO_AV_ENABLE_VIDEO |
         (host.audio_enabled && !host.fast_forward ? RETRO_AV_ENABLE_AUDIO :
          RETRO_AV_ENABLE_HARD_DISABLE_AUDIO);
      return true;
   case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
      if (!data)
         return false;
      ((struct retro_rumble_interface *)data)->set_rumble_state =
         unifrog_libretro_rumble_trampoline;
      return true;
   case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      if (!data)
         return false;
      *(unsigned *)data = 2;
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      if (!data)
         return false;
      *(bool *)data = false;
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
      return host_get_variable((struct retro_variable *)data);
   case RETRO_ENVIRONMENT_SET_VARIABLE:
      return false;
   case RETRO_ENVIRONMENT_SET_VARIABLES:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
   case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
   case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
   case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
      return true;
   case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      if (data) {
         const struct retro_memory_map *map =
            (const struct retro_memory_map *)data;
         printf("unifrog libretro env=set_memory_maps count=%u\n",
            (unsigned)map->num_descriptors);
      }
      return true;
   case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
      return data != NULL;
   case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
      if (data &&
          ((const struct retro_audio_buffer_status_callback *)data)->callback) {
         host.audio_status =
            *(const struct retro_audio_buffer_status_callback *)data;
         host.audio_status_enabled = 1;
      } else {
         memset(&host.audio_status, 0, sizeof(host.audio_status));
         host.audio_status_enabled = 0;
      }
      printf("unifrog libretro env=audio_buffer_status enabled=%d\n",
         host.audio_status_enabled);
      return true;
   case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
      if (data) {
         printf("unifrog libretro env=set_minimum_audio_latency ms=%u\n",
            *(const unsigned *)data);
      }
      return true;
   case RETRO_ENVIRONMENT_GET_LANGUAGE:
      if (!data)
         return false;
      *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
      return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
   case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
      if (!data)
         return false;
      *(const char **)data = "/media/mmcblk0";
      return true;
   case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
      if (!data)
         return false;
      *(unsigned *)data = 1;
      return true;
   case RETRO_ENVIRONMENT_SET_MESSAGE:
      if (data) {
         const struct retro_message *msg = (const struct retro_message *)data;
         printf("unifrog libretro message=%s\n",
            msg && msg->msg ? msg->msg : "");
      }
      return true;
   case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
      if (data) {
         const struct retro_message_ext *msg = (const struct retro_message_ext *)data;
         printf("unifrog libretro message_ext=%s\n",
            msg && msg->msg ? msg->msg : "");
      }
      return true;
   default:
      if (cmd < 128) {
         printf("unifrog libretro env unsupported cmd=%u\n", cmd);
         (void)unifrog_log_flush();
      }
      return false;
   }
}

static int exit_combo_down(void)
{
   return (host.buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT)) &&
      (host.buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START));
}

static int quick_js_status_value(void)
{
   unsigned backlight = 0;

   (void)unifrog_backlight_get(&backlight);
   if (backlight > 100)
      backlight = 100;
   return (int)(backlight * 100u + (unsigned)host.display_mode * 10u +
      (host.audio_enabled ? 1u : 0u));
}

static int quick_js_toggle_audio(void)
{
   host.audio_enabled = host.audio_enabled ? 0 : 1;
   host.options.audio_enabled = host.audio_enabled;
   host.audio_gate_open = 0;
   host.audio_quiet_batches = 0;
   if (host.audio_open)
      (void)unifrog_audio_set_output_enabled(&host.audio, 0);
   printf("unifrog quick_js audio=%d\n", host.audio_enabled);
   return quick_js_status_value();
}

static int quick_js_cycle_display(void)
{
   switch (host.display_mode) {
   case UNIFROG_LIBRETRO_DISPLAY_FIT:
      host.display_mode = UNIFROG_LIBRETRO_DISPLAY_STRETCH;
      break;
   case UNIFROG_LIBRETRO_DISPLAY_STRETCH:
      host.display_mode = UNIFROG_LIBRETRO_DISPLAY_ORIGINAL;
      break;
   default:
      host.display_mode = UNIFROG_LIBRETRO_DISPLAY_FIT;
      break;
   }
   host.options.display_mode = host.display_mode;
   if (host.presenter_open) {
      host.presenter.flags = present_flags_for_display_mode(host.display_mode);
      host.presenter.cleared_buffer_mask = 0;
   }
   printf("unifrog quick_js display=%s\n",
      display_mode_label(host.display_mode));
   return quick_js_status_value();
}

static int quick_js_cycle_backlight(void)
{
   unsigned current = 0;
   unsigned next = quick_backlight_levels[0];

   if (unifrog_backlight_get(&current) == 0) {
      for (unsigned i = 0; i < ARRAY_SIZE(quick_backlight_levels); i++) {
         if (quick_backlight_levels[i] > current) {
            next = quick_backlight_levels[i];
            break;
         }
      }
   }
   (void)unifrog_backlight_set(next);
   host.options.backlight_level = (int)next;
   printf("unifrog quick_js backlight=%u\n", next);
   return quick_js_status_value();
}

static int quick_js_state_slot(void)
{
   if (host.quick_state_slot >= LIBRETRO_STATE_SLOT_COUNT)
      host.quick_state_slot = 0;
   return (int)host.quick_state_slot;
}

static int quick_js_cycle_state_slot(int delta)
{
   if (host.quick_state_slot >= LIBRETRO_STATE_SLOT_COUNT)
      host.quick_state_slot = 0;
   if (delta < 0) {
      if (host.quick_state_slot == 0)
         host.quick_state_slot = LIBRETRO_STATE_SLOT_COUNT - 1u;
      else
         host.quick_state_slot--;
   } else {
      host.quick_state_slot++;
      if (host.quick_state_slot >= LIBRETRO_STATE_SLOT_COUNT)
         host.quick_state_slot = 0;
   }
   printf("unifrog quick_js state_slot=%u\n", host.quick_state_slot);
   return (int)host.quick_state_slot;
}

static unsigned quick_js_current_scpu_mhz(void)
{
   if (host.scpu_target_mhz)
      return host.scpu_target_mhz;
   return unifrog_scpu_current_mhz();
}

static int quick_js_cycle_scpu(int delta)
{
   unsigned current = quick_js_current_scpu_mhz();
   unsigned next = delta < 0 ?
      quick_scpu_mhz_options[ARRAY_SIZE(quick_scpu_mhz_options) - 1u] :
      quick_scpu_mhz_options[0];

   if (delta < 0) {
      for (unsigned i = ARRAY_SIZE(quick_scpu_mhz_options); i > 0; i--) {
         if (quick_scpu_mhz_options[i - 1u] < current) {
            next = quick_scpu_mhz_options[i - 1u];
            break;
         }
      }
   } else {
      for (unsigned i = 0; i < ARRAY_SIZE(quick_scpu_mhz_options); i++) {
         if (quick_scpu_mhz_options[i] > current) {
            next = quick_scpu_mhz_options[i];
            break;
         }
      }
   }

   if (!host.scpu_restore_valid)
      host.scpu_restore_valid =
         unifrog_scpu_capture(&host.scpu_restore) == 0 &&
         host.scpu_restore.valid;
   host.scpu_apply_ret = unifrog_scpu_apply_mhz(next);
   if (host.scpu_apply_ret == 0) {
      host.scpu_target_mhz = next;
      host.options.scpu_mhz = next;
      if (host.fps)
         host.frame_budget_count = host_compute_frame_budget(host.fps,
            &host.scpu_mhz_est, &host.count_hz_est,
            &host.count_hz_calibrated);
   }
   printf("unifrog quick_js scpu current=%u target=%u ret=%d now=%u restore_valid=%d budget=%u\n",
      current, next, host.scpu_apply_ret, unifrog_scpu_current_mhz(),
      host.scpu_restore_valid, host.frame_budget_count);
   (void)unifrog_log_flush_force();
   return host.scpu_apply_ret == 0 ? (int)next : -1;
}

static struct unifrog_surface quick_js_surface(void)
{
   return unifrog_fb_surface_for_buffer(&host.presenter.fb,
      host.quick_js_draw_buffer);
}

static void quick_js_begin_frame(void)
{
   if (!host.presenter_open || host.quick_js_frame_open)
      return;

   host.quick_js_draw_buffer = host.presenter.active_buffer;
   if (host.presenter.buffer_count > 1)
      host.quick_js_draw_buffer =
         (host.presenter.active_buffer + 1u) % host.presenter.buffer_count;
   host.quick_js_frame_open = 1;
}

static void quick_js_log(void *opaque, const char *message)
{
   (void)opaque;
   printf("js2300 quick: %s\n", message ? message : "");
}

static int quick_js_flush_log(void *opaque)
{
   (void)opaque;
   return unifrog_log_flush();
}

static uint32_t quick_js_millis(void *opaque)
{
   (void)opaque;
   return unifrog_perf_time_ms();
}

static void quick_js_sleep(void *opaque, uint32_t ms)
{
   (void)opaque;
   if (ms > 1000)
      ms = 1000;
   if (ms)
      usleep((useconds_t)ms * 1000u);
}

static void quick_js_video_clear(void *opaque, uint16_t color)
{
   struct unifrog_surface surface;
   (void)opaque;

   if (!host.presenter_open)
      return;
   quick_js_begin_frame();
   surface = quick_js_surface();
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height, color);
}

static void quick_js_video_rects(void *opaque,
   const struct js2300_rect *rects, size_t count)
{
   struct unifrog_surface surface;
   (void)opaque;

   if (!host.presenter_open || !rects)
      return;
   quick_js_begin_frame();
   surface = quick_js_surface();
   for (size_t i = 0; i < count; i++)
      unifrog_gfx_fill_rect(&surface, rects[i].x, rects[i].y,
         rects[i].w, rects[i].h, rects[i].color);
}

static void quick_js_video_text(void *opaque, int x, int y,
   const char *text, uint16_t color)
{
   struct unifrog_surface surface;
   (void)opaque;

   if (!host.presenter_open)
      return;
   quick_js_begin_frame();
   surface = quick_js_surface();
   unifrog_gfx_draw_text(&surface, x, y, text ? text : "", color, 1);
}

static void quick_js_video_present(void *opaque)
{
   (void)opaque;

   if (!host.presenter_open)
      return;
   if (!host.quick_js_frame_open) {
      (void)unifrog_fb_wait_vsync(&host.presenter.fb);
      return;
   }

   unifrog_fb_flush_buffer(&host.presenter.fb, host.quick_js_draw_buffer);
   (void)unifrog_fb_wait_vsync(&host.presenter.fb);
   if (unifrog_fb_pan(&host.presenter.fb, host.quick_js_draw_buffer) == 0)
      host.presenter.active_buffer = host.quick_js_draw_buffer;
   host.quick_js_frame_open = 0;
}

static uint32_t quick_js_input_mask(uint32_t buttons)
{
   uint32_t out = 0;

   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP))
      out |= 1u << 0;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN))
      out |= 1u << 1;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT))
      out |= 1u << 2;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT))
      out |= 1u << 3;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A))
      out |= 1u << 4;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B))
      out |= 1u << 5;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_X))
      out |= 1u << 6;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_Y))
      out |= 1u << 7;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_L))
      out |= 1u << 8;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_R))
      out |= 1u << 9;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT))
      out |= 1u << 10;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START))
      out |= 1u << 11;
   return out;
}

static uint32_t quick_js_input_poll(void *opaque)
{
   uint32_t buttons;
   (void)opaque;

   unifrog_input_save_previous();
   unifrog_input_poll_with_wireless_divisor(1);
   host.buttons = unifrog_input_buttons();
   buttons = unifrog_input_menu_buttons();
   return quick_js_input_mask(buttons);
}

static int quick_js_backlight(void *opaque, int level, int *out_level)
{
   unsigned current = 0;
   int ret = 0;
   (void)opaque;

   if (level >= 0) {
      if (level > 100)
         level = 100;
      ret = unifrog_backlight_set((unsigned)level);
      host.options.backlight_level = level;
   }
   if (unifrog_backlight_get(&current) != 0)
      return -1;
   if (out_level)
      *out_level = (int)current;
   return ret;
}

static int quick_js_action(void *opaque, const char *id)
{
   (void)opaque;

   if (!id)
      return -1;
   if (strcmp(id, "quick:status") == 0)
      return quick_js_status_value();
   if (strcmp(id, "quick:resume") == 0) {
      host.quick_js_action = QUICK_JS_ACTION_RESUME;
      return 0;
   }
   if (strcmp(id, "quick:return") == 0) {
      host.quick_js_action = QUICK_JS_ACTION_RETURN_MENU;
      return 0;
   }
   if (strcmp(id, "quick:audio") == 0)
      return quick_js_toggle_audio();
   if (strcmp(id, "quick:display") == 0)
      return quick_js_cycle_display();
   if (strcmp(id, "quick:backlight") == 0)
      return quick_js_cycle_backlight();
   if (strcmp(id, "quick:state-slot") == 0)
      return quick_js_state_slot();
   if (strcmp(id, "quick:state-slot-next") == 0)
      return quick_js_cycle_state_slot(1);
   if (strcmp(id, "quick:state-slot-prev") == 0)
      return quick_js_cycle_state_slot(-1);
   if (strcmp(id, "quick:save-state") == 0)
      return quick_save_state_file();
   if (strcmp(id, "quick:load-state") == 0)
      return quick_load_state_file();
   if (strcmp(id, "quick:cpu") == 0)
      return (int)quick_js_current_scpu_mhz();
   if (strcmp(id, "quick:cpu-next") == 0)
      return quick_js_cycle_scpu(1);
   if (strcmp(id, "quick:cpu-prev") == 0)
      return quick_js_cycle_scpu(-1);
   return -1;
}

static void quick_js_exit(void *opaque, const char *reason)
{
   (void)opaque;
   printf("unifrog quick_js exit reason=%s action=%d\n",
      reason ? reason : "", host.quick_js_action);
}

static void quick_js_configure_host(struct js2300_host *js_host)
{
   memset(js_host, 0, sizeof(*js_host));
   js_host->size = sizeof(*js_host);
   js_host->log = quick_js_log;
   js_host->flush_log = quick_js_flush_log;
   js_host->millis = quick_js_millis;
   js_host->sleep_ms = quick_js_sleep;
   js_host->video_clear = quick_js_video_clear;
   js_host->video_rects = quick_js_video_rects;
   js_host->video_text = quick_js_video_text;
   js_host->video_present = quick_js_video_present;
   js_host->input_poll = quick_js_input_poll;
   js_host->action = quick_js_action;
   js_host->exit = quick_js_exit;
   js_host->backlight = quick_js_backlight;
}

static int quick_js_run(const struct libretro_core_api *core,
   const char *rom_path)
{
   struct js2300_config config;
   struct js2300_host js_host;
   struct js2300_runtime *runtime = NULL;
   uint64_t start_us;
   int ret;

   if (!host.presenter_open)
      return 0;

   if (host.audio_open)
      (void)unifrog_audio_set_output_enabled(&host.audio, 0);
   host.audio_gate_open = 0;
   host.quick_js_action = QUICK_JS_ACTION_RESUME;
   host.quick_js_frame_open = 0;
   host.quick_core = core;
   host.quick_rom_path = rom_path;
   if (host.quick_state_slot >= LIBRETRO_STATE_SLOT_COUNT)
      host.quick_state_slot = 0;

   if (js2300_config_init(&config) != 0)
      return 1;
   config.app_root = LIBRETRO_QUICK_JS_ROOT;
   config.entry_script = LIBRETRO_QUICK_JS_ENTRY;
   config.heap_bytes = LIBRETRO_QUICK_JS_HEAP_BYTES;
   config.stack_bytes = LIBRETRO_QUICK_JS_STACK_BYTES;
   config.bytecode_cache_bytes = LIBRETRO_QUICK_JS_BYTECODE_BYTES;
   quick_js_configure_host(&js_host);

   start_us = host_time_us();
   printf("unifrog quick_js start entry=%s\n", LIBRETRO_QUICK_JS_ENTRY);
   (void)unifrog_log_flush_force();
   ret = js2300_runtime_create(&config, &js_host, &runtime);
   if (ret == 0)
      ret = js2300_runtime_run(runtime);
   js2300_runtime_destroy(runtime);
   if (ret != 0)
      host.quick_js_action = QUICK_JS_ACTION_RETURN_MENU;
   printf("unifrog quick_js done ret=%d action=%d ms=%u\n",
      ret, host.quick_js_action,
      host_elapsed_ms(start_us, host_time_us()));
   (void)unifrog_log_flush_force();

   host.quick_js_frame_open = 0;
   host.presenter.cleared_buffer_mask = 0;
   return host.quick_js_action == QUICK_JS_ACTION_RETURN_MENU ? 1 : 0;
}

static unsigned host_calibrate_count_hz(void)
{
   uint64_t start_us;
   uint64_t end_us;
   uint32_t start_count;
   uint32_t end_count;
   uint64_t elapsed_us;
   uint32_t elapsed_count;

   start_us = host_time_us();
   do {
      end_us = host_time_us();
   } while (end_us == start_us);

   start_us = end_us;
   start_count = unifrog_perf_count();
   usleep(LIBRETRO_COUNT_CALIBRATE_US);
   end_count = unifrog_perf_count();
   end_us = host_time_us();

   elapsed_us = end_us > start_us ? end_us - start_us : 0;
   if (elapsed_us < LIBRETRO_COUNT_CALIBRATE_US / 2u)
      return 0;

   elapsed_count = unifrog_perf_elapsed(start_count, end_count);
   return (unsigned)(((uint64_t)elapsed_count * 1000000ull) / elapsed_us);
}

static unsigned host_compute_frame_budget(unsigned fps, unsigned *scpu_mhz,
   unsigned *count_hz, unsigned *count_hz_calibrated)
{
   struct unifrog_perf_caps caps;
   unsigned counts_per_second;

   if (scpu_mhz)
      *scpu_mhz = 0;
   if (count_hz)
      *count_hz = 0;
   if (count_hz_calibrated)
      *count_hz_calibrated = 0;
   if (!fps || unifrog_perf_query_caps(&caps) != 0 || !caps.scpu_mhz_est)
      return 0;
   if (scpu_mhz)
      *scpu_mhz = caps.scpu_mhz_est;
   counts_per_second = host_calibrate_count_hz();
   if (counts_per_second) {
      if (count_hz_calibrated)
         *count_hz_calibrated = 1;
   } else {
      counts_per_second = caps.scpu_mhz_est * 500000u;
   }
   if (count_hz)
      *count_hz = counts_per_second;
   return counts_per_second / fps;
}

static unsigned host_compute_frame_period_us(double fps, unsigned fallback_fps)
{
   double timing_fps = fps > 1.0 ? fps : (double)fallback_fps;
   unsigned period_us;

   if (timing_fps < 1.0)
      return 0;
   period_us = (unsigned)((1000000.0 / timing_fps) + 0.5);
   return period_us ? period_us : 1u;
}

static uint64_t host_time_us(void)
{
   return unifrog_perf_time_us();
}

static unsigned host_elapsed_ms(uint64_t start_us, uint64_t end_us)
{
   uint64_t elapsed_us;

   if (end_us <= start_us)
      return 0;
   elapsed_us = end_us - start_us;
   if (elapsed_us / 1000ull > UINT32_MAX)
      return UINT32_MAX;
   return (unsigned)(elapsed_us / 1000ull);
}

static void host_pace_begin(void)
{
   uint64_t now_us = host_time_us();

   host.frame_deadline_us = now_us + host.frame_period_us;
   host.report_start_us = now_us;
}

static void host_pace_frame(void)
{
   uint64_t now_us;
   uint64_t after_us;

   if (!host.frame_period_us)
      return;

   now_us = host_time_us();
   if (now_us < host.frame_deadline_us) {
      uint64_t wait_us = host.frame_deadline_us - now_us;
      unsigned wait_clamped = wait_us > UINT32_MAX ? UINT32_MAX :
         (unsigned)wait_us;

      if (wait_clamped) {
         usleep(wait_clamped);
         after_us = host_time_us();
         wait_us = after_us > now_us ? after_us - now_us : wait_us;
         if (wait_us > UINT32_MAX)
            wait_us = UINT32_MAX;
         host.pace_wait_frames++;
         host.pace_wait_total_us += wait_us;
         if ((unsigned)wait_us > host.pace_wait_max_us)
            host.pace_wait_max_us = (unsigned)wait_us;
      } else {
         after_us = host_time_us();
      }
   } else {
      host.pace_late_frames++;
      after_us = now_us;
   }

   if (after_us >
       host.frame_deadline_us +
       (uint64_t)host.frame_period_us * LIBRETRO_PACE_RESET_LATE_FRAMES) {
      host.frame_deadline_us = after_us + host.frame_period_us;
      host.pace_reset_frames++;
   } else {
      host.frame_deadline_us += host.frame_period_us;
   }
}

static unsigned host_audio_capacity_frames(void)
{
   if (!host.audio_open || host.audio.frame_bytes == 0)
      return 0;
   return (host.audio.period_bytes * host.audio.periods) /
      host.audio.frame_bytes;
}

static void host_notify_audio_status(void)
{
   unsigned capacity = host_audio_capacity_frames();
   unsigned long delay = 0;
   unsigned occupancy = 0;
   bool active = false;
   bool underrun_likely = false;

   if (!host.audio_status_enabled || !host.audio_status.callback)
      return;

   if (host.options.frameskip == UNIFROG_LIBRETRO_FRAMESKIP_AUTO &&
       host.frame_period_us) {
      uint64_t now_us = host_time_us();
      uint64_t late_us = now_us > host.frame_deadline_us ?
         now_us - host.frame_deadline_us : 0;

      active = true;
      if (late_us >= (uint64_t)host.frame_period_us)
         occupancy = 0u;
      else if (late_us > 0) {
         uint64_t late_pct =
            (late_us * 100u) / (uint64_t)host.frame_period_us;

         occupancy = late_pct < 100u ? 100u - (unsigned)late_pct : 0u;
      } else {
         occupancy = 100u;
      }
      underrun_likely = occupancy == 0u;
   } else if (capacity && unifrog_audio_delay(&host.audio, &delay) == 0) {
      active = true;
      occupancy = (unsigned)(((uint64_t)delay * 100u) / capacity);
      if (occupancy > 100u)
         occupancy = 100u;
      underrun_likely = occupancy < 25u;
   }

   if (host.audio_status_count == 0 ||
       occupancy < host.audio_status_occupancy_min)
      host.audio_status_occupancy_min = occupancy;
   if (occupancy > host.audio_status_occupancy_max)
      host.audio_status_occupancy_max = occupancy;
   host.audio_status_count++;
   if (active)
      host.audio_status_active_count++;
   if (underrun_likely)
      host.audio_status_underrun_count++;
   host.audio_status_occupancy_total += occupancy;

   (void)unifrog_mips_call3(host.core_gp ? host.core_gp : host_expected_gp(),
      (uintptr_t)host.audio_status.callback, (uintptr_t)active,
      (uintptr_t)occupancy, (uintptr_t)underrun_likely);
   host_force_expected_gp();
}

static unsigned host_avg_count(uint64_t total, unsigned samples)
{
   if (!samples)
      return 0;
   return (unsigned)(total / samples);
}

static void host_report_perf(const char *core_id, int final)
{
   struct unifrog_presenter_stats present;
   unsigned frames = host.run_frames - host.run_report_frames;
   unsigned video_frames = host.video_frames - host.video_report_frames;
   unsigned audio_batches = host.audio_batches - host.audio_report_batches;
   unsigned audio_frames = host.audio_frames - host.audio_report_frames;
   unsigned audio_failures = host.audio_failures - host.audio_report_failures;
   unsigned long audio_delay = 0;
   unsigned active_avg;
   unsigned frame_wall_avg_us;
   unsigned audio_status_avg = host_avg_count(host.audio_status_occupancy_total,
      host.audio_status_count);
   uint64_t now_us = host_time_us();
   uint64_t wall_us = host.report_start_us && now_us > host.report_start_us ?
      now_us - host.report_start_us : 0;
   unsigned actual_fps_x100 = wall_us ?
      (unsigned)(((uint64_t)frames * 100000000ull) / wall_us) : 0;
   unsigned pace_wait_avg_us = host_avg_count(host.pace_wait_total_us,
      host.pace_wait_frames);
   unsigned scpu_now = unifrog_scpu_current_mhz();
   int audio_delay_ret = -1;

   if (frames == 0)
      return;

   if (host.presenter_open)
      unifrog_presenter_take_stats(&host.presenter, &present);
   else
      memset(&present, 0, sizeof(present));
   if (host.audio_open)
      audio_delay_ret = unifrog_audio_delay(&host.audio, &audio_delay);
   active_avg = host_avg_count(host.active_total_count, frames);
   frame_wall_avg_us = wall_us ? (unsigned)(wall_us / frames) : 0;

   printf("unifrog perf core=%s final=%d frames=%u fps=%u actual_fps_x100=%u wall_ms=%u frame_wall_avg_us=%u options_audio=%d audio_gain=%u frameskip=%d display=%s fast_forward=%d scpu_target=%u scpu_now=%u ge_clock=%d backlight=%d pace_period_us=%u pace_wait=%u pace_wait_avg_us=%u pace_wait_max_us=%u pace_late=%u pace_reset=%u save_autosaves=%u\n",
      core_id ? core_id : "?",
      final ? 1 : 0,
      frames, host.fps, actual_fps_x100,
      (unsigned)(wall_us / 1000u),
      frame_wall_avg_us,
      host.audio_enabled,
      host.audio_gain,
      host.options.frameskip,
      display_mode_label(host.display_mode),
      host.fast_forward,
      host.scpu_target_mhz,
      scpu_now,
      host.options.ge_clock,
      host.options.backlight_level,
      host.frame_period_us,
      host.pace_wait_frames,
      pace_wait_avg_us,
      host.pace_wait_max_us,
      host.pace_late_frames,
      host.pace_reset_frames,
      host.memory_autosaves);

   printf("unifrog perf_cpu core=%s slow=%u scpu=%u count_hz=%u count_cal=%u budget=%u run_avg=%u run_max=%u active_avg=%u active_max=%u video=%u present_frames=%u present_avg=%u present_max=%u ge=%u sync=%u vsync=%u pan=%u dst=%d,%d %dx%d\n",
      core_id ? core_id : "?",
      host.slow_frames,
      host.scpu_mhz_est, host.count_hz_est,
      host.count_hz_calibrated,
      host.frame_budget_count,
      host_avg_count(host.run_total_count, frames),
      host.run_max_count,
      active_avg,
      host.active_max_count,
      video_frames,
      present.frames,
      host_avg_count(present.total_count, present.frames),
      present.max_count,
      host_avg_count(present.ge_count, present.frames),
      host_avg_count(present.sync_count, present.frames),
      host_avg_count(present.vsync_count, present.frames),
      host_avg_count(present.pan_count, present.frames),
      present.dst_x, present.dst_y, present.dst_w, present.dst_h);

   printf("unifrog perf_audio core=%s batches=%u frames=%u delay_ret=%d delay=%lu fail=%u write_avg=%u write_max=%u gain=%u peak=%u clip=%u gate=%d quiet=%u status=%u status_active=%u status_under=%u occ_avg=%u occ_min=%u occ_max=%u\n",
      core_id ? core_id : "?",
      audio_batches, audio_frames, audio_delay_ret, audio_delay,
      audio_failures,
      host_avg_count(host.audio_write_total_count, host.audio_write_count),
      host.audio_write_max_count,
      host.audio_gain,
      host.audio_peak_max,
      host.audio_clip_count,
      host.audio_gate_open,
      host.audio_quiet_batches,
      host.audio_status_count,
      host.audio_status_active_count,
      host.audio_status_underrun_count,
      audio_status_avg,
      host.audio_status_count ? host.audio_status_occupancy_min : 0,
      host.audio_status_occupancy_max);

   host.run_report_frames = host.run_frames;
   host.video_report_frames = host.video_frames;
   host.audio_report_batches = host.audio_batches;
   host.audio_report_frames = host.audio_frames;
   host.audio_report_failures = host.audio_failures;
   host.slow_frames = 0;
   host.run_total_count = 0;
   host.run_max_count = 0;
   host.active_total_count = 0;
   host.active_max_count = 0;
   host.audio_write_count = 0;
   host.audio_write_total_count = 0;
   host.audio_write_max_count = 0;
   host.audio_peak_max = 0;
   host.audio_clip_count = 0;
   host.audio_status_count = 0;
   host.audio_status_active_count = 0;
   host.audio_status_underrun_count = 0;
   host.audio_status_occupancy_total = 0;
   host.audio_status_occupancy_min = 0;
   host.audio_status_occupancy_max = 0;
   host.pace_wait_frames = 0;
   host.pace_wait_total_us = 0;
   host.pace_wait_max_us = 0;
   host.pace_late_frames = 0;
   host.pace_reset_frames = 0;
   host.report_start_us = now_us;
}

static uint16_t read_le16(const uint8_t *data)
{
   return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
   return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
      ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static const char *path_extension(const char *path)
{
   const char *slash = strrchr(path, '/');
   const char *dot = strrchr(path, '.');

   if (!dot || (slash && dot < slash) || dot[1] == '\0')
      return NULL;
   return dot + 1;
}

static void *rom_alloc_aligned(size_t size)
{
   uintptr_t aligned;
   uint8_t *raw;
   struct rom_alloc_header *header;
   size_t total;

   if (size == 0)
      return NULL;
   if (size > SIZE_MAX - 31u - sizeof(*header))
      return NULL;
   total = size + 31u + sizeof(*header);

   if (host.content_alloc_appmem &&
       unifrog_abi_application_memory_reserve_top(total, 32u,
       (void **)&raw) == 0) {
      aligned = ((uintptr_t)(raw + sizeof(*header)) + 31u) &
         ~(uintptr_t)31u;
      header = ((struct rom_alloc_header *)aligned) - 1;
      header->magic = ROM_ALLOC_MAGIC;
      header->kind = ROM_ALLOC_APPMEM;
      header->raw = raw;
      header->reserved_bytes = total;
      header->payload_size = size;
      printf("unifrog libretro rom alloc appmem size=%u total=%u ptr=0x%08lx aligned=%lu\n",
         (unsigned)size, (unsigned)total, (unsigned long)aligned,
         (unsigned long)(aligned & 31u));
      return (void *)aligned;
   }

   raw = malloc(total);
   if (!raw)
      return NULL;
   aligned = ((uintptr_t)(raw + sizeof(*header)) + 31u) & ~(uintptr_t)31u;
   header = ((struct rom_alloc_header *)aligned) - 1;
   header->magic = ROM_ALLOC_MAGIC;
   header->kind = ROM_ALLOC_HEAP;
   header->raw = raw;
   header->reserved_bytes = total;
   header->payload_size = size;
   return (void *)aligned;
}

static void rom_free_aligned(void *ptr)
{
   struct rom_alloc_header *header;

   if (!ptr)
      return;
   header = ((struct rom_alloc_header *)ptr) - 1;
   if (header->magic != ROM_ALLOC_MAGIC) {
      printf("unifrog libretro rom free bad_header ptr=0x%08lx magic=0x%08lx\n",
         (unsigned long)(uintptr_t)ptr, (unsigned long)header->magic);
      return;
   }
   if (header->kind == ROM_ALLOC_APPMEM)
      unifrog_abi_application_memory_release_top(header->raw);
   else
      free(header->raw);
}

static int file_size(FILE *file, size_t *out_size)
{
   long size;

   if (!file || !out_size)
      return -1;
   if (fseek(file, 0, SEEK_END) != 0)
      return -1;
   size = ftell(file);
   if (size < 0 || fseek(file, 0, SEEK_SET) != 0)
      return -1;
   *out_size = (size_t)size;
   return 0;
}

static void probe_rom_seek_path(const char *path)
{
   static unsigned probe_count;
   struct stat st;
   uint8_t fd_first[LIBRETRO_FS_PROBE_SAMPLE];
   uint8_t fd_again[LIBRETRO_FS_PROBE_SAMPLE];
   uint8_t std_first[LIBRETRO_FS_PROBE_SAMPLE];
   uint8_t std_again[LIBRETRO_FS_PROBE_SAMPLE];
   off_t fd_pos_after = (off_t)-1;
   off_t fd_seek_cur = (off_t)-1;
   off_t fd_seek_mid = (off_t)-1;
   off_t fd_seek_zero = (off_t)-1;
   long std_tell_after = -1;
   long std_tell_mid = -1;
   ssize_t fd_read_first = -1;
   ssize_t fd_read_mid = -1;
   ssize_t fd_read_again = -1;
   size_t std_read_first = 0;
   size_t std_read_mid = 0;
   size_t std_read_again = 0;
   off_t mid;
   int fd = -1;
   FILE *file = NULL;
   int fd_same = 0;
   int std_same = 0;

   if (!path || probe_count >= LIBRETRO_FS_PROBE_MAX_LOGS ||
       stat(path, &st) != 0 || st.st_size < (off_t)LIBRETRO_FS_PROBE_MIN_SIZE)
      return;
   probe_count++;
   mid = st.st_size / 2;

   fd = open(path, O_RDONLY);
   if (fd >= 0) {
      fd_read_first = read(fd, fd_first, sizeof(fd_first));
      fd_pos_after = lseek(fd, 0, SEEK_CUR);
      fd_seek_cur = lseek(fd, 128, SEEK_CUR);
      fd_seek_mid = lseek(fd, mid, SEEK_SET);
      if (fd_seek_mid >= 0)
         fd_read_mid = read(fd, fd_again, sizeof(fd_again));
      fd_seek_zero = lseek(fd, 0, SEEK_SET);
      if (fd_seek_zero == 0)
         fd_read_again = read(fd, fd_again, sizeof(fd_again));
      fd_same = fd_read_first == (ssize_t)sizeof(fd_first) &&
         fd_read_again == (ssize_t)sizeof(fd_again) &&
         memcmp(fd_first, fd_again, sizeof(fd_first)) == 0;
      close(fd);
   }

   file = fopen(path, "rb");
   if (file) {
      std_read_first = fread(std_first, 1, sizeof(std_first), file);
      std_tell_after = ftell(file);
      if (fseek(file, (long)mid, SEEK_SET) == 0) {
         std_read_mid = fread(std_again, 1, sizeof(std_again), file);
         std_tell_mid = ftell(file);
      }
      if (fseek(file, 0, SEEK_SET) == 0)
         std_read_again = fread(std_again, 1, sizeof(std_again), file);
      std_same = std_read_first == sizeof(std_first) &&
         std_read_again == sizeof(std_again) &&
         memcmp(std_first, std_again, sizeof(std_first)) == 0;
      fclose(file);
   }

   printf("unifrog fs_probe path=%s size=%u fd_read0=%d fd_pos_after=%ld fd_seek_cur=%ld fd_seek_mid=%ld fd_read_mid=%d fd_seek0=%ld fd_read0b=%d fd_same0=%d std_read0=%u std_tell_after=%ld std_read_mid=%u std_tell_mid=%ld std_read0b=%u std_same0=%d\n",
      path, (unsigned)st.st_size, (int)fd_read_first, (long)fd_pos_after,
      (long)fd_seek_cur, (long)fd_seek_mid, (int)fd_read_mid,
      (long)fd_seek_zero, (int)fd_read_again, fd_same,
      (unsigned)std_read_first, std_tell_after, (unsigned)std_read_mid,
      std_tell_mid, (unsigned)std_read_again, std_same);
   (void)unifrog_log_flush();
}

static int read_fd_fully_to_buffer(int fd, const char *path, uint8_t *data,
   size_t size, const char *title, const char *label, unsigned progress_base,
   unsigned progress_span)
{
   size_t done = 0;
   unsigned last_progress = 0xffffffffu;

   if (fd < 0 || !data || size == 0)
      return -1;
   while (done < size) {
      size_t chunk = size - done;
      ssize_t got;
      unsigned progress;

      if (chunk > LIBRETRO_CONTENT_READ_CHUNK)
         chunk = LIBRETRO_CONTENT_READ_CHUNK;
      got = read(fd, data + done, chunk);
      if (got <= 0) {
         printf("unifrog libretro file read failed path=%s size=%u done=%u got=%d errno=%d label=%s\n",
            path ? path : "", (unsigned)size, (unsigned)done, (int)got,
            errno, label ? label : "");
         return -1;
      }
      done += (size_t)got;
      progress = progress_base + (unsigned)((done * progress_span) /
         (size ? size : 1u));
      if (progress != last_progress) {
         loading_draw(title ? title : "LOADING", label ? label : "READING",
            progress);
         last_progress = progress;
      }
   }
   return 0;
}

static int read_path_aligned_direct(const char *path, uint8_t **out_data,
   size_t *out_size, const char *label)
{
   struct stat st;
   uint8_t *data = NULL;
   size_t size;
   uint64_t start_us;
   uint64_t end_us;
   int fd = -1;
   int ret = -1;

   if (!path || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;
   if (stat(path, &st) != 0 || st.st_size <= 0)
      return -1;
   size = (size_t)st.st_size;
   data = rom_alloc_aligned(size);
   if (!data) {
      printf("unifrog libretro rom alloc failed path=%s size=%u label=%s\n",
         path, (unsigned)size, label ? label : "");
      return -1;
   }
   fd = open(path, O_RDONLY);
   if (fd < 0) {
      printf("unifrog libretro rom open failed path=%s errno=%d label=%s\n",
         path, errno, label ? label : "");
      goto out;
   }
   start_us = host_time_us();
   if (read_fd_fully_to_buffer(fd, path, data, size, "LOADING ROM",
       label ? label : "READING", 12, 58) != 0)
      goto out;
   end_us = host_time_us();

   *out_data = data;
   *out_size = size;
   data = NULL;
   printf("unifrog load_time stage=file_read mode=fd_aligned ms=%u bytes=%u chunk=%u label=%s path=%s\n",
      host_elapsed_ms(start_us, end_us), (unsigned)size,
      (unsigned)LIBRETRO_CONTENT_READ_CHUNK, label ? label : "", path);
   ret = 0;

out:
   if (fd >= 0)
      close(fd);
   rom_free_aligned(data);
   return ret;
}

static uint32_t hash_u32(uint32_t hash, uint32_t value)
{
   for (unsigned i = 0; i < 4; i++) {
      hash ^= value & 0xffu;
      hash *= 16777619u;
      value >>= 8;
   }
   return hash;
}

static uint32_t hash_text(uint32_t hash, const char *text)
{
   while (text && *text) {
      hash ^= (uint8_t)*text++;
      hash *= 16777619u;
   }
   return hash;
}

static uint32_t hash_bytes(uint32_t hash, const uint8_t *bytes, size_t len)
{
   for (size_t i = 0; bytes && i < len; i++) {
      hash ^= bytes[i];
      hash *= 16777619u;
   }
   return hash;
}

static uint32_t hash_source_file_sample(uint32_t hash, const char *path)
{
   FILE *file;
   struct stat st;
   uint8_t sample[256];
   size_t got;

   if (!path || stat(path, &st) != 0 || st.st_size <= 0)
      return hash;

   hash = hash_u32(hash, (uint32_t)st.st_size);
   hash = hash_u32(hash, (uint32_t)(st.st_size >> 32));
   file = fopen(path, "rb");
   if (!file)
      return hash;

   got = fread(sample, 1, sizeof(sample), file);
   hash = hash_bytes(hash, sample, got);
   if (st.st_size > (long)sizeof(sample) &&
       fseek(file, (long)(st.st_size - (long)sizeof(sample)), SEEK_SET) == 0) {
      got = fread(sample, 1, sizeof(sample), file);
      hash = hash_bytes(hash, sample, got);
   }
   fclose(file);
   return hash;
}

static int path_is_lz4(const char *path)
{
   return unifrog_text_ends_with_ci(path, ".lz4");
}

static int path_is_zstd(const char *path)
{
   return unifrog_text_ends_with_ci(path, ".zst") ||
      unifrog_text_ends_with_ci(path, ".zstd");
}

static int path_is_zip(const char *path)
{
   return unifrog_text_ends_with_ci(path, ".zip");
}

static int path_is_wrapped_compressed(const char *path)
{
   return path_is_lz4(path) || path_is_zstd(path);
}

static int copy_path_without_last_extension(const char *path, char *out,
   size_t out_size)
{
   const char *slash;
   const char *dot;
   size_t len;

   if (!path || !out || out_size == 0)
      return -1;
   slash = strrchr(path, '/');
   dot = strrchr(path, '.');
   if (!dot || (slash && dot < slash) || dot == path)
      return -1;
   len = (size_t)(dot - path);
   if (len >= out_size)
      len = out_size - 1u;
   memcpy(out, path, len);
   out[len] = '\0';
   return 0;
}

static int first_valid_extension(const char *valid_extensions, char *out,
   size_t out_size)
{
   const char *cursor = valid_extensions;
   size_t len;

   if (!out || out_size == 0)
      return -1;
   out[0] = '\0';
   if (!cursor || !cursor[0])
      return -1;
   while (*cursor == '.')
      cursor++;
   len = 0;
   while (cursor[len] && cursor[len] != '|' && len + 1u < out_size)
      len++;
   if (len == 0)
      return -1;
   memcpy(out, cursor, len);
   out[len] = '\0';
   return 0;
}

static int content_extension_for_cache(const char *source_path,
   const char *entry_name, const char *valid_extensions,
   char *out, size_t out_size)
{
   char stripped[256];
   const char *ext = NULL;

   if (!out || out_size == 0)
      return -1;
   out[0] = '\0';
   if (entry_name && entry_name[0])
      ext = path_extension(entry_name);
   if (!ext && path_is_wrapped_compressed(source_path) &&
       copy_path_without_last_extension(source_path, stripped,
       sizeof(stripped)) == 0)
      ext = path_extension(stripped);
   if (ext && ext[0]) {
      unifrog_text_copy(out, out_size, ext);
      return 0;
   }
   return first_valid_extension(valid_extensions, out, out_size);
}

static int ensure_content_cache_dir(void)
{
   if (mkdir("/media/mmcblk0/unifrog", 0777) != 0 && errno != EEXIST)
      return -1;
   if (mkdir(LIBRETRO_CONTENT_CACHE_DIR, 0777) != 0 && errno != EEXIST)
      return -1;
   return 0;
}

static int content_cache_path(const char *source_path, const char *entry_name,
   const char *valid_extensions, char *out, size_t out_size)
{
   uint32_t hash = 2166136261u;
   char ext[16];

   if (!source_path || !out || out_size == 0)
      return -1;
   if (content_extension_for_cache(source_path, entry_name, valid_extensions,
       ext, sizeof(ext)) != 0)
      return -1;
   hash = hash_text(hash, source_path);
   hash = hash_text(hash, entry_name ? entry_name : "");
   hash = hash_source_file_sample(hash, source_path);
   if (ensure_content_cache_dir() != 0)
      return -1;
   snprintf(out, out_size, "%s/uf%08x.%s",
      LIBRETRO_CONTENT_CACHE_DIR, (unsigned)hash, ext);
   return 0;
}

static int content_zip_cache_path(const char *source_path,
   const struct zip_rom_entry *entry, const char *valid_extensions,
   char *out, size_t out_size)
{
   struct stat st;
   uint32_t hash = 2166136261u;
   char ext[16];

   if (!source_path || !entry || !out || out_size == 0)
      return -1;
   if (content_extension_for_cache(source_path, entry->name,
       valid_extensions, ext, sizeof(ext)) != 0)
      return -1;
   hash = hash_text(hash, source_path);
   hash = hash_text(hash, entry->name);
   hash = hash_u32(hash, entry->crc32);
   hash = hash_u32(hash, entry->compressed_size);
   hash = hash_u32(hash, entry->uncompressed_size);
   hash = hash_u32(hash, entry->method);
   if (stat(source_path, &st) == 0) {
      hash = hash_u32(hash, (uint32_t)st.st_size);
      hash = hash_u32(hash, (uint32_t)(st.st_size >> 32));
   }
   if (ensure_content_cache_dir() != 0)
      return -1;
   snprintf(out, out_size, "%s/ufz%08x.%s",
      LIBRETRO_CONTENT_CACHE_DIR, (unsigned)hash, ext);
   return 0;
}

static int libretro_valid_extension_matches(const char *path,
   const char *valid_extensions)
{
   const char *ext = path_extension(path);
   const char *cursor = valid_extensions;
   size_t ext_len;

   if (!ext)
      return 0;
   if (strcasecmp(ext, "zip") == 0 ||
       strcasecmp(ext, "lz4") == 0 ||
       strcasecmp(ext, "zst") == 0 ||
       strcasecmp(ext, "zstd") == 0)
      return 0;
   if (!valid_extensions || !valid_extensions[0])
      return 1;
   ext_len = strlen(ext);
   while (*cursor) {
      const char *begin = cursor;
      size_t len;

      while (*cursor && *cursor != '|')
         cursor++;
      while (*begin == '.')
         begin++;
      len = (size_t)(cursor - begin);
      if (len == ext_len && strncasecmp(begin, ext, len) == 0)
         return 1;
      if (*cursor == '|')
         cursor++;
   }
   return 0;
}

static int zip_entry_name_is_dir(const char *name)
{
   size_t len = name ? strlen(name) : 0;

   return len > 0 && name[len - 1] == '/';
}

static int zip_find_eocd(const uint8_t *zip, size_t zip_size,
   size_t *eocd_offset)
{
   size_t min_pos;
   size_t pos;

   if (!zip || !eocd_offset || zip_size < 22)
      return -1;
   min_pos = zip_size > (0xffffu + 22u) ? zip_size - (0xffffu + 22u) : 0;
   pos = zip_size - 22u;
   for (;;) {
      if (read_le32(zip + pos) == 0x06054b50u) {
         uint16_t comment_len = read_le16(zip + pos + 20);

         if (pos + 22u + comment_len == zip_size) {
            *eocd_offset = pos;
            return 0;
         }
      }
      if (pos == min_pos)
         break;
      pos--;
   }
   return -1;
}

static int zip_select_rom_entry(const uint8_t *zip, size_t zip_size,
   const char *valid_extensions, struct zip_rom_entry *selected)
{
   size_t eocd;
   size_t cursor;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries;

   if (!selected || zip_find_eocd(zip, zip_size, &eocd) != 0)
      return -1;
   entries = read_le16(zip + eocd + 10);
   cd_size = read_le32(zip + eocd + 12);
   cd_offset = read_le32(zip + eocd + 16);
   if ((size_t)cd_offset + cd_size > zip_size)
      return -1;

   cursor = cd_offset;
   memset(selected, 0, sizeof(*selected));
   for (uint16_t i = 0; i < entries && cursor + 46u <= zip_size; i++) {
      const uint8_t *header = zip + cursor;
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      uint16_t method;
      uint16_t flags;
      uint32_t compressed_size;
      uint32_t uncompressed_size;
      char name[sizeof(selected->name)];
      size_t copy_len;

      if (read_le32(header) != 0x02014b50u)
         return -1;
      flags = read_le16(header + 8);
      method = read_le16(header + 10);
      compressed_size = read_le32(header + 20);
      uncompressed_size = read_le32(header + 24);
      name_len = read_le16(header + 28);
      extra_len = read_le16(header + 30);
      comment_len = read_le16(header + 32);
      if (cursor + 46u + name_len + extra_len + comment_len > zip_size)
         return -1;
      copy_len = name_len;
      if (copy_len >= sizeof(name))
         copy_len = sizeof(name) - 1u;
      memcpy(name, header + 46, copy_len);
      name[copy_len] = '\0';

      if (!zip_entry_name_is_dir(name) &&
          (method == 0 || method == 8) &&
          !(flags & 1u) &&
          uncompressed_size > 0 &&
          uncompressed_size <= LIBRETRO_ZIP_MAX_UNCOMPRESSED &&
          libretro_valid_extension_matches(name, valid_extensions)) {
         unifrog_text_copy(selected->name, sizeof(selected->name), name);
         selected->flags = flags;
         selected->method = method;
         selected->crc32 = read_le32(header + 16);
         selected->compressed_size = compressed_size;
         selected->uncompressed_size = uncompressed_size;
         selected->local_offset = read_le32(header + 42);
         return 0;
      }

      cursor += 46u + name_len + extra_len + comment_len;
   }
   return -1;
}

static int zip_inflate_raw(uint8_t *out, size_t out_size,
   const uint8_t *in, size_t in_size)
{
   z_stream stream;
   int zret;

   memset(&stream, 0, sizeof(stream));
   stream.next_in = (Bytef *)in;
   stream.avail_in = (uInt)in_size;
   stream.next_out = out;
   stream.avail_out = (uInt)out_size;
   zret = inflateInit2(&stream, -MAX_WBITS);
   if (zret != Z_OK)
      return zret;
   zret = inflate(&stream, Z_FINISH);
   (void)inflateEnd(&stream);
   if (zret != Z_STREAM_END || stream.total_out != out_size)
      return zret == Z_STREAM_END ? Z_BUF_ERROR : zret;
   return Z_OK;
}

static int zip_select_rom_entry_stream(FILE *file, const char *zip_path,
   size_t zip_size, const char *valid_extensions,
   struct zip_rom_entry *selected)
{
   uint8_t fixed[46];
   uint8_t *tail = NULL;
   size_t tail_size;
   size_t tail_eocd;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries;
   int out_ret = -1;

   if (!file || !zip_path || !selected || zip_size < 22)
      return -1;
   tail_size = zip_size > (0xffffu + 22u) ?
      (0xffffu + 22u) : zip_size;
   tail = malloc(tail_size);
   if (!tail)
      return -1;
   if (fseek(file, (long)(zip_size - tail_size), SEEK_SET) != 0 ||
       fread(tail, 1, tail_size, file) != tail_size)
      goto out;
   if (zip_find_eocd(tail, tail_size, &tail_eocd) != 0)
      goto out;
   entries = read_le16(tail + tail_eocd + 10);
   cd_size = read_le32(tail + tail_eocd + 12);
   cd_offset = read_le32(tail + tail_eocd + 16);
   if ((size_t)cd_offset + cd_size > zip_size)
      goto out;
   if (fseek(file, (long)cd_offset, SEEK_SET) != 0)
      goto out;

   memset(selected, 0, sizeof(*selected));
   for (uint16_t i = 0; i < entries; i++) {
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      uint16_t flags;
      uint16_t method;
      uint32_t compressed_size;
      uint32_t uncompressed_size;
      uint32_t local_offset;
      char name[sizeof(selected->name)];
      size_t copy_len;

      if (fread(fixed, 1, sizeof(fixed), file) != sizeof(fixed))
         goto out;
      if (read_le32(fixed) != 0x02014b50u)
         goto out;
      flags = read_le16(fixed + 8);
      method = read_le16(fixed + 10);
      compressed_size = read_le32(fixed + 20);
      uncompressed_size = read_le32(fixed + 24);
      name_len = read_le16(fixed + 28);
      extra_len = read_le16(fixed + 30);
      comment_len = read_le16(fixed + 32);
      local_offset = read_le32(fixed + 42);
      if ((size_t)local_offset >= zip_size)
         goto out;
      copy_len = name_len;
      if (copy_len >= sizeof(name))
         copy_len = sizeof(name) - 1u;
      if (fread(name, 1, copy_len, file) != copy_len)
         goto out;
      name[copy_len] = '\0';
      if ((size_t)name_len > copy_len &&
          fseek(file, (long)((size_t)name_len - copy_len), SEEK_CUR) != 0)
         goto out;
      if (fseek(file, (long)((size_t)extra_len + comment_len),
          SEEK_CUR) != 0)
         goto out;

      if (!zip_entry_name_is_dir(name) &&
          (method == 0 || method == 8) &&
          !(flags & 1u) &&
          uncompressed_size > 0 &&
          uncompressed_size <= LIBRETRO_ZIP_MAX_UNCOMPRESSED &&
          libretro_valid_extension_matches(name, valid_extensions)) {
         unifrog_text_copy(selected->name, sizeof(selected->name), name);
         selected->flags = flags;
         selected->method = method;
         selected->crc32 = read_le32(fixed + 16);
         selected->compressed_size = compressed_size;
         selected->uncompressed_size = uncompressed_size;
         selected->local_offset = local_offset;
         out_ret = 0;
         break;
      }
   }

out:
   free(tail);
   if (out_ret != 0)
      printf("unifrog libretro zip stream select failed path=%s\n",
         zip_path);
   return out_ret;
}

static int zip_locate_entry_data(FILE *file, const char *zip_path,
   size_t zip_size, const struct zip_rom_entry *entry, size_t *data_offset)
{
   uint8_t local[30];
   uint16_t local_name_len;
   uint16_t local_extra_len;
   size_t offset;

   if (!file || !entry || !data_offset ||
       (size_t)entry->local_offset + sizeof(local) > zip_size)
      return -1;
   if (fseek(file, (long)entry->local_offset, SEEK_SET) != 0 ||
       fread(local, 1, sizeof(local), file) != sizeof(local) ||
       read_le32(local) != 0x04034b50u) {
      printf("unifrog libretro zip local header failed path=%s entry=%s\n",
         zip_path, entry->name);
      return -1;
   }
   if (read_le16(local + 8) != entry->method) {
      printf("unifrog libretro zip method mismatch path=%s entry=%s\n",
         zip_path, entry->name);
      return -1;
   }
   local_name_len = read_le16(local + 26);
   local_extra_len = read_le16(local + 28);
   offset = (size_t)entry->local_offset + sizeof(local) +
      local_name_len + local_extra_len;
   if (offset > zip_size || entry->compressed_size > zip_size - offset) {
      printf("unifrog libretro zip data offset failed path=%s entry=%s\n",
         zip_path, entry->name);
      return -1;
   }
   *data_offset = offset;
   return 0;
}

static int zip_read_stored_stream(FILE *file, const char *zip_path,
   const struct zip_rom_entry *entry, uint8_t *out, size_t out_size)
{
   size_t done = 0;

   if (entry->compressed_size != entry->uncompressed_size ||
       out_size != entry->uncompressed_size)
      return -1;
   loading_draw("LOADING ZIP", "COPYING", 36);
   while (done < out_size) {
      size_t chunk = out_size - done;

      if (chunk > LIBRETRO_CONTENT_STREAM_IN)
         chunk = LIBRETRO_CONTENT_STREAM_IN;
      if (fread(out + done, 1, chunk, file) != chunk) {
         printf("unifrog libretro zip stored read failed path=%s entry=%s done=%u\n",
            zip_path, entry->name, (unsigned)done);
         return -1;
      }
      done += chunk;
      loading_draw("LOADING ZIP", "COPYING",
         36u + (unsigned)((done * 30u) /
         (out_size ? out_size : 1u)));
   }
   return 0;
}

static int zip_inflate_raw_stream(FILE *file, const char *zip_path,
   const struct zip_rom_entry *entry, uint8_t *out, size_t out_size)
{
   z_stream stream;
   uint8_t *in_buf = NULL;
   size_t remaining;
   int zret = Z_OK;
   int out_ret = -1;

   if (!file || !entry || !out || out_size == 0)
      return -1;
   in_buf = malloc(LIBRETRO_CONTENT_STREAM_IN);
   if (!in_buf)
      return -1;
   memset(&stream, 0, sizeof(stream));
   zret = inflateInit2(&stream, -MAX_WBITS);
   if (zret != Z_OK)
      goto out;

   remaining = entry->compressed_size;
   loading_draw("LOADING ZIP", "INFLATE", 36);
   while (remaining > 0 && zret != Z_STREAM_END) {
      size_t chunk = remaining;

      if (chunk > LIBRETRO_CONTENT_STREAM_IN)
         chunk = LIBRETRO_CONTENT_STREAM_IN;
      if (fread(in_buf, 1, chunk, file) != chunk) {
         printf("unifrog libretro zip entry read failed path=%s entry=%s\n",
            zip_path, entry->name);
         goto out_inflate;
      }
      remaining -= chunk;
      stream.next_in = in_buf;
      stream.avail_in = (uInt)chunk;
      while (stream.avail_in > 0 && zret != Z_STREAM_END) {
         if ((size_t)stream.total_out >= out_size)
            goto out_inflate;
         stream.next_out = out + stream.total_out;
         stream.avail_out = (uInt)(out_size - (size_t)stream.total_out);
         zret = inflate(&stream, Z_NO_FLUSH);
         if (zret != Z_OK && zret != Z_STREAM_END)
            goto out_inflate;
      }
      loading_draw("LOADING ZIP", "INFLATE",
         36u + (unsigned)(((entry->compressed_size - remaining) * 30u) /
         (entry->compressed_size ? entry->compressed_size : 1u)));
   }

   if (zret != Z_STREAM_END || stream.total_out != out_size)
      goto out_inflate;
   out_ret = 0;

out_inflate:
   (void)inflateEnd(&stream);
out:
   if (out_ret != 0)
      printf("unifrog libretro zip inflate failed path=%s entry=%s zret=%d out=%u expected=%u\n",
         zip_path, entry ? entry->name : "", zret,
         (unsigned)stream.total_out, (unsigned)out_size);
   free(in_buf);
   return out_ret;
}

static int zip_load_rom_data_stream_entry(FILE *file, const char *zip_path,
   size_t zip_size, const struct zip_rom_entry *entry, uint8_t **out_data,
   size_t *out_size, char *out_name, size_t out_name_size)
{
   uint8_t *rom = NULL;
   size_t data_offset;
   uint64_t start_us = host_time_us();
   uint64_t end_us;
   int ret = -1;

   if (!file || !zip_path || !entry || !out_data || !out_size)
      return -1;
   printf("unifrog libretro zip entry name=%s method=%u compressed=%u uncompressed=%u flags=0x%04x mode=stream_ram\n",
      entry->name, entry->method, entry->compressed_size,
      entry->uncompressed_size, entry->flags);
   if (zip_locate_entry_data(file, zip_path, zip_size, entry,
       &data_offset) != 0)
      return -1;

   rom = rom_alloc_aligned(entry->uncompressed_size);
   if (!rom) {
      printf("unifrog libretro zip data alloc failed path=%s entry=%s uncompressed=%u mode=stream_ram\n",
         zip_path, entry->name, entry->uncompressed_size);
      return -1;
   }
   if (fseek(file, (long)data_offset, SEEK_SET) != 0)
      goto out;
   if (entry->method == 0)
      ret = zip_read_stored_stream(file, zip_path, entry, rom,
         entry->uncompressed_size);
   else
      ret = zip_inflate_raw_stream(file, zip_path, entry, rom,
         entry->uncompressed_size);
   if (ret != 0)
      goto out;
   if ((uint32_t)crc32(crc32(0L, Z_NULL, 0), rom,
       (uInt)entry->uncompressed_size) != entry->crc32) {
      printf("unifrog libretro zip crc mismatch path=%s entry=%s\n",
         zip_path, entry->name);
      ret = -1;
      goto out;
   }

   end_us = host_time_us();
   if (out_name && out_name_size)
      unifrog_text_copy(out_name, out_name_size, entry->name);
   *out_data = rom;
   *out_size = entry->uncompressed_size;
   rom = NULL;
   printf("unifrog libretro zip loaded entry=%s size=%u aligned=%lu mode=stream_ram\n",
      entry->name, (unsigned)*out_size,
      (unsigned long)((uintptr_t)*out_data & 31u));
   printf("unifrog load_time stage=zip_memory ms=%u compressed=%u uncompressed=%u mode=stream_ram method=%u\n",
      host_elapsed_ms(start_us, end_us), entry->compressed_size,
      entry->uncompressed_size, entry->method);
   loading_draw("LOADING ZIP", "READY", 68);
   ret = 0;

out:
   rom_free_aligned(rom);
   return ret;
}

static int zip_load_rom_data_stream(FILE *file, const char *zip_path,
   const struct retro_system_info *info, uint8_t **out_data,
   size_t *out_size, char *out_name, size_t out_name_size)
{
   struct zip_rom_entry entry;
   size_t zip_size;

   if (!file || !zip_path || !info || !out_data || !out_size)
      return -1;
   if (file_size(file, &zip_size) != 0)
      return -1;
   if (zip_select_rom_entry_stream(file, zip_path, zip_size,
       info->valid_extensions, &entry) != 0)
      return -1;
   return zip_load_rom_data_stream_entry(file, zip_path, zip_size, &entry,
      out_data, out_size, out_name, out_name_size);
}

static int zip_load_rom_data_cached(FILE *file, const char *zip_path,
   const struct retro_system_info *info, uint8_t **out_data,
   size_t *out_size, int *out_cache)
{
   struct zip_rom_entry entry;
   size_t zip_size;
   char cache_path[160];

   if (out_cache)
      *out_cache = 0;
   if (!file || !zip_path || !info || !out_data || !out_size)
      return -1;
   if (file_size(file, &zip_size) != 0)
      return -1;
   if (zip_select_rom_entry_stream(file, zip_path, zip_size,
       info->valid_extensions, &entry) != 0)
      return -1;

   cache_path[0] = '\0';
   if (content_zip_cache_path(zip_path, &entry, info->valid_extensions,
       cache_path, sizeof(cache_path)) == 0 &&
       content_cache_file_valid(cache_path, entry.uncompressed_size)) {
      FILE *cache_file = fopen(cache_path, "rb");

      if (cache_file) {
         if (read_path_aligned_direct(cache_path, out_data, out_size,
             "READ ZIP CACHE") == 0 ||
             read_file_aligned(cache_file, cache_path, out_data, out_size,
             "READ ZIP CACHE") == 0) {
            fclose(cache_file);
            if (*out_size == entry.uncompressed_size) {
               if (out_cache)
                  *out_cache = 1;
               printf("unifrog libretro zip cache hit path=%s cache=%s entry=%s size=%u\n",
                  zip_path, cache_path, entry.name, (unsigned)*out_size);
               loading_draw("LOADING ROM", "CACHE", 68);
               return 0;
            }
            rom_free_aligned(*out_data);
            *out_data = NULL;
            *out_size = 0;
         } else {
            fclose(cache_file);
         }
      }
      printf("unifrog libretro zip cache read failed path=%s cache=%s\n",
         zip_path, cache_path);
      unlink(cache_path);
   }

   if (zip_load_rom_data_stream_entry(file, zip_path, zip_size, &entry,
       out_data, out_size, NULL, 0) != 0)
      return -1;

   if (cache_path[0] &&
       content_cache_write_buffer(cache_path, *out_data, *out_size) == 0) {
      printf("unifrog libretro zip cache write path=%s cache=%s entry=%s size=%u\n",
         zip_path, cache_path, entry.name, (unsigned)*out_size);
   }
   return 0;
}

static int read_file_aligned(FILE *file, const char *path, uint8_t **out_data,
   size_t *out_size, const char *label)
{
   uint8_t *data = NULL;
   size_t size;

   if (!file || !out_data || !out_size)
      return -1;
   if (file_size(file, &size) != 0)
      return -1;
   data = rom_alloc_aligned(size);
   if (!data) {
      printf("unifrog libretro rom alloc failed path=%s size=%u label=%s\n",
         path ? path : "", (unsigned)size, label ? label : "");
      return -1;
   }
   for (size_t done = 0; done < size;) {
      size_t chunk = size - done;

      if (chunk > LIBRETRO_CONTENT_READ_CHUNK)
         chunk = LIBRETRO_CONTENT_READ_CHUNK;
      if (fread(data + done, 1, chunk, file) != chunk) {
         printf("unifrog libretro rom read failed path=%s size=%u done=%u label=%s\n",
            path ? path : "", (unsigned)size, (unsigned)done,
            label ? label : "");
         rom_free_aligned(data);
         return -1;
      }
      done += chunk;
      loading_draw("LOADING ROM", label ? label : "READING",
         12u + (unsigned)((done * 58u) / (size ? size : 1u)));
   }
   *out_data = data;
   *out_size = size;
   return 0;
}

static int read_path_heap_sequential(const char *path, uint8_t **out_data,
   size_t *out_size, size_t max_size, const char *label)
{
   uint8_t *data = NULL;
   struct stat st;
   size_t size;
   uint64_t start_us;
   uint64_t end_us;
   int fd = -1;
   int ret = -1;

   if (!path || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;
   if (stat(path, &st) != 0 || st.st_size <= 0)
      return -1;
   size = (size_t)st.st_size;
   if (size > max_size) {
      printf("unifrog libretro compressed input too large path=%s size=%u max=%u label=%s\n",
         path, (unsigned)size, (unsigned)max_size, label ? label : "");
      return -1;
   }
   data = malloc(size);
   if (!data) {
      printf("unifrog libretro compressed input alloc failed path=%s size=%u label=%s\n",
         path, (unsigned)size, label ? label : "");
      return -1;
   }
   fd = open(path, O_RDONLY);
   if (fd < 0) {
      printf("unifrog libretro compressed open failed path=%s errno=%d label=%s\n",
         path, errno, label ? label : "");
      goto out;
   }
   start_us = host_time_us();
   if (read_fd_fully_to_buffer(fd, path, data, size,
       label ? label : "LOADING", "READING", 10, 20) != 0)
      goto out;
   end_us = host_time_us();

   *out_data = data;
   *out_size = size;
   data = NULL;
   printf("unifrog load_time stage=file_read mode=fd_heap ms=%u bytes=%u chunk=%u label=%s path=%s\n",
      host_elapsed_ms(start_us, end_us), (unsigned)size,
      (unsigned)LIBRETRO_CONTENT_READ_CHUNK, label ? label : "", path);
   ret = 0;

out:
   if (fd >= 0)
      close(fd);
   free(data);
   return ret;
}

static int decompress_zstd_memory(const char *path, const uint8_t *compressed,
   size_t compressed_size, uint8_t **out_data, size_t *out_size)
{
   unsigned long long frame_size;
   uint8_t *rom = NULL;
   size_t ret;

   frame_size = ZSTD_getFrameContentSize(compressed, compressed_size);
   if (frame_size == ZSTD_CONTENTSIZE_ERROR ||
       frame_size == ZSTD_CONTENTSIZE_UNKNOWN ||
       frame_size == 0 ||
       frame_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED) {
      printf("unifrog libretro zstd memory size unsupported path=%s compressed=%u frame=%llu\n",
         path, (unsigned)compressed_size, frame_size);
      return -1;
   }
   rom = rom_alloc_aligned((size_t)frame_size);
   if (!rom) {
      printf("unifrog libretro zstd memory alloc failed path=%s compressed=%u uncompressed=%u\n",
         path, (unsigned)compressed_size, (unsigned)frame_size);
      return -1;
   }
   loading_draw("LOADING ZSTD", "DECOMPRESS", 36);
   ret = ZSTD_decompress(rom, (size_t)frame_size, compressed, compressed_size);
   if (ZSTD_isError(ret) || ret != (size_t)frame_size) {
      printf("unifrog libretro zstd memory decode failed path=%s err=%s out=%u expected=%u\n",
         path, ZSTD_isError(ret) ? ZSTD_getErrorName(ret) : "short_output",
         (unsigned)ret, (unsigned)frame_size);
      rom_free_aligned(rom);
      return -1;
   }
   *out_data = rom;
   *out_size = (size_t)frame_size;
   return 0;
}

static int reserve_heap_output(uint8_t **data, size_t *capacity,
   size_t required, size_t max_size, const char *path, const char *type)
{
   size_t new_capacity;
   uint8_t *new_data;

   if (!data || !capacity || required > max_size)
      return -1;
   if (*capacity >= required)
      return 0;

   new_capacity = *capacity ? *capacity : LIBRETRO_COMPRESSED_GROW_INITIAL;
   if (new_capacity > max_size)
      new_capacity = max_size;
   while (new_capacity < required) {
      if (new_capacity > max_size / 2u) {
         new_capacity = max_size;
         break;
      }
      new_capacity *= 2u;
   }
   if (new_capacity < required)
      return -1;

   new_data = realloc(*data, new_capacity);
   if (!new_data) {
      printf("unifrog libretro %s memory grow failed path=%s required=%u capacity=%u max=%u\n",
         type ? type : "compressed", path ? path : "",
         (unsigned)required, (unsigned)new_capacity, (unsigned)max_size);
      return -1;
   }
   *data = new_data;
   *capacity = new_capacity;
   return 0;
}

static int align_heap_output(const char *path, const char *type,
   uint8_t *heap_data, size_t heap_size, uint8_t **out_data, size_t *out_size)
{
   uint8_t *rom;

   if (!heap_data || heap_size == 0 ||
       heap_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED)
      return -1;
   rom = rom_alloc_aligned(heap_size);
   if (!rom) {
      printf("unifrog libretro %s memory final alloc failed path=%s size=%u\n",
         type ? type : "compressed", path ? path : "", (unsigned)heap_size);
      return -1;
   }
   memcpy(rom, heap_data, heap_size);
   *out_data = rom;
   *out_size = heap_size;
   return 0;
}

static int decompress_zstd_memory_stream(const char *path,
   const uint8_t *compressed, size_t compressed_size, uint8_t **out_data,
   size_t *out_size)
{
   ZSTD_DStream *stream = NULL;
   ZSTD_inBuffer input;
   uint8_t *heap_data = NULL;
   size_t heap_capacity = 0;
   int done = 0;
   int out_ret = -1;

   stream = ZSTD_createDStream();
   if (!stream)
      return -1;
   if (ZSTD_isError(ZSTD_initDStream(stream)))
      goto out;

   input.src = compressed;
   input.size = compressed_size;
   input.pos = 0;
   loading_draw("LOADING ZSTD", "DECOMPRESS", 36);
   while (!done && input.pos < input.size) {
      ZSTD_outBuffer output;
      size_t ret;
      size_t before_in;
      size_t before_out;

      if (reserve_heap_output(&heap_data, &heap_capacity,
          *out_size + LIBRETRO_CONTENT_STREAM_OUT,
          LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED, path, "zstd") != 0)
         goto out;

      output.dst = heap_data + *out_size;
      output.size = heap_capacity - *out_size;
      output.pos = 0;
      before_in = input.pos;
      before_out = output.pos;
      ret = ZSTD_decompressStream(stream, &output, &input);
      if (ZSTD_isError(ret)) {
         printf("unifrog libretro zstd memory decode failed path=%s err=%s\n",
            path, ZSTD_getErrorName(ret));
         goto out;
      }
      *out_size += output.pos;
      if (ret == 0)
         done = 1;
      if (input.pos == before_in && output.pos == before_out && !done)
         goto out;
      loading_draw("LOADING ZSTD", "DECOMPRESS",
         36u + (unsigned)((input.pos * 30u) /
         (compressed_size ? compressed_size : 1u)));
   }

   if (!done || align_heap_output(path, "zstd", heap_data, *out_size,
       out_data, out_size) != 0)
      goto out;
   out_ret = 0;

out:
   free(heap_data);
   if (stream)
      ZSTD_freeDStream(stream);
   return out_ret;
}

static int zstd_file_frame_size(const char *path, size_t compressed_size,
   size_t *out_size)
{
   FILE *file = NULL;
   uint8_t *header = NULL;
   size_t got;
   size_t want = compressed_size;
   unsigned long long frame_size;
   int ret = -1;

   if (!path || !out_size || compressed_size == 0)
      return -1;
   *out_size = 0;
   if (want > LIBRETRO_CONTENT_STREAM_IN)
      want = LIBRETRO_CONTENT_STREAM_IN;
   header = malloc(want);
   if (!header)
      return -1;
   file = fopen(path, "rb");
   if (!file)
      goto out;
   got = fread(header, 1, want, file);
   if (got == 0)
      goto out;

   frame_size = ZSTD_getFrameContentSize(header, got);
   if (frame_size == ZSTD_CONTENTSIZE_ERROR ||
       frame_size == ZSTD_CONTENTSIZE_UNKNOWN ||
       frame_size == 0 ||
       frame_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED)
      goto out;
   *out_size = (size_t)frame_size;
   ret = 0;

out:
   if (file)
      fclose(file);
   free(header);
   return ret;
}

static int zstd_stream_count_output(const char *path, size_t compressed_size,
   size_t *out_size)
{
   FILE *file = NULL;
   ZSTD_DStream *stream = NULL;
   uint8_t *in_buf = NULL;
   uint8_t *scratch = NULL;
   size_t in_cap = ZSTD_DStreamInSize();
   size_t out_cap = ZSTD_DStreamOutSize();
   size_t read_total = 0;
   int done = 0;
   int out_ret = -1;

   if (!path || !out_size)
      return -1;
   *out_size = 0;
   if (in_cap == 0 || in_cap > LIBRETRO_CONTENT_STREAM_IN)
      in_cap = LIBRETRO_CONTENT_STREAM_IN;
   if (out_cap == 0 || out_cap > LIBRETRO_CONTENT_STREAM_OUT)
      out_cap = LIBRETRO_CONTENT_STREAM_OUT;
   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro zstd stream open failed path=%s mode=count\n",
         path);
      return -1;
   }
   stream = ZSTD_createDStream();
   if (!stream || ZSTD_isError(ZSTD_initDStream(stream)))
      goto out;
   in_buf = malloc(in_cap);
   scratch = malloc(out_cap);
   if (!in_buf || !scratch)
      goto out;

   loading_draw("LOADING ZSTD", "MEASURE", 34);
   while (!done) {
      size_t got = fread(in_buf, 1, in_cap, file);
      ZSTD_inBuffer input;

      if (got == 0) {
         if (ferror(file))
            goto out;
         break;
      }
      read_total += got;
      input.src = in_buf;
      input.size = got;
      input.pos = 0;
      while (input.pos < input.size) {
         ZSTD_outBuffer output;
         size_t before_in;
         size_t ret;

         output.dst = scratch;
         output.size = out_cap;
         output.pos = 0;
         before_in = input.pos;
         ret = ZSTD_decompressStream(stream, &output, &input);
         if (ZSTD_isError(ret)) {
            printf("unifrog libretro zstd stream count failed path=%s err=%s\n",
               path, ZSTD_getErrorName(ret));
            goto out;
         }
         if (output.pos > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED - *out_size) {
            printf("unifrog libretro zstd stream count too large path=%s out=%u max=%u\n",
               path, (unsigned)*out_size,
               (unsigned)LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED);
            goto out;
         }
         *out_size += output.pos;
         if (ret == 0) {
            done = 1;
            break;
         }
         if (input.pos == before_in && output.pos == 0)
            goto out;
      }
      loading_draw("LOADING ZSTD", "MEASURE",
         34u + (unsigned)((read_total * 18u) /
         (compressed_size ? compressed_size : 1u)));
   }

   out_ret = done && *out_size > 0 ? 0 : -1;

out:
   free(scratch);
   free(in_buf);
   if (stream)
      ZSTD_freeDStream(stream);
   if (file)
      fclose(file);
   return out_ret;
}

static int zstd_stream_to_buffer(const char *path, size_t compressed_size,
   uint8_t *out_data, size_t out_size)
{
   FILE *file = NULL;
   ZSTD_DStream *stream = NULL;
   uint8_t *in_buf = NULL;
   size_t in_cap = ZSTD_DStreamInSize();
   size_t read_total = 0;
   size_t dst_pos = 0;
   int done = 0;
   int out_ret = -1;

   if (!path || !out_data || out_size == 0)
      return -1;
   if (in_cap == 0 || in_cap > LIBRETRO_CONTENT_STREAM_IN)
      in_cap = LIBRETRO_CONTENT_STREAM_IN;
   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro zstd stream open failed path=%s mode=decode\n",
         path);
      return -1;
   }
   stream = ZSTD_createDStream();
   if (!stream || ZSTD_isError(ZSTD_initDStream(stream)))
      goto out;
   in_buf = malloc(in_cap);
   if (!in_buf)
      goto out;

   loading_draw("LOADING ZSTD", "DECOMPRESS", 52);
   while (!done) {
      size_t got = fread(in_buf, 1, in_cap, file);
      ZSTD_inBuffer input;

      if (got == 0) {
         if (ferror(file))
            goto out;
         break;
      }
      read_total += got;
      input.src = in_buf;
      input.size = got;
      input.pos = 0;
      while (input.pos < input.size) {
         ZSTD_outBuffer output;
         size_t before_in;
         size_t ret;

         if (dst_pos >= out_size)
            goto out;
         output.dst = out_data + dst_pos;
         output.size = out_size - dst_pos;
         output.pos = 0;
         before_in = input.pos;
         ret = ZSTD_decompressStream(stream, &output, &input);
         if (ZSTD_isError(ret)) {
            printf("unifrog libretro zstd stream decode failed path=%s err=%s\n",
               path, ZSTD_getErrorName(ret));
            goto out;
         }
         dst_pos += output.pos;
         if (ret == 0) {
            done = 1;
            break;
         }
         if (input.pos == before_in && output.pos == 0)
            goto out;
      }
      loading_draw("LOADING ZSTD", "DECOMPRESS",
         52u + (unsigned)((read_total * 16u) /
         (compressed_size ? compressed_size : 1u)));
   }

   if (!done || dst_pos != out_size) {
      printf("unifrog libretro zstd stream incomplete path=%s out=%u expected=%u\n",
         path, (unsigned)dst_pos, (unsigned)out_size);
      goto out;
   }
   out_ret = 0;

out:
   free(in_buf);
   if (stream)
      ZSTD_freeDStream(stream);
   if (file)
      fclose(file);
   return out_ret;
}

static int load_zstd_rom_data_stream(const char *path, uint64_t start_us,
   uint8_t **out_data, size_t *out_size)
{
   struct stat st;
   uint8_t *rom = NULL;
   uint64_t count_done_us;
   uint64_t alloc_done_us;
   uint64_t decode_done_us;
   size_t compressed_size;
   size_t expected_size = 0;
   int counted = 0;
   int out_ret = -1;

   if (!path || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;
   if (stat(path, &st) != 0 || st.st_size <= 0)
      return -1;
   compressed_size = (size_t)st.st_size;
   if (compressed_size > LIBRETRO_COMPRESSED_MAX_INPUT) {
      printf("unifrog libretro zstd stream input too large path=%s size=%u max=%u\n",
         path, (unsigned)compressed_size,
         (unsigned)LIBRETRO_COMPRESSED_MAX_INPUT);
      return -1;
   }

   if (zstd_file_frame_size(path, compressed_size, &expected_size) != 0) {
      if (zstd_stream_count_output(path, compressed_size, &expected_size) != 0)
         return -1;
      counted = 1;
   }
   count_done_us = host_time_us();
   printf("unifrog libretro zstd stream %s path=%s compressed=%u uncompressed=%u\n",
      counted ? "counted" : "sized", path, (unsigned)compressed_size,
      (unsigned)expected_size);

   rom = rom_alloc_aligned(expected_size);
   alloc_done_us = host_time_us();
   if (!rom) {
      printf("unifrog libretro zstd stream alloc failed path=%s compressed=%u uncompressed=%u\n",
         path, (unsigned)compressed_size, (unsigned)expected_size);
      goto out;
   }

   if (zstd_stream_to_buffer(path, compressed_size, rom, expected_size) != 0)
      goto out;
   decode_done_us = host_time_us();

   *out_data = rom;
   *out_size = expected_size;
   rom = NULL;
   printf("unifrog libretro compressed memory loaded path=%s type=zstd compressed=%u uncompressed=%u aligned=%lu mode=stream_ram\n",
      path, (unsigned)compressed_size, (unsigned)*out_size,
      (unsigned long)((uintptr_t)*out_data & 31u));
   printf("unifrog load_time stage=compressed_memory type=zstd count_ms=%u alloc_ms=%u decode_ms=%u total_ms=%u compressed=%u uncompressed=%u mode=stream_ram size_source=%s\n",
      host_elapsed_ms(start_us, count_done_us),
      host_elapsed_ms(count_done_us, alloc_done_us),
      host_elapsed_ms(alloc_done_us, decode_done_us),
      host_elapsed_ms(start_us, decode_done_us),
      (unsigned)compressed_size, (unsigned)*out_size,
      counted ? "scan" : "frame");
   loading_draw("LOADING ROM", "READY", 68);
   out_ret = 0;

out:
   rom_free_aligned(rom);
   return out_ret;
}

static int decompress_lz4_count_output(const char *path,
   const uint8_t *compressed, size_t compressed_size, LZ4F_dctx *ctx,
   size_t *out_size)
{
   uint8_t *scratch = NULL;
   size_t src_pos = 0;
   int done = 0;
   int out_ret = -1;

   if (!compressed || !ctx || !out_size)
      return -1;
   *out_size = 0;
   scratch = malloc(LIBRETRO_CONTENT_STREAM_OUT);
   if (!scratch)
      return -1;

   loading_draw("LOADING LZ4", "MEASURE", 34);
   while (!done && src_pos < compressed_size) {
      size_t src_size = compressed_size - src_pos;
      size_t dst_size = LIBRETRO_CONTENT_STREAM_OUT;
      size_t before_src = src_pos;
      size_t ret;

      ret = LZ4F_decompress(ctx, scratch, &dst_size,
         compressed + src_pos, &src_size, NULL);
      if (LZ4F_isError(ret)) {
         printf("unifrog libretro lz4 memory count failed path=%s err=%s\n",
            path, LZ4F_getErrorName(ret));
         goto out;
      }
      if (dst_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED - *out_size) {
         printf("unifrog libretro lz4 memory count too large path=%s out=%u max=%u\n",
            path ? path : "", (unsigned)*out_size,
            (unsigned)LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED);
         goto out;
      }
      src_pos += src_size;
      *out_size += dst_size;
      if (ret == 0)
         done = 1;
      if (src_pos == before_src && dst_size == 0 && !done)
         goto out;
      loading_draw("LOADING LZ4", "MEASURE",
         34u + (unsigned)((src_pos * 18u) /
         (compressed_size ? compressed_size : 1u)));
   }

   out_ret = done && *out_size > 0 ? 0 : -1;

out:
   free(scratch);
   return out_ret;
}

static int decompress_lz4_to_buffer(const char *path,
   const uint8_t *compressed, size_t compressed_size, LZ4F_dctx *ctx,
   uint8_t *out_data, size_t out_size)
{
   uint8_t overflow[64];
   size_t src_pos = 0;
   size_t dst_pos = 0;
   int done = 0;
   int out_ret = -1;

   if (!compressed || !ctx || !out_data || out_size == 0)
      return -1;

   loading_draw("LOADING LZ4", "DECOMPRESS", 52);
   while (!done && src_pos < compressed_size) {
      LZ4F_decompressOptions_t opts;
      size_t src_size = compressed_size - src_pos;
      size_t dst_size;
      size_t before_src = src_pos;
      size_t ret;
      uint8_t *dst;

      if (dst_pos >= out_size) {
         dst = overflow;
         dst_size = sizeof(overflow);
      } else {
         dst = out_data + dst_pos;
         dst_size = out_size - dst_pos;
      }
      memset(&opts, 0, sizeof(opts));
      opts.stableDst = 1;
      ret = LZ4F_decompress(ctx, dst, &dst_size,
         compressed + src_pos, &src_size, &opts);
      if (LZ4F_isError(ret)) {
         printf("unifrog libretro lz4 memory decode failed path=%s err=%s\n",
            path, LZ4F_getErrorName(ret));
         goto out;
      }
      src_pos += src_size;
      if (dst_pos >= out_size) {
         if (dst_size != 0) {
            printf("unifrog libretro lz4 memory output exceeded path=%s out=%u extra=%u expected=%u\n",
               path, (unsigned)dst_pos, (unsigned)dst_size,
               (unsigned)out_size);
            goto out;
         }
      } else {
         dst_pos += dst_size;
      }
      if (ret == 0)
         done = 1;
      if (src_pos == before_src && dst_size == 0 && !done)
         goto out;
      loading_draw("LOADING LZ4", "DECOMPRESS",
         52u + (unsigned)((src_pos * 16u) /
         (compressed_size ? compressed_size : 1u)));
   }

   if (!done || dst_pos != out_size) {
      printf("unifrog libretro lz4 memory incomplete path=%s out=%u expected=%u\n",
         path, (unsigned)dst_pos, (unsigned)out_size);
      goto out;
   }
   out_ret = 0;

out:
   return out_ret;
}

static int decompress_lz4_memory(const char *path, const uint8_t *compressed,
   size_t compressed_size, uint64_t start_us, uint64_t read_done_us,
   uint8_t **out_data, size_t *out_size)
{
   LZ4F_dctx *ctx = NULL;
   uint8_t *rom = NULL;
   uint64_t count_done_us;
   uint64_t decode_done_us;
   size_t expected_size = 0;
   int out_ret = -1;

   if (!compressed || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;

   if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION)))
      goto out;
   if (decompress_lz4_count_output(path, compressed, compressed_size, ctx,
       &expected_size) != 0)
      goto out;
   count_done_us = host_time_us();
   printf("unifrog libretro lz4 memory counted path=%s compressed=%u uncompressed=%u\n",
      path, (unsigned)compressed_size, (unsigned)expected_size);

   rom = rom_alloc_aligned(expected_size);
   if (!rom) {
      printf("unifrog libretro lz4 memory alloc failed path=%s compressed=%u uncompressed=%u\n",
         path, (unsigned)compressed_size, (unsigned)expected_size);
      goto out;
   }

   LZ4F_resetDecompressionContext(ctx);
   if (decompress_lz4_to_buffer(path, compressed, compressed_size, ctx,
       rom, expected_size) != 0)
      goto out;
   decode_done_us = host_time_us();

   *out_data = rom;
   *out_size = expected_size;
   rom = NULL;
   printf("unifrog libretro compressed memory loaded path=%s type=lz4 compressed=%u uncompressed=%u aligned=%lu mode=ram\n",
      path, (unsigned)compressed_size, (unsigned)*out_size,
      (unsigned long)((uintptr_t)*out_data & 31u));
   printf("unifrog load_time stage=compressed_memory type=lz4 read_ms=%u count_ms=%u decode_ms=%u total_ms=%u compressed=%u uncompressed=%u mode=ram\n",
      host_elapsed_ms(start_us, read_done_us),
      host_elapsed_ms(read_done_us, count_done_us),
      host_elapsed_ms(count_done_us, decode_done_us),
      host_elapsed_ms(start_us, decode_done_us),
      (unsigned)compressed_size, (unsigned)*out_size);
   loading_draw("LOADING ROM", "READY", 68);
   out_ret = 0;

out:
   rom_free_aligned(rom);
   if (ctx)
      LZ4F_freeDecompressionContext(ctx);
   return out_ret;
}

static int lz4_stream_count_output(const char *path, size_t compressed_size,
   size_t *out_size)
{
   FILE *file = NULL;
   LZ4F_dctx *ctx = NULL;
   uint8_t *in_buf = NULL;
   uint8_t *scratch = NULL;
   size_t read_total = 0;
   int done = 0;
   int out_ret = -1;

   if (!path || !out_size)
      return -1;
   *out_size = 0;
   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro lz4 stream open failed path=%s mode=count\n",
         path);
      return -1;
   }
   if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION)))
      goto out;
   in_buf = malloc(LIBRETRO_CONTENT_STREAM_IN);
   scratch = malloc(LIBRETRO_CONTENT_STREAM_OUT);
   if (!in_buf || !scratch)
      goto out;

   loading_draw("LOADING LZ4", "MEASURE", 34);
   while (!done) {
      size_t in_size = fread(in_buf, 1, LIBRETRO_CONTENT_STREAM_IN, file);
      size_t in_pos = 0;

      if (in_size == 0) {
         if (ferror(file))
            goto out;
         break;
      }
      read_total += in_size;
      while (in_pos < in_size) {
         size_t src_size = in_size - in_pos;
         size_t dst_size = LIBRETRO_CONTENT_STREAM_OUT;
         size_t before_in = in_pos;
         size_t ret = LZ4F_decompress(ctx, scratch, &dst_size,
            in_buf + in_pos, &src_size, NULL);

         if (LZ4F_isError(ret)) {
            printf("unifrog libretro lz4 stream count failed path=%s err=%s\n",
               path, LZ4F_getErrorName(ret));
            goto out;
         }
         if (dst_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED - *out_size) {
            printf("unifrog libretro lz4 stream count too large path=%s out=%u max=%u\n",
               path, (unsigned)*out_size,
               (unsigned)LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED);
            goto out;
         }
         in_pos += src_size;
         *out_size += dst_size;
         if (ret == 0) {
            done = 1;
            break;
         }
         if (in_pos == before_in && dst_size == 0)
            goto out;
      }
      loading_draw("LOADING LZ4", "MEASURE",
         34u + (unsigned)((read_total * 18u) /
         (compressed_size ? compressed_size : 1u)));
   }

   out_ret = done && *out_size > 0 ? 0 : -1;

out:
   free(scratch);
   free(in_buf);
   if (ctx)
      LZ4F_freeDecompressionContext(ctx);
   if (file)
      fclose(file);
   return out_ret;
}

static int lz4_file_frame_size(const char *path, size_t compressed_size,
   size_t *out_size)
{
   FILE *file = NULL;
   LZ4F_dctx *ctx = NULL;
   LZ4F_frameInfo_t info;
   uint8_t header[LZ4F_HEADER_SIZE_MAX];
   size_t got;
   size_t src_size;
   size_t ret;
   int out_ret = -1;

   if (!path || !out_size || compressed_size == 0)
      return -1;
   *out_size = 0;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   got = fread(header, 1, sizeof(header), file);
   if (got < LZ4F_HEADER_SIZE_MIN)
      goto out;
   if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION)))
      goto out;

   memset(&info, 0, sizeof(info));
   src_size = got;
   ret = LZ4F_getFrameInfo(ctx, &info, header, &src_size);
   if (LZ4F_isError(ret) || info.contentSize == 0 ||
       info.contentSize > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED)
      goto out;
   *out_size = (size_t)info.contentSize;
   out_ret = 0;

out:
   if (ctx)
      LZ4F_freeDecompressionContext(ctx);
   if (file)
      fclose(file);
   return out_ret;
}

static int lz4_stream_to_buffer(const char *path, size_t compressed_size,
   uint8_t *out_data, size_t out_size, LZ4F_dctx *ctx, uint8_t *in_buf,
   size_t in_cap)
{
   FILE *file = NULL;
   uint8_t overflow[64];
   size_t read_total = 0;
   size_t dst_pos = 0;
   int done = 0;
   int out_ret = -1;

   if (!path || !out_data || out_size == 0 || !ctx || !in_buf ||
       in_cap == 0)
      return -1;
   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro lz4 stream open failed path=%s mode=decode\n",
         path);
      return -1;
   }
   LZ4F_resetDecompressionContext(ctx);

   loading_draw("LOADING LZ4", "DECOMPRESS", 52);
   while (!done) {
      size_t in_size = fread(in_buf, 1, in_cap, file);
      size_t in_pos = 0;

      if (in_size == 0) {
         if (ferror(file)) {
            printf("unifrog libretro lz4 stream read failed path=%s read=%u\n",
               path, (unsigned)read_total);
            goto out;
         }
         break;
      }
      read_total += in_size;
      while (in_pos < in_size) {
         LZ4F_decompressOptions_t opts;
         size_t src_size = in_size - in_pos;
         size_t dst_size;
         size_t before_in = in_pos;
         size_t ret;
         uint8_t *dst;

         if (dst_pos >= out_size) {
            dst = overflow;
            dst_size = sizeof(overflow);
         } else {
            dst = out_data + dst_pos;
            dst_size = out_size - dst_pos;
         }
         memset(&opts, 0, sizeof(opts));
         opts.stableDst = 1;
         ret = LZ4F_decompress(ctx, dst, &dst_size,
            in_buf + in_pos, &src_size, &opts);
         if (LZ4F_isError(ret)) {
            printf("unifrog libretro lz4 stream decode failed path=%s err=%s\n",
               path, LZ4F_getErrorName(ret));
            goto out;
         }
         in_pos += src_size;
         if (dst_pos >= out_size) {
            if (dst_size != 0) {
               printf("unifrog libretro lz4 stream output exceeded path=%s out=%u extra=%u expected=%u\n",
                  path, (unsigned)dst_pos, (unsigned)dst_size,
                  (unsigned)out_size);
               goto out;
            }
         } else {
            dst_pos += dst_size;
         }
         if (ret == 0) {
            done = 1;
            break;
         }
         if (in_pos == before_in && dst_size == 0) {
            printf("unifrog libretro lz4 stream no progress path=%s read=%u in_pos=%u in_size=%u out=%u need=%u\n",
               path, (unsigned)read_total, (unsigned)in_pos,
               (unsigned)in_size, (unsigned)dst_pos, (unsigned)ret);
            goto out;
         }
      }
      loading_draw("LOADING LZ4", "DECOMPRESS",
         52u + (unsigned)((read_total * 16u) /
         (compressed_size ? compressed_size : 1u)));
   }

   if (!done || dst_pos != out_size) {
      printf("unifrog libretro lz4 stream incomplete path=%s out=%u expected=%u\n",
         path, (unsigned)dst_pos, (unsigned)out_size);
      goto out;
   }
   out_ret = 0;

out:
   if (file)
      fclose(file);
   return out_ret;
}

static int load_lz4_rom_data_stream(const char *path, uint64_t start_us,
   uint8_t **out_data, size_t *out_size)
{
   struct stat st;
   uint8_t *rom = NULL;
   uint8_t *decode_in = NULL;
   LZ4F_dctx *decode_ctx = NULL;
   uint64_t count_done_us;
   uint64_t alloc_done_us;
   uint64_t decode_done_us;
   size_t compressed_size;
   size_t expected_size = 0;
   int counted = 0;
   int out_ret = -1;

   if (!path || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;
   if (stat(path, &st) != 0 || st.st_size <= 0)
      return -1;
   compressed_size = (size_t)st.st_size;
   if (compressed_size > LIBRETRO_COMPRESSED_MAX_INPUT) {
      printf("unifrog libretro lz4 stream input too large path=%s size=%u max=%u\n",
         path, (unsigned)compressed_size,
         (unsigned)LIBRETRO_COMPRESSED_MAX_INPUT);
      return -1;
   }

   if (lz4_file_frame_size(path, compressed_size, &expected_size) != 0) {
      if (lz4_stream_count_output(path, compressed_size, &expected_size) != 0)
         return -1;
      counted = 1;
   }
   count_done_us = host_time_us();
   printf("unifrog libretro lz4 stream %s path=%s compressed=%u uncompressed=%u\n",
      counted ? "counted" : "sized", path, (unsigned)compressed_size,
      (unsigned)expected_size);

   decode_in = malloc(LIBRETRO_CONTENT_STREAM_IN);
   if (!decode_in) {
      printf("unifrog libretro lz4 stream work alloc failed path=%s bytes=%u\n",
         path, (unsigned)LIBRETRO_CONTENT_STREAM_IN);
      goto out;
   }
   if (LZ4F_isError(LZ4F_createDecompressionContext(&decode_ctx,
       LZ4F_VERSION))) {
      printf("unifrog libretro lz4 stream context alloc failed path=%s\n",
         path);
      goto out;
   }

   rom = rom_alloc_aligned(expected_size);
   alloc_done_us = host_time_us();
   if (!rom) {
      printf("unifrog libretro lz4 stream alloc failed path=%s compressed=%u uncompressed=%u\n",
         path, (unsigned)compressed_size, (unsigned)expected_size);
      goto out;
   }

   if (lz4_stream_to_buffer(path, compressed_size, rom, expected_size,
       decode_ctx, decode_in, LIBRETRO_CONTENT_STREAM_IN) != 0)
      goto out;
   decode_done_us = host_time_us();

   *out_data = rom;
   *out_size = expected_size;
   rom = NULL;
   printf("unifrog libretro compressed memory loaded path=%s type=lz4 compressed=%u uncompressed=%u aligned=%lu mode=stream_ram\n",
      path, (unsigned)compressed_size, (unsigned)*out_size,
      (unsigned long)((uintptr_t)*out_data & 31u));
   printf("unifrog load_time stage=compressed_memory type=lz4 read_ms=%u count_ms=%u alloc_ms=%u decode_ms=%u total_ms=%u compressed=%u uncompressed=%u mode=stream_ram size_source=%s\n",
      0u,
      host_elapsed_ms(start_us, count_done_us),
      host_elapsed_ms(count_done_us, alloc_done_us),
      host_elapsed_ms(alloc_done_us, decode_done_us),
      host_elapsed_ms(start_us, decode_done_us),
      (unsigned)compressed_size, (unsigned)*out_size,
      counted ? "scan" : "frame");
   loading_draw("LOADING ROM", "READY", 68);
   out_ret = 0;

out:
   rom_free_aligned(rom);
   if (decode_ctx)
      LZ4F_freeDecompressionContext(decode_ctx);
   free(decode_in);
   return out_ret;
}

static int load_wrapped_compressed_rom_data(const char *path,
   uint8_t **out_data, size_t *out_size)
{
   uint8_t *compressed = NULL;
   size_t compressed_size = 0;
   uint64_t start_us;
   uint64_t read_done_us;
   uint64_t decode_done_us;
   int ret;

   if (!path || !out_data || !out_size)
      return -1;
   *out_size = 0;
   if (path_is_lz4(path)) {
      struct stat st;

      start_us = host_time_us();
      if (stat(path, &st) == 0 &&
          st.st_size > (long)LIBRETRO_COMPRESSED_STREAM_FIRST_INPUT) {
         ret = load_lz4_rom_data_stream(path, start_us, out_data, out_size);
         if (ret == 0)
            return 0;
         printf("unifrog libretro lz4 stream_ram failed; trying heap path=%s\n",
            path);
      }
      ret = read_path_heap_sequential(path, &compressed, &compressed_size,
         LIBRETRO_COMPRESSED_MAX_INPUT, "LOADING LZ4");
      if (ret == 0) {
         read_done_us = host_time_us();
         ret = decompress_lz4_memory(path, compressed, compressed_size,
            start_us, read_done_us, out_data, out_size);
         free(compressed);
         if (ret == 0)
            return 0;
         printf("unifrog libretro lz4 heap path failed; trying stream_ram path=%s\n",
            path);
      } else {
         printf("unifrog libretro lz4 heap read failed; trying stream_ram path=%s\n",
            path);
      }
      return load_lz4_rom_data_stream(path, start_us, out_data, out_size);
   }

   start_us = host_time_us();
   if (path_is_zstd(path)) {
      struct stat st;

      if (stat(path, &st) == 0 &&
          st.st_size > (long)LIBRETRO_COMPRESSED_STREAM_FIRST_INPUT) {
         ret = load_zstd_rom_data_stream(path, start_us, out_data, out_size);
         if (ret == 0)
            return 0;
         printf("unifrog libretro zstd stream_ram failed; trying heap path=%s\n",
            path);
      }
   }

   ret = read_path_heap_sequential(path, &compressed, &compressed_size,
      LIBRETRO_COMPRESSED_MAX_INPUT, "LOADING ZSTD");
   if (ret != 0)
      return load_zstd_rom_data_stream(path, start_us, out_data, out_size);
   read_done_us = host_time_us();
   ret = decompress_zstd_memory(path, compressed, compressed_size, out_data,
      out_size);
   if (ret != 0) {
      *out_size = 0;
      ret = decompress_zstd_memory_stream(path, compressed, compressed_size,
         out_data, out_size);
   }
   decode_done_us = host_time_us();
   free(compressed);
   compressed = NULL;
   if (ret != 0) {
      printf("unifrog libretro zstd heap path failed; trying stream_ram path=%s\n",
         path);
      return load_zstd_rom_data_stream(path, start_us, out_data, out_size);
   }
   if (ret == 0) {
      printf("unifrog libretro compressed memory loaded path=%s type=%s compressed=%u uncompressed=%u aligned=%lu\n",
         path, "zstd", (unsigned)compressed_size, (unsigned)*out_size,
         (unsigned long)((uintptr_t)*out_data & 31u));
      printf("unifrog load_time stage=compressed_memory type=%s read_ms=%u decode_ms=%u total_ms=%u compressed=%u uncompressed=%u\n",
         "zstd", host_elapsed_ms(start_us, read_done_us),
         host_elapsed_ms(read_done_us, decode_done_us),
         host_elapsed_ms(start_us, decode_done_us),
         (unsigned)compressed_size, (unsigned)*out_size);
      loading_draw("LOADING ROM", "READY", 68);
   }
   return ret;
}

static int write_all(FILE *file, const void *data, size_t size)
{
   const uint8_t *bytes = data;

   while (size > 0) {
      size_t wrote = fwrite(bytes, 1, size, file);

      if (wrote == 0)
         return -1;
      bytes += wrote;
      size -= wrote;
   }
   return 0;
}

static int content_cache_file_valid(const char *path, size_t expected_size)
{
   struct stat st;

   return path && stat(path, &st) == 0 && st.st_size >= 0 &&
      (size_t)st.st_size == expected_size;
}

static int content_cache_write_buffer(const char *path, const void *data,
   size_t size)
{
   FILE *file;
   int ok;

   if (!path || !data || size == 0)
      return -1;
   file = fopen(path, "wb");
   if (!file)
      return -1;
   ok = write_all(file, data, size) == 0 && fflush(file) == 0;
   fclose(file);
   if (!ok) {
      unlink(path);
      return -1;
   }
   return 0;
}

static int stream_lz4_to_file(FILE *in, FILE *out, const char *path)
{
   LZ4F_dctx *ctx = NULL;
   uint8_t *in_buf = NULL;
   uint8_t *out_buf = NULL;
   size_t in_size = 0;
   size_t in_pos = 0;
   size_t ret;
   int done = 0;
   int out_ret = -1;

   if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION)))
      return -1;
   in_buf = malloc(LIBRETRO_CONTENT_STREAM_IN);
   out_buf = malloc(LIBRETRO_CONTENT_STREAM_OUT);
   if (!in_buf || !out_buf)
      goto out;

   loading_draw("LOADING LZ4", "EXTRACTING", 24);
   while (!done) {
      LZ4F_decompressOptions_t opts;
      size_t src_size;
      size_t dst_size;

      if (in_pos == in_size) {
         in_size = fread(in_buf, 1, LIBRETRO_CONTENT_STREAM_IN, in);
         in_pos = 0;
         if (in_size == 0) {
            if (ferror(in))
               goto out;
            break;
         }
      }

      memset(&opts, 0, sizeof(opts));
      src_size = in_size - in_pos;
      dst_size = LIBRETRO_CONTENT_STREAM_OUT;
      ret = LZ4F_decompress(ctx, out_buf, &dst_size,
         in_buf + in_pos, &src_size, &opts);
      if (LZ4F_isError(ret)) {
         printf("unifrog libretro lz4 decode failed path=%s err=%s\n",
            path, LZ4F_getErrorName(ret));
         goto out;
      }
      in_pos += src_size;
      if (dst_size && write_all(out, out_buf, dst_size) != 0)
         goto out;
      if (ret == 0)
         done = 1;
      if (src_size == 0 && dst_size == 0 && !done)
         goto out;
   }
   out_ret = done ? 0 : -1;

out:
   free(out_buf);
   free(in_buf);
   if (ctx)
      LZ4F_freeDecompressionContext(ctx);
   return out_ret;
}

static int stream_zstd_to_file(FILE *in, FILE *out, const char *path)
{
   ZSTD_DStream *stream = NULL;
   uint8_t *in_buf = NULL;
   uint8_t *out_buf = NULL;
   size_t in_cap = ZSTD_DStreamInSize();
   size_t out_cap = ZSTD_DStreamOutSize();
   int done = 0;
   int out_ret = -1;

   if (in_cap == 0 || in_cap > LIBRETRO_CONTENT_STREAM_IN)
      in_cap = LIBRETRO_CONTENT_STREAM_IN;
   if (out_cap == 0 || out_cap > LIBRETRO_CONTENT_STREAM_OUT)
      out_cap = LIBRETRO_CONTENT_STREAM_OUT;

   stream = ZSTD_createDStream();
   if (!stream)
      return -1;
   if (ZSTD_isError(ZSTD_initDStream(stream)))
      goto out;
   in_buf = malloc(in_cap);
   out_buf = malloc(out_cap);
   if (!in_buf || !out_buf)
      goto out;

   loading_draw("LOADING ZSTD", "EXTRACTING", 24);
   while (!done) {
      size_t got = fread(in_buf, 1, in_cap, in);
      ZSTD_inBuffer input;

      if (got == 0) {
         if (ferror(in))
            goto out;
         break;
      }
      input.src = in_buf;
      input.size = got;
      input.pos = 0;
      while (input.pos < input.size) {
         ZSTD_outBuffer output;
         size_t ret;

         output.dst = out_buf;
         output.size = out_cap;
         output.pos = 0;
         ret = ZSTD_decompressStream(stream, &output, &input);
         if (ZSTD_isError(ret)) {
            printf("unifrog libretro zstd decode failed path=%s err=%s\n",
               path, ZSTD_getErrorName(ret));
            goto out;
         }
         if (output.pos && write_all(out, out_buf, output.pos) != 0)
            goto out;
         if (ret == 0) {
            done = 1;
            break;
         }
         if (output.pos == 0 && input.pos == input.size)
            break;
      }
   }
   out_ret = done ? 0 : -1;

out:
   free(out_buf);
   free(in_buf);
   if (stream)
      ZSTD_freeDStream(stream);
   return out_ret;
}

static int extract_wrapped_compressed_to_cache(const char *path,
   const struct retro_system_info *info, char *cache_path,
   size_t cache_path_size)
{
   FILE *in = NULL;
   FILE *out = NULL;
   uint64_t start_us = host_time_us();
   uint64_t end_us;
   int ret = -1;

   if (!path || !info || !cache_path)
      return -1;
   if (content_cache_path(path, NULL, info->valid_extensions, cache_path,
       cache_path_size) != 0) {
      printf("unifrog libretro compressed cache path failed path=%s exts=%s\n",
         path, info->valid_extensions ? info->valid_extensions : "?");
      return -1;
   }
   in = fopen(path, "rb");
   if (!in) {
      printf("unifrog libretro compressed open failed path=%s\n", path);
      return -1;
   }
   out = fopen(cache_path, "wb");
   if (!out) {
      printf("unifrog libretro compressed cache open failed path=%s cache=%s\n",
         path, cache_path);
      goto out;
   }
   printf("unifrog libretro compressed extract path=%s cache=%s type=%s\n",
      path, cache_path, path_is_lz4(path) ? "lz4" : "zstd");
   (void)unifrog_log_flush();
   ret = path_is_lz4(path) ?
      stream_lz4_to_file(in, out, path) :
      stream_zstd_to_file(in, out, path);
   if (fflush(out) != 0)
      ret = -1;

out:
   if (out)
      fclose(out);
   if (in)
      fclose(in);
   if (ret != 0)
      unlink(cache_path);
   else {
      end_us = host_time_us();
      printf("unifrog load_time stage=compressed_cache_extract type=%s ms=%u cache=%s\n",
         path_is_lz4(path) ? "lz4" : "zstd",
         host_elapsed_ms(start_us, end_us), cache_path);
      loading_draw("LOADING ROM", "EXTRACTED", 58);
   }
   return ret;
}

static int zip_load_rom_data(FILE *file, const char *zip_path,
   const struct retro_system_info *info, uint8_t **out_data,
   size_t *out_size, char *out_name, size_t out_name_size)
{
   struct zip_rom_entry entry;
   uint8_t local[30];
   uint8_t *zip = NULL;
   uint8_t *compressed = NULL;
   uint8_t *rom = NULL;
   size_t zip_size;
   uint16_t local_name_len;
   uint16_t local_extra_len;
   int ret = -1;

   if (!file || !zip_path || !info || !out_data || !out_size)
      return -1;
   if (zip_load_rom_data_stream(file, zip_path, info, out_data, out_size,
       out_name, out_name_size) == 0)
      return 0;
   (void)fseek(file, 0, SEEK_SET);
   printf("unifrog libretro zip stream_ram failed; trying heap path=%s\n",
      zip_path);
   if (file_size(file, &zip_size) != 0)
      return -1;
   zip = malloc(zip_size);
   if (!zip) {
      printf("unifrog libretro zip alloc failed path=%s size=%u\n",
         zip_path, (unsigned)zip_size);
      return -1;
   }
   loading_draw("LOADING ZIP", "READING", 15);
   if (fread(zip, 1, zip_size, file) != zip_size) {
      printf("unifrog libretro zip read failed path=%s size=%u\n",
         zip_path, (unsigned)zip_size);
      goto out;
   }

   if (zip_select_rom_entry(zip, zip_size, info->valid_extensions,
       &entry) != 0) {
      printf("unifrog libretro zip no supported entry path=%s exts=%s\n",
         zip_path, info->valid_extensions ? info->valid_extensions : "?");
      goto out;
   }
   printf("unifrog libretro zip entry name=%s method=%u compressed=%u uncompressed=%u flags=0x%04x\n",
      entry.name, entry.method, entry.compressed_size,
      entry.uncompressed_size, entry.flags);
   loading_draw("LOADING ZIP", "EXTRACTING", 35);

   if ((size_t)entry.local_offset + sizeof(local) > zip_size ||
       fseek(file, (long)entry.local_offset, SEEK_SET) != 0 ||
       fread(local, 1, sizeof(local), file) != sizeof(local) ||
       read_le32(local) != 0x04034b50u) {
      printf("unifrog libretro zip local header failed path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }
   if (read_le16(local + 8) != entry.method) {
      printf("unifrog libretro zip method mismatch path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }
   local_name_len = read_le16(local + 26);
   local_extra_len = read_le16(local + 28);
   if ((size_t)entry.local_offset + sizeof(local) + local_name_len +
       local_extra_len + entry.compressed_size > zip_size ||
       fseek(file, (long)(entry.local_offset + sizeof(local) +
       local_name_len + local_extra_len), SEEK_SET) != 0) {
      printf("unifrog libretro zip data offset failed path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }

   compressed = malloc(entry.compressed_size);
   rom = rom_alloc_aligned(entry.uncompressed_size);
   if (!compressed || !rom) {
      printf("unifrog libretro zip data alloc failed path=%s entry=%s compressed=%u uncompressed=%u\n",
         zip_path, entry.name, entry.compressed_size,
         entry.uncompressed_size);
      goto out;
   }
   if (fread(compressed, 1, entry.compressed_size, file) !=
       entry.compressed_size) {
      printf("unifrog libretro zip entry read failed path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }
   if (entry.method == 0) {
      if (entry.compressed_size != entry.uncompressed_size) {
         printf("unifrog libretro zip stored size mismatch path=%s entry=%s\n",
            zip_path, entry.name);
         goto out;
      }
      memcpy(rom, compressed, entry.uncompressed_size);
   } else if (zip_inflate_raw(rom, entry.uncompressed_size, compressed,
       entry.compressed_size) != Z_OK) {
      printf("unifrog libretro zip inflate failed path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }
   if ((uint32_t)crc32(crc32(0L, Z_NULL, 0), rom,
       (uInt)entry.uncompressed_size) != entry.crc32) {
      printf("unifrog libretro zip crc mismatch path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }

   free(compressed);
   free(zip);
   if (out_name && out_name_size)
      unifrog_text_copy(out_name, out_name_size, entry.name);
   *out_data = rom;
   *out_size = entry.uncompressed_size;
   printf("unifrog libretro zip loaded entry=%s size=%u\n",
      entry.name, (unsigned)*out_size);
   loading_draw("LOADING ZIP", "READY", 68);
   return 0;

out:
   rom_free_aligned(rom);
   free(compressed);
   free(zip);
   return ret;
}

static int zip_extract_rom_to_cache(FILE *file, const char *zip_path,
   const struct retro_system_info *info, char *cache_path,
   size_t cache_path_size)
{
   char entry_name[256];
   uint8_t *rom = NULL;
   size_t rom_size = 0;
   FILE *out = NULL;
   int ret = -1;

   if (zip_load_rom_data(file, zip_path, info, &rom, &rom_size,
       entry_name, sizeof(entry_name)) != 0)
      return -1;
   if (content_cache_path(zip_path, entry_name, info->valid_extensions,
       cache_path, cache_path_size) != 0) {
      printf("unifrog libretro zip cache path failed path=%s entry=%s\n",
         zip_path, entry_name);
      goto out;
   }
   out = fopen(cache_path, "wb");
   if (!out) {
      printf("unifrog libretro zip cache open failed path=%s cache=%s\n",
         zip_path, cache_path);
      goto out;
   }
   if (write_all(out, rom, rom_size) != 0 || fflush(out) != 0) {
      printf("unifrog libretro zip cache write failed path=%s cache=%s size=%u\n",
         zip_path, cache_path, (unsigned)rom_size);
      goto out;
   }
   printf("unifrog libretro zip extracted entry=%s cache=%s size=%u\n",
      entry_name, cache_path, (unsigned)rom_size);
   loading_draw("LOADING ZIP", "EXTRACTED", 58);
   ret = 0;

out:
   if (out)
      fclose(out);
   if (ret != 0 && cache_path && cache_path[0])
      unlink(cache_path);
   rom_free_aligned(rom);
   return ret;
}

static int run_core(const struct libretro_core_api *core, const char *path,
   const struct unifrog_libretro_run_options *options)
{
   struct retro_system_info info;
   struct retro_system_av_info av;
   struct retro_game_info game;
   uint8_t *rom_data = NULL;
   size_t rom_size = 0;
   char prepared_path[256];
   const char *game_path = path;
   unsigned sample_rate = DEFAULT_SAMPLE_RATE;
   size_t old_log_auto_flush;
   unsigned api_version;
   uintptr_t initial_gp;
   uint64_t total_start_us;
   uint64_t stage_start_us;
   uint64_t core_init_done_us;
   uint64_t content_start_us;
   uint64_t content_done_us;
   uint64_t retro_load_start_us;
   uint64_t retro_load_done_us;
   uint64_t save_load_start_us;
   uint64_t save_load_done_us;
   const char *content_kind = "none";
   int content_cache = 0;
   int ret = -1;
   int game_loaded = 0;
   int core_initialized = 0;

   if (!libretro_core_available(core) || !path || !path[0])
      return -1;

   total_start_us = host_time_us();
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(LIBRETRO_LOG_AUTO_FLUSH_BYTES);

   memset(&host, 0, sizeof(host));
   host.pixel_format = RETRO_PIXEL_FORMAT_RGB565;
   host.core_id = core->id;
   host.core_gp = core_call_gp(core);
   host_configure_options(options);
   initial_gp = host_read_gp();
   host_force_expected_gp();
   printf("unifrog libretro %s start path=%s\n", core->id, path);
   printf("unifrog libretro gp initial=0x%08lx expected=0x%08lx core=0x%08lx external=%d\n",
      (unsigned long)initial_gp, (unsigned long)host_expected_gp(),
      (unsigned long)host.core_gp, core->external);
   printf("unifrog libretro build commit=%s dirty=%d sdk=%s cores=%s\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY,
      UNIFROG_SDK_GIT_COMMIT, UNIFROG_CORES_GIT_COMMIT);
   printf("unifrog libretro options audio=%d gain=%u scpu=%u ge_clock=%d backlight=%d frameskip=%d display=%s\n",
      host.audio_enabled, host.audio_gain, host.scpu_target_mhz,
      host.options.ge_clock, host.options.backlight_level,
      host.options.frameskip, display_mode_label(host.display_mode));
   (void)unifrog_log_flush();
   host_apply_runtime_options();
   (void)unifrog_log_flush();
   loading_draw("LOADING GAME", "START", 2);
   api_version = CORE_CALL0_RET(core, core->api_version);
   printf("unifrog libretro %s api=%u\n", core->id, api_version);
   (void)unifrog_log_flush();

   printf("unifrog libretro step=set_callbacks\n");
   (void)unifrog_log_flush();
   CORE_CALL1_VOID(core, core->set_environment,
      unifrog_libretro_environment_trampoline);
   CORE_CALL1_VOID(core, core->set_video_refresh,
      unifrog_libretro_video_refresh_trampoline);
   CORE_CALL1_VOID(core, core->set_audio_sample,
      unifrog_libretro_audio_sample_trampoline);
   CORE_CALL1_VOID(core, core->set_audio_sample_batch,
      unifrog_libretro_audio_batch_trampoline);
   CORE_CALL1_VOID(core, core->set_input_poll,
      unifrog_libretro_input_poll_trampoline);
   CORE_CALL1_VOID(core, core->set_input_state,
      unifrog_libretro_input_state_trampoline);

   printf("unifrog libretro step=get_system_info_preinit\n");
   (void)unifrog_log_flush();
   memset(&info, 0, sizeof(info));
   CORE_CALL1_VOID(core, core->get_system_info, &info);
   printf("unifrog libretro core preinit name=%s version=%s fullpath=%d exts=%s\n",
      info.library_name ? info.library_name : "?",
      info.library_version ? info.library_version : "?",
      info.need_fullpath ? 1 : 0,
      info.valid_extensions ? info.valid_extensions : "?");
   (void)unifrog_log_flush();

   memset(&game, 0, sizeof(game));
   prepared_path[0] = '\0';
   game.path = path;
   host.content_alloc_appmem = core->external && !info.need_fullpath;
   if (host.content_alloc_appmem) {
      printf("unifrog libretro content appmem_reserve enabled core=%s path=%s\n",
         core->id, path);
      (void)unifrog_log_flush();
   }
   probe_rom_seek_path(path);
   content_start_us = host_time_us();
   if (info.need_fullpath) {
      content_kind = "fullpath";
      if (path_is_zip(path)) {
         FILE *file = fopen(path, "rb");

         if (!file) {
            printf("unifrog libretro zip open failed path=%s\n", path);
            goto out_finish;
         }
         if (zip_extract_rom_to_cache(file, path, &info, prepared_path,
             sizeof(prepared_path)) != 0) {
            fclose(file);
            goto out_finish;
         }
         fclose(file);
         game_path = prepared_path;
         content_kind = "zip_cache";
         content_cache = 1;
      } else if (path_is_wrapped_compressed(path)) {
         if (extract_wrapped_compressed_to_cache(path, &info, prepared_path,
             sizeof(prepared_path)) != 0)
            goto out_finish;
         game_path = prepared_path;
         content_kind = "compressed_cache";
         content_cache = 1;
      }
      game.path = game_path;
      printf("unifrog libretro rom fullpath path=%s source=%s\n",
         game.path, path);
      (void)unifrog_log_flush();
      loading_draw("LOADING ROM", "OPENING", 12);
   } else {
      const char *read_path = path;
      FILE *file;

      if (path_is_wrapped_compressed(path)) {
         content_kind = "compressed_memory";
         if (load_wrapped_compressed_rom_data(path, &rom_data,
             &rom_size) != 0) {
            printf("unifrog libretro compressed memory fallback path=%s\n",
               path);
            if (extract_wrapped_compressed_to_cache(path, &info,
                prepared_path, sizeof(prepared_path)) != 0)
               goto out_finish;
            read_path = prepared_path;
            content_kind = "compressed_cache_read";
            content_cache = 1;
         }
      } else {
         content_kind = path_is_zip(read_path) ? "zip_memory" : "raw_memory";
         file = fopen(read_path, "rb");
         if (!file) {
            printf("unifrog libretro rom open failed path=%s source=%s\n",
               read_path, path);
            goto out_finish;
         }
         if (path_is_zip(read_path)) {
            int zip_cache = 0;

            if (zip_load_rom_data_cached(file, read_path, &info, &rom_data,
                &rom_size, &zip_cache) != 0) {
               fclose(file);
               goto out_finish;
            }
            if (zip_cache) {
               content_kind = "zip_cache_read";
               content_cache = 1;
            }
         } else {
            if (read_path_aligned_direct(read_path, &rom_data, &rom_size,
                "READING") != 0 &&
                read_file_aligned(file, read_path, &rom_data, &rom_size,
                "READING") != 0) {
               fclose(file);
               goto out_finish;
            }
         }
         fclose(file);
      }
      if (!rom_data) {
         file = fopen(read_path, "rb");
         if (!file) {
            printf("unifrog libretro rom open failed path=%s source=%s\n",
               read_path, path);
            goto out_finish;
         }
         if (read_path_aligned_direct(read_path, &rom_data, &rom_size,
             "READ CACHE") != 0 &&
             read_file_aligned(file, read_path, &rom_data, &rom_size,
             "READ CACHE") != 0) {
            fclose(file);
            goto out_finish;
         }
         fclose(file);
      }
      game.data = rom_data;
      game.size = rom_size;
      printf("unifrog libretro rom loaded size=%u path=%s source=%s aligned=%lu\n",
         (unsigned)rom_size, read_path, path,
         (unsigned long)((uintptr_t)rom_data & 31u));
      (void)unifrog_log_flush();
   }
   host.content_alloc_appmem = 0;
   content_done_us = host_time_us();
   printf("unifrog load_time core=%s stage=content_prepare ms=%u kind=%s cache=%d bytes=%u path=%s source=%s\n",
      core->id, host_elapsed_ms(content_start_us, content_done_us),
      content_kind, content_cache, (unsigned)rom_size,
      game.path ? game.path : "", path);
   (void)unifrog_log_flush();

   printf("unifrog libretro step=retro_init\n");
   (void)unifrog_log_flush();
   loading_draw("LOADING GAME", "CORE INIT", 70);
   stage_start_us = host_time_us();
   CORE_CALL0_VOID(core, core->init);
   core_initialized = 1;

   memset(&info, 0, sizeof(info));
   CORE_CALL1_VOID(core, core->get_system_info, &info);
   core_init_done_us = host_time_us();
   printf("unifrog libretro core name=%s version=%s fullpath=%d exts=%s\n",
      info.library_name ? info.library_name : "?",
      info.library_version ? info.library_version : "?",
      info.need_fullpath ? 1 : 0,
      info.valid_extensions ? info.valid_extensions : "?");
   printf("unifrog load_time core=%s stage=core_init ms=%u\n",
      core->id, host_elapsed_ms(stage_start_us, core_init_done_us));
   (void)unifrog_log_flush();

   memset(&av, 0, sizeof(av));
   CORE_CALL1_VOID(core, core->get_system_av_info, &av);
   host.fps = av.timing.fps > 1.0 ? (unsigned)(av.timing.fps + 0.5) :
      (CORE_CALL0_RET(core, core->get_region) == RETRO_REGION_PAL ? 50u : 60u);
   if (av.timing.sample_rate > 1.0)
      sample_rate = (unsigned)(av.timing.sample_rate + 0.5);
   host.frame_budget_count = host_compute_frame_budget(host.fps,
      &host.scpu_mhz_est, &host.count_hz_est, &host.count_hz_calibrated);
   host.frame_period_us = host_compute_frame_period_us(av.timing.fps,
      host.fps);
   printf("unifrog libretro av base=%ux%u max=%ux%u fps=%u sample_rate=%u\n",
      (unsigned)av.geometry.base_width, (unsigned)av.geometry.base_height,
      (unsigned)av.geometry.max_width, (unsigned)av.geometry.max_height,
      host.fps, sample_rate);
   printf("unifrog libretro perf_setup report_frames=%u scpu=%u count_hz=%u count_cal=%u budget=%u pace_period_us=%u\n",
      LIBRETRO_PERF_REPORT_FRAMES, host.scpu_mhz_est, host.count_hz_est,
      host.count_hz_calibrated, host.frame_budget_count,
      host.frame_period_us);
   (void)unifrog_log_flush();

   loading_draw("LOADING GAME", "CORE LOAD", 72);
   printf("unifrog libretro step=retro_load_game\n");
   (void)unifrog_log_flush();
   libretro_watchdog_start();
   libretro_watchdog_enter(LIBRETRO_WATCHDOG_PHASE_LOAD, 0);
   retro_load_start_us = host_time_us();
   if (!CORE_CALL1_RET(core, core->load_game, &game)) {
      libretro_watchdog_leave();
      retro_load_done_us = host_time_us();
      printf("unifrog load_time core=%s stage=retro_load_game ms=%u ok=0\n",
         core->id, host_elapsed_ms(retro_load_start_us,
         retro_load_done_us));
      printf("unifrog libretro load failed path=%s\n", path);
      goto out_unload;
   }
   retro_load_done_us = host_time_us();
   libretro_watchdog_leave();
   game_loaded = 1;
   printf("unifrog libretro step=retro_load_game_ok\n");
   printf("unifrog load_time core=%s stage=retro_load_game ms=%u ok=1\n",
      core->id, host_elapsed_ms(retro_load_start_us, retro_load_done_us));
   save_load_start_us = host_time_us();
   quick_load_all_memory_files(core, path);
   save_load_done_us = host_time_us();
   printf("unifrog load_time core=%s stage=save_memory_load ms=%u\n",
      core->id, host_elapsed_ms(save_load_start_us, save_load_done_us));

   loading_draw("LOADING GAME", "READY", 100);
   loading_close();
   printf("unifrog libretro step=presenter_open\n");
   (void)unifrog_log_flush();
   if (unifrog_presenter_open_with_clock(&host.presenter, 2,
       present_flags_for_display_mode(host.display_mode),
       host.ge_clock) == 0) {
      host.presenter_open = 1;
      unifrog_presenter_clear(&host.presenter, 0xff000000u);
   } else {
      printf("unifrog libretro presenter open failed\n");
      goto out_unload;
   }

   printf("unifrog libretro step=audio_open\n");
   (void)unifrog_log_flush();
   host.audio_input_rate = sample_rate;
   host.audio_output_rate = LIBRETRO_AUDIO_OUTPUT_RATE;
   host.audio_gate_open = 0;
   host.audio_quiet_batches = 0;
   if (!host.audio_enabled) {
      printf("unifrog libretro audio disabled rate=%u/%u\n",
         host.audio_input_rate, host.audio_output_rate);
   } else if (host.audio_gain == 0) {
      printf("unifrog libretro audio muted rate=%u/%u gain=0\n",
         host.audio_input_rate, host.audio_output_rate);
   } else if (unifrog_audio_open(&host.audio, host.audio_output_rate,
       LIBRETRO_AUDIO_CHANNELS, LIBRETRO_AUDIO_PERIOD_BYTES,
       LIBRETRO_AUDIO_PERIODS) == 0) {
      int volume_ret;
      int mute_ret;
      int silence_ret = 0;
      int start_ret;
      int unmute_ret;
      int output_ret;
      unsigned silence_frames;

      host.audio_open = 1;
      volume_ret = unifrog_audio_set_volume(&host.audio, LIBRETRO_AUDIO_VOLUME);
      mute_ret = unifrog_audio_set_mute(&host.audio, 1);
      silence_frames = LIBRETRO_AUDIO_WRITE_CHUNK_FRAMES;
      if (host.audio.frame_bytes && host.audio.period_bytes < silence_frames)
         silence_frames = host.audio.period_bytes;
      if (silence_frames > 384)
         silence_frames = 384;
      for (unsigned i = 0; i < 8 && silence_frames > 0; i++) {
         if (unifrog_audio_write(&host.audio, audio_silence_buffer,
             silence_frames) != 0)
            silence_ret = -1;
      }
      start_ret = unifrog_audio_start(&host.audio);
      unmute_ret = unifrog_audio_set_mute(&host.audio, 0);
      output_ret = unifrog_audio_set_output_enabled(&host.audio, 0);
      printf("unifrog libretro audio_open ok rate=%u/%u channels=%u period=%u periods=%u volume=%u gain=%u route=%s volume_ret=%d mute_ret=%d silence_ret=%d start_ret=%d unmute_ret=%d output_ret=%d\n",
         host.audio_input_rate, host.audio_output_rate,
         host.audio.channels, host.audio.period_bytes, host.audio.periods,
         LIBRETRO_AUDIO_VOLUME, host.audio_gain, LIBRETRO_AUDIO_ROUTE,
         volume_ret, mute_ret, silence_ret, start_ret, unmute_ret,
         output_ret);
      (void)unifrog_log_flush();
   } else {
      printf("unifrog libretro audio open failed\n");
   }

   CORE_CALL2_VOID(core, core->set_controller_port_device, 0,
      RETRO_DEVICE_JOYPAD);
   CORE_CALL2_VOID(core, core->set_controller_port_device, 1,
      RETRO_DEVICE_JOYPAD);
   printf("unifrog load_time core=%s stage=total ms=%u content_ms=%u retro_ms=%u save_ms=%u kind=%s cache=%d bytes=%u\n",
      core->id, host_elapsed_ms(total_start_us, host_time_us()),
      host_elapsed_ms(content_start_us, content_done_us),
      host_elapsed_ms(retro_load_start_us, retro_load_done_us),
      host_elapsed_ms(save_load_start_us, save_load_done_us),
      content_kind, content_cache, (unsigned)rom_size);
   printf("unifrog libretro step=run_loop fps=%u\n", host.fps);
   (void)unifrog_log_flush();
   host_pace_begin();
   unifrog_log_defer_begin();

   for (;;) {
      uint32_t run_start = unifrog_perf_count();
      unsigned video_before = host.video_frames;
      unsigned run_count;
      unsigned active_count;
      unsigned vsync_count;

      unifrog_input_save_previous();
      unifrog_input_poll_with_wireless_divisor(LIBRETRO_WIRELESS_POLL_DIVISOR);
      host.buttons = unifrog_input_buttons();
      if (exit_combo_down()) {
         printf("unifrog libretro quick_js core=%s frame=%u\n",
            core->id, host.run_frames);
         (void)unifrog_log_flush_force();
         if (quick_js_run(core, path)) {
            printf("unifrog libretro return_to_js core=%s frame=%u\n",
               core->id, host.run_frames);
            (void)unifrog_log_flush_force();
            break;
         }
         host_pace_begin();
         continue;
      }

      host_notify_audio_status();
      libretro_watchdog_enter(LIBRETRO_WATCHDOG_PHASE_RUN,
         host.run_frames + 1u);
      CORE_CALL0_VOID(core, core->run);
      libretro_watchdog_leave();
      run_count = unifrog_perf_elapsed(run_start, unifrog_perf_count());
      vsync_count = host.presenter_open && host.video_frames != video_before ?
         host.presenter.last_vsync_count : 0;
      active_count = run_count;
      if (vsync_count < active_count)
         active_count -= vsync_count;
      host.run_frames++;
      if (host.run_frames - host.memory_autosave_frame >=
          LIBRETRO_MEMORY_AUTOSAVE_FRAMES) {
         host.memory_autosave_frame = host.run_frames;
         quick_autosave_memory_files(core, path);
      }
      host.run_total_count += run_count;
      host.active_total_count += active_count;
      if (run_count > host.run_max_count)
         host.run_max_count = run_count;
      if (active_count > host.active_max_count)
         host.active_max_count = active_count;
      if (host.frame_budget_count && active_count > host.frame_budget_count)
         host.slow_frames++;
      if (!host.fast_forward)
         host_pace_frame();
      if ((host.run_frames - host.run_report_frames) >=
          LIBRETRO_PERF_REPORT_FRAMES)
         host_report_perf(core->id, 0);
   }

   ret = 0;

out_unload:
   libretro_watchdog_stop();
   if (game_loaded)
      quick_save_all_memory_files(core, path);
   if (core_initialized)
      CORE_CALL0_VOID(core, core->unload_game);
out_deinit:
   libretro_watchdog_stop();
   host_report_perf(core->id, 1);
   unifrog_log_defer_end();
   if (host.audio_open)
      unifrog_audio_close(&host.audio);
   if (host.presenter_open)
      unifrog_presenter_close(&host.presenter);
   free(host.video_convert_buffer);
   if (core_initialized)
      CORE_CALL0_VOID(core, core->deinit);
out_finish:
   host.content_alloc_appmem = 0;
   rom_free_aligned(rom_data);
   loading_close();
   host_restore_runtime_options();
   unifrog_input_recover_after_core();
   printf("unifrog libretro %s end ret=%d video=%ux%u pitch=%u\n",
      core->id, ret, host.video_width, host.video_height, host.video_pitch);
   unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
   (void)unifrog_log_flush();
   return ret;
}

static const char *libretro_canonical_core_id(const char *id)
{
   if (!id || !id[0])
      return NULL;
   if (strcmp(id, "pce_fast") == 0)
      return "pce-fast";
   return id;
}

static const struct libretro_core_api *libretro_builtin_core_for_id(
   const char *id)
{
   if (!id || !id[0])
      return NULL;
   if (strcmp(id, "gambatte") == 0)
      return libretro_core_if_available(&gambatte_core);
   if (strcmp(id, "gpsp") == 0)
      return libretro_core_if_available(&gpsp_core);
   if (strcmp(id, "picodrive") == 0)
      return libretro_core_if_available(&picodrive_core);
   if (strcmp(id, "snes9x2005") == 0)
      return libretro_core_if_available(&snes9x2005_core);
   if (strcmp(id, "snes9x2002") == 0)
      return libretro_core_if_available(&snes9x2002_core);
   if (strcmp(id, "quicknes") == 0)
      return libretro_core_if_available(&quicknes_core);
   if (strcmp(id, "fceumm") == 0)
      return libretro_core_if_available(&fceumm_core);
   if (strcmp(id, "gearboy") == 0)
      return libretro_core_if_available(&gearboy_core);
   if (strcmp(id, "pce-fast") == 0 || strcmp(id, "pce_fast") == 0)
      return libretro_core_if_available(&pce_fast_core);
   return NULL;
}

static int libretro_load_external_core(const char *id, const char *core_path,
   struct unifrog_core_module_loaded *loaded, struct libretro_core_api *api)
{
   char path[256];
   const struct unifrog_core_module_exports *exports;

   if (!id || !id[0] || !loaded || !api)
      return -1;

   if (core_path && core_path[0])
      unifrog_text_copy(path, sizeof(path), core_path);
   else
      snprintf(path, sizeof(path), "/media/mmcblk0/unifrog/cores/%s.bin", id);
   if (unifrog_core_module_load_file(path, id, loaded) != 0)
      return -1;

   exports = loaded->exports;
   memset(api, 0, sizeof(*api));
   api->id = exports->core_id;
   api->call_gp = loaded->gp_addr;
   api->external = 1;
   api->set_environment = exports->retro_set_environment;
   api->set_video_refresh = exports->retro_set_video_refresh;
   api->set_audio_sample = exports->retro_set_audio_sample;
   api->set_audio_sample_batch = exports->retro_set_audio_sample_batch;
   api->set_input_poll = exports->retro_set_input_poll;
   api->set_input_state = exports->retro_set_input_state;
   api->init = exports->retro_init;
   api->deinit = exports->retro_deinit;
   api->api_version = exports->retro_api_version;
   api->get_system_info = exports->retro_get_system_info;
   api->get_system_av_info = exports->retro_get_system_av_info;
   api->set_controller_port_device =
      exports->retro_set_controller_port_device;
   api->run = exports->retro_run;
   api->unload_game = exports->retro_unload_game;
   api->load_game = exports->retro_load_game;
   api->get_region = exports->retro_get_region;
   if (exports->size >= offsetof(struct unifrog_core_module_exports,
       retro_unserialize) + sizeof(exports->retro_unserialize)) {
      api->serialize_size = exports->retro_serialize_size;
      api->serialize = exports->retro_serialize;
      api->unserialize = exports->retro_unserialize;
   }
   if (exports->size >= offsetof(struct unifrog_core_module_exports,
       retro_cheat_set) + sizeof(exports->retro_cheat_set)) {
      api->get_memory_data = exports->retro_get_memory_data;
      api->get_memory_size = exports->retro_get_memory_size;
      api->cheat_reset = exports->retro_cheat_reset;
      api->cheat_set = exports->retro_cheat_set;
   }
   if (!libretro_core_available(api)) {
      printf("unifrog libretro external_core incomplete id=%s\n", id);
      unifrog_core_module_unload(loaded);
      return -1;
   }
   return 0;
}

static int run_core_id(const char *id, const char *path,
   const struct unifrog_libretro_run_options *options)
{
   struct unifrog_core_module_loaded loaded;
   struct libretro_core_api external_core;
   const struct libretro_core_api *core;
   const char *canonical = libretro_canonical_core_id(id);
   int loaded_external = 0;
   int ret;

   if (!canonical)
      return -1;

   unifrog_input_recover_core_transition("core_load");
   core = libretro_builtin_core_for_id(canonical);
   if (!core &&
       libretro_load_external_core(canonical,
       options ? options->core_path : NULL, &loaded, &external_core) == 0) {
      core = &external_core;
      loaded_external = 1;
   }
   if (!core) {
      printf("unifrog libretro core unavailable id=%s\n", canonical);
      return -1;
   }

   ret = run_core(core, path, options);
   if (loaded_external)
      unifrog_core_module_unload(&loaded);
   return ret;
}

int unifrog_libretro_run_gambatte_ex(const char *path,
   const struct unifrog_libretro_run_options *options)
{
   return run_core_id("gambatte", path, options);
}

int unifrog_libretro_run_gpsp_ex(const char *path,
   const struct unifrog_libretro_run_options *options)
{
   return run_core_id("gpsp", path, options);
}

static int path_has_any_suffix(const char *path, const char *const *suffixes,
   unsigned count)
{
   for (unsigned i = 0; i < count; i++) {
      if (unifrog_text_ends_with_ci(path, suffixes[i]))
         return 1;
   }
   return 0;
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const char *libretro_default_core_id_for_path(const char *path)
{
   static const char *const gb_suffixes[] = { ".gb", ".gbc" };
   static const char *const genesis_suffixes[] = {
      ".md", ".gen", ".smd", ".sms", ".gg", ".sg", ".32x"
   };
   static const char *const snes_suffixes[] = { ".sfc", ".smc" };
   static const char *const nes_suffixes[] = { ".nes", ".fds" };
   static const char *const pce_suffixes[] = { ".pce", ".sgx" };
   static const char *const psx_suffixes[] = {
      ".cue", ".iso", ".img", ".pbp", ".bin"
   };
   char stripped[256];
   const char *detect_path = path;

   if (!path)
      return NULL;
   if (path_is_wrapped_compressed(path) &&
       copy_path_without_last_extension(path, stripped, sizeof(stripped)) == 0)
      detect_path = stripped;
   if (unifrog_text_ends_with_ci(detect_path, ".gba"))
      return "gpsp";
   if (path_has_any_suffix(detect_path, gb_suffixes, ARRAY_SIZE(gb_suffixes)))
      return "gambatte";
   if (path_has_any_suffix(detect_path, genesis_suffixes,
       ARRAY_SIZE(genesis_suffixes)))
      return "picodrive";
   if (path_has_any_suffix(detect_path, snes_suffixes, ARRAY_SIZE(snes_suffixes)))
      return "snes9x2005";
   if (path_has_any_suffix(detect_path, nes_suffixes, ARRAY_SIZE(nes_suffixes)))
      return "fceumm";
   if (path_has_any_suffix(detect_path, pce_suffixes, ARRAY_SIZE(pce_suffixes)))
      return "pce-fast";
   if (path_has_any_suffix(detect_path, psx_suffixes, ARRAY_SIZE(psx_suffixes)))
      return "qpsx";
   return NULL;
}

int unifrog_libretro_run_path_ex(const char *path,
   const struct unifrog_libretro_run_options *options)
{
   const char *core_id;
   int ret;

   if (!path) {
      printf("unifrog libretro dispatch failed path=null\n");
      (void)unifrog_log_flush();
      return -1;
   }

   core_id = options && options->core_id[0] ?
      libretro_canonical_core_id(options->core_id) :
      libretro_default_core_id_for_path(path);
   printf("unifrog libretro dispatch path=%s requested_core=%s\n",
      path, options && options->core_id[0] ? options->core_id : "auto");
   (void)unifrog_log_flush();
   if (core_id) {
      ret = run_core_id(core_id, path, options);
      printf("unifrog libretro dispatch core=%s ret=%d\n", core_id, ret);
      (void)unifrog_log_flush();
      return ret;
   }

   printf("unifrog libretro dispatch unsupported path=%s requested_core=%s\n",
      path, options && options->core_id[0] ? options->core_id : "auto");
   (void)unifrog_log_flush();
   return -1;
}

int unifrog_libretro_run_gambatte(const char *path)
{
   return unifrog_libretro_run_gambatte_ex(path, NULL);
}

int unifrog_libretro_run_gpsp(const char *path)
{
   return unifrog_libretro_run_gpsp_ex(path, NULL);
}

int unifrog_libretro_run_path(const char *path)
{
   return unifrog_libretro_run_path_ex(path, NULL);
}
