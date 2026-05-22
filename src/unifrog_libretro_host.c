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
#include <strings.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if UNIFROG_FRONTEND_JS2300
#include <js2300/js2300.h>
#endif

#include <kernel/lib/zlib.h>

#include <unifrog/audio.h>
#include <unifrog/abi.h>
#include <unifrog/backlight.h>
#include <unifrog/build_info.h>
#include <unifrog/diag.h>
#include <unifrog/exception_record.h>
#include <unifrog/fb.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/log.h>
#include <unifrog/panic.h>
#include <unifrog/paths.h>
#include <unifrog/perf.h>
#include <unifrog/platform.h>
#include <unifrog/presenter.h>
#include <unifrog/scpu.h>
#include <unifrog/surface_alloc.h>
#include <unifrog/text.h>

#include <libretro.h>

#include <third_party/lz4/lz4frame.h>
#include <zstd.h>

#include "unifrog_core_module_loader.h"
#include "unifrog_mips_call.h"

#define printf unifrog_log

#define DEFAULT_SAMPLE_RATE 32000u
#define LIBRETRO_AUDIO_MIN_OUTPUT_RATE 8000u
#define LIBRETRO_AUDIO_MAX_OUTPUT_RATE 48000u
#define LIBRETRO_AUDIO_DEFAULT_GAIN 1u
#define LIBRETRO_AUDIO_VOLUME 65u
#define LIBRETRO_AUDIO_ROUTE "sf2000_audsink_mono_left"
#define LIBRETRO_AUDIO_BACKEND UNIFROG_AUDIO_BACKEND_AUDSINK
#define LIBRETRO_AUDIO_GATE_OPEN_LEVEL 256u
#define LIBRETRO_AUDIO_GATE_CLOSE_LEVEL 96u
#define LIBRETRO_AUDIO_GATE_CLOSE_BATCHES 45u
#define LIBRETRO_AUDIO_CHANNELS 1u
#define LIBRETRO_AUDIO_PERIOD_BYTES 512u
#define LIBRETRO_AUDIO_PERIODS 8u
#define LIBRETRO_AUDIO_WRITE_CHUNK_FRAMES 512u
#define LIBRETRO_AUDIO_SAMPLE_BUFFER_FRAMES 512u
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
#define LIBRETRO_NO_OUTPUT_GRACE_FRAMES 300u
#define LIBRETRO_SAVE_DIR "/media/mmcblk0/unifrog/saves"
#define LIBRETRO_CONTENT_CACHE_DIR "/media/mmcblk0/unifrog/cache"
#define LIBRETRO_QUICK_JS_ROOT "/media/mmcblk0/unifrog"
#define LIBRETRO_QUICK_JS_ENTRY "quick-menu.js"
#define LIBRETRO_QUICK_JS_HEAP_BYTES (2u * 1024u * 1024u)
#define LIBRETRO_QUICK_JS_STACK_BYTES (96u * 1024u)
#define LIBRETRO_QUICK_JS_BYTECODE_BYTES (512u * 1024u)
#define LIBRETRO_MEMORY_AUTOSAVE_FRAMES 600u
#define LIBRETRO_MEMORY_FILE_MAX (16u * 1024u * 1024u)
#define LIBRETRO_SAVE_FAST_READ_MIN_BYTES (64u * 1024u)
#define LIBRETRO_STATE_FILE_MAX (16u * 1024u * 1024u)
#define LIBRETRO_STATE_SLOT_COUNT 10u
#define LIBRETRO_STATE_MAGIC 0x55465354u
#define LIBRETRO_STATE_VERSION 1u
#define LIBRETRO_STATE_FLAG_COMPRESSED 1u
#define LIBRETRO_STORAGE_POST_CORE_QUIET_MS 15000u
#define LIBRETRO_CORE_OPTION_MAX 48u
#define LIBRETRO_CORE_OPTION_VALUE_MAX 32u
#define LIBRETRO_CORE_OPTION_KEY_MAX 48u
#define LIBRETRO_CORE_OPTION_LABEL_MAX 64u
#define LIBRETRO_CORE_OPTION_VALUE_TEXT_MAX 48u
#define LIBRETRO_ZIP_MAX_UNCOMPRESSED (64u * 1024u * 1024u)
#define LIBRETRO_COMPRESSED_MAX_INPUT (64u * 1024u * 1024u)
#define LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED LIBRETRO_ZIP_MAX_UNCOMPRESSED
#define LIBRETRO_COMPRESSED_GROW_INITIAL (2u * 1024u * 1024u)
#define LIBRETRO_COMPRESSED_STREAM_FIRST_INPUT (4u * 1024u * 1024u)
#define LIBRETRO_CONTENT_READ_CHUNK (512u * 1024u)
#define LIBRETRO_CONTENT_READ_CHUNK_EXPERIMENTAL (64u * 1024u)
#define LIBRETRO_FAST_READ_MIN_BYTES_PER_SEC (1024u * 1024u)
#define LIBRETRO_FAST_READ_MIN_TIMEOUT_MS 1000u
#define LIBRETRO_FAST_READ_TIMEOUT_GRACE_MS 250u
#define LIBRETRO_CONTENT_STREAM_IN (64u * 1024u)
#define LIBRETRO_CONTENT_STREAM_OUT (256u * 1024u)
#define LIBRETRO_FS_PROBE_MIN_SIZE (512u * 1024u)
#define LIBRETRO_FS_PROBE_SAMPLE 64u
#define LIBRETRO_FS_PROBE_MAX_LOGS 12u
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

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

struct quick_core_option {
   char key[LIBRETRO_CORE_OPTION_KEY_MAX];
   char label[LIBRETRO_CORE_OPTION_LABEL_MAX];
   char values[LIBRETRO_CORE_OPTION_VALUE_MAX][LIBRETRO_CORE_OPTION_VALUE_TEXT_MAX];
   char value_labels[LIBRETRO_CORE_OPTION_VALUE_MAX][LIBRETRO_CORE_OPTION_LABEL_MAX];
   unsigned value_count;
   unsigned selected;
   int visible;
};

struct quick_state_file_header {
   uint32_t magic;
   uint32_t version;
   uint32_t flags;
   uint32_t raw_size;
   uint32_t data_size;
};

struct quick_state_recovery {
   uint8_t *data;
   size_t data_size;
   size_t raw_size;
   uint32_t flags;
   unsigned slot;
   int pending;
   char path[256];
   char core_id[24];
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
   int framebuffer_format;
   unsigned video_width;
   unsigned video_height;
   unsigned video_pitch;
   unsigned video_max_width;
   unsigned video_max_height;
   void *software_framebuffer;
   size_t software_framebuffer_bytes;
   unsigned software_framebuffer_width;
   unsigned software_framebuffer_height;
   unsigned software_framebuffer_pitch;
   enum retro_pixel_format software_framebuffer_format;
   unsigned software_framebuffer_requests;
   unsigned software_framebuffer_hits;
   unsigned software_framebuffer_presents;
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
   int variables_dirty;
   unsigned scpu_target_mhz;
   int backlight_restore_valid;
   int backlight_apply_ret;
   unsigned backlight_restore_level;
   struct unifrog_fb loading_fb;
   TaskHandle_t loading_task;
   int loading_open;
   volatile int loading_task_running;
   volatile int loading_task_stop;
   unsigned loading_anim_tick;
   unsigned loading_percent;
   unsigned loading_visual_percent;
   char loading_title[32];
   char loading_detail[64];
   unsigned loading_log_stage_hash;
   unsigned loading_log_percent_bucket;
   uint32_t memory_hash[QUICK_MEMORY_COUNT];
   int memory_hash_valid[QUICK_MEMORY_COUNT];
   unsigned memory_autosave_frame;
   unsigned memory_autosaves;
   int fast_forward;
   int fast_forward_force_present;
   unsigned fast_forward_multiplier;
   int content_alloc_appmem;
   const struct libretro_core_api *quick_core;
   const char *quick_rom_path;
   unsigned quick_state_slot;
   int quick_state_auto_load;
   int quick_state_auto_save;
   int quick_js_action;
   int quick_js_frame_open;
   unsigned quick_js_draw_buffer;
   int quick_combo_armed;
   char quick_status[96];
   struct quick_core_option core_options[LIBRETRO_CORE_OPTION_MAX];
   unsigned core_option_count;
   int core_options_dirty;
   int core_options_loaded;
   int input_profile;
   int input_profile_dirty;
   int16_t audio_sample_buffer[LIBRETRO_AUDIO_SAMPLE_BUFFER_FRAMES * 2u];
   unsigned audio_sample_buffer_frames;
};

static struct libretro_host host;
static struct quick_state_recovery quick_state_recovery;

static int experimental_sd_log_defer_begin(const char *tag)
{
   unifrog_log_defer_begin();
   unifrog_platform_set_storage_log_suspended(1);
   printf("unifrog log defer begin tag=%s reason=core_session mode=%s pending=%u capacity=%u\n",
      tag ? tag : "", UNIFROG_SD_MODE,
      (unsigned)unifrog_log_pending(), (unsigned)unifrog_log_capacity());
   return 1;
}

static void experimental_sd_log_defer_end(int active, const char *tag, int ret)
{
   if (!active)
      return;
   printf("unifrog log defer end tag=%s reason=core_session mode=%s ret=%d pending=%u deferred=%d\n",
      tag ? tag : "", UNIFROG_SD_MODE, ret,
      (unsigned)unifrog_log_pending(), unifrog_log_flush_deferred());
   unifrog_platform_set_storage_log_suspended(0);
   unifrog_log_note_storage_quiet(3000u);
   unifrog_log_defer_end();
}

static int16_t audio_mix_buffer[2048 * 2];
static int16_t audio_silence_buffer[384 * 2];
static volatile unsigned watchdog_active;
static volatile unsigned watchdog_phase;
static volatile unsigned watchdog_marker;
static volatile unsigned watchdog_heartbeat;
static int libretro_fast_read_timeout_enabled;
static int libretro_read_profile_load_session_active;

#define LIBRETRO_READ_PROFILE_OWNED 1
#define LIBRETRO_READ_PROFILE_BORROWED 2

static void loading_draw(const char *title, const char *detail,
   unsigned percent);
static uint64_t host_time_us(void);
static unsigned host_elapsed_ms(uint64_t start_us, uint64_t end_us);
static unsigned host_compute_frame_budget(unsigned fps, unsigned *scpu_mhz,
   unsigned *count_hz, unsigned *count_hz_calibrated);
static int read_file_aligned(FILE *file, const char *path,
   uint8_t **out_data, size_t *out_size, const char *label);
static int read_file_aligned_timeout(FILE *file, const char *path,
   uint8_t **out_data, size_t *out_size, const char *label,
   unsigned timeout_ms);
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

static size_t libretro_content_read_chunk(void)
{
   return UNIFROG_SD_EXPERIMENTAL ?
      LIBRETRO_CONTENT_READ_CHUNK_EXPERIMENTAL :
      LIBRETRO_CONTENT_READ_CHUNK;
}

static unsigned libretro_fast_read_timeout_ms(size_t bytes)
{
   uint64_t ms = ((uint64_t)bytes * 1000ull) /
      LIBRETRO_FAST_READ_MIN_BYTES_PER_SEC;

   if (ms < LIBRETRO_FAST_READ_MIN_TIMEOUT_MS)
      ms = LIBRETRO_FAST_READ_MIN_TIMEOUT_MS;
   ms += LIBRETRO_FAST_READ_TIMEOUT_GRACE_MS;
   if (ms > UINT32_MAX)
      return UINT32_MAX;
   return (unsigned)ms;
}

static unsigned libretro_storage_attempts(void)
{
   return UNIFROG_SD_EXPERIMENTAL ? 3u : 1u;
}

static int libretro_recover_storage(const char *tag)
{
   if (!UNIFROG_SD_EXPERIMENTAL)
      return -1;
   return unifrog_platform_recover_storage(tag, 4, 100);
}

static void quick_state_recovery_clear(void)
{
   free(quick_state_recovery.data);
   memset(&quick_state_recovery, 0, sizeof(quick_state_recovery));
}

static int quick_state_build_image(const void *raw, size_t raw_size,
   uint8_t **out_data, size_t *out_size, uint32_t *out_flags)
{
   struct quick_state_file_header header;
   uint8_t *buffer = NULL;
   uLongf compressed_size;
   uint8_t *payload;
   size_t payload_size;
   int compressed_ok = 0;

   if (!raw || !out_data || !out_size || !out_flags)
      return -1;
   *out_data = NULL;
   *out_size = 0;
   *out_flags = 0;

   payload_size = raw_size;
   compressed_size = compressBound((uLong)raw_size);
   if (compressed_size > 0 && compressed_size < SIZE_MAX - sizeof(header)) {
      buffer = malloc(sizeof(header) + (size_t)compressed_size);
      if (buffer) {
         payload = buffer + sizeof(header);
         compressed_size = (uLongf)compressed_size;
         if (compress2(payload, &compressed_size, raw, (uLong)raw_size,
             Z_BEST_SPEED) == Z_OK && compressed_size > 0 &&
             compressed_size < (uLongf)raw_size) {
            compressed_ok = 1;
            payload_size = (size_t)compressed_size;
            *out_flags = LIBRETRO_STATE_FLAG_COMPRESSED;
         }
      }
   }
   if (!compressed_ok) {
      free(buffer);
      buffer = malloc(sizeof(header) + raw_size);
      if (!buffer)
         return -1;
      payload = buffer + sizeof(header);
      memcpy(payload, raw, raw_size);
      payload_size = raw_size;
   }

   header.magic = LIBRETRO_STATE_MAGIC;
   header.version = LIBRETRO_STATE_VERSION;
   header.flags = *out_flags;
   header.raw_size = (uint32_t)raw_size;
   header.data_size = (uint32_t)payload_size;
   memcpy(buffer, &header, sizeof(header));
   *out_data = buffer;
   *out_size = sizeof(header) + payload_size;
   return 0;
}

static void quick_state_recovery_store(const char *core_id,
   const char *path, unsigned slot, const uint8_t *data, size_t size,
   size_t raw_size, uint32_t flags)
{
   uint8_t *copy;

   if (!data || size == 0 || !path || !path[0])
      return;
   copy = malloc(size);
   if (!copy)
      return;
   memcpy(copy, data, size);
   quick_state_recovery_clear();
   quick_state_recovery.data = copy;
   quick_state_recovery.data_size = size;
   quick_state_recovery.raw_size = raw_size;
   quick_state_recovery.flags = flags;
   quick_state_recovery.slot = slot;
   quick_state_recovery.pending = 1;
   snprintf(quick_state_recovery.path, sizeof(quick_state_recovery.path),
      "%s", path);
   snprintf(quick_state_recovery.core_id, sizeof(quick_state_recovery.core_id),
      "%s", core_id ? core_id : "");
}

static int quick_state_recovery_try_flush(void)
{
   FILE *file;
   int ok;

   if (!quick_state_recovery.pending || !quick_state_recovery.data ||
       !quick_state_recovery.path[0])
      return 0;
   if (!unifrog_platform_storage_ready())
      return -1;

   (void)mkdir(UNIFROG_SAVE_ROOT, 0777);
   file = fopen(quick_state_recovery.path, "wb");
   if (!file)
      return -1;

   ok = fwrite(quick_state_recovery.data, 1,
      quick_state_recovery.data_size, file) ==
      quick_state_recovery.data_size && fflush(file) == 0;
   fclose(file);
   if (!ok)
      return -1;

   printf("unifrog quick state recovery flushed core=%s path=%s size=%u raw=%u flags=0x%lx slot=%u\n",
      quick_state_recovery.core_id, quick_state_recovery.path,
      (unsigned)quick_state_recovery.data_size,
      (unsigned)quick_state_recovery.raw_size,
      (unsigned long)quick_state_recovery.flags, quick_state_recovery.slot);
   quick_state_recovery_clear();
   return 0;
}

int unifrog_libretro_recover_saved_state(void)
{
   return quick_state_recovery_try_flush();
}

static int libretro_log_flush_force_if_safe(void)
{
   if (UNIFROG_SD_EXPERIMENTAL)
      return 0;
   if (host.quick_core) {
      unifrog_log_note_storage_quiet(LIBRETRO_STORAGE_POST_CORE_QUIET_MS);
      return 0;
   }
   return unifrog_log_flush_force();
}

static void libretro_storage_quiet_begin(const char *tag)
{
   unifrog_log_defer_begin();
   unifrog_platform_set_storage_log_suspended(1);
   printf("unifrog storage quiet begin tag=%s pending=%u capacity=%u\n",
      tag ? tag : "", (unsigned)unifrog_log_pending(),
      (unsigned)unifrog_log_capacity());
}

static void libretro_storage_quiet_end(const char *tag, int flush)
{
   printf("unifrog storage quiet end tag=%s pending=%u deferred=%d flush=%d\n",
      tag ? tag : "", (unsigned)unifrog_log_pending(),
      unifrog_log_flush_deferred(), flush ? 1 : 0);
   unifrog_log_defer_end();
   if (flush)
      (void)unifrog_log_flush();
   unifrog_platform_set_storage_log_suspended(0);
}

static int libretro_read_profile_name_enabled(const char *profile)
{
   if (!profile || !profile[0])
      return 0;
   if (strcmp(profile, "boot") == 0 || strcmp(profile, "safe") == 0 ||
       strcmp(profile, "off") == 0 || strcmp(profile, "none") == 0)
      return 0;
   if (strcmp(profile, UNIFROG_SD_MODE) == 0)
      return 0;
   return 1;
}

static int libretro_read_profile_enabled(void)
{
   return libretro_read_profile_name_enabled(UNIFROG_SD_READ_MODE);
}

static unsigned libretro_read_profile_backoff_windows;

static void libretro_read_profile_backoff(const char *tag)
{
   libretro_read_profile_backoff_windows = 6;
   unifrog_log_note_storage_quiet(5000u);
   printf("unifrog libretro read_profile backoff windows=%u profile=%s tag=%s\n",
      libretro_read_profile_backoff_windows, UNIFROG_SD_READ_MODE,
      tag ? tag : "");
}

static int libretro_begin_read_profile(const char *tag)
{
   char detail[160];
   char restore_detail[160];
   int ret;
   int restore_ret;

   if (!libretro_read_profile_enabled())
      return 0;
   if (libretro_read_profile_load_session_active) {
      printf("unifrog libretro read_profile reuse profile=%s tag=%s session=load\n",
         UNIFROG_SD_READ_MODE, tag ? tag : "");
      return LIBRETRO_READ_PROFILE_BORROWED;
   }
   if (libretro_read_profile_backoff_windows > 0) {
      printf("unifrog libretro read_profile skip profile=%s tag=%s reason=backoff remaining=%u\n",
         UNIFROG_SD_READ_MODE, tag ? tag : "",
         libretro_read_profile_backoff_windows);
      libretro_read_profile_backoff_windows--;
      return 0;
   }
   if (!unifrog_platform_sd_runtime_supported()) {
      printf("unifrog libretro read_profile skip profile=%s tag=%s reason=no_runtime\n",
         UNIFROG_SD_READ_MODE, tag ? tag : "");
      return 0;
   }

   detail[0] = '\0';
   restore_detail[0] = '\0';
   libretro_storage_quiet_begin(tag);
   unifrog_log_sync("read_profile begin profile=%s boot=%s tag=%s",
      UNIFROG_SD_READ_MODE, UNIFROG_SD_MODE, tag ? tag : "");
   printf("unifrog libretro read_profile begin profile=%s boot=%s tag=%s\n",
      UNIFROG_SD_READ_MODE, UNIFROG_SD_MODE, tag ? tag : "");
   ret = unifrog_platform_sd_apply_profile(UNIFROG_SD_READ_MODE, 4, 100,
      detail, sizeof(detail));
   unifrog_log_sync("read_profile switch ret=%d profile=%s tag=%s detail=%s",
      ret, UNIFROG_SD_READ_MODE, tag ? tag : "", detail);
   printf("unifrog libretro read_profile switch ret=%d profile=%s detail=%s\n",
      ret, UNIFROG_SD_READ_MODE, detail);
   if (ret == 0)
      return LIBRETRO_READ_PROFILE_OWNED;

   restore_ret = unifrog_platform_sd_restore_boot(4, 100, restore_detail,
      sizeof(restore_detail));
   unifrog_log_sync("read_profile fallback_restore ret=%d profile=%s tag=%s detail=%s",
      restore_ret, UNIFROG_SD_READ_MODE, tag ? tag : "", restore_detail);
   printf("unifrog libretro read_profile fallback restore_ret=%d detail=%s\n",
      restore_ret, restore_detail);
   libretro_storage_quiet_end(tag, 1);
   return 0;
}

static int libretro_read_profile_load_session_begin(const char *tag)
{
   int active;

   active = libretro_begin_read_profile(tag);
   if (active == LIBRETRO_READ_PROFILE_OWNED) {
      libretro_read_profile_load_session_active = 1;
      printf("unifrog libretro read_profile load_session begin profile=%s tag=%s\n",
         UNIFROG_SD_READ_MODE, tag ? tag : "");
      return active;
   }
   return 0;
}

static unsigned libretro_fast_read_timeout_for_path(const char *path,
   unsigned floor_ms)
{
   struct stat st;
   unsigned long bytes;
   unsigned timeout_ms;

   if (!path || stat(path, &st) != 0 || st.st_size <= 0)
      return floor_ms;
   bytes = (unsigned long)st.st_size;
   timeout_ms = (unsigned)((bytes + 1024ul * 1024ul - 1ul) /
      (1024ul * 1024ul)) * 1000u;
   timeout_ms += 1500u;
   if (timeout_ms < floor_ms)
      timeout_ms = floor_ms;
   return timeout_ms;
}

static void libretro_end_read_profile_ex(int active, const char *tag, int ok,
   int flush)
{
   char detail[160];
   int ret;

   if (!active)
      return;
   if (active == LIBRETRO_READ_PROFILE_BORROWED) {
      printf("unifrog libretro read_profile reuse end ok=%d flush=%d profile=%s tag=%s session=load\n",
         ok ? 1 : 0, flush ? 1 : 0, UNIFROG_SD_READ_MODE,
         tag ? tag : "");
      return;
   }
   detail[0] = '\0';
   if (libretro_read_profile_load_session_active)
      libretro_read_profile_load_session_active = 0;
   ret = unifrog_platform_sd_restore_boot(4, 100, detail, sizeof(detail));
   unifrog_log_sync("read_profile restore ret=%d ok=%d flush=%d profile=%s tag=%s detail=%s",
      ret, ok ? 1 : 0, flush ? 1 : 0, UNIFROG_SD_READ_MODE,
      tag ? tag : "", detail);
   printf("unifrog libretro read_profile restore ret=%d ok=%d flush=%d profile=%s tag=%s detail=%s\n",
      ret, ok ? 1 : 0, flush ? 1 : 0, UNIFROG_SD_READ_MODE,
      tag ? tag : "", detail);
   libretro_storage_quiet_end(tag, flush);
}

static void libretro_end_read_profile(int active, const char *tag, int ok)
{
   libretro_end_read_profile_ex(active, tag, ok, 1);
}

static void libretro_read_profile_load_session_end(const char *tag, int ok)
{
   if (!libretro_read_profile_load_session_active)
      return;
   printf("unifrog libretro read_profile load_session end profile=%s tag=%s ok=%d\n",
      UNIFROG_SD_READ_MODE, tag ? tag : "", ok ? 1 : 0);
   libretro_end_read_profile(LIBRETRO_READ_PROFILE_OWNED,
      tag ? tag : "core_load_session", ok);
}

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
   options->framebuffer_format = UNIFROG_LIBRETRO_FB_RGB565;
   options->input_profile = UNIFROG_LIBRETRO_INPUT_DEFAULT;
   options->state_auto_load = 0;
   options->state_auto_save = 0;
   options->state_slot = 0;
   options->max_frames = 0;
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

static unsigned sanitize_fast_forward_multiplier(unsigned multiplier)
{
   switch (multiplier) {
   case 0:
   case 2:
   case 4:
   case 8:
   case 16:
      return multiplier;
   default:
      return 0;
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

static int sanitize_framebuffer_format(int framebuffer_format)
{
   switch (framebuffer_format) {
   case UNIFROG_LIBRETRO_FB_RGB565:
   case UNIFROG_LIBRETRO_FB_XRGB8888:
      return framebuffer_format;
   default:
      return UNIFROG_LIBRETRO_FB_RGB565;
   }
}

static const char *framebuffer_format_label(int framebuffer_format)
{
   return sanitize_framebuffer_format(framebuffer_format) ==
      UNIFROG_LIBRETRO_FB_XRGB8888 ? "XRGB8888" : "RGB565";
}

static const char *framebuffer_format_opt_value(int framebuffer_format)
{
   return sanitize_framebuffer_format(framebuffer_format) ==
      UNIFROG_LIBRETRO_FB_XRGB8888 ? "xrgb8888" : "rgb565";
}

static int framebuffer_format_from_text(const char *text, int fallback)
{
   if (!text || !text[0])
      return fallback;
   if (strcasecmp(text, "xrgb8888") == 0 ||
       strcasecmp(text, "argb8888") == 0 ||
       strcasecmp(text, "rgb8888") == 0 ||
       strcasecmp(text, "8888") == 0 ||
       strcasecmp(text, "32") == 0)
      return UNIFROG_LIBRETRO_FB_XRGB8888;
   if (strcasecmp(text, "rgb565") == 0 ||
       strcasecmp(text, "565") == 0 ||
       strcasecmp(text, "16") == 0)
      return UNIFROG_LIBRETRO_FB_RGB565;
   return fallback;
}

static int sanitize_input_profile(int input_profile)
{
   switch (input_profile) {
   case UNIFROG_LIBRETRO_INPUT_DEFAULT:
   case UNIFROG_LIBRETRO_INPUT_RETROARCH:
   case UNIFROG_LIBRETRO_INPUT_GENESIS:
   case UNIFROG_LIBRETRO_INPUT_SWAP_AB:
   case UNIFROG_LIBRETRO_INPUT_SWAP_XY:
      return input_profile;
   default:
      return UNIFROG_LIBRETRO_INPUT_DEFAULT;
   }
}

static int input_profile_from_text(const char *text, int fallback)
{
   if (!text || !text[0])
      return sanitize_input_profile(fallback);
   if (strcasecmp(text, "default") == 0 ||
       strcasecmp(text, "stock") == 0)
      return UNIFROG_LIBRETRO_INPUT_DEFAULT;
   if (strcasecmp(text, "retroarch") == 0 ||
       strcasecmp(text, "ra") == 0)
      return UNIFROG_LIBRETRO_INPUT_RETROARCH;
   if (strcasecmp(text, "genesis") == 0 ||
       strcasecmp(text, "md") == 0)
      return UNIFROG_LIBRETRO_INPUT_GENESIS;
   if (strcasecmp(text, "swap_ab") == 0 ||
       strcasecmp(text, "swap-ab") == 0)
      return UNIFROG_LIBRETRO_INPUT_SWAP_AB;
   if (strcasecmp(text, "swap_xy") == 0 ||
       strcasecmp(text, "swap-xy") == 0)
      return UNIFROG_LIBRETRO_INPUT_SWAP_XY;
   return sanitize_input_profile(fallback);
}

static const char *input_profile_opt_value(int input_profile)
{
   switch (sanitize_input_profile(input_profile)) {
   case UNIFROG_LIBRETRO_INPUT_RETROARCH:
      return "retroarch";
   case UNIFROG_LIBRETRO_INPUT_GENESIS:
      return "genesis";
   case UNIFROG_LIBRETRO_INPUT_SWAP_AB:
      return "swap_ab";
   case UNIFROG_LIBRETRO_INPUT_SWAP_XY:
      return "swap_xy";
   default:
      return "default";
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
   host.options.audio_gain = LIBRETRO_AUDIO_DEFAULT_GAIN;
   if (!valid_scpu_mhz(host.options.scpu_mhz))
      host.options.scpu_mhz = 0;
   host.ge_clock = sanitize_ge_clock(host.options.ge_clock);
   host.options.ge_clock = (int)host.ge_clock;
   if (host.options.backlight_level > 100)
      host.options.backlight_level = 100;
   host.options.frameskip = sanitize_frameskip(host.options.frameskip);
   host.options.display_mode = sanitize_display_mode(host.options.display_mode);
   host.options.framebuffer_format =
      sanitize_framebuffer_format(host.options.framebuffer_format);
   host.options.input_profile =
      sanitize_input_profile(host.options.input_profile);
   if (host.options.state_slot >= LIBRETRO_STATE_SLOT_COUNT)
      host.options.state_slot = 0;
   host.options.state_auto_load = host.options.state_auto_load ? 1 : 0;
   host.options.state_auto_save = host.options.state_auto_save ? 1 : 0;
   host.options.core_id[sizeof(host.options.core_id) - 1] = '\0';

   host.audio_enabled = host.options.audio_enabled;
   host.audio_gain = host.options.audio_gain;
   host.scpu_target_mhz = host.options.scpu_mhz;
   host.display_mode = host.options.display_mode;
   host.framebuffer_format = host.options.framebuffer_format;
   host.input_profile = host.options.input_profile;
   host.input_profile_dirty = 0;
   host.quick_state_slot = host.options.state_slot;
   host.quick_state_auto_load = host.options.state_auto_load;
   host.quick_state_auto_save = host.options.state_auto_save;
   host.fast_forward_multiplier = 0;
}

static uintptr_t host_read_gp(void)
{
   uintptr_t gp;

   __asm__ volatile("move %0, $28" : "=r"(gp));
   return gp;
}

static uintptr_t host_read_sp(void)
{
   uintptr_t sp;

   __asm__ volatile("move %0, $29" : "=r"(sp));
   return sp;
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

static void libretro_activity_set(const struct libretro_core_api *core,
   const char *path, uint32_t phase, uint32_t marker)
{
   uint32_t core_hash = unifrog_exception_activity_hash(
      core && core->id ? core->id : "");
   uint32_t path_hash = unifrog_exception_activity_hash(path ? path : "");

   unifrog_exception_activity_set(phase, marker, core_hash, path_hash);
}

static void libretro_activity_set_core_call(
   const struct libretro_core_api *core, uint32_t phase, uint32_t marker,
   const void *fn)
{
   unifrog_exception_activity_set(phase, marker,
      (uint32_t)(uintptr_t)fn, (uint32_t)core_call_gp(core));
}

static void libretro_core_call_probe(const char *stage,
   const struct libretro_core_api *core, const void *fn)
{
   UBaseType_t stack_free_words = uxTaskGetStackHighWaterMark(NULL);

   printf("unifrog libretro core_call stage=%s core=%s fn=0x%08lx call_gp=0x%08lx host_gp=0x%08lx expected_gp=0x%08lx sp=0x%08lx stack_free_words=%lu\n",
      stage ? stage : "",
      core && core->id ? core->id : "",
      (unsigned long)(uintptr_t)fn,
      (unsigned long)core_call_gp(core),
      (unsigned long)host_read_gp(),
      (unsigned long)host_expected_gp(),
      (unsigned long)host_read_sp(),
       (unsigned long)stack_free_words);
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
      (void)libretro_log_flush_force_if_safe();
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
   uint64_t start_us;
   uint64_t open_done_us;
   uint64_t read_done_us;
   int ok;

   if (quick_memory_data(core, id, &data, &size) != 0) {
      if (manual)
         snprintf(host.quick_status, sizeof(host.quick_status),
            "SAVE RAM UNSUPPORTED");
      return -1;
   }

   quick_memory_path(core, rom_path, extension, path, sizeof(path));
   start_us = host_time_us();
   file = fopen(path, "rb");
   open_done_us = host_time_us();
   if (!file) {
      if (manual)
         snprintf(host.quick_status, sizeof(host.quick_status),
            "NO SAVE RAM FILE");
      printf("unifrog quick load_memory missing id=%u path=%s size=%u open_ms=%u manual=%d\n",
         id, path, (unsigned)size,
         host_elapsed_ms(start_us, open_done_us), manual ? 1 : 0);
      return -1;
   }
   read_size = fread(data, 1, size, file);
   read_done_us = host_time_us();
   ok = read_size == size && ferror(file) == 0;
   fclose(file);
   if (manual)
      snprintf(host.quick_status, sizeof(host.quick_status),
         ok ? "SAVE RAM LOADED" : "SAVE RAM READ FAILED");
   printf("unifrog quick load_memory id=%u path=%s size=%u read=%u ok=%d manual=%d open_ms=%u read_ms=%u total_ms=%u\n",
      id, path, (unsigned)size, (unsigned)read_size, ok, manual ? 1 : 0,
      host_elapsed_ms(start_us, open_done_us),
      host_elapsed_ms(open_done_us, read_done_us),
      host_elapsed_ms(start_us, read_done_us));
   if (ok)
      (void)quick_note_memory_hash(core, id);
   if (manual)
      (void)libretro_log_flush_force_if_safe();
   return ok ? 0 : -1;
}

static size_t quick_existing_memory_file_bytes(
   const struct libretro_core_api *core, const char *rom_path)
{
   size_t total = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(quick_memory_files); i++) {
      char path[160];
      struct stat st;

      quick_memory_path(core, rom_path, quick_memory_files[i].extension,
         path, sizeof(path));
      if (stat(path, &st) == 0 && st.st_size > 0)
         total += (size_t)st.st_size;
   }
   return total;
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
      printf("unifrog quick save_state unsupported_api core=%s serialize_size=%p serialize=%p slot=%u\n",
         core && core->id ? core->id : "(none)",
         core ? (void *)core->serialize_size : NULL,
         core ? (void *)core->serialize : NULL, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   size = CORE_CALL0_RET(core, core->serialize_size);
   if (size == 0 || size > LIBRETRO_STATE_FILE_MAX) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         size == 0 ? "STATE UNSUPPORTED" : "STATE TOO LARGE");
      printf("unifrog quick save_state unsupported core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   data = malloc(size);
   if (!data) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE OUT OF MEMORY");
      printf("unifrog quick save_state oom core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)libretro_log_flush_force_if_safe();
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
   (void)libretro_log_flush_force_if_safe();
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
   uint8_t *file_data = NULL;
   struct quick_state_file_header header;
   size_t size;
   size_t read_size;
   size_t file_size;
   int extra;
   int ok;
   int ret = -1;

   if (!core || !core->serialize_size || !core->unserialize) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE UNSUPPORTED");
      printf("unifrog quick load_state unsupported_api core=%s serialize_size=%p unserialize=%p slot=%u\n",
         core && core->id ? core->id : "(none)",
         core ? (void *)core->serialize_size : NULL,
         core ? (void *)core->unserialize : NULL, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   (void)quick_state_recovery_try_flush();
   size = CORE_CALL0_RET(core, core->serialize_size);
   if (size == 0 || size > LIBRETRO_STATE_FILE_MAX) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         size == 0 ? "STATE UNSUPPORTED" : "STATE TOO LARGE");
      printf("unifrog quick load_state unsupported core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   data = malloc(size);
   if (!data) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE OUT OF MEMORY");
      printf("unifrog quick load_state oom core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   quick_state_path(core, rom_path, slot, path, sizeof(path));
   file = fopen(path, "rb");
   if (!file) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "NO STATE FILE");
      goto out;
   }

   read_size = fread(&header, 1, sizeof(header), file);
   if (read_size == sizeof(header) && header.magic == LIBRETRO_STATE_MAGIC &&
       header.version == LIBRETRO_STATE_VERSION &&
       header.raw_size == (uint32_t)size &&
       header.data_size > 0 && header.data_size <= LIBRETRO_STATE_FILE_MAX &&
       header.data_size <= LIBRETRO_STATE_FILE_MAX - sizeof(header)) {
      file_size = header.data_size;
      file_data = malloc(file_size);
      if (!file_data) {
         snprintf(host.quick_status, sizeof(host.quick_status),
            "STATE OUT OF MEMORY");
         printf("unifrog quick load_state oom compressed core=%s path=%s size=%u slot=%u data=%u\n",
            core->id, path, (unsigned)size, slot, (unsigned)file_size);
         goto out;
      }
      read_size = fread(file_data, 1, file_size, file);
      extra = fgetc(file);
      ok = read_size == file_size && extra == EOF && ferror(file) == 0;
      fclose(file);
      file = NULL;
      if (!ok) {
         snprintf(host.quick_status, sizeof(host.quick_status),
            "STATE SIZE MISMATCH");
         printf("unifrog quick load_state read_failed compressed core=%s path=%s size=%u read=%u slot=%u extra=%d\n",
            core->id, path, (unsigned)file_size, (unsigned)read_size, slot,
            extra);
         goto out;
      }
      if (header.flags & LIBRETRO_STATE_FLAG_COMPRESSED) {
         uLongf raw_size = (uLongf)size;

         ok = uncompress(data, &raw_size, file_data, (uLongf)file_size) == Z_OK &&
            raw_size == (uLongf)size;
         if (!ok) {
            snprintf(host.quick_status, sizeof(host.quick_status),
               "STATE DECODE FAILED");
            printf("unifrog quick load_state decode_failed core=%s path=%s size=%u compressed=%u slot=%u\n",
               core->id, path, (unsigned)size, (unsigned)file_size, slot);
            goto out;
         }
      } else {
         if (file_size != size) {
            snprintf(host.quick_status, sizeof(host.quick_status),
               "STATE SIZE MISMATCH");
            printf("unifrog quick load_state raw_size_mismatch core=%s path=%s size=%u read=%u slot=%u\n",
               core->id, path, (unsigned)size, (unsigned)file_size, slot);
            goto out;
         }
         memcpy(data, file_data, size);
      }
   } else {
      if (fseek(file, 0, SEEK_SET) != 0) {
         snprintf(host.quick_status, sizeof(host.quick_status),
            "STATE READ FAILED");
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
   free(file_data);
   (void)libretro_log_flush_force_if_safe();
   return ret;
}

static unsigned libretro_watchdog_load_stall_polls(void)
{
   return UNIFROG_SD_EXPERIMENTAL ? 120u :
      LIBRETRO_WATCHDOG_LOAD_STALL_POLLS;
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
          stable_polls >= libretro_watchdog_load_stall_polls()) {
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
   loading_draw(host.loading_title[0] ? host.loading_title : "LOADING GAME",
      detail, percent);

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
         (void)unifrog_log_flush();
   }
}

static void loading_draw_frame(const char *title, const char *detail,
   unsigned percent)
{
   static const char spin[] = "|/-\\";
   struct unifrog_surface surface;
   unsigned buffer = 0;
   char detail_spin[80];
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
   libretro_watchdog_load_progress(detail ? detail : title, percent, 100);
   buffer = host.loading_fb.current_buffer;
   if (host.loading_fb.buffer_count > 1)
      buffer = (host.loading_fb.current_buffer + 1) % host.loading_fb.buffer_count;
   surface = unifrog_fb_surface_for_buffer(&host.loading_fb, buffer);
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height,
      UNIFROG_RGB565(0, 0, 0));
   unifrog_gfx_draw_text(&surface, 18, 54, title ? title : "LOADING",
      UNIFROG_RGB565(236, 241, 246), 2);
   if (detail && detail[0])
      snprintf(detail_spin, sizeof(detail_spin), "%c %s",
         spin[host.loading_anim_tick % 4u], detail);
   else
      snprintf(detail_spin, sizeof(detail_spin), "%c",
         spin[host.loading_anim_tick % 4u]);
   if (detail_spin[0])
      unifrog_gfx_draw_text(&surface, 18, 86, detail_spin,
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

static void loading_task(void *arg)
{
   (void)arg;
   host.loading_task_running = 1;
   while (!host.loading_task_stop) {
      vTaskDelay(250 / portTICK_PERIOD_MS);
      if (host.loading_open) {
         host.loading_anim_tick++;
         if (host.loading_visual_percent < 95u &&
             host.loading_visual_percent <= host.loading_percent + 12u)
            host.loading_visual_percent++;
         loading_draw_frame(host.loading_title, host.loading_detail,
            host.loading_visual_percent);
      }
   }
   host.loading_task_running = 0;
   vTaskDelete(NULL);
}

static void loading_start_task(void)
{
   if (host.loading_task_running || host.loading_task)
      return;
   host.loading_task_stop = 0;
   if (xTaskCreate(loading_task, (const char *)"lr_load_anim",
       configTASK_STACK_DEPTH, NULL, portPRI_TASK_NORMAL,
       &host.loading_task) != pdPASS) {
      host.loading_task = NULL;
      host.loading_task_running = 0;
   }
}

static void loading_close(void)
{
   if (host.loading_task) {
      host.loading_task_stop = 1;
      for (unsigned i = 0; host.loading_task_running && i < 20u; i++)
         vTaskDelay(10 / portTICK_PERIOD_MS);
      host.loading_task = NULL;
   }
   if (host.loading_open) {
      unifrog_fb_close(&host.loading_fb);
      host.loading_open = 0;
   }
}

static void loading_draw(const char *title, const char *detail, unsigned percent)
{
   unsigned old_percent = host.loading_percent;

   if (percent > 100u)
      percent = 100u;
   host.loading_percent = percent;
   if (!host.loading_open || percent < old_percent ||
       host.loading_visual_percent < percent || percent >= 100u)
      host.loading_visual_percent = percent;
   unifrog_text_copy(host.loading_title, sizeof(host.loading_title),
      title ? title : "LOADING");
   unifrog_text_copy(host.loading_detail, sizeof(host.loading_detail),
      detail ? detail : "");
   loading_start_task();
   loading_draw_frame(title, detail, percent);
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

static void core_option_copy(char *dst, size_t size, const char *src)
{
   if (!dst || size == 0)
      return;
   if (!src)
      src = "";
   snprintf(dst, size, "%s", src);
}

static int core_option_find(const char *key)
{
   if (!key || !key[0])
      return -1;
   for (unsigned i = 0; i < host.core_option_count; i++) {
      if (strcmp(host.core_options[i].key, key) == 0)
         return (int)i;
   }
   return -1;
}

static void core_option_set_selected(struct quick_core_option *option,
   const char *value)
{
   if (!option || !value)
      return;
   for (unsigned i = 0; i < option->value_count; i++) {
      if (strcmp(option->values[i], value) == 0) {
         option->selected = i;
         return;
      }
   }
}

static void core_options_load_opt_file(void)
{
   char path[128];
   FILE *file;
   char line[160];

   if (host.core_options_loaded || !host.core_id || !host.core_id[0])
      return;
   host.core_options_loaded = 1;
   snprintf(path, sizeof(path), "%s/%s.opt", LIBRETRO_SAVE_DIR,
      host.core_id);
   file = fopen(path, "rb");
   if (!file)
      return;
   while (fgets(line, sizeof(line), file)) {
      char *eq;
      char *key;
      char *value;
      int index;

      key = line;
      while (*key == ' ' || *key == '\t')
         key++;
      if (*key == '#' || *key == '\n' || *key == '\r' || *key == '\0')
         continue;
      eq = strchr(key, '=');
      if (!eq)
         continue;
      *eq = '\0';
      value = eq + 1;
      while (eq > key && (eq[-1] == ' ' || eq[-1] == '\t'))
         *--eq = '\0';
      while (*value == ' ' || *value == '\t')
         value++;
      value[strcspn(value, "\r\n")] = '\0';
      if (strcmp(key, "unifrog_keymap") == 0 ||
          strcmp(key, "input_profile") == 0) {
         host.input_profile = input_profile_from_text(value,
            host.input_profile);
         host.options.input_profile = host.input_profile;
         continue;
      }
      index = core_option_find(key);
      if (index >= 0)
         core_option_set_selected(&host.core_options[index], value);
   }
   fclose(file);
   printf("unifrog core_options opt_load core=%s path=%s count=%u\n",
      host.core_id, path, host.core_option_count);
}

static void core_options_save_opt_file(void)
{
   char path[128];
   FILE *file;

   if (!host.core_options_dirty || !host.core_id || !host.core_id[0])
      return;
   (void)mkdir("/media/mmcblk0/unifrog", 0777);
   (void)mkdir(LIBRETRO_SAVE_DIR, 0777);
   snprintf(path, sizeof(path), "%s/%s.opt", LIBRETRO_SAVE_DIR,
      host.core_id);
   file = fopen(path, "wb");
   if (!file) {
      printf("unifrog core_options opt_save open_failed core=%s path=%s\n",
         host.core_id, path);
      return;
   }
   for (unsigned i = 0; i < host.core_option_count; i++) {
      const struct quick_core_option *option = &host.core_options[i];
      const char *value = option->selected < option->value_count ?
         option->values[option->selected] : "";
      if (option->key[0] && value[0])
         fprintf(file, "%s=%s\n", option->key, value);
   }
   fprintf(file, "unifrog_keymap=%s\n",
      input_profile_opt_value(host.input_profile));
   fclose(file);
   host.core_options_dirty = 0;
   host.input_profile_dirty = 0;
   printf("unifrog core_options opt_save core=%s path=%s count=%u\n",
      host.core_id, path, host.core_option_count);
}

static void core_options_reset(void)
{
   memset(host.core_options, 0, sizeof(host.core_options));
   host.core_option_count = 0;
   host.core_options_dirty = 0;
   host.core_options_loaded = 0;
   host.input_profile_dirty = 0;
}

static void core_option_add_value(struct quick_core_option *option,
   const char *value, const char *label)
{
   if (!option || !value || !value[0] ||
       option->value_count >= LIBRETRO_CORE_OPTION_VALUE_MAX)
      return;
   core_option_copy(option->values[option->value_count],
      sizeof(option->values[0]), value);
   core_option_copy(option->value_labels[option->value_count],
      sizeof(option->value_labels[0]), label && label[0] ? label : value);
   option->value_count++;
}

static void core_options_register_one(const char *key, const char *label,
   const struct retro_core_option_value *values, const char *default_value)
{
   struct quick_core_option *option;

   if (!key || !key[0] || host.core_option_count >= LIBRETRO_CORE_OPTION_MAX)
      return;
   if (core_option_find(key) >= 0)
      return;
   option = &host.core_options[host.core_option_count];
   memset(option, 0, sizeof(*option));
   core_option_copy(option->key, sizeof(option->key), key);
   core_option_copy(option->label, sizeof(option->label),
      label && label[0] ? label : key);
   option->visible = 1;
   if (values) {
      for (unsigned i = 0;
           i < RETRO_NUM_CORE_OPTION_VALUES_MAX &&
           values[i].value && option->value_count < LIBRETRO_CORE_OPTION_VALUE_MAX;
           i++)
         core_option_add_value(option, values[i].value, values[i].label);
   }
   if (option->value_count == 0)
      core_option_add_value(option, default_value ? default_value : "enabled",
         default_value ? default_value : "enabled");
   if (default_value)
      core_option_set_selected(option, default_value);
   host.core_option_count++;
}

static void core_options_register_definitions(
   const struct retro_core_option_definition *defs)
{
   if (!defs)
      return;
   for (unsigned i = 0; defs[i].key && i < LIBRETRO_CORE_OPTION_MAX; i++)
      core_options_register_one(defs[i].key, defs[i].desc,
         defs[i].values, defs[i].default_value);
   core_options_load_opt_file();
   printf("unifrog core_options registered legacy core=%s count=%u\n",
      host.core_id ? host.core_id : "", host.core_option_count);
}

static void core_options_register_v2(
   const struct retro_core_option_v2_definition *defs)
{
   if (!defs)
      return;
   for (unsigned i = 0; defs[i].key && i < LIBRETRO_CORE_OPTION_MAX; i++)
      core_options_register_one(defs[i].key,
         defs[i].desc_categorized && defs[i].desc_categorized[0] ?
            defs[i].desc_categorized : defs[i].desc,
         defs[i].values, defs[i].default_value);
   core_options_load_opt_file();
   printf("unifrog core_options registered v2 core=%s count=%u\n",
      host.core_id ? host.core_id : "", host.core_option_count);
}

static void core_options_register_variables(const struct retro_variable *vars)
{
   if (!vars)
      return;
   for (unsigned i = 0; vars[i].key && i < LIBRETRO_CORE_OPTION_MAX; i++) {
      struct retro_core_option_value values[LIBRETRO_CORE_OPTION_VALUE_MAX];
      char buffer[256];
      char *semi;
      char *token;
      unsigned count = 0;

      memset(values, 0, sizeof(values));
      core_option_copy(buffer, sizeof(buffer), vars[i].value);
      semi = strchr(buffer, ';');
      token = semi ? semi + 1 : buffer;
      while (*token == ' ')
         token++;
      while (token && *token && count < LIBRETRO_CORE_OPTION_VALUE_MAX - 1u) {
         char *next = strchr(token, '|');
         if (next)
            *next++ = '\0';
         while (*token == ' ')
            token++;
         if (*token) {
            values[count].value = token;
            values[count].label = token;
            count++;
         }
         token = next;
      }
      core_options_register_one(vars[i].key, buffer, values,
         count ? values[0].value : NULL);
   }
   core_options_load_opt_file();
   printf("unifrog core_options registered variables core=%s count=%u\n",
      host.core_id ? host.core_id : "", host.core_option_count);
}

static bool host_get_variable(struct retro_variable *var)
{
   int option_index;

   if (!var || !var->key)
      return false;

   option_index = core_option_find(var->key);
   if (option_index >= 0) {
      struct quick_core_option *option = &host.core_options[option_index];
      if (option->selected < option->value_count) {
         var->value = option->values[option->selected];
         printf("unifrog libretro variable %s=%s source=core_options\n",
            var->key, var->value);
         return true;
      }
   }

   if (strcmp(var->key, "gpsp_drc") == 0) {
      var->value = "enabled";
      printf("unifrog libretro variable %s=%s\n", var->key, var->value);
      return true;
   }
   if (host.core_id && (strcmp(host.core_id, "gpsp") == 0 ||
       strcmp(host.core_id, "gpsp-gbac-prosty") == 0)) {
      if (strcmp(var->key, "gpsp_frameskip") == 0) {
         if (host.fast_forward && host.fast_forward_multiplier > 1)
            var->value = "fixed_interval";
         else if (host.options.frameskip == UNIFROG_LIBRETRO_FRAMESKIP_AUTO)
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
         if (host.fast_forward && host.fast_forward_multiplier > 1) {
            switch (sanitize_fast_forward_multiplier(
               host.fast_forward_multiplier)) {
            case 16:
               var->value = "15";
               break;
            case 8:
               var->value = "7";
               break;
            case 4:
               var->value = "3";
               break;
            default:
               var->value = "1";
               break;
            }
         } else if (host.options.frameskip ==
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
   enum unifrog_button mapped;

   switch (id) {
   case RETRO_DEVICE_ID_JOYPAD_B:
      mapped = host.input_profile == UNIFROG_LIBRETRO_INPUT_SWAP_AB ?
         UNIFROG_BUTTON_A : UNIFROG_BUTTON_B;
      return (buttons & UNIFROG_BUTTON_MASK(mapped)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_Y:
      mapped = host.input_profile == UNIFROG_LIBRETRO_INPUT_SWAP_XY ?
         UNIFROG_BUTTON_X : UNIFROG_BUTTON_Y;
      return (buttons & UNIFROG_BUTTON_MASK(mapped)) != 0;
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
      mapped = host.input_profile == UNIFROG_LIBRETRO_INPUT_SWAP_AB ?
         UNIFROG_BUTTON_B : UNIFROG_BUTTON_A;
      return (buttons & UNIFROG_BUTTON_MASK(mapped)) != 0;
   case RETRO_DEVICE_ID_JOYPAD_X:
      mapped = host.input_profile == UNIFROG_LIBRETRO_INPUT_SWAP_XY ?
         UNIFROG_BUTTON_Y : UNIFROG_BUTTON_X;
      return (buttons & UNIFROG_BUTTON_MASK(mapped)) != 0;
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

static unsigned host_audio_output_rate(unsigned input_rate)
{
   if (input_rate >= LIBRETRO_AUDIO_MIN_OUTPUT_RATE &&
       input_rate <= LIBRETRO_AUDIO_MAX_OUTPUT_RATE)
      return input_rate;
   return DEFAULT_SAMPLE_RATE;
}

static int host_audio_scale_mono(int left, int right, unsigned *abs_out)
{
   int sample = (left + right) >> 1;
   int scaled = host_audio_scale_sample(sample);
   unsigned abs_value;

   if (scaled > 32767) {
      scaled = 32767;
      host.audio_clip_count++;
   } else if (scaled < -32768) {
      scaled = -32768;
      host.audio_clip_count++;
   }
   abs_value = scaled < 0 ? (unsigned)-scaled : (unsigned)scaled;
   if (abs_value > host.audio_peak_max)
      host.audio_peak_max = abs_value;
   if (abs_out)
      *abs_out = abs_value;
   if (abs_value < LIBRETRO_AUDIO_GATE_CLOSE_LEVEL)
      scaled = 0;
   return scaled;
}

static void host_audio_update_gate(unsigned peak_out)
{
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
}

static int host_audio_write_frames(const int16_t *frames, unsigned frame_count,
   unsigned batch_index)
{
   uint32_t write_start;
   unsigned write_count;
   int write_ret;

   if (!frame_count)
      return 0;
   write_start = unifrog_perf_count();
   write_ret = unifrog_audio_write_timeout(&host.audio, frames, frame_count,
      host_audio_write_attempts(), host_audio_write_poll_ms());
   write_count = unifrog_perf_elapsed(write_start, unifrog_perf_count());

   host.audio_write_total_count += write_count;
   if (write_count > host.audio_write_max_count)
      host.audio_write_max_count = write_count;
   host.audio_write_count++;
   if (write_ret != 0) {
      if (host.audio_failures < 8) {
         printf("unifrog libretro audio_write_fail batch=%u frames=%u count=%u\n",
            batch_index, frame_count, write_count);
         (void)unifrog_log_flush();
      }
      host.audio_failures++;
   }
   return write_ret;
}

static int host_audio_flush_sample_buffer(void)
{
   unsigned peak_out = 0;

   if (!host.audio_sample_buffer_frames)
      return 0;
   if (!host.audio_open) {
      host.audio_frames += host.audio_sample_buffer_frames;
      host.audio_sample_buffer_frames = 0;
      return 0;
   }
   for (unsigned i = 0; i < host.audio_sample_buffer_frames; i++) {
      int16_t sample = host.audio_sample_buffer[i];
      unsigned abs_out = sample < 0 ? (unsigned)-sample : (unsigned)sample;

      if (abs_out > peak_out)
         peak_out = abs_out;
   }
   host_audio_update_gate(peak_out);
   if (host_audio_write_frames(host.audio_sample_buffer,
       host.audio_sample_buffer_frames, host.audio_batches) != 0) {
      host.audio_sample_buffer_frames = 0;
      return -1;
   }
   host.audio_frames += host.audio_sample_buffer_frames;
   host.audio_batches++;
   host.audio_sample_buffer_frames = 0;
   return 0;
}

void unifrog_libretro_audio_sample_cb(int16_t left, int16_t right)
{
   unsigned abs_out = 0;
   int scaled;

   host_force_expected_gp();
   if (!host.audio_enabled || host.audio_gain == 0 || host.fast_forward)
      return;
   scaled = host_audio_scale_mono(left, right, &abs_out);
   (void)abs_out;
   host.audio_sample_buffer[host.audio_sample_buffer_frames++] =
      (int16_t)scaled;
   if (host.audio_sample_buffer_frames >= LIBRETRO_AUDIO_SAMPLE_BUFFER_FRAMES)
      (void)host_audio_flush_sample_buffer();
}

size_t unifrog_libretro_audio_batch_cb(const int16_t *data, size_t frames)
{
   size_t offset = 0;
   size_t input_offset = 0;
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
      unsigned peak_out = 0;

      while (input_offset < frames &&
             out_frames < LIBRETRO_AUDIO_WRITE_CHUNK_FRAMES) {
         int left = data[input_offset * 2];
         int right = data[input_offset * 2 + 1];
         int scaled;
         unsigned abs_out;

         input_offset++;
         host.audio_resample_accum += output_rate;
         if (host.audio_resample_accum < input_rate)
            continue;
         host.audio_resample_accum -= input_rate;
         scaled = host_audio_scale_mono(left, right, &abs_out);
         if (abs_out > peak_out)
            peak_out = abs_out;
         audio_mix_buffer[out_frames] = (int16_t)scaled;
         out_frames++;
      }
      if (out_frames == 0)
         continue;
      host_audio_update_gate(peak_out);
      if (host_audio_write_frames(audio_mix_buffer, (unsigned)out_frames,
          host.audio_batches) != 0)
         break;
      offset += out_frames;
   }

   host.audio_batches++;
   host.audio_frames += (unsigned)offset;

   return input_offset ? input_offset : frames;
}

void unifrog_libretro_video_refresh_cb(const void *data, unsigned width,
   unsigned height, size_t pitch)
{
   host_force_expected_gp();
   if (!data || width == 0 || height == 0)
      return;
   if (host.fast_forward) {
      if (host.fast_forward_force_present) {
         host.fast_forward_force_present = 0;
      } else if ((host.video_frames %
          sanitize_fast_forward_multiplier(host.fast_forward_multiplier)) != 0) {
         host.video_frames++;
         if (host.presenter_open)
            host.presenter.last_vsync_count = 0;
         return;
      }
   }

   if (host.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
      if (pitch < (size_t)width * 4u)
         return;
   } else if (host.pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
      if (pitch < (size_t)width * 2u)
         return;
   } else {
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
   host.video_pitch = (unsigned)pitch;
   host.video_frames++;
   if (data == host.software_framebuffer &&
       pitch == host.software_framebuffer_pitch)
      host.software_framebuffer_presents++;
   if (host.presenter_open) {
      if (host.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
         (void)unifrog_presenter_present_xrgb8888(&host.presenter,
            data, width, height, (unsigned)pitch);
      else
         (void)unifrog_presenter_present_rgb565(&host.presenter,
            data, width, height, (unsigned)pitch);
   }
}

static unsigned host_pixel_format_bytes(enum retro_pixel_format format)
{
   switch (format) {
   case RETRO_PIXEL_FORMAT_RGB565:
      return 2u;
   case RETRO_PIXEL_FORMAT_XRGB8888:
      return 4u;
   default:
      return 0;
   }
}

static int host_software_framebuffer_ensure(unsigned width, unsigned height,
   enum retro_pixel_format format)
{
   unsigned bpp = host_pixel_format_bytes(format);
   unsigned pitch;
   size_t bytes;
   void *buffer;

   if (!width || !height || !bpp)
      return -1;
   if (width > 640u || height > 480u)
      return -1;
   pitch = width * bpp;
   if (pitch & 63u)
      pitch = (pitch + 63u) & ~63u;
   bytes = (size_t)pitch * height;
   if (host.software_framebuffer &&
       host.software_framebuffer_bytes >= bytes &&
       host.software_framebuffer_width == width &&
       host.software_framebuffer_height == height &&
       host.software_framebuffer_pitch == pitch &&
       host.software_framebuffer_format == format)
      return 0;

   buffer = unifrog_surface_memalign(64u, bytes);
   if (!buffer)
      return -1;
   unifrog_surface_free(host.software_framebuffer);
   host.software_framebuffer = buffer;
   host.software_framebuffer_bytes = bytes;
   host.software_framebuffer_width = width;
   host.software_framebuffer_height = height;
   host.software_framebuffer_pitch = pitch;
   host.software_framebuffer_format = format;
   printf("unifrog libretro software_fb alloc width=%u height=%u pitch=%u fmt=%u bytes=%u mmz=%d\n",
      width, height, pitch, format, (unsigned)bytes,
      unifrog_surface_is_mmz(buffer));
   return 0;
}

static bool host_get_current_software_framebuffer(struct retro_framebuffer *fb)
{
   unsigned width;
   unsigned height;
   enum retro_pixel_format format;

   if (!fb)
      return false;
   host.software_framebuffer_requests++;
   width = fb->width ? fb->width : host.video_max_width;
   height = fb->height ? fb->height : host.video_max_height;
   if (!width)
      width = 320u;
   if (!height)
      height = 240u;
   format = host.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888 ?
      RETRO_PIXEL_FORMAT_XRGB8888 : RETRO_PIXEL_FORMAT_RGB565;
   if (host_software_framebuffer_ensure(width, height, format) != 0)
      return false;
   fb->data = host.software_framebuffer;
   fb->width = width;
   fb->height = height;
   fb->pitch = host.software_framebuffer_pitch;
   fb->format = format;
   fb->memory_flags = RETRO_MEMORY_TYPE_CACHED;
   host.software_framebuffer_hits++;
   return true;
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
      if (host.fast_forward) {
         *(int *)data = RETRO_AV_ENABLE_VIDEO |
            RETRO_AV_ENABLE_HARD_DISABLE_AUDIO;
      } else {
         *(int *)data = RETRO_AV_ENABLE_VIDEO |
            (host.audio_enabled ? RETRO_AV_ENABLE_AUDIO :
             RETRO_AV_ENABLE_HARD_DISABLE_AUDIO);
      }
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
      *(bool *)data = host.variables_dirty ? true : false;
      host.variables_dirty = 0;
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
      return host_get_variable((struct retro_variable *)data);
   case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER:
      return host_get_current_software_framebuffer(
         (struct retro_framebuffer *)data);
   case RETRO_ENVIRONMENT_SET_VARIABLE:
      return false;
   case RETRO_ENVIRONMENT_SET_VARIABLES:
      core_options_register_variables((const struct retro_variable *)data);
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
      core_options_register_definitions(
         (const struct retro_core_option_definition *)data);
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
      if (data) {
         const struct retro_core_options_intl *intl =
            (const struct retro_core_options_intl *)data;
         core_options_register_definitions(intl->us ? intl->us : intl->local);
      }
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      if (data) {
         const struct retro_core_options_v2 *options =
            (const struct retro_core_options_v2 *)data;
         core_options_register_v2(options->definitions);
      }
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
      if (data) {
         const struct retro_core_options_v2_intl *intl =
            (const struct retro_core_options_v2_intl *)data;
         const struct retro_core_options_v2 *options =
            intl->us ? intl->us : intl->local;
         if (options)
            core_options_register_v2(options->definitions);
      }
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      if (data) {
         const struct retro_core_option_display *display =
            (const struct retro_core_option_display *)data;
         int index = core_option_find(display->key);
         if (index >= 0)
            host.core_options[index].visible = display->visible ? 1 : 0;
      }
      return true;
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
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
   case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
      if (!data)
         return false;
      *(const char **)data = "/media/mmcblk0";
      return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      if (!data)
         return false;
      *(const char **)data = UNIFROG_BIOS_ROOT;
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

static void quick_js_apply_fast_forward_speed(unsigned multiplier)
{
   multiplier = sanitize_fast_forward_multiplier(multiplier);
   host.fast_forward_multiplier = multiplier;
   host.fast_forward = multiplier > 0 ? 1 : 0;
   host.fast_forward_force_present = host.fast_forward ? 1 : 0;
   host.variables_dirty = 1;
   host.frame_deadline_us = host_time_us();
   host.audio_gate_open = 0;
   host.audio_quiet_batches = 0;
   if (host.audio_open)
      (void)unifrog_audio_set_output_enabled(&host.audio, 0);
   printf("unifrog quick_js fast_forward=%d multiplier=%u\n",
      host.fast_forward, host.fast_forward_multiplier);
}

static const char *quick_js_frameskip_label(void)
{
   switch (host.options.frameskip) {
   case UNIFROG_LIBRETRO_FRAMESKIP_AUTO:
      return "Auto";
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1:
      return "1";
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2:
      return "2";
   default:
      return "Off";
   }
}

static int quick_js_cycle_frameskip(int delta)
{
   static const int frameskip_values[] = {
      UNIFROG_LIBRETRO_FRAMESKIP_OFF,
      UNIFROG_LIBRETRO_FRAMESKIP_AUTO,
      UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1,
      UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2,
   };
   unsigned index = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(frameskip_values); i++) {
      if (frameskip_values[i] == host.options.frameskip) {
         index = i;
         break;
      }
   }
   if (delta < 0) {
      if (index == 0)
         index = ARRAY_SIZE(frameskip_values) - 1u;
      else
         index--;
   } else {
      index++;
      if (index >= ARRAY_SIZE(frameskip_values))
         index = 0;
   }
   host.options.frameskip = frameskip_values[index];
   host.variables_dirty = 1;
   printf("unifrog quick_js frameskip=%d label=%s\n",
      host.options.frameskip, quick_js_frameskip_label());
   return host.options.frameskip;
}

static int quick_js_cycle_fast_forward_multiplier(int delta)
{
   static const unsigned multipliers[] = { 0, 2, 4, 8, 16 };
   unsigned current = sanitize_fast_forward_multiplier(
      host.fast_forward_multiplier);
   unsigned index = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(multipliers); i++) {
      if (multipliers[i] == current) {
         index = i;
         break;
      }
   }
   if (delta < 0) {
      if (index == 0)
         index = ARRAY_SIZE(multipliers) - 1u;
      else
         index--;
   } else {
      index++;
      if (index >= ARRAY_SIZE(multipliers))
         index = 0;
   }
   quick_js_apply_fast_forward_speed(multipliers[index]);
   return (int)host.fast_forward_multiplier;
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

static unsigned quick_muos_current_backlight(void)
{
   unsigned current = 0;

   if (unifrog_backlight_get(&current) != 0)
      current = host.options.backlight_level >= 0 ?
         (unsigned)host.options.backlight_level : 50u;
   if (current > 100u)
      current = 100u;
   return current;
}

static unsigned quick_muos_cycle_backlight(int delta)
{
   unsigned current = quick_muos_current_backlight();
   unsigned next = delta < 0 ?
      quick_backlight_levels[ARRAY_SIZE(quick_backlight_levels) - 1u] :
      quick_backlight_levels[0];

   if (delta < 0) {
      for (unsigned i = ARRAY_SIZE(quick_backlight_levels); i > 0; i--) {
         if (quick_backlight_levels[i - 1u] < current) {
            next = quick_backlight_levels[i - 1u];
            break;
         }
      }
   } else {
      for (unsigned i = 0; i < ARRAY_SIZE(quick_backlight_levels); i++) {
         if (quick_backlight_levels[i] > current) {
            next = quick_backlight_levels[i];
            break;
         }
      }
   }
   (void)unifrog_backlight_set(next);
   host.options.backlight_level = (int)next;
   printf("unifrog quick_muos backlight=%u\n", next);
   return next;
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
   (void)libretro_log_flush_force_if_safe();
   return host.scpu_apply_ret == 0 ? (int)next : -1;
}

static const char *quick_muos_ge_label(enum unifrog_ge_clock clock)
{
   switch (clock) {
   case UNIFROG_GE_CLOCK_148MHZ:
      return "148";
   case UNIFROG_GE_CLOCK_225MHZ:
      return "225";
   case UNIFROG_GE_CLOCK_238MHZ:
      return "238";
   default:
      return "198";
   }
}

static int quick_muos_cycle_ge_clock(int delta)
{
   static const enum unifrog_ge_clock clocks[] = {
      UNIFROG_GE_CLOCK_148MHZ,
      UNIFROG_GE_CLOCK_198MHZ,
      UNIFROG_GE_CLOCK_225MHZ,
      UNIFROG_GE_CLOCK_238MHZ,
   };
   unsigned index = 1;

   for (unsigned i = 0; i < ARRAY_SIZE(clocks); i++) {
      if (clocks[i] == host.ge_clock) {
         index = i;
         break;
      }
   }
   if (delta < 0) {
      if (index == 0)
         index = ARRAY_SIZE(clocks) - 1u;
      else
         index--;
   } else {
      index++;
      if (index >= ARRAY_SIZE(clocks))
         index = 0;
   }
   host.ge_clock = clocks[index];
   host.options.ge_clock = (int)host.ge_clock;
   if (host.presenter_open)
      (void)unifrog_ge_set_clock(&host.presenter.ge, host.ge_clock);
   printf("unifrog quick_muos ge_clock=%s enum=%d\n",
      quick_muos_ge_label(host.ge_clock), host.options.ge_clock);
   return host.options.ge_clock;
}

static int quick_muos_cycle_input_profile(int delta)
{
   static const int profiles[] = {
      UNIFROG_LIBRETRO_INPUT_DEFAULT,
      UNIFROG_LIBRETRO_INPUT_RETROARCH,
      UNIFROG_LIBRETRO_INPUT_GENESIS,
      UNIFROG_LIBRETRO_INPUT_SWAP_AB,
      UNIFROG_LIBRETRO_INPUT_SWAP_XY,
   };
   unsigned index = 0;

   host.input_profile = sanitize_input_profile(host.input_profile);
   for (unsigned i = 0; i < ARRAY_SIZE(profiles); i++) {
      if (profiles[i] == host.input_profile) {
         index = i;
         break;
      }
   }
   if (delta < 0) {
      if (index == 0)
         index = ARRAY_SIZE(profiles) - 1u;
      else
         index--;
   } else {
      index++;
      if (index >= ARRAY_SIZE(profiles))
         index = 0;
   }
   host.input_profile = profiles[index];
   host.options.input_profile = host.input_profile;
   host.input_profile_dirty = 1;
   host.core_options_dirty = 1;
   core_options_save_opt_file();
   printf("unifrog quick_muos keymap=%s\n",
      input_profile_opt_value(host.input_profile));
   return host.input_profile;
}

#if UNIFROG_FRONTEND_JS2300
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
   if (strcmp(id, "quick:fast-forward-status") == 0)
      return host.fast_forward ? 1 : 0;
   if (strcmp(id, "quick:fast-forward-speed") == 0)
      return (int)sanitize_fast_forward_multiplier(
         host.fast_forward_multiplier);
   if (strcmp(id, "quick:fast-forward-speed-next") == 0)
      return quick_js_cycle_fast_forward_multiplier(1);
   if (strcmp(id, "quick:fast-forward-speed-prev") == 0)
      return quick_js_cycle_fast_forward_multiplier(-1);
   if (strcmp(id, "quick:frameskip") == 0)
      return host.options.frameskip;
   if (strcmp(id, "quick:frameskip-next") == 0)
      return quick_js_cycle_frameskip(1);
   if (strcmp(id, "quick:frameskip-prev") == 0)
      return quick_js_cycle_frameskip(-1);
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
   unifrog_diag_memory_snapshot("quick_js.start");
   (void)libretro_log_flush_force_if_safe();
   ret = js2300_runtime_create(&config, &js_host, &runtime);
   unifrog_diag_memory_snapshot("quick_js.created");
   if (ret == 0)
      ret = js2300_runtime_run(runtime);
   unifrog_diag_memory_snapshot("quick_js.after_run");
   js2300_runtime_destroy(runtime);
   unifrog_diag_memory_snapshot("quick_js.destroyed");
   if (ret != 0)
      host.quick_js_action = QUICK_JS_ACTION_RETURN_MENU;
   printf("unifrog quick_js done ret=%d action=%d ms=%u\n",
      ret, host.quick_js_action,
      host_elapsed_ms(start_us, host_time_us()));
   (void)libretro_log_flush_force_if_safe();

   host.quick_js_frame_open = 0;
   host.presenter.cleared_buffer_mask = 0;
   if (host.fast_forward)
      host.fast_forward_force_present = 1;
   return host.quick_js_action == QUICK_JS_ACTION_RETURN_MENU ? 1 : 0;
}
#else
static void quick_muos_draw_row(struct unifrog_surface *surface, int y,
   const char *label, const char *detail, int focused)
{
   uint16_t bg = focused ? UNIFROG_RGB565(45, 95, 110) :
      UNIFROG_RGB565(22, 29, 39);
   uint16_t fg = focused ? UNIFROG_RGB565(255, 255, 255) :
      UNIFROG_RGB565(230, 238, 240);
   uint16_t muted = focused ? UNIFROG_RGB565(205, 240, 235) :
      UNIFROG_RGB565(139, 154, 160);
   int w = (int)surface->width - 20;

   unifrog_gfx_fill_rect(surface, 10, y - 3, w, 22, bg);
   if (focused)
      unifrog_gfx_fill_rect(surface, 10, y - 3, 4, 22,
         UNIFROG_RGB565(120, 214, 189));
   unifrog_gfx_draw_text(surface, 14, y + 2, label ? label : "", fg, 1);
   if (detail && detail[0])
      unifrog_gfx_draw_text(surface, 216, y + 2, detail, muted, 1);
}

static void quick_muos_draw_tab(struct unifrog_surface *surface, int x, int y,
   int w, const char *label, int focused)
{
   uint16_t bg = focused ? UNIFROG_RGB565(52, 104, 132) :
      UNIFROG_RGB565(22, 29, 39);
   uint16_t fg = focused ? UNIFROG_RGB565(255, 255, 255) :
      UNIFROG_RGB565(205, 240, 235);

   unifrog_gfx_fill_rect(surface, x, y, w, 20, bg);
   if (focused)
      unifrog_gfx_fill_rect(surface, x, y, w, 2,
         UNIFROG_RGB565(120, 214, 189));
   unifrog_gfx_draw_text(surface, x + 8, y + 6, label ? label : "", fg, 1);
}

static void quick_muos_detail(char *dst, size_t size, unsigned value)
{
   if (!dst || size == 0)
      return;
   snprintf(dst, size, "< %u >", value);
}

static void quick_muos_fast_forward_detail(char *dst, size_t size)
{
   unsigned multiplier;

   if (!dst || size == 0)
      return;
   multiplier = sanitize_fast_forward_multiplier(host.fast_forward_multiplier);
   if (multiplier == 0)
      snprintf(dst, size, "< Off >");
   else
      snprintf(dst, size, "< %ux >", multiplier);
}

static unsigned quick_muos_visible_core_option_count(void)
{
   unsigned count = 0;

   for (unsigned i = 0; i < host.core_option_count; i++) {
      if (host.core_options[i].visible && host.core_options[i].value_count)
         count++;
   }
   return count;
}

static int quick_muos_visible_core_option_index(unsigned visible_index)
{
   unsigned count = 0;

   for (unsigned i = 0; i < host.core_option_count; i++) {
      if (!host.core_options[i].visible || !host.core_options[i].value_count)
         continue;
      if (count == visible_index)
         return (int)i;
      count++;
   }
   return -1;
}

static void quick_muos_core_option_detail(char *dst, size_t size,
   const struct quick_core_option *option)
{
   const char *value;

   if (!dst || size == 0)
      return;
   if (!option || option->selected >= option->value_count) {
      dst[0] = '\0';
      return;
   }
   value = option->value_labels[option->selected][0] ?
      option->value_labels[option->selected] : option->values[option->selected];
   snprintf(dst, size, "< %.18s >", value);
}

static void quick_muos_cycle_core_option(unsigned visible_index, int delta)
{
   int actual = quick_muos_visible_core_option_index(visible_index);
   struct quick_core_option *option;

   if (actual < 0)
      return;
   option = &host.core_options[actual];
   if (option->value_count == 0)
      return;
   if (delta < 0) {
      if (option->selected == 0)
         option->selected = option->value_count - 1u;
      else
         option->selected--;
   } else {
      option->selected++;
      if (option->selected >= option->value_count)
         option->selected = 0;
   }
   host.variables_dirty = 1;
   host.core_options_dirty = 1;
   printf("unifrog quick_muos core_option key=%s value=%s index=%u/%u\n",
      option->key, option->values[option->selected],
      option->selected + 1u, option->value_count);
   core_options_save_opt_file();
}

static void quick_muos_present(unsigned buffer)
{
   unifrog_fb_flush_buffer(&host.presenter.fb, buffer);
   if (unifrog_fb_pan(&host.presenter.fb, buffer) == 0)
      host.presenter.active_buffer = buffer;
}

static int quick_muos_run(const struct libretro_core_api *core,
   const char *rom_path)
{
   enum quick_muos_page {
      QUICK_MUOS_PAGE_MAIN = 0,
      QUICK_MUOS_PAGE_CORE = 1,
      QUICK_MUOS_PAGE_UNIFROG = 2,
      QUICK_MUOS_PAGE_CONFIRM = 3,
   };
   enum quick_muos_main_row {
      QUICK_MUOS_MAIN_RESUME,
      QUICK_MUOS_MAIN_SAVE,
      QUICK_MUOS_MAIN_LOAD,
      QUICK_MUOS_MAIN_CORE,
      QUICK_MUOS_MAIN_UNIFROG,
      QUICK_MUOS_MAIN_RETURN,
      QUICK_MUOS_MAIN_COUNT,
   };
   enum quick_muos_core_row {
      QUICK_MUOS_CORE_BACK,
      QUICK_MUOS_CORE_FIXED_COUNT,
   };
   enum quick_muos_unifrog_row {
      QUICK_MUOS_UNIFROG_BACK,
      QUICK_MUOS_UNIFROG_FAST_FORWARD,
      QUICK_MUOS_UNIFROG_FRAMESKIP,
      QUICK_MUOS_UNIFROG_AUDIO,
      QUICK_MUOS_UNIFROG_DISPLAY,
      QUICK_MUOS_UNIFROG_KEYMAP,
      QUICK_MUOS_UNIFROG_CPU,
      QUICK_MUOS_UNIFROG_GE,
      QUICK_MUOS_UNIFROG_BACKLIGHT,
      QUICK_MUOS_UNIFROG_COUNT,
   };
   enum quick_muos_confirm_row {
      QUICK_MUOS_CONFIRM_YES,
      QUICK_MUOS_CONFIRM_CANCEL,
      QUICK_MUOS_CONFIRM_COUNT,
   };
   enum quick_muos_confirm_action {
      QUICK_MUOS_CONFIRM_NONE,
      QUICK_MUOS_CONFIRM_SAVE,
      QUICK_MUOS_CONFIRM_LOAD,
   };
   static const char *main_labels[] = {
      "Resume", "Save state", "Load state", "Core Options",
      "UniFrog Settings", "Return to UniFrog",
   };
   static const char *core_labels[] = {
      "Back",
   };
   static const char *unifrog_labels[] = {
      "Back", "Fast forward", "Frameskip", "Audio", "Display", "Keymap",
      "CPU", "GE", "Backlight",
   };
   static const char *confirm_labels[] = {
      "Confirm", "Cancel",
   };
   unsigned selected = 0;
   unsigned page = QUICK_MUOS_PAGE_MAIN;
   unsigned previous_page = QUICK_MUOS_PAGE_MAIN;
   unsigned previous_selected = QUICK_MUOS_MAIN_SAVE;
   unsigned confirm_action = QUICK_MUOS_CONFIRM_NONE;
   unsigned save_slot;
   unsigned load_slot;
   uint32_t previous = 0;
   uint32_t repeat_button = 0;
   uint32_t next_repeat_ms = 0;
   int input_ready = 0;
   int entry_combo_released = 0;
   const uint32_t entry_combo =
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT) |
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START);
   const uint32_t action_buttons =
      entry_combo |
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A) |
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B);
   static const char *labels[QUICK_MUOS_CORE_FIXED_COUNT +
      LIBRETRO_CORE_OPTION_MAX];
   static char label_storage[LIBRETRO_CORE_OPTION_MAX]
      [LIBRETRO_CORE_OPTION_LABEL_MAX];
   static char detail[QUICK_MUOS_CORE_FIXED_COUNT +
      LIBRETRO_CORE_OPTION_MAX][28];

   (void)core;
   (void)rom_path;

   if (!host.presenter_open)
      return 0;
   if (host.audio_open)
      (void)unifrog_audio_set_output_enabled(&host.audio, 0);
   host.audio_gate_open = 0;
   host.quick_js_action = QUICK_JS_ACTION_RESUME;
   host.quick_combo_armed = 0;
   host.quick_status[0] = '\0';
   host.quick_core = core;
   host.quick_rom_path = rom_path;
   if (host.quick_state_slot >= LIBRETRO_STATE_SLOT_COUNT)
      host.quick_state_slot = 0;
   save_slot = host.quick_state_slot;
   load_slot = host.quick_state_slot;

   printf("unifrog quick_muos start entry_buttons=0x%08lx\n",
      (unsigned long)unifrog_input_menu_buttons());

   for (;;) {
      uint32_t buttons;
      uint32_t pressed;
      uint32_t now_ms;
      unsigned draw_buffer;
      struct unifrog_surface surface;
      unsigned row_count;
      unsigned visible_rows;
      unsigned first_row;
      const char *footer;

      draw_buffer = host.presenter.active_buffer;
      if (host.presenter.buffer_count > 1)
         draw_buffer = (host.presenter.active_buffer + 1u) %
            host.presenter.buffer_count;
      surface = unifrog_fb_surface_for_buffer(&host.presenter.fb,
         draw_buffer);

      memset(detail, 0, sizeof(detail));
      if (page == QUICK_MUOS_PAGE_MAIN) {
         for (unsigned i = 0; i < QUICK_MUOS_MAIN_COUNT; i++)
            labels[i] = main_labels[i];
         row_count = QUICK_MUOS_MAIN_COUNT;
         quick_muos_detail(detail[QUICK_MUOS_MAIN_SAVE],
            sizeof(detail[QUICK_MUOS_MAIN_SAVE]), save_slot + 1u);
         quick_muos_detail(detail[QUICK_MUOS_MAIN_LOAD],
            sizeof(detail[QUICK_MUOS_MAIN_LOAD]), load_slot + 1u);
         snprintf(detail[QUICK_MUOS_MAIN_CORE],
            sizeof(detail[QUICK_MUOS_MAIN_CORE]), "%u options",
            quick_muos_visible_core_option_count());
      } else {
         if (page == QUICK_MUOS_PAGE_CORE) {
            unsigned visible_core_options = quick_muos_visible_core_option_count();

            for (unsigned i = 0; i < QUICK_MUOS_CORE_FIXED_COUNT; i++)
               labels[i] = core_labels[i];
            row_count = QUICK_MUOS_CORE_FIXED_COUNT + visible_core_options;
            for (unsigned i = 0; i < visible_core_options; i++) {
               int actual = quick_muos_visible_core_option_index(i);
               unsigned row = QUICK_MUOS_CORE_FIXED_COUNT + i;
               if (actual < 0)
                  continue;
               snprintf(label_storage[i], sizeof(label_storage[i]), "%.25s",
                  host.core_options[actual].label);
               labels[row] = label_storage[i];
               quick_muos_core_option_detail(detail[row], sizeof(detail[row]),
                  &host.core_options[actual]);
            }
         } else if (page == QUICK_MUOS_PAGE_UNIFROG) {
            for (unsigned i = 0; i < QUICK_MUOS_UNIFROG_COUNT; i++)
               labels[i] = unifrog_labels[i];
            row_count = QUICK_MUOS_UNIFROG_COUNT;
            quick_muos_fast_forward_detail(
               detail[QUICK_MUOS_UNIFROG_FAST_FORWARD],
               sizeof(detail[QUICK_MUOS_UNIFROG_FAST_FORWARD]));
            snprintf(detail[QUICK_MUOS_UNIFROG_FRAMESKIP],
               sizeof(detail[QUICK_MUOS_UNIFROG_FRAMESKIP]), "< %s >",
               quick_js_frameskip_label());
            snprintf(detail[QUICK_MUOS_UNIFROG_AUDIO],
               sizeof(detail[QUICK_MUOS_UNIFROG_AUDIO]), "< %s >",
               host.audio_enabled ? "on" : "off");
            snprintf(detail[QUICK_MUOS_UNIFROG_DISPLAY],
               sizeof(detail[QUICK_MUOS_UNIFROG_DISPLAY]), "< %s >",
               display_mode_label(host.display_mode));
            snprintf(detail[QUICK_MUOS_UNIFROG_KEYMAP],
               sizeof(detail[QUICK_MUOS_UNIFROG_KEYMAP]), "< %s >",
               input_profile_opt_value(host.input_profile));
            snprintf(detail[QUICK_MUOS_UNIFROG_CPU],
               sizeof(detail[QUICK_MUOS_UNIFROG_CPU]), "< %u >",
               quick_js_current_scpu_mhz());
            snprintf(detail[QUICK_MUOS_UNIFROG_GE],
               sizeof(detail[QUICK_MUOS_UNIFROG_GE]), "< %s >",
               quick_muos_ge_label(host.ge_clock));
            quick_muos_detail(detail[QUICK_MUOS_UNIFROG_BACKLIGHT],
               sizeof(detail[QUICK_MUOS_UNIFROG_BACKLIGHT]),
               quick_muos_current_backlight());
         } else {
            for (unsigned i = 0; i < QUICK_MUOS_CONFIRM_COUNT; i++)
               labels[i] = confirm_labels[i];
            row_count = QUICK_MUOS_CONFIRM_COUNT;
            snprintf(detail[QUICK_MUOS_CONFIRM_YES],
               sizeof(detail[QUICK_MUOS_CONFIRM_YES]), "< slot %u >",
               confirm_action == QUICK_MUOS_CONFIRM_LOAD ?
                  load_slot + 1u : save_slot + 1u);
         }
      }
      footer = page == QUICK_MUOS_PAGE_CONFIRM ?
         "A confirm  B cancel" : "A choose  Left/Right adjust  B back";
      visible_rows = page == QUICK_MUOS_PAGE_CONFIRM ? 2u : 8u;
      if (visible_rows > row_count)
         visible_rows = row_count;
      first_row = 0;
      if (selected >= visible_rows)
         first_row = selected - visible_rows + 1u;

      unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height,
         UNIFROG_RGB565(8, 10, 14));
      unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, 36,
         UNIFROG_RGB565(22, 29, 39));
      unifrog_gfx_fill_rect(&surface, 0, 36, surface.width, 1,
         UNIFROG_RGB565(120, 214, 189));
      unifrog_gfx_draw_text(&surface, 12, 10, "Pause Menu",
         UNIFROG_RGB565(230, 238, 240), 1);
      unifrog_gfx_draw_text(&surface, (int)surface.width - 58, 10,
         page == QUICK_MUOS_PAGE_MAIN ? "paused" :
            page == QUICK_MUOS_PAGE_CORE ? "core" :
            page == QUICK_MUOS_PAGE_UNIFROG ? "settings" : "sure?",
         UNIFROG_RGB565(139, 154, 160), 1);
      quick_muos_draw_tab(&surface, 10, 38, 70, "Pause",
         page == QUICK_MUOS_PAGE_MAIN);
      quick_muos_draw_tab(&surface, 84, 38, 70, "Core",
         page == QUICK_MUOS_PAGE_CORE);
      quick_muos_draw_tab(&surface, 158, 38, 78, "UniFrog",
         page == QUICK_MUOS_PAGE_UNIFROG);
      quick_muos_draw_tab(&surface, 240, 38, 70, "State",
         page == QUICK_MUOS_PAGE_CONFIRM);
      if (page == QUICK_MUOS_PAGE_CONFIRM)
         unifrog_gfx_draw_text(&surface, 14, 64,
            confirm_action == QUICK_MUOS_CONFIRM_LOAD ?
               "Load selected state?" : "Save selected state?",
            UNIFROG_RGB565(230, 238, 240), 1);
      for (unsigned i = 0; i < visible_rows; i++) {
         unsigned row = first_row + i;
         quick_muos_draw_row(&surface,
            (page == QUICK_MUOS_PAGE_CONFIRM ? 90 : 66) + (int)i * 18,
            labels[row], detail[row], row == selected);
      }
      if (host.quick_status[0])
         unifrog_gfx_draw_text(&surface, 12, (int)surface.height - 34,
            host.quick_status, UNIFROG_RGB565(205, 240, 235), 1);
      unifrog_gfx_fill_rect(&surface, 0, (int)surface.height - 22,
         surface.width, 22, UNIFROG_RGB565(22, 29, 39));
      unifrog_gfx_fill_rect(&surface, 0, (int)surface.height - 23,
         surface.width, 1, UNIFROG_RGB565(120, 214, 189));
      unifrog_gfx_draw_text(&surface, 12, (int)surface.height - 15,
         footer, UNIFROG_RGB565(139, 154, 160), 1);
      quick_muos_present(draw_buffer);

      unifrog_input_save_previous();
      unifrog_input_poll_with_wireless_divisor(1);
      host.buttons = unifrog_input_buttons();
      buttons = unifrog_input_menu_buttons();
      now_ms = unifrog_perf_time_ms();

      if (!input_ready) {
         previous = buttons;
         if ((buttons & action_buttons) == 0) {
            input_ready = 1;
            entry_combo_released = 1;
            repeat_button = 0;
            next_repeat_ms = 0;
            printf("unifrog quick_muos input_ready ms=%lu\n",
               (unsigned long)now_ms);
         }
         usleep(16000);
         continue;
      }

      pressed = buttons & ~previous;

      if ((buttons & entry_combo) != entry_combo)
         entry_combo_released = 1;

      if (!(buttons & (UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP) |
                       UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN)))) {
         repeat_button = 0;
         next_repeat_ms = 0;
      }
      if ((pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP)) ||
          (repeat_button == UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP) &&
           now_ms >= next_repeat_ms)) {
         selected = selected == 0 ? row_count - 1u : selected - 1u;
         repeat_button = UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP);
         next_repeat_ms = now_ms + ((pressed & repeat_button) ? 320u : 90u);
      }
      if ((pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN)) ||
          (repeat_button == UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN) &&
           now_ms >= next_repeat_ms)) {
         selected++;
         if (selected >= row_count)
            selected = 0;
         repeat_button = UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN);
         next_repeat_ms = now_ms + ((pressed & repeat_button) ? 320u : 90u);
      }
      if (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT)) {
         if (page == QUICK_MUOS_PAGE_CONFIRM &&
             selected == QUICK_MUOS_CONFIRM_YES) {
            if (confirm_action == QUICK_MUOS_CONFIRM_LOAD)
               load_slot = load_slot == 0 ? LIBRETRO_STATE_SLOT_COUNT - 1u :
                  load_slot - 1u;
            else
               save_slot = save_slot == 0 ? LIBRETRO_STATE_SLOT_COUNT - 1u :
                  save_slot - 1u;
         } else if (page == QUICK_MUOS_PAGE_MAIN) {
            if (selected == QUICK_MUOS_MAIN_SAVE)
               save_slot = save_slot == 0 ? LIBRETRO_STATE_SLOT_COUNT - 1u :
                  save_slot - 1u;
            else if (selected == QUICK_MUOS_MAIN_LOAD)
               load_slot = load_slot == 0 ? LIBRETRO_STATE_SLOT_COUNT - 1u :
                  load_slot - 1u;
         } else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_FAST_FORWARD)
            (void)quick_js_cycle_fast_forward_multiplier(-1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_FRAMESKIP)
            (void)quick_js_cycle_frameskip(-1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_AUDIO)
            (void)quick_js_toggle_audio();
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_DISPLAY)
            (void)quick_js_cycle_display();
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_KEYMAP)
            (void)quick_muos_cycle_input_profile(-1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_CPU)
            (void)quick_js_cycle_scpu(-1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_GE)
            (void)quick_muos_cycle_ge_clock(-1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_BACKLIGHT)
            (void)quick_muos_cycle_backlight(-1);
         else if (page == QUICK_MUOS_PAGE_CORE &&
               selected >= QUICK_MUOS_CORE_FIXED_COUNT)
            quick_muos_cycle_core_option(
               selected - QUICK_MUOS_CORE_FIXED_COUNT, -1);
      }
      if (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT)) {
         if (page == QUICK_MUOS_PAGE_CONFIRM &&
             selected == QUICK_MUOS_CONFIRM_YES) {
            unsigned *slot = confirm_action == QUICK_MUOS_CONFIRM_LOAD ?
               &load_slot : &save_slot;
            (*slot)++;
            if (*slot >= LIBRETRO_STATE_SLOT_COUNT)
               *slot = 0;
         } else if (page == QUICK_MUOS_PAGE_MAIN) {
            if (selected == QUICK_MUOS_MAIN_SAVE) {
               save_slot++;
               if (save_slot >= LIBRETRO_STATE_SLOT_COUNT)
                  save_slot = 0;
            } else if (selected == QUICK_MUOS_MAIN_LOAD) {
               load_slot++;
               if (load_slot >= LIBRETRO_STATE_SLOT_COUNT)
                  load_slot = 0;
            }
         } else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_FAST_FORWARD)
            (void)quick_js_cycle_fast_forward_multiplier(1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_FRAMESKIP)
            (void)quick_js_cycle_frameskip(1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_AUDIO)
            (void)quick_js_toggle_audio();
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_DISPLAY)
            (void)quick_js_cycle_display();
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_KEYMAP)
            (void)quick_muos_cycle_input_profile(1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_CPU)
            (void)quick_js_cycle_scpu(1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_GE)
            (void)quick_muos_cycle_ge_clock(1);
         else if (page == QUICK_MUOS_PAGE_UNIFROG &&
               selected == QUICK_MUOS_UNIFROG_BACKLIGHT)
            (void)quick_muos_cycle_backlight(1);
         else if (page == QUICK_MUOS_PAGE_CORE &&
               selected >= QUICK_MUOS_CORE_FIXED_COUNT)
            quick_muos_cycle_core_option(
               selected - QUICK_MUOS_CORE_FIXED_COUNT, 1);
      }
      if (entry_combo_released &&
          (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT)) &&
          (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START)))
         break;
      if (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B)) {
         if (page != QUICK_MUOS_PAGE_MAIN) {
            page = previous_page;
            selected = previous_selected;
            confirm_action = QUICK_MUOS_CONFIRM_NONE;
            previous = buttons;
            usleep(16000);
            continue;
         }
         break;
      }
      if (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A)) {
         if (page == QUICK_MUOS_PAGE_MAIN) {
            if (selected == QUICK_MUOS_MAIN_RESUME) {
               break;
            } else if (selected == QUICK_MUOS_MAIN_SAVE) {
               previous_page = page;
               previous_selected = selected;
               confirm_action = QUICK_MUOS_CONFIRM_SAVE;
               page = QUICK_MUOS_PAGE_CONFIRM;
               selected = QUICK_MUOS_CONFIRM_YES;
               snprintf(host.quick_status, sizeof(host.quick_status),
                  "Save to slot %u", save_slot + 1u);
            } else if (selected == QUICK_MUOS_MAIN_LOAD) {
               previous_page = page;
               previous_selected = selected;
               confirm_action = QUICK_MUOS_CONFIRM_LOAD;
               page = QUICK_MUOS_PAGE_CONFIRM;
               selected = QUICK_MUOS_CONFIRM_YES;
               snprintf(host.quick_status, sizeof(host.quick_status),
                  "Load from slot %u", load_slot + 1u);
            } else if (selected == QUICK_MUOS_MAIN_CORE) {
               previous_page = page;
               previous_selected = selected;
               page = QUICK_MUOS_PAGE_CORE;
               selected = QUICK_MUOS_CORE_FIXED_COUNT;
               if (selected >= QUICK_MUOS_CORE_FIXED_COUNT +
                   quick_muos_visible_core_option_count())
                  selected = QUICK_MUOS_CORE_BACK;
            } else if (selected == QUICK_MUOS_MAIN_UNIFROG) {
               previous_page = page;
               previous_selected = selected;
               page = QUICK_MUOS_PAGE_UNIFROG;
               selected = QUICK_MUOS_UNIFROG_FAST_FORWARD;
            } else if (selected == QUICK_MUOS_MAIN_RETURN) {
               host.quick_js_action = QUICK_JS_ACTION_RETURN_MENU;
               break;
            }
         } else if (page == QUICK_MUOS_PAGE_CORE) {
            if (selected == QUICK_MUOS_CORE_BACK) {
               page = QUICK_MUOS_PAGE_MAIN;
               selected = QUICK_MUOS_MAIN_CORE;
            } else if (selected >= QUICK_MUOS_CORE_FIXED_COUNT) {
               quick_muos_cycle_core_option(
                  selected - QUICK_MUOS_CORE_FIXED_COUNT, 1);
            }
         } else if (page == QUICK_MUOS_PAGE_UNIFROG) {
            if (selected == QUICK_MUOS_UNIFROG_BACK) {
               page = QUICK_MUOS_PAGE_MAIN;
               selected = QUICK_MUOS_MAIN_UNIFROG;
            } else if (selected == QUICK_MUOS_UNIFROG_FAST_FORWARD) {
               (void)quick_js_cycle_fast_forward_multiplier(1);
            } else if (selected == QUICK_MUOS_UNIFROG_FRAMESKIP) {
               (void)quick_js_cycle_frameskip(1);
            } else if (selected == QUICK_MUOS_UNIFROG_AUDIO) {
               (void)quick_js_toggle_audio();
            } else if (selected == QUICK_MUOS_UNIFROG_DISPLAY) {
               (void)quick_js_cycle_display();
            } else if (selected == QUICK_MUOS_UNIFROG_KEYMAP) {
               (void)quick_muos_cycle_input_profile(1);
            } else if (selected == QUICK_MUOS_UNIFROG_CPU) {
               (void)quick_js_cycle_scpu(1);
            } else if (selected == QUICK_MUOS_UNIFROG_GE) {
               (void)quick_muos_cycle_ge_clock(1);
            } else if (selected == QUICK_MUOS_UNIFROG_BACKLIGHT) {
               (void)quick_muos_cycle_backlight(1);
            }
         } else {
            if (selected == QUICK_MUOS_CONFIRM_CANCEL) {
               page = previous_page;
               selected = previous_selected;
               confirm_action = QUICK_MUOS_CONFIRM_NONE;
            } else if (confirm_action == QUICK_MUOS_CONFIRM_SAVE) {
               int ret;

               host.quick_state_slot = save_slot;
               printf("unifrog quick_muos confirm save slot=%u\n",
                  save_slot);
               ret = quick_save_state_file();
               if (ret >= 0)
                  break;
               page = previous_page;
               selected = previous_selected;
            } else if (confirm_action == QUICK_MUOS_CONFIRM_LOAD) {
               int ret;

               host.quick_state_slot = load_slot;
               printf("unifrog quick_muos confirm load slot=%u\n",
                  load_slot);
               ret = quick_load_state_file();
               if (ret >= 0)
                  break;
               page = previous_page;
               selected = previous_selected;
            }
         }
      }
      previous = buttons;
      usleep(16000);
   }

   host.presenter.cleared_buffer_mask = 0;
   if (host.fast_forward)
      host.fast_forward_force_present = 1;
   printf("unifrog quick_muos done action=%d\n", host.quick_js_action);
   return host.quick_js_action == QUICK_JS_ACTION_RETURN_MENU ? 1 : 0;
}

static int quick_js_run(const struct libretro_core_api *core,
   const char *rom_path)
{
   return quick_muos_run(core, rom_path);
}
#endif

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

   if (!host.frame_period_us || host.fast_forward)
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
   if (host.fast_forward)
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

   printf("unifrog perf core=%s final=%d frames=%u fps=%u actual_fps_x100=%u wall_ms=%u frame_wall_avg_us=%u options_audio=%d audio_gain=%u frameskip=%d display=%s fast_forward=%d fast_forward_multiplier=%u scpu_target=%u scpu_now=%u ge_clock=%d backlight=%d pace_period_us=%u pace_wait=%u pace_wait_avg_us=%u pace_wait_max_us=%u pace_late=%u pace_reset=%u save_autosaves=%u\n",
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
      sanitize_fast_forward_multiplier(host.fast_forward_multiplier),
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

   printf("unifrog perf_cpu core=%s slow=%u scpu=%u count_hz=%u count_cal=%u budget=%u run_avg=%u run_max=%u active_avg=%u active_max=%u video=%u present_frames=%u present_avg=%u present_max=%u ge=%u sync=%u vsync=%u pan=%u blit=%u stretch=%u swfb_req=%u swfb_hit=%u swfb_present=%u dst=%d,%d %dx%d\n",
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
      present.blits,
      present.stretches,
      host.software_framebuffer_requests,
      host.software_framebuffer_hits,
      host.software_framebuffer_presents,
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
   host.software_framebuffer_requests = 0;
   host.software_framebuffer_hits = 0;
   host.software_framebuffer_presents = 0;
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

#include "unifrog_libretro_content_helpers.inc"
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
   uint8_t *tail = NULL;
   uint8_t *cd = NULL;
   size_t tail_size;
   size_t tail_eocd;
   uint32_t cd_offset;
   uint32_t cd_size;
   size_t cursor = 0;
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
   if (cd_size == 0 || cd_size > 4u * 1024u * 1024u)
      goto out;
   cd = malloc(cd_size);
   if (!cd)
      goto out;
   if (fseek(file, (long)cd_offset, SEEK_SET) != 0 ||
       fread(cd, 1, cd_size, file) != cd_size)
      goto out;

   memset(selected, 0, sizeof(*selected));
   for (uint16_t i = 0; i < entries && cursor + 46u <= cd_size; i++) {
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
      size_t record_len;

      if (read_le32(cd + cursor) != 0x02014b50u)
         goto out;
      flags = read_le16(cd + cursor + 8);
      method = read_le16(cd + cursor + 10);
      compressed_size = read_le32(cd + cursor + 20);
      uncompressed_size = read_le32(cd + cursor + 24);
      name_len = read_le16(cd + cursor + 28);
      extra_len = read_le16(cd + cursor + 30);
      comment_len = read_le16(cd + cursor + 32);
      local_offset = read_le32(cd + cursor + 42);
      if ((size_t)local_offset >= zip_size)
         goto out;
      record_len = 46u + (size_t)name_len + extra_len + comment_len;
      if (record_len > cd_size - cursor)
         goto out;
      copy_len = name_len;
      if (copy_len >= sizeof(name))
         copy_len = sizeof(name) - 1u;
      memcpy(name, cd + cursor + 46u, copy_len);
      name[copy_len] = '\0';
      cursor += record_len;

      if (!zip_entry_name_is_dir(name) &&
          (method == 0 || method == 8) &&
          !(flags & 1u) &&
          uncompressed_size > 0 &&
          uncompressed_size <= LIBRETRO_ZIP_MAX_UNCOMPRESSED &&
          libretro_valid_extension_matches(name, valid_extensions)) {
         unifrog_text_copy(selected->name, sizeof(selected->name), name);
         selected->flags = flags;
         selected->method = method;
         selected->crc32 = read_le32(cd + cursor - record_len + 16);
         selected->compressed_size = compressed_size;
         selected->uncompressed_size = uncompressed_size;
         selected->local_offset = local_offset;
         out_ret = 0;
         break;
      }
   }

out:
   free(cd);
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
   unsigned last_percent = 0xffu;
   size_t old_auto_flush = 0;
   int storage_quiet = 0;

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
   loading_draw("LOADING ZIP", "INFLATE 0%", 36);
   unifrog_log_sync("zip_inflate begin path=%s entry=%s compressed=%u uncompressed=%u",
      zip_path, entry->name, entry->compressed_size,
      entry->uncompressed_size);
   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_log_defer_begin();
   unifrog_platform_set_storage_log_suspended(1);
   storage_quiet = 1;
   unifrog_platform_sd_debug_dump("zip_inflate_start");
   while (remaining > 0 && zret != Z_STREAM_END) {
      size_t chunk = remaining;
      unsigned percent;

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
      percent = entry->compressed_size ?
         (unsigned)(((entry->compressed_size - remaining) * 100u) /
         entry->compressed_size) : 100u;
      if (percent / 10u != last_percent / 10u || remaining == 0) {
         char detail[32];
         unsigned mapped_percent = 36u + (unsigned)(((uint64_t)percent * 30u) /
            100u);

         last_percent = percent;
         snprintf(detail, sizeof(detail), "INFLATE %u%%", percent);
         loading_draw("LOADING ZIP", detail, mapped_percent);
         libretro_watchdog_load_progress("zip inflate", percent, 100);
         unifrog_log_sync("zip_inflate progress path=%s entry=%s percent=%u in=%u out=%u",
            zip_path, entry->name, percent,
            (unsigned)(entry->compressed_size - remaining),
            (unsigned)stream.total_out);
      }
      usleep(1000);
   }

   if (zret != Z_STREAM_END || stream.total_out != out_size)
      goto out_inflate;
   out_ret = 0;
   unifrog_platform_sd_debug_dump("zip_inflate_done");
   unifrog_log_sync("zip_inflate done path=%s entry=%s out=%u",
      zip_path, entry->name, (unsigned)stream.total_out);

out_inflate:
   if (out_ret != 0)
      unifrog_platform_sd_debug_dump("zip_inflate_fail");
   if (storage_quiet) {
      unifrog_platform_set_storage_log_suspended(0);
      unifrog_log_defer_end();
      unifrog_log_set_auto_flush_bytes(old_auto_flush);
      if (out_ret == 0)
         (void)libretro_log_flush_force_if_safe();
   }
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

static int zip_load_rom_data_stream_path(const char *zip_path,
   const struct retro_system_info *info, uint8_t **out_data,
   size_t *out_size, char *out_name, size_t out_name_size)
{
   unsigned attempts = libretro_storage_attempts();

   if (!zip_path || !info || !out_data || !out_size)
      return -1;
   for (unsigned attempt = 0; attempt < attempts; attempt++) {
      FILE *file = fopen(zip_path, "rb");
      int ret;

      if (!file) {
         printf("unifrog libretro zip open failed path=%s attempt=%u errno=%d\n",
            zip_path, attempt + 1u, errno);
         ret = -1;
      } else {
         ret = zip_load_rom_data_stream(file, zip_path, info, out_data,
            out_size, out_name, out_name_size);
         fclose(file);
      }
      if (ret == 0) {
         if (attempt > 0)
            printf("unifrog libretro zip recovered path=%s attempts=%u\n",
               zip_path, attempt + 1u);
         return 0;
      }
      if (attempt + 1u < attempts)
         (void)libretro_recover_storage("zip_memory");
   }

   return -1;
}

static int read_path_memory_with_fallback(const char *path,
   uint8_t **out_data, size_t *out_size, const char *label)
{
   FILE *file;
   int ret;

   errno = 0;
   if (read_path_aligned_direct(path, out_data, out_size, label) == 0)
      return 0;
   if (errno == ETIMEDOUT)
      return -1;

   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro rom fallback open failed path=%s errno=%d label=%s\n",
         path ? path : "", errno, label ? label : "");
      return -1;
   }
   ret = read_file_aligned(file, path, out_data, out_size, label);
   fclose(file);
   return ret;
}

static int read_file_aligned(FILE *file, const char *path, uint8_t **out_data,
   size_t *out_size, const char *label)
{
   return read_file_aligned_timeout(file, path, out_data, out_size, label, 0);
}

static int read_file_aligned_timeout(FILE *file, const char *path,
   uint8_t **out_data, size_t *out_size, const char *label,
   unsigned timeout_ms)
{
   uint8_t *data = NULL;
   size_t size;
   uint64_t read_start_us;

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
   read_start_us = host_time_us();
   for (size_t done = 0; done < size;) {
      size_t chunk = size - done;
      size_t max_chunk = libretro_content_read_chunk();
      uint64_t now_us;

      if (chunk > max_chunk)
         chunk = max_chunk;
      if (fread(data + done, 1, chunk, file) != chunk) {
         printf("unifrog libretro rom read failed path=%s size=%u done=%u label=%s\n",
            path ? path : "", (unsigned)size, (unsigned)done,
            label ? label : "");
         unifrog_log_sync("content_stdio_read fail path=%s size=%u done=%u label=%s",
            path ? path : "", (unsigned)size, (unsigned)done,
            label ? label : "");
         rom_free_aligned(data);
         return -1;
      }
      done += chunk;
      now_us = host_time_us();
      if (timeout_ms &&
          host_elapsed_ms(read_start_us, now_us) > timeout_ms) {
         printf("unifrog libretro rom read timeout path=%s size=%u done=%u elapsed=%u timeout=%u label=%s\n",
            path ? path : "", (unsigned)size, (unsigned)done,
            host_elapsed_ms(read_start_us, now_us), timeout_ms,
            label ? label : "");
         unifrog_log_sync("content_stdio_read timeout path=%s size=%u done=%u elapsed=%u timeout=%u label=%s",
            path ? path : "", (unsigned)size, (unsigned)done,
            host_elapsed_ms(read_start_us, now_us), timeout_ms,
            label ? label : "");
         unifrog_platform_sd_debug_dump("content_stdio_read_timeout");
         rom_free_aligned(data);
         errno = ETIMEDOUT;
         return -1;
      }
      loading_draw("LOADING ROM", label ? label : "READING",
         12u + (unsigned)((done * 58u) / (size ? size : 1u)));
   }
   *out_data = data;
   *out_size = size;
   unifrog_log_sync("content_stdio_read done path=%s size=%u elapsed=%u label=%s",
      path ? path : "", (unsigned)size,
      host_elapsed_ms(read_start_us, host_time_us()), label ? label : "");
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
      (unsigned)libretro_content_read_chunk(), label ? label : "", path);
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
   struct zip_rom_entry entry;
   char entry_name[256];
   uint8_t *rom = NULL;
   size_t rom_size = 0;
   size_t zip_size = 0;
   FILE *out = NULL;
   int ret = -1;

   if (file_size(file, &zip_size) == 0 &&
       zip_select_rom_entry_stream(file, zip_path, zip_size,
       info->valid_extensions, &entry) == 0 &&
       content_cache_path(zip_path, entry.name, info->valid_extensions,
       cache_path, cache_path_size) == 0) {
      struct stat st;

      if (stat(cache_path, &st) == 0 &&
          st.st_size == (off_t)entry.uncompressed_size) {
         printf("unifrog libretro zip cache hit entry=%s cache=%s size=%u\n",
            entry.name, cache_path, entry.uncompressed_size);
         unifrog_log_sync("zip_cache hit path=%s entry=%s cache=%s size=%u",
            zip_path, entry.name, cache_path, entry.uncompressed_size);
         loading_draw("LOADING ZIP", "CACHE HIT", 58);
         return 0;
      }
      (void)fseek(file, 0, SEEK_SET);
   }

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
   int run_loop_log_defer = 0;
   int content_prepare_failed = 0;
   int read_profile_retry_safe = 0;
   int read_profile_active = 0;
   int save_read_profile_active = 0;
   int retro_read_profile_active = 0;
   int content_retry_log_quiet = 0;
   size_t content_retry_old_auto_flush = 0;
   size_t save_existing_bytes = 0;

   if (!libretro_core_available(core) || !path || !path[0])
      return -1;

   unifrog_log_sync("libretro run enter core=%s path=%s external=%d gp=0x%08lx",
      core->id ? core->id : "", path, core->external,
      (unsigned long)core_call_gp(core));
   libretro_activity_set(core, path, UNIFROG_ACTIVITY_PHASE_LIBRETRO_BEGIN,
      core->external ? 1u : 0u);
   total_start_us = host_time_us();
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(LIBRETRO_LOG_AUTO_FLUSH_BYTES);

   memset(&host, 0, sizeof(host));
   host.pixel_format = RETRO_PIXEL_FORMAT_RGB565;
   host.core_id = core->id;
   host.core_gp = core_call_gp(core);
   core_options_reset();
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
   printf("unifrog libretro options audio=%d gain=%u scpu=%u ge_clock=%d backlight=%d frameskip=%d display=%s framebuffer=%s keymap=%s\n",
      host.audio_enabled, host.audio_gain, host.scpu_target_mhz,
      host.options.ge_clock, host.options.backlight_level,
      host.options.frameskip, display_mode_label(host.display_mode),
      framebuffer_format_label(host.framebuffer_format),
      input_profile_opt_value(host.input_profile));
   if (host.options.max_frames)
      printf("unifrog libretro bounded_run core=%s max_frames=%u\n",
         core->id, host.options.max_frames);
   unifrog_diag_memory_snapshot("libretro.run_start");
   (void)unifrog_log_flush();
   host_apply_runtime_options();
   (void)unifrog_log_flush();
   loading_draw("LOADING GAME", "START", 2);
   unifrog_log_sync("libretro api_version begin core=%s gp=0x%08lx fn=0x%08lx",
      core->id ? core->id : "",
      (unsigned long)core_call_gp(core),
      (unsigned long)(uintptr_t)core->api_version);
   libretro_activity_set_core_call(core,
      UNIFROG_ACTIVITY_PHASE_LIBRETRO_API, 1u, core->api_version);
   api_version = CORE_CALL0_RET(core, core->api_version);
   unifrog_log_sync("libretro api_version done core=%s api=%u",
      core->id ? core->id : "", api_version);
   printf("unifrog libretro %s api=%u\n", core->id, api_version);
   unifrog_diag_memory_snapshot("libretro.after_api_version");
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
   libretro_watchdog_start();
   libretro_watchdog_enter(LIBRETRO_WATCHDOG_PHASE_LOAD, 0);
   probe_rom_seek_path(path);
   unifrog_diag_memory_snapshot("libretro.before_content_prepare");
   content_start_us = host_time_us();
retry_content_prepare:
   if (!read_profile_retry_safe && !info.need_fullpath &&
       !path_is_wrapped_compressed(path)) {
      read_profile_active = libretro_begin_read_profile("content_prepare");
      libretro_fast_read_timeout_enabled = read_profile_active ? 1 : 0;
   } else if (!read_profile_retry_safe && libretro_read_profile_enabled()) {
      printf("unifrog libretro read_profile skip profile=%s reason=%s path=%s\n",
         UNIFROG_SD_READ_MODE,
         info.need_fullpath ? "core_needs_fullpath" : "compressed_wrapper",
         path);
   }
   if (info.need_fullpath) {
      content_kind = "fullpath";
      if (path_is_zip(path)) {
         FILE *file = fopen(path, "rb");

         if (!file) {
            printf("unifrog libretro zip open failed path=%s\n", path);
            content_prepare_failed = 1;
            goto out_content_prepare;
         }
         if (zip_extract_rom_to_cache(file, path, &info, prepared_path,
             sizeof(prepared_path)) != 0) {
            fclose(file);
            content_prepare_failed = 1;
            goto out_content_prepare;
         }
         fclose(file);
         game_path = prepared_path;
         content_kind = "zip_cache";
         content_cache = 1;
      } else if (path_is_wrapped_compressed(path)) {
         if (extract_wrapped_compressed_to_cache(path, &info, prepared_path,
             sizeof(prepared_path)) != 0) {
            content_prepare_failed = 1;
            goto out_content_prepare;
         }
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

      if (path_is_wrapped_compressed(path)) {
         content_kind = "compressed_memory";
         if (load_wrapped_compressed_rom_data(path, &rom_data,
             &rom_size) != 0) {
            printf("unifrog libretro compressed memory fallback path=%s\n",
               path);
            if (extract_wrapped_compressed_to_cache(path, &info,
                prepared_path, sizeof(prepared_path)) != 0) {
               content_prepare_failed = 1;
               goto out_content_prepare;
            }
            read_path = prepared_path;
            content_kind = "compressed_cache_read";
            content_cache = 1;
         }
      } else {
         content_kind = path_is_zip(read_path) ? "zip_memory" : "raw_memory";
         if (path_is_zip(read_path)) {
            if (zip_load_rom_data_stream_path(read_path, &info, &rom_data,
                &rom_size, NULL, 0) != 0) {
               content_prepare_failed = 1;
               goto out_content_prepare;
            }
         } else {
            if (read_path_memory_with_fallback(read_path, &rom_data,
                &rom_size, "READING") != 0) {
               content_prepare_failed = 1;
               goto out_content_prepare;
            }
         }
      }
      if (!rom_data) {
         if (read_path_memory_with_fallback(read_path, &rom_data,
             &rom_size, "READ CACHE") != 0) {
            content_prepare_failed = 1;
            goto out_content_prepare;
         }
      }
      game.data = rom_data;
      game.size = rom_size;
      printf("unifrog libretro rom loaded size=%u path=%s source=%s aligned=%lu\n",
         (unsigned)rom_size, read_path, path,
         (unsigned long)((uintptr_t)rom_data & 31u));
      (void)unifrog_log_flush();
   }
out_content_prepare:
   if (content_prepare_failed && read_profile_active &&
       !read_profile_retry_safe) {
      content_retry_old_auto_flush = unifrog_log_auto_flush_bytes();
      unifrog_log_set_auto_flush_bytes(0);
      content_retry_log_quiet = 1;
      libretro_end_read_profile_ex(read_profile_active, "content_prepare",
         0, 0);
      libretro_fast_read_timeout_enabled = 0;
      read_profile_active = 0;
      read_profile_retry_safe = 1;
      content_prepare_failed = 0;
      rom_free_aligned(rom_data);
      rom_data = NULL;
      rom_size = 0;
      prepared_path[0] = '\0';
      game_path = path;
      game.path = path;
      content_kind = "none";
      content_cache = 0;
      printf("unifrog libretro read_profile retry_safe profile=%s path=%s\n",
         UNIFROG_SD_READ_MODE, path);
      goto retry_content_prepare;
   }
   libretro_end_read_profile(read_profile_active, "content_prepare",
      !content_prepare_failed);
   libretro_fast_read_timeout_enabled = 0;
   read_profile_active = 0;
   if (content_retry_log_quiet) {
      unifrog_log_set_auto_flush_bytes(content_retry_old_auto_flush);
      content_retry_log_quiet = 0;
   }
   if (content_prepare_failed)
      goto out_finish;
   host.content_alloc_appmem = 0;
   content_done_us = host_time_us();
   printf("unifrog load_time core=%s stage=content_prepare ms=%u kind=%s cache=%d bytes=%u path=%s source=%s\n",
      core->id, host_elapsed_ms(content_start_us, content_done_us),
      content_kind, content_cache, (unsigned)rom_size,
      game.path ? game.path : "", path);
   unifrog_diag_memory_snapshot("libretro.after_content_prepare");
   (void)unifrog_log_flush();

   printf("unifrog libretro step=retro_init\n");
   (void)unifrog_log_flush();
   loading_draw("LOADING GAME", "CORE INIT", 70);
   stage_start_us = host_time_us();
   CORE_CALL0_VOID(core, core->init);
   core_initialized = 1;
   unifrog_diag_memory_snapshot("libretro.after_core_init_call");

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
   unifrog_diag_memory_snapshot("libretro.after_core_init");
   (void)unifrog_log_flush();

   memset(&av, 0, sizeof(av));
   CORE_CALL1_VOID(core, core->get_system_av_info, &av);
   host.fps = av.timing.fps > 1.0 ? (unsigned)(av.timing.fps + 0.5) :
      (CORE_CALL0_RET(core, core->get_region) == RETRO_REGION_PAL ? 50u : 60u);
   host.video_max_width = av.geometry.max_width ? av.geometry.max_width :
      av.geometry.base_width;
   host.video_max_height = av.geometry.max_height ? av.geometry.max_height :
      av.geometry.base_height;
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
   libretro_watchdog_enter(LIBRETRO_WATCHDOG_PHASE_LOAD, 0);
   retro_load_start_us = host_time_us();
   if (info.need_fullpath) {
      (void)unifrog_log_flush();
      retro_read_profile_active =
         libretro_begin_read_profile("retro_load_game_fullpath");
   }
   if (!CORE_CALL1_RET(core, core->load_game, &game)) {
      libretro_end_read_profile(retro_read_profile_active,
         "retro_load_game_fullpath", 0);
      retro_read_profile_active = 0;
      libretro_watchdog_leave();
      retro_load_done_us = host_time_us();
      printf("unifrog load_time core=%s stage=retro_load_game ms=%u ok=0\n",
         core->id, host_elapsed_ms(retro_load_start_us,
         retro_load_done_us));
      printf("unifrog libretro load failed path=%s\n", path);
      unifrog_diag_memory_snapshot("libretro.retro_load_failed");
      goto out_unload;
   }
   retro_load_done_us = host_time_us();
   libretro_end_read_profile(retro_read_profile_active,
      "retro_load_game_fullpath", 1);
   retro_read_profile_active = 0;
   libretro_watchdog_leave();
   game_loaded = 1;
   printf("unifrog libretro step=retro_load_game_ok\n");
   printf("unifrog load_time core=%s stage=retro_load_game ms=%u ok=1\n",
      core->id, host_elapsed_ms(retro_load_start_us, retro_load_done_us));
   unifrog_diag_memory_snapshot("libretro.after_retro_load_game");
   save_load_start_us = host_time_us();
   save_existing_bytes = quick_existing_memory_file_bytes(core, path);
   printf("unifrog libretro save_memory_probe core=%s bytes=%u fast_min=%u\n",
      core->id, (unsigned)save_existing_bytes,
      (unsigned)LIBRETRO_SAVE_FAST_READ_MIN_BYTES);
   if (save_existing_bytes >= LIBRETRO_SAVE_FAST_READ_MIN_BYTES) {
      (void)unifrog_log_flush();
      save_read_profile_active = libretro_begin_read_profile("save_memory_load");
   }
   quick_load_all_memory_files(core, path);
   libretro_end_read_profile(save_read_profile_active, "save_memory_load", 1);
   save_read_profile_active = 0;
   save_load_done_us = host_time_us();
   printf("unifrog load_time core=%s stage=save_memory_load ms=%u\n",
      core->id, host_elapsed_ms(save_load_start_us, save_load_done_us));
   unifrog_diag_memory_snapshot("libretro.after_save_memory_load");
   libretro_read_profile_load_session_end("load_complete", 1);

   loading_draw("LOADING GAME", "READY", 100);
   loading_close();
   printf("unifrog libretro step=presenter_open\n");
   (void)unifrog_log_flush();
   if (unifrog_presenter_open_with_clock(&host.presenter, 2,
       present_flags_for_display_mode(host.display_mode),
       host.ge_clock) == 0) {
      host.presenter_open = 1;
      unifrog_presenter_clear(&host.presenter, 0xff000000u);
      unifrog_diag_memory_snapshot("libretro.after_presenter_open");
   } else {
      printf("unifrog libretro presenter open failed\n");
      goto out_unload;
   }

   printf("unifrog libretro step=audio_open\n");
   (void)unifrog_log_flush();
   host.audio_input_rate = sample_rate;
   host.audio_output_rate = host_audio_output_rate(sample_rate);
   host.audio_gate_open = 0;
   host.audio_quiet_batches = 0;
   if (!host.audio_enabled) {
      printf("unifrog libretro audio disabled rate=%u/%u\n",
         host.audio_input_rate, host.audio_output_rate);
   } else if (unifrog_audio_open_backend(&host.audio, host.audio_output_rate,
       LIBRETRO_AUDIO_CHANNELS, LIBRETRO_AUDIO_PERIOD_BYTES,
       LIBRETRO_AUDIO_PERIODS, LIBRETRO_AUDIO_BACKEND) == 0) {
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
      unifrog_diag_memory_snapshot("libretro.after_audio_open");
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
   unifrog_diag_memory_snapshot("libretro.before_run_loop");
   (void)unifrog_log_flush();
   host_pace_begin();
   if (UNIFROG_SD_EXPERIMENTAL) {
      unifrog_log_defer_begin();
      run_loop_log_defer = 1;
   }

   for (;;) {
      uint32_t run_start = unifrog_perf_count();
      unsigned video_before = host.video_frames;
      unsigned run_count;
      unsigned active_count;
      unsigned vsync_count;

      unifrog_input_save_previous();
      unifrog_input_poll_with_wireless_divisor(LIBRETRO_WIRELESS_POLL_DIVISOR);
      host.buttons = unifrog_input_buttons();
      if (!exit_combo_down())
         host.quick_combo_armed = 1;
      if (host.quick_combo_armed && exit_combo_down()) {
         host.quick_combo_armed = 0;
         printf("unifrog libretro quick_js core=%s frame=%u\n",
            core->id, host.run_frames);
         (void)libretro_log_flush_force_if_safe();
         if (quick_js_run(core, path)) {
            printf("unifrog libretro return_to_js core=%s frame=%u\n",
               core->id, host.run_frames);
            (void)libretro_log_flush_force_if_safe();
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
      (void)host_audio_flush_sample_buffer();
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
      if (host.run_frames >= LIBRETRO_NO_OUTPUT_GRACE_FRAMES &&
          host.video_frames == 0 && host.audio_batches == 0) {
         printf("unifrog libretro no_output_abort core=%s path=%s frames=%u video=%u audio=%u\n",
            core->id, path, host.run_frames, host.video_frames,
            host.audio_batches);
         unifrog_log_sync("libretro no_output_abort core=%s path=%s frames=%u video=%u audio=%u",
            core->id, path, host.run_frames, host.video_frames,
            host.audio_batches);
         ret = -1;
         break;
      }
      if (host.options.max_frames &&
          host.run_frames >= host.options.max_frames) {
         printf("unifrog libretro max_frames_return core=%s frames=%u max=%u\n",
            core->id, host.run_frames, host.options.max_frames);
         unifrog_log_sync("libretro max_frames_return core=%s path=%s frames=%u max=%u",
            core->id, path, host.run_frames, host.options.max_frames);
         break;
      }
      if (!host.fast_forward)
         host_pace_frame();
      if ((host.run_frames - host.run_report_frames) >=
          LIBRETRO_PERF_REPORT_FRAMES)
         host_report_perf(core->id, 0);
   }

   ret = 0;

out_unload:
   unifrog_diag_memory_snapshot("libretro.out_unload");
   libretro_watchdog_stop();
   if (game_loaded)
      quick_save_all_memory_files(core, path);
   if (core_initialized)
      CORE_CALL0_VOID(core, core->unload_game);
out_deinit:
   unifrog_diag_memory_snapshot("libretro.out_deinit");
   libretro_watchdog_stop();
   (void)host_audio_flush_sample_buffer();
   host_report_perf(core->id, 1);
   if (run_loop_log_defer)
      unifrog_log_defer_end();
   if (host.audio_open)
      unifrog_audio_close(&host.audio);
   if (host.presenter_open)
      unifrog_presenter_close(&host.presenter);
   unifrog_surface_free(host.software_framebuffer);
   host.software_framebuffer = NULL;
   if (core_initialized)
      CORE_CALL0_VOID(core, core->deinit);
out_finish:
   libretro_read_profile_load_session_end("out_finish", ret == 0);
   libretro_watchdog_stop();
   host.content_alloc_appmem = 0;
   rom_free_aligned(rom_data);
   unifrog_diag_memory_snapshot("libretro.out_finish");
   loading_close();
   host_restore_runtime_options();
   unifrog_input_recover_after_core();
   printf("unifrog libretro %s end ret=%d video=%ux%u pitch=%u\n",
      core->id, ret, host.video_width, host.video_height, host.video_pitch);
   unifrog_exception_activity_clear();
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
   uint64_t load_start_us;
   uint64_t load_done_us;
   size_t old_log_auto_flush;
   int ret = -1;

   if (!id || !id[0] || !loaded || !api)
      return -1;

   if (core_path && core_path[0])
      unifrog_text_copy(path, sizeof(path), core_path);
   else
      snprintf(path, sizeof(path), "/media/mmcblk0/unifrog/cores/%s.bin", id);
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   loading_draw("LOADING CORE", id, 4);
   unifrog_log_sync("external_core begin core=%s path=%s", id, path);
   (void)unifrog_log_flush();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_diag_memory_snapshot("libretro.before_external_core_load");
   load_start_us = host_time_us();
   if (libretro_read_profile_enabled()) {
      unsigned timeout_ms = libretro_fast_read_timeout_for_path(path, 2500u);
      int read_profile_active;

      loading_draw("LOADING CORE", "FAST READ", 8);
      read_profile_active = libretro_begin_read_profile("external_core_load");
      unifrog_log_sync("external_core fast_read core=%s path=%s active=%d timeout=%u",
         id, path, read_profile_active, timeout_ms);
      printf("unifrog libretro external_core fast_read core=%s path=%s active=%d timeout=%u\n",
         id, path, read_profile_active, timeout_ms);
      if (read_profile_active) {
         if (unifrog_core_module_load_file_timeout(path, id, loaded,
             timeout_ms) == 0) {
            libretro_end_read_profile(read_profile_active,
               "external_core_load", 1);
         } else {
            libretro_end_read_profile_ex(read_profile_active,
               "external_core_load", 0, 0);
            if (read_profile_active == LIBRETRO_READ_PROFILE_BORROWED)
               libretro_read_profile_load_session_end(
                  "external_core_timeout", 0);
            libretro_read_profile_backoff("external_core_load");
            loading_draw("LOADING CORE", "SAFE RETRY", 16);
            printf("unifrog libretro external_core safe_retry core=%s path=%s\n",
               id, path);
            unifrog_log_sync("external_core safe_retry core=%s path=%s",
               id, path);
            if (unifrog_core_module_load_file(path, id, loaded) != 0)
               goto out_restore_log;
         }
      } else {
         loading_draw("LOADING CORE", "BOOT READ", 8);
         printf("unifrog libretro external_core boot_read core=%s path=%s reason=read_profile_inactive\n",
            id, path);
         unifrog_log_sync("external_core boot_read core=%s path=%s reason=read_profile_inactive",
            id, path);
         if (unifrog_core_module_load_file(path, id, loaded) != 0)
            goto out_restore_log;
      }
   } else {
      loading_draw("LOADING CORE", "BOOT READ", 8);
      printf("unifrog libretro external_core boot_read core=%s path=%s reason=no_runtime_sd_profile\n",
         id, path);
      unifrog_log_sync("external_core boot_read core=%s path=%s reason=no_runtime_sd_profile",
         id, path);
      if (unifrog_core_module_load_file(path, id, loaded) != 0)
         goto out_restore_log;
   }
   load_done_us = host_time_us();
   printf("unifrog load_time core=%s stage=external_core_load ms=%u path=%s\n",
      id, host_elapsed_ms(load_start_us, load_done_us), path);
   unifrog_log_sync("external_core done core=%s path=%s ms=%u",
      id, path, host_elapsed_ms(load_start_us, load_done_us));
   (void)unifrog_log_flush();

   exports = loaded->exports;
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_EXTERNAL_CORE_API,
      unifrog_exception_activity_hash(id), (uint32_t)(uintptr_t)exports,
      (uint32_t)loaded->gp_addr);
   unifrog_log_sync("external_core api begin core=%s exports=0x%08lx size=%u gp=0x%08lx image=%u memory=%u",
      id, (unsigned long)(uintptr_t)exports,
      exports ? exports->size : 0u, (unsigned long)loaded->gp_addr,
      (unsigned)loaded->image_size, (unsigned)loaded->memory_size);
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
      unifrog_log_sync("external_core api incomplete core=%s exports=0x%08lx id=0x%08lx init=0x%08lx api=0x%08lx load=0x%08lx run=0x%08lx",
         id, (unsigned long)(uintptr_t)exports,
         (unsigned long)(uintptr_t)api->id,
         (unsigned long)(uintptr_t)api->init,
         (unsigned long)(uintptr_t)api->api_version,
         (unsigned long)(uintptr_t)api->load_game,
         (unsigned long)(uintptr_t)api->run);
      unifrog_core_module_unload(loaded);
      unifrog_diag_memory_snapshot("libretro.external_core_unloaded_incomplete");
      goto out_restore_log;
   }
   unifrog_log_sync("external_core api ready core=%s id=%s gp=0x%08lx api=0x%08lx init=0x%08lx load=0x%08lx run=0x%08lx",
      id, api->id ? api->id : "",
      (unsigned long)api->call_gp,
      (unsigned long)(uintptr_t)api->api_version,
      (unsigned long)(uintptr_t)api->init,
      (unsigned long)(uintptr_t)api->load_game,
      (unsigned long)(uintptr_t)api->run);
   ret = 0;

out_restore_log:
   unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
   if (ret != 0) {
      unifrog_log_sync("external_core fail core=%s path=%s", id, path);
      (void)unifrog_log_flush();
   }
   return ret;
}

static int run_core_id(const char *id, const char *path,
   const struct unifrog_libretro_run_options *options)
{
   struct unifrog_core_module_loaded loaded;
   struct libretro_core_api external_core;
   const struct libretro_core_api *core;
   const char *canonical = libretro_canonical_core_id(id);
   int loaded_external = 0;
   int load_session_active = 0;
   int ret;

   if (!canonical)
      return -1;

   unifrog_diag_memory_snapshot("libretro.core_id_start");
   unifrog_input_recover_core_transition("core_load");
   load_session_active =
      libretro_read_profile_load_session_begin("core_load_session");
   core = libretro_builtin_core_for_id(canonical);
   if (!core &&
       libretro_load_external_core(canonical,
       options ? options->core_path : NULL, &loaded, &external_core) == 0) {
      core = &external_core;
      loaded_external = 1;
   }
   if (!core) {
      printf("unifrog libretro core unavailable id=%s\n", canonical);
      libretro_read_profile_load_session_end("core_unavailable", 0);
      return -1;
   }

   ret = run_core(core, path, options);
   if (load_session_active)
      libretro_read_profile_load_session_end("core_return", ret == 0);
   if (loaded_external) {
      unifrog_core_module_unload(&loaded);
      unifrog_diag_memory_snapshot("libretro.after_external_core_unload");
   }
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

static int path_has_dir_component_ci(const char *path, const char *component)
{
   const char *p = path;
   size_t component_len;

   if (!path || !component || !component[0])
      return 0;
   component_len = strlen(component);
   while (*p) {
      const char *start;
      size_t len;

      while (*p == '/')
         p++;
      if (!*p)
         break;
      start = p;
      while (*p && *p != '/')
         p++;
      len = (size_t)(p - start);
      if (len == component_len &&
          strncasecmp(start, component, component_len) == 0)
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
   if (path_is_zip(path)) {
      if (path_has_dir_component_ci(path, "SFC") ||
          path_has_dir_component_ci(path, "SNES"))
         return "snes9x2005";
      if (path_has_dir_component_ci(path, "MD") ||
          path_has_dir_component_ci(path, "GENESIS") ||
          path_has_dir_component_ci(path, "MEGADRIVE") ||
          path_has_dir_component_ci(path, "MS") ||
          path_has_dir_component_ci(path, "SMS") ||
          path_has_dir_component_ci(path, "GG") ||
          path_has_dir_component_ci(path, "32X"))
         return "picodrive";
      if (path_has_dir_component_ci(path, "FC") ||
          path_has_dir_component_ci(path, "NES"))
         return "fceumm";
      if (path_has_dir_component_ci(path, "GB") ||
          path_has_dir_component_ci(path, "GBC"))
         return "gambatte";
      if (path_has_dir_component_ci(path, "GBA"))
         return "gpsp";
      if (path_has_dir_component_ci(path, "PCE") ||
          path_has_dir_component_ci(path, "TG16"))
         return "pce-fast";
   }
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

static long libretro_min_content_size_for_path(const char *path,
   const char *core_id)
{
   (void)core_id;

   if (unifrog_text_ends_with_ci(path, ".md") ||
       unifrog_text_ends_with_ci(path, ".gen") ||
       unifrog_text_ends_with_ci(path, ".smd") ||
       unifrog_text_ends_with_ci(path, ".bin"))
      return 512;
   if (unifrog_text_ends_with_ci(path, ".nes") ||
       unifrog_text_ends_with_ci(path, ".fds"))
      return 16;
   if (unifrog_text_ends_with_ci(path, ".gb") ||
       unifrog_text_ends_with_ci(path, ".gbc") ||
       unifrog_text_ends_with_ci(path, ".gba") ||
       unifrog_text_ends_with_ci(path, ".sfc") ||
       unifrog_text_ends_with_ci(path, ".smc") ||
       unifrog_text_ends_with_ci(path, ".pce"))
      return 512;
   return 1;
}

static int libretro_validate_content_file(const char *path,
   const char *core_id)
{
   unsigned char sample;
   struct stat st;
   FILE *file;
   long min_size;
   int stat_errno;
   int read_errno;

   errno = 0;
   if (stat(path, &st) != 0) {
      stat_errno = errno;
      printf("unifrog libretro content invalid path=%s core=%s reason=stat errno=%d\n",
         path ? path : "", core_id ? core_id : "", stat_errno);
      unifrog_log_sync("libretro dispatch fail invalid_file path=%s core=%s reason=stat errno=%d",
         path ? path : "", core_id ? core_id : "", stat_errno);
      return -1;
   }

   min_size = libretro_min_content_size_for_path(path, core_id);
   if (!S_ISREG(st.st_mode) || st.st_size < min_size) {
      printf("unifrog libretro content invalid path=%s core=%s mode=0x%lx size=%ld min=%ld\n",
         path, core_id ? core_id : "", (unsigned long)st.st_mode,
         (long)st.st_size, min_size);
      unifrog_log_sync("libretro dispatch fail invalid_file path=%s core=%s mode=0x%lx size=%ld min=%ld",
         path, core_id ? core_id : "", (unsigned long)st.st_mode,
         (long)st.st_size, min_size);
      return -1;
   }

   errno = 0;
   file = fopen(path, "rb");
   read_errno = errno;
   if (!file) {
      printf("unifrog libretro content invalid path=%s core=%s reason=open errno=%d\n",
         path, core_id ? core_id : "", read_errno);
      unifrog_log_sync("libretro dispatch fail invalid_file path=%s core=%s reason=open errno=%d",
         path, core_id ? core_id : "", read_errno);
      return -1;
   }
   errno = 0;
   if (fread(&sample, 1, 1, file) != 1) {
      read_errno = errno;
      fclose(file);
      printf("unifrog libretro content invalid path=%s core=%s reason=read_first errno=%d\n",
         path, core_id ? core_id : "", read_errno);
      unifrog_log_sync("libretro dispatch fail invalid_file path=%s core=%s reason=read_first errno=%d",
         path, core_id ? core_id : "", read_errno);
      return -1;
   }
   fclose(file);
   return 0;
}

int unifrog_libretro_run_path_ex(const char *path,
   const struct unifrog_libretro_run_options *options)
{
   const char *core_id;
   int log_defer;
   int ret;

   if (!path) {
      printf("unifrog libretro dispatch failed path=null\n");
      (void)unifrog_log_flush();
      return -1;
   }
   unifrog_log_set_disk_suspended(0);
   log_defer = experimental_sd_log_defer_begin("libretro_dispatch");
   if (UNIFROG_SD_EXPERIMENTAL)
      printf("unifrog libretro dispatch storage_precheck=skipped mode=%s path=%s\n",
         UNIFROG_SD_MODE, path);
   core_id = options && options->core_id[0] ?
      libretro_canonical_core_id(options->core_id) :
      libretro_default_core_id_for_path(path);
   printf("unifrog libretro dispatch path=%s requested_core=%s\n",
      path, options && options->core_id[0] ? options->core_id : "auto");
   unifrog_diag_memory_snapshot("libretro.dispatch");
   (void)unifrog_log_flush();
   if (core_id) {
      if (libretro_validate_content_file(path, core_id) != 0) {
         (void)unifrog_log_flush();
         experimental_sd_log_defer_end(log_defer, "libretro_dispatch", -1);
         return -1;
      }
      ret = run_core_id(core_id, path, options);
      printf("unifrog libretro dispatch core=%s ret=%d\n", core_id, ret);
      (void)unifrog_log_flush();
      experimental_sd_log_defer_end(log_defer, "libretro_dispatch", ret);
      return ret;
   }

   printf("unifrog libretro dispatch unsupported path=%s requested_core=%s\n",
      path, options && options->core_id[0] ? options->core_id : "auto");
   (void)unifrog_log_flush();
   experimental_sd_log_defer_end(log_defer, "libretro_dispatch", -1);
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
