#ifndef JS2300_FRONTEND_INTERNAL_H
#define JS2300_FRONTEND_INTERNAL_H

#include "js2300_frontend.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <kernel/delay.h>

#include <js2300/js2300.h>

#include <unifrog/av.h>
#include <unifrog/backlight.h>
#include <unifrog/battery.h>
#include <unifrog/build_info.h>
#include <unifrog/boot.h>
#include <unifrog/boot_logo.h>
#include <unifrog/boot_trace.h>
#include <unifrog/diag.h>
#include <unifrog/display_benchmark.h>
#include <unifrog/fb.h>
#include <unifrog/ge.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/libretro_host.h>
#include <unifrog/log.h>
#include <unifrog/media.h>
#include <unifrog/panic.h>
#include <unifrog/path.h>
#include <unifrog/paths.h>
#include <unifrog/perf.h>
#include <unifrog/platform.h>
#include <unifrog/png.h>
#include <unifrog/text.h>

#define printf unifrog_log

#define JS2300_FRONTEND_APP_ROOT UNIFROG_DIST_ROOT
#define JS2300_FRONTEND_DATA_ROOT UNIFROG_DATA_ROOT
#define JS2300_FRONTEND_HCRTOS_MEDIA_MODULE \
   JS2300_FRONTEND_APP_ROOT "/modules/hcrtos-media.bin"
#define JS2300_FRONTEND_MAX_PATH 256
#define JS2300_FRONTEND_MANIFEST JS2300_FRONTEND_APP_ROOT "/manifest.ini"
#define JS2300_FRONTEND_SYSTEM_CHECK_REPORT \
   UNIFROG_REPORT_ROOT "/system-check.txt"
#define JS2300_FRONTEND_STORAGE_TEST_REPORT \
   UNIFROG_REPORT_ROOT "/storage-test-result.txt"
#define JS2300_FRONTEND_STORAGE_FULL_TEST_REPORT \
   UNIFROG_REPORT_ROOT "/storage-full-test-result.txt"
#define JS2300_FRONTEND_STORAGE_MODE_TEST_REPORT \
   UNIFROG_REPORT_ROOT "/storage-mode-test-result.txt"
#define JS2300_FRONTEND_STORAGE_STRESS_REPORT \
   UNIFROG_REPORT_ROOT "/storage-stress-result.txt"
#define JS2300_FRONTEND_STORAGE_QUICK_BENCH_REPORT \
   UNIFROG_REPORT_ROOT "/storage-quick-benchmark.txt"
#define JS2300_FRONTEND_STORAGE_TEST_PROBE \
   JS2300_FRONTEND_DATA_ROOT "/storage-test-probe.txt"
#define JS2300_FRONTEND_ICON_CACHE 24u
#define JS2300_FRONTEND_INDEX_MAX_DEPTH 12u
#define JS2300_FRONTEND_INDEX_MAX_FILES 65535u
#define JS2300_FRONTEND_INDEX_MAX_DIRS 4096u
#define JS2300_FRONTEND_INDEX_MAX_BYTES ((2u * 1024u * 1024u) - 8192u)

#define FRONTEND_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct system_check_report {
   char body[3072];
   size_t used;
};

struct js2300_cached_icon {
   char path[JS2300_FRONTEND_MAX_PATH];
   struct unifrog_png_image image;
   uint32_t last_used;
   int loaded;
   int failed;
};

struct js2300_frontend {
   struct unifrog_fb fb;
   struct unifrog_ge ge;
   struct unifrog_battery_status battery;
   struct unifrog_libretro_run_options run_options;
   struct js2300_cached_icon icon_cache[JS2300_FRONTEND_ICON_CACHE];
   char action[32];
   char path[JS2300_FRONTEND_MAX_PATH];
   int video_preset;
   int video_disable_audio;
   unsigned draw_buffer;
   int frame_open;
   unsigned frame_draw_ops;
   int frame_has_visible_content;
   int frame_ge_dirty;
   int ge_ready;
   unsigned boot_logo_present_skips;
   int pending_backlight_valid;
   unsigned pending_backlight_level;
   int pending_av_valid;
   int pending_av_mode;
   unsigned video_present_count;
   unsigned video_present_log_count;
   uint32_t frontend_start_ms;
   int owns_framebuffer;
   int extension_mode;
   int relaunch;
   int input_recovered;
   uint32_t icon_use_counter;
};

struct frontend_catalog_entry {
   const char *system;
   const char *core;
   const char *const *suffixes;
   unsigned suffix_count;
   const char *const *folders;
   unsigned folder_count;
};

struct frontend_index_scan {
   FILE *game_file;
   FILE *media_file;
   struct js2300_fs_index_result *result;
   uint32_t game_bytes;
   uint32_t media_bytes;
};

int is_video_file(const char *name);
const struct frontend_catalog_entry *frontend_catalog_for_path(const char *path);
int frontend_is_game_name(const char *name);
int frontend_should_hide_file(const char *name, const char *dir);
int frontend_path_join_checked(char *dst, size_t dst_size,
   const char *base, const char *name);
int frontend_dirent_is_dot(const struct dirent *entry);
int frontend_dirent_is_dir(const struct dirent *entry, const char *full);
int frontend_index_scan_dir(const char *dir, unsigned depth,
   struct frontend_index_scan *scan);

int frontend_fb_open(struct js2300_frontend *frontend);
void frontend_fb_reopen(struct js2300_frontend *frontend, const char *tag);
void frontend_icon_cache_clear(struct js2300_frontend *frontend);

void host_log(void *opaque, const char *message);
int host_flush_log(void *opaque);
uint32_t host_millis(void *opaque);
void host_sleep(void *opaque, uint32_t ms);
void host_video_clear(void *opaque, uint16_t color);
void host_video_rects(void *opaque, const struct js2300_rect *rects,
   size_t count);
void host_video_text(void *opaque, int x, int y, const char *text,
   uint16_t color);
int host_video_image(void *opaque, const char *path, int x, int y, int w,
   int h);
void host_video_present(void *opaque);
int host_video_font(void *opaque, const char *path);
uint32_t host_input_poll(void *opaque);
void host_battery(void *opaque, struct js2300_battery_status *status);
int host_backlight(void *opaque, int level, int *out_level);
int host_av_output(void *opaque, int mode, int *out_mode);
int host_fs_list(void *opaque, const char *path,
   struct js2300_fs_entry *entries, size_t max_entries);
int host_fs_read_text(void *opaque, const char *path,
   char *out, size_t out_size);
int host_fs_write_text(void *opaque, const char *path,
   const char *text, size_t size);
int host_fs_index(void *opaque, const char *root,
   const char *game_index_path, const char *media_index_path,
   struct js2300_fs_index_result *result);
int host_action(void *opaque, const char *id);
void host_exit(void *opaque, const char *reason);

void frontend_configure_host(struct js2300_frontend *frontend,
   struct js2300_host *host);
void js2300_frontend_default_run_options(
   struct unifrog_libretro_run_options *options);
int run_js_script_file(struct js2300_frontend *frontend, const char *path);
int run_requested_action(struct js2300_frontend *frontend);
int run_storage_test(struct js2300_frontend *frontend);
int run_storage_full_test(struct js2300_frontend *frontend);
int run_storage_mode_test(struct js2300_frontend *frontend, const char *profile);
int run_storage_stress_test(struct js2300_frontend *frontend,
   const char *profile);
int run_storage_quick_benchmark(struct js2300_frontend *frontend);

#endif
