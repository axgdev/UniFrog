#include "js2300_frontend.h"

#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <kernel/delay.h>

#include <js2300/js2300.h>

#include <unifrog/av.h>
#include <unifrog/backlight.h>
#include <unifrog/battery.h>
#include <unifrog/boot.h>
#include <unifrog/fb.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/libretro_host.h>
#include <unifrog/log.h>
#include <unifrog/media.h>
#include <unifrog/panic.h>
#include <unifrog/path.h>
#include <unifrog/perf.h>
#include <unifrog/png.h>
#include <unifrog/text.h>

#define printf unifrog_log

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
#ifndef UNIFROG_JS2300_GIT_COMMIT
#define UNIFROG_JS2300_GIT_COMMIT "unknown"
#endif
#ifndef UNIFROG_FRONTEND_GIT_COMMIT
#define UNIFROG_FRONTEND_GIT_COMMIT "unknown"
#endif

#define JS2300_FRONTEND_APP_ROOT "/media/mmcblk0/unifrog"
#define JS2300_FRONTEND_ENTRY "main.js"
#define JS2300_FRONTEND_MAX_PATH 256
#define JS2300_FRONTEND_MANIFEST JS2300_FRONTEND_APP_ROOT "/manifest.ini"
#define JS2300_FRONTEND_SYSTEM_CHECK_REPORT \
   JS2300_FRONTEND_APP_ROOT "/system-check.txt"
#define JS2300_FRONTEND_ICON_CACHE 24u

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
   struct unifrog_battery_status battery;
   struct unifrog_libretro_run_options run_options;
   struct js2300_cached_icon icon_cache[JS2300_FRONTEND_ICON_CACHE];
   char action[32];
   char path[JS2300_FRONTEND_MAX_PATH];
   int video_preset;
   int video_disable_audio;
   unsigned draw_buffer;
   int frame_open;
   int relaunch;
   uint32_t icon_use_counter;
};

static void frontend_draw_status(struct js2300_frontend *frontend,
   const char *title, const char *detail);
static void frontend_configure_host(struct js2300_frontend *frontend,
   struct js2300_host *host);
static int run_requested_action(struct js2300_frontend *frontend);

static int is_video_file(const char *name)
{
   static const char *suffixes[] = {
      ".mp4", ".mov", ".mkv", ".avi", ".ts",
      ".m2ts", ".mpg", ".mpeg", ".h264", ".264",
      ".mp3", ".wav", ".flac", ".ogg", ".opus", ".aac", ".m4a",
      ".jpg", ".jpeg", ".png", ".gif", ".bmp",
   };

   for (unsigned i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
      if (unifrog_text_ends_with_ci(name, suffixes[i]))
         return 1;
   }
   return 0;
}

static int frontend_fb_open(struct js2300_frontend *frontend)
{
   if (unifrog_fb_open(&frontend->fb, UNIFROG_FB_OPEN_DEFAULT) != 0) {
      printf("unifrog js fb open failed\n");
      return -1;
   }
   if (unifrog_fb_set_buffer_count(&frontend->fb, 2) != 0)
      (void)unifrog_fb_set_buffer_count(&frontend->fb, 1);
   frontend->draw_buffer = frontend->fb.current_buffer;
   frontend->frame_open = 0;
   printf("unifrog js fb ready %ux%u stride=%u buffers=%u\n",
      frontend->fb.width, frontend->fb.height, frontend->fb.stride_pixels,
      frontend->fb.buffer_count);
   return 0;
}

static void frontend_fb_reopen(struct js2300_frontend *frontend, const char *tag)
{
   unifrog_fb_close(&frontend->fb);
   if (frontend_fb_open(frontend) == 0) {
      printf("unifrog js fb reopen tag=%s ret=0\n", tag ? tag : "none");
      frontend_draw_status(frontend, "RETURNING TO MENU", "JS FRONTEND");
   } else {
      printf("unifrog js fb reopen tag=%s ret=-1\n", tag ? tag : "none");
   }
}

static struct unifrog_surface frontend_surface(struct js2300_frontend *frontend)
{
   return unifrog_fb_surface_for_buffer(&frontend->fb, frontend->draw_buffer);
}

static void frontend_begin_frame(struct js2300_frontend *frontend)
{
   if (frontend->frame_open)
      return;
   frontend->draw_buffer = frontend->fb.current_buffer;
   if (frontend->fb.buffer_count > 1)
      frontend->draw_buffer = (frontend->fb.current_buffer + 1) % frontend->fb.buffer_count;
   frontend->frame_open = 1;
}

static void host_log(void *opaque, const char *message)
{
   (void)opaque;
   printf("js2300: %s\n", message ? message : "");
}

static int host_flush_log(void *opaque)
{
   (void)opaque;
   return unifrog_log_flush();
}

static uint32_t host_millis(void *opaque)
{
   (void)opaque;
   return unifrog_perf_time_ms();
}

static void host_sleep(void *opaque, uint32_t ms)
{
   (void)opaque;
   if (ms > 1000)
      ms = 1000;
   if (ms)
      msleep(ms);
}

static void host_video_clear(void *opaque, uint16_t color)
{
   struct js2300_frontend *frontend = opaque;
   struct unifrog_surface surface;

   frontend_begin_frame(frontend);
   surface = frontend_surface(frontend);
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height, color);
}

static void host_video_rects(void *opaque, const struct js2300_rect *rects,
   size_t count)
{
   struct js2300_frontend *frontend = opaque;
   struct unifrog_surface surface;

   frontend_begin_frame(frontend);
   surface = frontend_surface(frontend);
   for (size_t i = 0; i < count; i++)
      unifrog_gfx_fill_rect(&surface, rects[i].x, rects[i].y,
         rects[i].w, rects[i].h, rects[i].color);
}

static void host_video_text(void *opaque, int x, int y, const char *text,
   uint16_t color)
{
   struct js2300_frontend *frontend = opaque;
   struct unifrog_surface surface;

   frontend_begin_frame(frontend);
   surface = frontend_surface(frontend);
   unifrog_gfx_draw_text(&surface, x, y, text, color, 1);
}

static void frontend_icon_cache_clear(struct js2300_frontend *frontend)
{
   if (!frontend)
      return;
   for (unsigned i = 0; i < JS2300_FRONTEND_ICON_CACHE; i++) {
      unifrog_png_free(&frontend->icon_cache[i].image);
      memset(&frontend->icon_cache[i], 0, sizeof(frontend->icon_cache[i]));
   }
}

static struct js2300_cached_icon *frontend_icon_cache_get(
   struct js2300_frontend *frontend, const char *path)
{
   struct js2300_cached_icon *slot = NULL;
   uint32_t oldest = UINT32_MAX;

   if (!frontend || !path || !path[0])
      return NULL;
   for (unsigned i = 0; i < JS2300_FRONTEND_ICON_CACHE; i++) {
      if (frontend->icon_cache[i].path[0] &&
          strcmp(frontend->icon_cache[i].path, path) == 0) {
         frontend->icon_cache[i].last_used = ++frontend->icon_use_counter;
         return &frontend->icon_cache[i];
      }
      if (!frontend->icon_cache[i].path[0] && !slot)
         slot = &frontend->icon_cache[i];
   }
   if (!slot) {
      for (unsigned i = 0; i < JS2300_FRONTEND_ICON_CACHE; i++) {
         if (frontend->icon_cache[i].last_used <= oldest) {
            oldest = frontend->icon_cache[i].last_used;
            slot = &frontend->icon_cache[i];
         }
      }
      if (slot) {
         unifrog_png_free(&slot->image);
         memset(slot, 0, sizeof(*slot));
      }
   }
   if (!slot)
      return NULL;
   unifrog_text_copy(slot->path, sizeof(slot->path), path);
   slot->last_used = ++frontend->icon_use_counter;
   if (unifrog_png_load_file(path, &slot->image) == 0) {
      slot->loaded = 1;
      printf("js2300 icon load path=%s size=%ux%u\n",
         path, slot->image.width, slot->image.height);
   } else {
      slot->failed = 1;
      printf("js2300 icon load_failed path=%s\n", path);
   }
   return slot;
}

static int host_video_image(void *opaque, const char *path,
   int x, int y, int w, int h)
{
   struct js2300_frontend *frontend = opaque;
   struct js2300_cached_icon *icon;
   struct unifrog_surface surface;

   if (!frontend || !path || !path[0])
      return -1;
   icon = frontend_icon_cache_get(frontend, path);
   if (!icon || !icon->loaded)
      return -1;
   frontend_begin_frame(frontend);
   surface = frontend_surface(frontend);
   unifrog_png_draw(&surface, &icon->image, x, y, w, h);
   return 0;
}

static void host_video_present(void *opaque)
{
   struct js2300_frontend *frontend = opaque;

   if (!frontend->frame_open) {
      unifrog_fb_wait_vsync(&frontend->fb);
      return;
   }
   unifrog_fb_flush_buffer(&frontend->fb, frontend->draw_buffer);
   unifrog_fb_wait_vsync(&frontend->fb);
   unifrog_fb_pan(&frontend->fb, frontend->draw_buffer);
   frontend->frame_open = 0;
}

static int host_video_font(void *opaque, const char *path)
{
   int ret;
   (void)opaque;

   ret = unifrog_gfx_load_font5x7_file(path);
   printf("js2300 font load path=%s ret=%d\n", path ? path : "?", ret);
   return ret;
}

static void frontend_draw_status(struct js2300_frontend *frontend,
   const char *title, const char *detail)
{
   for (unsigned i = 0; i < frontend->fb.buffer_count; i++) {
      struct unifrog_surface surface =
         unifrog_fb_surface_for_buffer(&frontend->fb, i);

      unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height,
         UNIFROG_RGB565(7, 10, 14));
      unifrog_gfx_draw_text(&surface, 18, 70,
         title ? title : "LOADING MENU", UNIFROG_RGB565(235, 241, 246), 2);
      if (detail && detail[0])
         unifrog_gfx_draw_text(&surface, 18, 104, detail,
            UNIFROG_RGB565(150, 166, 180), 1);
      unifrog_fb_flush_buffer(&frontend->fb, i);
   }
   (void)unifrog_fb_pan(&frontend->fb, 0);
   frontend->draw_buffer = 0;
   frontend->frame_open = 0;
}

static uint32_t host_input_poll(void *opaque)
{
   uint32_t buttons;
   uint32_t out = 0;
   (void)opaque;

   unifrog_input_save_previous();
   unifrog_input_poll_with_wireless_divisor(1);
   buttons = unifrog_input_menu_buttons();

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

static void host_battery(void *opaque, struct js2300_battery_status *status)
{
   struct js2300_frontend *frontend = opaque;

   if (!status)
      return;
   unifrog_battery_update(&frontend->battery, 0);
   status->percent = frontend->battery.available ?
      (int)(frontend->battery.bars * 25u) : -1;
   status->charging = 0;
   status->low = frontend->battery.low;
}

static int host_backlight(void *opaque, int level, int *out_level)
{
   unsigned current = 0;
   int ret = 0;
   (void)opaque;

   if (level >= 0) {
      if (level > 100)
         level = 100;
      ret = unifrog_backlight_set((unsigned)level);
   }
   if (unifrog_backlight_get(&current) != 0) {
      if (level >= 0)
         printf("js2300 backlight request=%d ret=%d get_failed=1\n",
            level, ret);
      return -1;
   }
   if (out_level)
      *out_level = (int)current;
   if (level >= 0)
      printf("js2300 backlight request=%d ret=%d current=%u\n",
         level, ret, current);
   return ret;
}

static int host_av_output(void *opaque, int mode, int *out_mode)
{
   int current = 0;
   int ret = 0;
   (void)opaque;

   if (mode >= 0)
      ret = unifrog_av_set_mode(mode);
   if (unifrog_av_get_mode(&current) != 0) {
      if (mode >= 0)
         printf("js2300 av_output request=%d ret=%d get_failed=1\n",
            mode, ret);
      return -1;
   }
   if (out_mode)
      *out_mode = current;
   if (mode >= 0)
      printf("js2300 av_output request=%d ret=%d current=%d\n",
         mode, ret, current);
   return ret;
}

static int host_fs_list(void *opaque, const char *path,
   struct js2300_fs_entry *entries, size_t max_entries)
{
   DIR *dir;
   struct dirent *entry;
   int count = 0;
   (void)opaque;

   if (!path || !entries || max_entries == 0)
      return -1;
   dir = opendir(path);
   if (!dir) {
      printf("js2300 fs list open_fail path=%s\n", path);
      return -1;
   }
   while ((entry = readdir(dir)) != NULL && (size_t)count < max_entries) {
      char full[JS2300_FRONTEND_MAX_PATH];
      struct stat st;

      if (entry->d_name[0] == '.' &&
         (entry->d_name[1] == '\0' ||
          (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
         continue;
      unifrog_path_join(full, sizeof(full), path, entry->d_name);
      if (stat(full, &st) != 0)
         continue;
      unifrog_text_copy(entries[count].name, sizeof(entries[count].name),
         entry->d_name);
      entries[count].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
      count++;
   }
   closedir(dir);
   printf("js2300 fs list path=%s count=%d\n", path, count);
   return count;
}

static int host_fs_read_text(void *opaque, const char *path,
   char *out, size_t out_size)
{
   FILE *file;
   size_t got;
   (void)opaque;

   if (!path || !out || out_size == 0)
      return -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   got = fread(out, 1, out_size - 1, file);
   if (ferror(file)) {
      fclose(file);
      return -1;
   }
   out[got] = '\0';
   fclose(file);
   return (int)got;
}

static int host_fs_write_text(void *opaque, const char *path,
   const char *text, size_t size)
{
   char tmp[JS2300_FRONTEND_MAX_PATH + 8];
   FILE *file;
   int ret = -1;
   (void)opaque;

   if (!path || !path[0] || !text)
      return -1;
   snprintf(tmp, sizeof(tmp), "%s.tmp", path);
   file = fopen(tmp, "wb");
   if (!file) {
      printf("js2300 fs write open_fail path=%s errno=%d\n", tmp, errno);
      return -1;
   }
   if (size == 0 || fwrite(text, 1, size, file) == size)
      ret = 0;
   if (fclose(file) != 0)
      ret = -1;
   if (ret == 0) {
      unlink(path);
      if (rename(tmp, path) != 0) {
         printf("js2300 fs write rename_fail path=%s errno=%d\n",
            path, errno);
         ret = -1;
      }
   }
   if (ret != 0)
      unlink(tmp);
   return ret;
}

static int action_key_equals(const char *begin, const char *end,
   const char *key)
{
   size_t key_len = strlen(key);

   return (size_t)(end - begin) == key_len &&
      strncmp(begin, key, key_len) == 0;
}

static int action_parse_int(const char *begin, const char *end, int *value)
{
   int sign = 1;
   int out = 0;

   if (!begin || !end || begin >= end || !value)
      return -1;
   if (*begin == '-') {
      sign = -1;
      begin++;
      if (begin >= end)
         return -1;
   }
   while (begin < end) {
      if (*begin < '0' || *begin > '9')
         return -1;
      if (out < 100000)
         out = out * 10 + (*begin - '0');
      begin++;
   }
   *value = out * sign;
   return 0;
}

static void action_copy_text(char *dst, size_t dst_size, const char *begin,
   const char *end)
{
   size_t len;

   if (!dst || !dst_size)
      return;
   if (!begin || !end || begin >= end) {
      dst[0] = '\0';
      return;
   }
   len = (size_t)(end - begin);
   if (len >= dst_size)
      len = dst_size - 1;
   memcpy(dst, begin, len);
   dst[len] = '\0';
}

static void parse_run_option_list(struct unifrog_libretro_run_options *options,
   const char *begin, const char *end)
{
   const char *cursor = begin;

   while (cursor && cursor < end) {
      const char *key_begin = cursor;
      const char *key_end;
      const char *value_begin;
      const char *value_end;
      int value;

      while (cursor < end && *cursor != '=' && *cursor != ',')
         cursor++;
      if (cursor >= end || *cursor != '=')
         break;
      key_end = cursor++;
      value_begin = cursor;
      while (cursor < end && *cursor != ',')
         cursor++;
      value_end = cursor;

      if (action_key_equals(key_begin, key_end, "core")) {
         action_copy_text(options->core_id, sizeof(options->core_id),
            value_begin, value_end);
      } else if (action_key_equals(key_begin, key_end, "corefile")) {
         action_copy_text(options->core_path, sizeof(options->core_path),
            value_begin, value_end);
      } else if (action_parse_int(value_begin, value_end, &value) == 0) {
         if (action_key_equals(key_begin, key_end, "audio"))
            options->audio_enabled = value ? 1 : 0;
         else if (action_key_equals(key_begin, key_end, "gain") && value >= 0)
            options->audio_gain = (unsigned)value;
         else if (action_key_equals(key_begin, key_end, "cpu") && value >= 0)
            options->scpu_mhz = (unsigned)value;
         else if (action_key_equals(key_begin, key_end, "ge"))
            options->ge_clock = value;
         else if (action_key_equals(key_begin, key_end, "backlight"))
            options->backlight_level = value;
         else if (action_key_equals(key_begin, key_end, "fs") ||
                  action_key_equals(key_begin, key_end, "frameskip"))
            options->frameskip = value;
         else if (action_key_equals(key_begin, key_end, "display"))
            options->display_mode = value;
      }

      if (cursor < end && *cursor == ',')
         cursor++;
   }
}

static const char *parse_run_action(struct js2300_frontend *frontend,
   const char *id)
{
   const char *path = NULL;

   unifrog_libretro_run_options_init(&frontend->run_options);
   if (strncmp(id, "run:", 4) == 0) {
      path = id + 4;
   } else if (strncmp(id, "run+", 4) == 0) {
      const char *options_begin = id + 4;
      const char *options_end = strchr(options_begin, ':');

      if (!options_end)
         return NULL;
      parse_run_option_list(&frontend->run_options,
         options_begin, options_end);
      path = options_end + 1;
   }

   if (!path || !path[0])
      return NULL;
   return path;
}

static int system_check_read_file(const char *path, char *out, size_t out_size)
{
   FILE *file;
   size_t got;

   if (!path || !out || out_size == 0)
      return -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   got = fread(out, 1, out_size - 1, file);
   if (ferror(file)) {
      fclose(file);
      return -1;
   }
   out[got] = '\0';
   fclose(file);
   return (int)got;
}

static int system_check_manifest_value(const char *text, const char *key,
   char *out, size_t out_size)
{
   size_t key_len;
   const char *line;

   if (!text || !key || !out || out_size == 0)
      return -1;
   key_len = strlen(key);
   line = text;
   while (*line) {
      const char *end = strchr(line, '\n');
      const char *cursor = line;
      const char *value;
      size_t len;

      if (!end)
         end = line + strlen(line);
      while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
         cursor++;
      if (cursor + key_len < end &&
          strncmp(cursor, key, key_len) == 0 &&
          cursor[key_len] == '=') {
         value = cursor + key_len + 1;
         while (end > value && (end[-1] == '\r' || end[-1] == ' ' ||
                end[-1] == '\t'))
            end--;
         len = (size_t)(end - value);
         if (len >= out_size)
            len = out_size - 1;
         memcpy(out, value, len);
         out[len] = '\0';
         return 0;
      }
      line = *end ? end + 1 : end;
   }
   return -1;
}

static void system_check_report_append(struct system_check_report *report,
   const char *fmt, ...)
{
   va_list ap;
   int wrote;

   if (!report || report->used >= sizeof(report->body))
      return;
   va_start(ap, fmt);
   wrote = vsnprintf(report->body + report->used,
      sizeof(report->body) - report->used, fmt, ap);
   va_end(ap);
   if (wrote <= 0)
      return;
   if ((size_t)wrote >= sizeof(report->body) - report->used)
      report->used = sizeof(report->body) - 1;
   else
      report->used += (size_t)wrote;
}

static const char *system_check_manifest_label(const char *key)
{
   if (strcmp(key, "firmware_commit") == 0)
      return "Firmware build";
   if (strcmp(key, "firmware_dirty") == 0)
      return "Firmware dirty flag";
   if (strcmp(key, "sdk_commit") == 0)
      return "SDK revision";
   if (strcmp(key, "cores_commit") == 0)
      return "Cores package";
   if (strcmp(key, "js2300_commit") == 0)
      return "JS2300 runtime";
   if (strcmp(key, "frontend_commit") == 0)
      return "Frontend package";
   return key;
}

static void system_check_manifest_key(const char *manifest, const char *key,
   const char *expected, unsigned *stale_count,
   struct system_check_report *report)
{
   char actual[64];
   int ok;

   if (system_check_manifest_value(manifest, key, actual, sizeof(actual)) != 0) {
      printf("unifrog system_check manifest key=%s missing=1 expected=%s\n",
         key, expected ? expected : "?");
      system_check_report_append(report,
         "item|STALE|%s|Expected %s|Manifest key %s is missing\n",
         system_check_manifest_label(key), expected ? expected : "?", key);
      (*stale_count)++;
      return;
   }
   ok = expected && strcmp(actual, expected) == 0;
   printf("unifrog system_check manifest key=%s expected=%s actual=%s ok=%d\n",
      key, expected ? expected : "?", actual, ok);
   if (!ok) {
      system_check_report_append(report,
         "item|STALE|%s|Expected %s|Found %s\n",
         system_check_manifest_label(key), expected ? expected : "?",
         actual);
      (*stale_count)++;
   }
}

static void system_check_file(const char *path, unsigned *missing_count,
   struct system_check_report *report)
{
   struct stat st;
   int ok = stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;

   printf("unifrog system_check file=%s ok=%d size=%lu\n",
      path ? path : "?", ok, ok ? (unsigned long)st.st_size : 0ul);
   if (!ok) {
      system_check_report_append(report,
         "item|MISSING|%s|Expected packaged file|Not found or empty\n",
         path ? path : "?");
      (*missing_count)++;
   }
}

static void system_check_write_report(const struct system_check_report *report,
   unsigned missing, unsigned stale)
{
   char tmp[sizeof(JS2300_FRONTEND_SYSTEM_CHECK_REPORT) + 8];
   FILE *file;
   int ret = -1;

   snprintf(tmp, sizeof(tmp), "%s.tmp", JS2300_FRONTEND_SYSTEM_CHECK_REPORT);
   file = fopen(tmp, "wb");
   if (!file) {
      printf("unifrog system_check report open_fail path=%s errno=%d\n",
         tmp, errno);
      return;
   }

   fprintf(file, "show=1\n");
   fprintf(file, "title=%s\n",
      (missing || stale) ? "SD FILES NEED REFRESH" : "SYSTEM CHECK OK");
   fprintf(file, "detail=%u missing  %u stale\n", missing, stale);
   fprintf(file, "missing=%u\n", missing);
   fprintf(file, "stale=%u\n", stale);
   if (report && report->used)
      fwrite(report->body, 1, report->used, file);
   else
      fprintf(file, "item|OK|SD package|Files match this build|Ready\n");

   if (fclose(file) == 0) {
      unlink(JS2300_FRONTEND_SYSTEM_CHECK_REPORT);
      if (rename(tmp, JS2300_FRONTEND_SYSTEM_CHECK_REPORT) == 0)
         ret = 0;
   }
   if (ret != 0) {
      printf("unifrog system_check report write_fail path=%s errno=%d\n",
         JS2300_FRONTEND_SYSTEM_CHECK_REPORT, errno);
      unlink(tmp);
   } else {
      printf("unifrog system_check report path=%s missing=%u stale=%u\n",
         JS2300_FRONTEND_SYSTEM_CHECK_REPORT, missing, stale);
   }
}

static int run_system_check(struct js2300_frontend *frontend)
{
   static const char *required_files[] = {
      "/media/mmcblk0/bios/bisrv.asd",
      "/media/mmcblk0/firmware/unifrog.bin",
      JS2300_FRONTEND_APP_ROOT "/main.js",
      JS2300_FRONTEND_APP_ROOT "/manifest.ini",
      JS2300_FRONTEND_APP_ROOT "/scripts/smoke-test.js",
      JS2300_FRONTEND_APP_ROOT "/themes/default.ini",
      JS2300_FRONTEND_APP_ROOT "/themes/system-icons/icons/gba.png",
      JS2300_FRONTEND_APP_ROOT "/themes/system-icons/icons/snes.png",
      JS2300_FRONTEND_APP_ROOT "/themes/system-icons/icons/settings.png",
      JS2300_FRONTEND_APP_ROOT "/cores/js2300.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/gpsp.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/gambatte.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/picodrive.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/snes9x2005.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/snes9x2002.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/quicknes.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/fceumm.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/gearboy.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/pce-fast.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/qpsx.bin",
      JS2300_FRONTEND_APP_ROOT "/cores/pmp-video.bin",
   };
   char manifest[2048];
   char dirty[16];
   char detail[64];
   struct system_check_report report;
   unsigned stale = 0;
   unsigned missing = 0;
   int manifest_ret;

   memset(&report, 0, sizeof(report));

   printf("unifrog system_check begin running firmware=%s dirty=%d sdk=%s cores=%s js2300=%s frontend=%s\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY, UNIFROG_SDK_GIT_COMMIT,
      UNIFROG_CORES_GIT_COMMIT, UNIFROG_JS2300_GIT_COMMIT,
      UNIFROG_FRONTEND_GIT_COMMIT);

   for (unsigned i = 0; i < sizeof(required_files) / sizeof(required_files[0]); i++)
      system_check_file(required_files[i], &missing, &report);

   manifest_ret = system_check_read_file(JS2300_FRONTEND_MANIFEST,
      manifest, sizeof(manifest));
   printf("unifrog system_check manifest path=%s ret=%d\n",
      JS2300_FRONTEND_MANIFEST, manifest_ret);
   if (manifest_ret < 0) {
      system_check_report_append(&report,
         "item|STALE|Manifest|Expected %s|Cannot read %s\n",
         JS2300_FRONTEND_MANIFEST, JS2300_FRONTEND_MANIFEST);
      stale++;
   } else {
      snprintf(dirty, sizeof(dirty), "%d", UNIFROG_GIT_DIRTY);
      system_check_manifest_key(manifest, "firmware_commit",
         UNIFROG_GIT_COMMIT, &stale, &report);
      system_check_manifest_key(manifest, "firmware_dirty", dirty,
         &stale, &report);
      system_check_manifest_key(manifest, "sdk_commit",
         UNIFROG_SDK_GIT_COMMIT, &stale, &report);
      system_check_manifest_key(manifest, "cores_commit",
         UNIFROG_CORES_GIT_COMMIT, &stale, &report);
      system_check_manifest_key(manifest, "js2300_commit",
         UNIFROG_JS2300_GIT_COMMIT, &stale, &report);
      system_check_manifest_key(manifest, "frontend_commit",
         UNIFROG_FRONTEND_GIT_COMMIT, &stale, &report);
   }

   printf("unifrog system_check result missing=%u stale=%u\n",
      missing, stale);
   system_check_write_report(&report, missing, stale);
   (void)unifrog_log_flush();
   if (missing || stale) {
      snprintf(detail, sizeof(detail), "%u missing  %u stale",
         missing, stale);
      frontend_draw_status(frontend, "SD FILES NEED REFRESH", detail);
   } else {
      frontend_draw_status(frontend, "SYSTEM CHECK OK",
         "SD files match this build");
   }
   msleep(1700);
   return 0;
}

static int host_action(void *opaque, const char *id)
{
   struct js2300_frontend *frontend = opaque;
   const char *run_path;

   if (!id || !*id)
      return -1;
   if (frontend->action[0])
      return 0;

   run_path = parse_run_action(frontend, id);
   if (run_path) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action), "run");
      unifrog_text_copy(frontend->path, sizeof(frontend->path), run_path);
      printf("js2300 action run path=%s core=%s corefile=%s audio=%d gain=%u scpu=%u ge=%d backlight=%d fs=%d display=%d\n",
         frontend->path,
         frontend->run_options.core_id[0] ?
            frontend->run_options.core_id : "auto",
         frontend->run_options.core_path,
         frontend->run_options.audio_enabled,
         frontend->run_options.audio_gain,
         frontend->run_options.scpu_mhz, frontend->run_options.ge_clock,
         frontend->run_options.backlight_level,
         frontend->run_options.frameskip,
         frontend->run_options.display_mode);
      return 0;
   }
   if (strncmp(id, "script:", 7) == 0) {
      const char *path = id + 7;

      if (!path[0] || !unifrog_text_ends_with_ci(path, ".js"))
         return -1;
      unifrog_text_copy(frontend->action, sizeof(frontend->action), "script");
      unifrog_text_copy(frontend->path, sizeof(frontend->path), path);
      printf("js2300 action script path=%s\n", frontend->path);
      return 0;
   }
   if (strcmp(id, "developer:exception") == 0) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action),
         "exception");
      printf("js2300 action developer exception\n");
      return 0;
   }
   if (strcmp(id, "developer:cpu_exception") == 0) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action),
         "cpu_exception");
      printf("js2300 action developer cpu_exception\n");
      return 0;
   }
   if (strcmp(id, "developer:system_check") == 0) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action),
         "system_check");
      printf("js2300 action developer system_check\n");
      return 0;
   }
   if (strncmp(id, "video:", 6) == 0) {
      const char *path = id + 6;
      int preset = 0;
      int disable_audio = 0;

      if (path[0] == 'n' && path[1] == ':') {
         disable_audio = 1;
         path += 2;
      }
      if (path[0] >= '0' && path[0] <= '9' && path[1] == ':') {
         preset = path[0] - '0';
         path += 2;
      }
      if (!is_video_file(path))
         return -1;
      unifrog_text_copy(frontend->action, sizeof(frontend->action), "video");
      unifrog_text_copy(frontend->path, sizeof(frontend->path), path);
      frontend->video_preset = preset;
      frontend->video_disable_audio = disable_audio;
      printf("js2300 action video preset=%d no_audio=%d path=%s\n",
         frontend->video_preset, frontend->video_disable_audio,
         frontend->path);
      return 0;
   }
   if (strncmp(id, "firmware:", 9) == 0) {
      if (!unifrog_boot_firmware_name_supported(id + 9)) {
         printf("js2300 action firmware unsupported name=%s\n", id + 9);
         return -1;
      }
      unifrog_text_copy(frontend->action, sizeof(frontend->action), "firmware");
      unifrog_text_copy(frontend->path, sizeof(frontend->path), id + 9);
      printf("js2300 action firmware name=%s\n", frontend->path);
      return 0;
   }
   if (strcmp(id, "continue") == 0) {
      printf("js2300 action continue ignored: missing explicit path\n");
      return -1;
   }
   return -1;
}

static void host_exit(void *opaque, const char *reason)
{
   (void)opaque;
   printf("js2300 exit reason=%s\n", reason ? reason : "");
}

static int split_script_path(const char *path, char *root, size_t root_size,
   char *entry, size_t entry_size)
{
   const char *slash;
   size_t root_len;

   if (!path || !path[0] || !root || !entry ||
       root_size == 0 || entry_size == 0)
      return -1;
   slash = strrchr(path, '/');
   if (!slash || slash == path)
      return -1;
   root_len = (size_t)(slash - path);
   if (root_len >= root_size || strlen(slash + 1) >= entry_size)
      return -1;
   memcpy(root, path, root_len);
   root[root_len] = '\0';
   strcpy(entry, slash + 1);
   return 0;
}

static int run_js_script_file(struct js2300_frontend *frontend,
   const char *path)
{
   struct js2300_config config;
   struct js2300_host host;
   struct js2300_runtime *runtime = NULL;
   char root[JS2300_FRONTEND_MAX_PATH];
   char entry[96];
   int ret;

   if (!frontend || split_script_path(path, root, sizeof(root),
       entry, sizeof(entry)) != 0)
      return -1;

   js2300_config_init(&config);
   config.app_root = root;
   config.entry_script = entry;
   config.heap_bytes = 8u * 1024u * 1024u;
   frontend_configure_host(frontend, &host);
   printf("js2300 script launch root=%s script=%s\n", root, entry);
   (void)unifrog_log_flush();
   ret = js2300_runtime_create(&config, &host, &runtime);
   if (ret == 0)
      ret = js2300_runtime_run(runtime);
   js2300_runtime_destroy(runtime);
   printf("js2300 script done ret=%d path=%s action=%s\n",
      ret, path, frontend->action);
   (void)unifrog_log_flush();
   return ret;
}

static void frontend_configure_host(struct js2300_frontend *frontend,
   struct js2300_host *host)
{
   memset(host, 0, sizeof(*host));
   host->size = sizeof(*host);
   host->opaque = frontend;
   host->log = host_log;
   host->flush_log = host_flush_log;
   host->millis = host_millis;
   host->sleep_ms = host_sleep;
   host->video_clear = host_video_clear;
   host->video_rects = host_video_rects;
   host->video_text = host_video_text;
   host->video_image = host_video_image;
   host->video_present = host_video_present;
   host->font_load = host_video_font;
   host->input_poll = host_input_poll;
   host->battery = host_battery;
   host->fs_list = host_fs_list;
   host->action = host_action;
   host->exit = host_exit;
   host->backlight = host_backlight;
   host->av_output = host_av_output;
   host->fs_read_text = host_fs_read_text;
   host->fs_write_text = host_fs_write_text;
}

static int run_requested_action(struct js2300_frontend *frontend)
{
   frontend->relaunch = 1;
   printf("js2300 run action dispatch action=%s path=%s preset=%d\n",
      frontend->action, frontend->path, frontend->video_preset);
   (void)unifrog_log_flush();
   if (strcmp(frontend->action, "run") == 0) {
      int ret = unifrog_libretro_run_path_ex(frontend->path,
         &frontend->run_options);

      printf("js2300 run action core ret=%d path=%s\n", ret, frontend->path);
      (void)unifrog_log_flush();
      frontend_fb_reopen(frontend, "libretro_return");
      return 0;
   }
   if (strcmp(frontend->action, "video") == 0) {
      struct unifrog_media_video_options options;

      memset(&options, 0, sizeof(options));
      options.preset = frontend->video_preset;
      options.disable_audio = frontend->video_disable_audio;
      unifrog_fb_close(&frontend->fb);
      (void)unifrog_media_play_video_ex(frontend->path, &options);
      frontend_fb_reopen(frontend, "video_return");
      return 0;
   }
   if (strcmp(frontend->action, "continue") == 0) {
      printf("js2300 continue ignored: no explicit last-game path\n");
      (void)unifrog_log_flush();
      frontend_fb_reopen(frontend, "continue_return");
      return 0;
   }
   if (strcmp(frontend->action, "firmware") == 0) {
      int ret;

      printf("js2300 firmware boot request name=%s\n", frontend->path);
      (void)unifrog_log_flush();
      ret = unifrog_boot_firmware_asd(frontend->path);
      printf("js2300 firmware boot failed ret=%d name=%s\n", ret,
         frontend->path);
      (void)unifrog_log_flush();
      return ret;
   }
   if (strcmp(frontend->action, "script") == 0) {
      char script_path[JS2300_FRONTEND_MAX_PATH];
      int ret;

      unifrog_text_copy(script_path, sizeof(script_path), frontend->path);
      frontend->action[0] = '\0';
      frontend->path[0] = '\0';
      ret = run_js_script_file(frontend, script_path);
      if (ret == 0 && frontend->action[0])
         return run_requested_action(frontend);
      frontend_fb_reopen(frontend, "script_return");
      return ret;
   }
   if (strcmp(frontend->action, "exception") == 0) {
      printf("js2300 developer trigger exception\n");
      (void)unifrog_log_flush();
      unifrog_fb_close(&frontend->fb);
      unifrog_panic_trigger_test_exception();
      return -1;
   }
   if (strcmp(frontend->action, "cpu_exception") == 0) {
      printf("js2300 developer trigger cpu_exception\n");
      (void)unifrog_log_flush();
      unifrog_fb_close(&frontend->fb);
      unifrog_panic_trigger_cpu_exception();
      return -1;
   }
   if (strcmp(frontend->action, "system_check") == 0) {
      int ret = run_system_check(frontend);

      frontend_fb_reopen(frontend, "system_check_return");
      return ret;
   }
   return -1;
}

int js2300_frontend_core_main(void)
{
   struct js2300_frontend frontend;
   struct js2300_config config;
   struct js2300_host host;
   uint32_t frontend_start_ms;
   unsigned launch_count = 0;
   int ret = 0;

   memset(&frontend, 0, sizeof(frontend));
   frontend_start_ms = unifrog_perf_time_ms();
   unifrog_battery_status_init(&frontend.battery);

   if (frontend_fb_open(&frontend) != 0)
      return -1;
   printf("unifrog boot_time stage=js_fb_ready total_ms=%lu\n",
      (unsigned long)(unifrog_perf_time_ms() - frontend_start_ms));
   frontend_draw_status(&frontend, "LOADING MENU", "JS FRONTEND");
   printf("unifrog boot_time stage=loading_screen total_ms=%lu\n",
      (unsigned long)(unifrog_perf_time_ms() - frontend_start_ms));

   js2300_config_init(&config);
   config.app_root = JS2300_FRONTEND_APP_ROOT;
   config.entry_script = JS2300_FRONTEND_ENTRY;
   config.heap_bytes = 8u * 1024u * 1024u;

   frontend_configure_host(&frontend, &host);

   do {
      struct js2300_runtime *runtime = NULL;
      size_t old_auto_flush;

      frontend.relaunch = 0;
      frontend.action[0] = 0;
      frontend.path[0] = 0;
      frontend.video_preset = 0;
      frontend.video_disable_audio = 0;
      unifrog_input_recover_core_transition(launch_count++ ?
         "frontend_relaunch" : "frontend_launch");
      unifrog_libretro_run_options_init(&frontend.run_options);
      old_auto_flush = unifrog_log_auto_flush_bytes();
      unifrog_log_set_auto_flush_bytes(64u * 1024u);
      printf("unifrog js launch root=%s script=%s boot_ms=%lu relaunch=%u\n",
         config.app_root, config.entry_script,
         (unsigned long)(unifrog_perf_time_ms() - frontend_start_ms),
         launch_count - 1u);
      unifrog_log_flush();

      ret = js2300_runtime_create(&config, &host, &runtime);
      printf("unifrog boot_time stage=js_runtime_created total_ms=%lu ret=%d relaunch=%u\n",
         (unsigned long)(unifrog_perf_time_ms() - frontend_start_ms),
         ret, launch_count - 1u);
      if (ret == 0)
         ret = js2300_runtime_run(runtime);
      js2300_runtime_destroy(runtime);
      unifrog_log_set_auto_flush_bytes(old_auto_flush);
      printf("unifrog js done ret=%d action=%s path=%s\n",
         ret, frontend.action, frontend.path);

      if (ret == 0 && frontend.action[0])
         ret = run_requested_action(&frontend);
      else
         unifrog_log_flush();
   } while (ret == 0 && frontend.relaunch);

   frontend_icon_cache_clear(&frontend);
   unifrog_fb_close(&frontend.fb);
   return ret;
}
