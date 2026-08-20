#ifndef UNIFROG_LIBRETRO_INTERNAL_H
#define UNIFROG_LIBRETRO_INTERNAL_H

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
#include <unistd.h>

#include <unifrog/audio.h>
#include <unifrog/abi.h>
#include <unifrog/backlight.h>
#include <unifrog/battery.h>
#include <unifrog/build_info.h>
#include <unifrog/config.h>
#include <unifrog/clock.h>
#include <unifrog/diag.h>
#include <unifrog/exception_record.h>
#include <unifrog/fb.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/log.h>
#include <unifrog/libretro_policy.h>
#include <unifrog/libretro_extension.h>
#include <unifrog/panic.h>
#include <unifrog/paths.h>
#include <unifrog/perf.h>
#include <unifrog/platform.h>
#include <unifrog/presenter.h>
#include <unifrog/scpu.h>
#include <unifrog/storage_io.h>
#include <unifrog/storage_profile.h>
#include <unifrog/surface_alloc.h>
#include <unifrog/task.h>
#include <unifrog/text.h>
#include <unifrog/zlib_port.h>

#include <libretro.h>

#ifndef UNIFROG_LIBRETRO_NO_COMPRESSED
#include <lz4frame.h>
#include <zstd.h>
#endif

#ifndef UNIFROG_LIBRETRO_NATIVE_DLOPEN
#include "abi/unifrog_mips_call.h"
#include "modules/unifrog_core_module_loader.h"
#endif

#define printf unifrog_log

#define DEFAULT_SAMPLE_RATE 32000u
#define LIBRETRO_AUDIO_DEFAULT_GAIN 1u
#define LIBRETRO_AUDIO_VOLUME 65u
#define LIBRETRO_AUDIO_ROUTE "sf2000_audsink_mono_left"
#define LIBRETRO_AUDIO_BACKEND UNIFROG_AUDIO_BACKEND_AUDSINK
#define LIBRETRO_AUDIO_GATE_OPEN_LEVEL 5u
#define LIBRETRO_AUDIO_GATE_CLOSE_LEVEL 4u
#define LIBRETRO_AUDIO_GATE_CLOSE_MS 1500u
#define LIBRETRO_AUDIO_CHANNELS 1u
#define LIBRETRO_AUDIO_MAX_CHANNELS 2u
#define LIBRETRO_AUDIO_PERIOD_BYTES 512u
#define LIBRETRO_AUDIO_PERIODS 8u
#define LIBRETRO_AUDIO_GB300_PERIOD_BYTES 2048u
#define LIBRETRO_AUDIO_GB300_PERIODS 16u
#define LIBRETRO_AUDIO_WRITE_CHUNK_FRAMES 512u
#define LIBRETRO_AUDIO_SAMPLE_BUFFER_FRAMES 512u
#define LIBRETRO_AUDIO_SF2000_GATE_SETTLE_US 8000u
#define LIBRETRO_AUDIO_GB300_GATE_SETTLE_US 12000u
#define LIBRETRO_AUDIO_WRITE_ATTEMPTS 2u
#define LIBRETRO_AUDIO_WRITE_POLL_MS 1u
#define LIBRETRO_WIRELESS_POLL_DIVISOR 2u
#define LIBRETRO_PERF_REPORT_FRAMES 600u
/* Give slow cores time to catch up before resetting the pacing deadline. */
#define LIBRETRO_PACE_RESET_LATE_FRAMES 12u
/* Preserve a short recovery window after reset without allowing long bursts. */
#define LIBRETRO_PACE_RESET_CATCHUP_FRAMES 3u
#define LIBRETRO_COUNT_CALIBRATE_US 20000u
#define LIBRETRO_LOG_AUTO_FLUSH_BYTES (64u * 1024u)
#define LIBRETRO_LOAD_LOG_PERCENT_STEP 10u
#define LIBRETRO_WATCHDOG_MS 100u
#define LIBRETRO_WATCHDOG_LOAD_STALL_POLLS 600u
#define LIBRETRO_WATCHDOG_RUN_STALL_POLLS 100u
#define LIBRETRO_WATCHDOG_PHASE_LOAD 1u
#define LIBRETRO_WATCHDOG_PHASE_RUN 2u
#define LIBRETRO_NO_OUTPUT_GRACE_FRAMES 300u
#define LIBRETRO_SAVE_DIR UNIFROG_DIST_SAVE_ROOT
#define LIBRETRO_CONTENT_CACHE_DIR UNIFROG_DIST_CACHE_ROOT
#define LIBRETRO_MEMORY_FILE_MAX (16u * 1024u * 1024u)
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
#define LIBRETRO_STORAGE_RECOVER_ATTEMPTS 24u
#define LIBRETRO_STORAGE_RECOVER_DELAY_MS 250u
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

enum quick_menu_action {
   QUICK_MENU_ACTION_RESUME = 0,
   QUICK_MENU_ACTION_RETURN_MENU = 1,
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

struct zip_rom_entry {
   uint32_t crc32;
   uint32_t compressed_size;
   uint32_t uncompressed_size;
   uint32_t local_offset;
   uint16_t flags;
   uint16_t method;
   char name[256];
};

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

struct libretro_host {
   struct unifrog_presenter presenter;
   struct unifrog_audio audio;
   struct unifrog_libretro_run_options options;
   struct unifrog_scpu_clock scpu_restore;
   struct retro_audio_buffer_status_callback audio_status;
   const char *core_id;
   uintptr_t core_gp;
   uint32_t buttons;
   uint32_t local_buttons;
   uint32_t port_buttons[UNIFROG_INPUT_MAX_PORTS];
   uint16_t port_joypad_masks[UNIFROG_INPUT_MAX_PORTS];
   unsigned input_poll_frame;
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
   unsigned audio_channels;
   int video_seen;
   unsigned run_frames;
   unsigned video_frames;
   unsigned audio_batches;
   unsigned audio_frames;
   unsigned audio_input_rate;
   unsigned audio_output_rate;
   unsigned audio_resample_accum;
   unsigned audio_failures;
   unsigned audio_quiet_frames;
   unsigned audio_gain;
   unsigned audio_peak_max;
   unsigned audio_clip_count;
   unsigned audio_write_count;
   uint64_t audio_write_total_count;
   unsigned audio_write_max_count;
   unsigned audio_write_attempts;
   unsigned audio_write_poll_ms;
   unsigned audio_gate_close_frames;
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
   unsigned active_max_frame;
   unsigned slow_frames_125;
   unsigned slow_frames_150;
   unsigned slow_frames_200;
   unsigned slow_detail_logs;
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
   unifrog_task_handle loading_task;
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
   int fast_forward;
   int fast_forward_force_present;
   unsigned fast_forward_multiplier;
   int content_alloc_appmem;
   const struct libretro_core_api *quick_core;
   const char *quick_rom_path;
   unsigned quick_state_slot;
   int quick_state_auto_load;
   int quick_state_auto_save;
   int quick_menu_action;
   int quick_combo_armed;
   char quick_status[96];
   struct quick_core_option core_options[LIBRETRO_CORE_OPTION_MAX];
   unsigned core_option_count;
   int core_options_dirty;
   int core_options_loaded;
   int input_profile;
   int input_profile_dirty;
   int16_t audio_sample_buffer[LIBRETRO_AUDIO_SAMPLE_BUFFER_FRAMES *
      LIBRETRO_AUDIO_MAX_CHANNELS];
   unsigned audio_sample_buffer_frames;
   unsigned audio_sample_buffer_peak;
   struct unifrog_battery_status battery;
   uint32_t battery_check_ms;
   uint32_t battery_warning_until_ms;
   int battery_warning_active;
   int launch_requested;
   int shutdown_requested;
   char launch_core_id[UNIFROG_LIBRETRO_CORE_ID_MAX];
   char launch_path[UNIFROG_LIBRETRO_CONTENT_PATH_MAX];
};

extern struct libretro_host host;
extern int16_t audio_mix_buffer[2048 * LIBRETRO_AUDIO_MAX_CHANNELS];
extern int16_t audio_silence_buffer[384 * LIBRETRO_AUDIO_MAX_CHANNELS];
extern volatile unsigned watchdog_active;
extern volatile unsigned watchdog_phase;
extern volatile unsigned watchdog_marker;
extern volatile unsigned watchdog_heartbeat;
#ifndef UNIFROG_LIBRETRO_NATIVE_DLOPEN
extern char _gp[];
#endif

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

extern const struct quick_memory_file quick_memory_files[2];
extern const unsigned quick_backlight_levels[12];

uint64_t host_time_us(void);
unsigned host_elapsed_ms(uint64_t start_us, uint64_t end_us);
unsigned host_compute_frame_budget(unsigned fps, unsigned *scpu_mhz,
   unsigned *count_hz, unsigned *count_hz_calibrated);
void host_pace_begin(void);
void loading_draw(const char *title, const char *detail, unsigned percent);
int libretro_recover_storage(const char *tag);
int libretro_log_flush_force_if_safe(void);
unsigned sanitize_fast_forward_multiplier(unsigned multiplier);
const char *display_mode_label(int display_mode);
const char *framebuffer_format_label(int framebuffer_format);
const char *input_profile_opt_value(int input_profile);
int sanitize_input_profile(int input_profile);
unsigned present_flags_for_display_mode(int display_mode);
void host_configure_options(const struct unifrog_libretro_run_options *options);
uintptr_t host_read_gp(void);
uintptr_t host_expected_gp(void);
void host_force_expected_gp(void);
uintptr_t core_call_gp(const struct libretro_core_api *core);
void libretro_activity_set(const struct libretro_core_api *core,
   const char *path, uint32_t phase, uint32_t marker);
void libretro_activity_set_core_call(const struct libretro_core_api *core,
   uint32_t phase, uint32_t marker, const void *fn);
void host_apply_runtime_options(void);
void host_restore_runtime_options(void);
void quick_load_all_memory_files(const struct libretro_core_api *core,
   const char *rom_path);
void quick_save_all_memory_files(const struct libretro_core_api *core,
   const char *rom_path);
int quick_save_state_file(void);
int quick_load_state_file(void);
size_t libretro_content_read_chunk(void);
unsigned libretro_storage_attempts(void);
void libretro_watchdog_start(void);
void libretro_watchdog_stop(void);
void libretro_watchdog_enter(unsigned phase, unsigned marker);
void libretro_watchdog_load_progress(const char *stage,
   unsigned current, unsigned total);
void libretro_watchdog_leave(void);
void loading_task(void *arg);
void loading_close(void);
void core_options_reset(void);
int core_options_save_scope(int content_scope);
int core_options_clear_scope(int content_scope);
unsigned host_audio_output_rate(unsigned input_rate);
unsigned host_audio_output_channels(void);
const char *host_audio_route_name(void);
int host_audio_backend(void);
unsigned host_audio_period_bytes(void);
unsigned host_audio_periods(void);
unsigned host_audio_runtime_volume(void);
int host_audio_flush_sample_buffer(void);
int host_audio_flush_sample_buffer_force(void);
int quick_menu_run(const struct libretro_core_api *core, const char *rom_path);
int exit_combo_down(void);
bool unifrog_libretro_environment_cb(unsigned cmd, void *data);
void unifrog_libretro_video_refresh_cb(const void *data, unsigned width,
   unsigned height, size_t pitch);
void unifrog_libretro_audio_sample_cb(int16_t left, int16_t right);
size_t unifrog_libretro_audio_batch_cb(const int16_t *data, size_t frames);
void unifrog_libretro_input_poll_cb(void);
int16_t unifrog_libretro_input_state_cb(unsigned port, unsigned device,
   unsigned index, unsigned id);
bool unifrog_libretro_rumble_cb(unsigned port,
   enum retro_rumble_effect effect, uint16_t strength);

int path_is_zip(const char *path);
int path_is_wrapped_compressed(const char *path);
void probe_rom_seek_path(const char *path);
int copy_path_without_last_extension(const char *path, char *out,
   size_t out_size);
void rom_free_aligned(void *ptr);
int load_wrapped_compressed_rom_data(const char *path, uint8_t **out_data,
   size_t *out_size);
int zip_load_rom_data_stream_path(const char *zip_path,
   const struct retro_system_info *info, uint8_t **out_data,
   size_t *out_size, char *out_name, size_t out_name_size);
int read_path_memory_with_fallback(const char *path, uint8_t **out_data,
   size_t *out_size, const char *label);
int extract_wrapped_compressed_to_cache(const char *path,
   const struct retro_system_info *info, char *out_path, size_t out_path_size);
int zip_extract_rom_to_cache(FILE *file, const char *zip_path,
   const struct retro_system_info *info, char *out_path, size_t out_path_size);

#define UNIFROG_LIBRETRO_CORE_CALL_RESTORE_HOST_GP 1
#include "unifrog_libretro_core_call.h"
#undef UNIFROG_LIBRETRO_CORE_CALL_RESTORE_HOST_GP

#endif
