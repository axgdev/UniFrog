#ifndef JS2300_JS2300_H
#define JS2300_JS2300_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JS2300_API_VERSION_MAJOR 0u
#define JS2300_API_VERSION_MINOR 5u
#define JS2300_API_VERSION_PATCH 0u

#define JS2300_API_VERSION_ENCODE(major, minor, patch) \
   ((((uint32_t)(major) & 0xffu) << 16) | \
    (((uint32_t)(minor) & 0xffu) << 8) | \
    ((uint32_t)(patch) & 0xffu))

#define JS2300_API_VERSION \
   JS2300_API_VERSION_ENCODE(JS2300_API_VERSION_MAJOR, \
                             JS2300_API_VERSION_MINOR, \
                             JS2300_API_VERSION_PATCH)

struct js2300_config {
   uint32_t size;
   uint32_t api_version;
   const char *app_root;
   const char *entry_script;
   size_t heap_bytes;
   size_t stack_bytes;
   size_t bytecode_cache_bytes;
};

struct js2300_rect {
   int x;
   int y;
   int w;
   int h;
   uint16_t color;
};

struct js2300_battery_status {
   int percent;
   int charging;
   int low;
};

struct js2300_fs_entry {
   char name[96];
   uint8_t is_dir;
};

struct js2300_host {
   uint32_t size;
   void *opaque;

   void (*log)(void *opaque, const char *message);
   int (*flush_log)(void *opaque);
   uint32_t (*millis)(void *opaque);
   void (*sleep_ms)(void *opaque, uint32_t ms);

   void (*video_clear)(void *opaque, uint16_t color);
   void (*video_rects)(void *opaque, const struct js2300_rect *rects,
                       size_t count);
   void (*video_text)(void *opaque, int x, int y, const char *text,
                      uint16_t color);
   int (*video_image)(void *opaque, const char *path, int x, int y,
                      int w, int h);
   void (*video_present)(void *opaque);

   uint32_t (*input_poll)(void *opaque);
   void (*battery)(void *opaque, struct js2300_battery_status *status);
   int (*fs_list)(void *opaque, const char *path,
                  struct js2300_fs_entry *entries, size_t max_entries);
   int (*action)(void *opaque, const char *id);
   void (*exit)(void *opaque, const char *reason);
   int (*backlight)(void *opaque, int level, int *out_level);
   int (*av_output)(void *opaque, int mode, int *out_mode);
   int (*fs_read_text)(void *opaque, const char *path,
                       char *out, size_t out_size);
   int (*fs_write_text)(void *opaque, const char *path,
                        const char *text, size_t size);
   int (*font_load)(void *opaque, const char *path);
};

struct js2300_runtime;

const char *js2300_version_string(void);
int js2300_config_init(struct js2300_config *config);
int js2300_runtime_create(const struct js2300_config *config,
                          const struct js2300_host *host,
                          struct js2300_runtime **out_runtime);
int js2300_runtime_run(struct js2300_runtime *runtime);
void js2300_runtime_destroy(struct js2300_runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif
