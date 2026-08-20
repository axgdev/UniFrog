#include "internal.h"

void host_log(void *opaque, const char *message)
{
   (void)opaque;
   printf("js2300: %s\n", message ? message : "");
}

int host_flush_log(void *opaque)
{
   (void)opaque;
   return unifrog_log_flush();
}

uint32_t host_millis(void *opaque)
{
   (void)opaque;
   return unifrog_perf_time_ms();
}

void host_sleep(void *opaque, uint32_t ms)
{
   (void)opaque;
   unifrog_perf_delay_us(ms * 1000u);
}

void host_video_clear(void *opaque, uint16_t color)
{
   struct js2300_frontend *frontend = opaque;

   if (!frontend)
      return;
   if (!frontend->ui_open) {
      if (unifrog_ui_open(&frontend->ui, 0) != 0)
         return;
      frontend->ui_open = 1;
   }
   unifrog_ui_begin(&frontend->ui, color);
}

void host_video_rects(void *opaque, const struct js2300_rect *rects,
   size_t count)
{
   struct js2300_frontend *frontend = opaque;

   if (!frontend || !frontend->ui_open || !rects)
      return;
   for (size_t i = 0; i < count; i++)
      unifrog_ui_rect(&frontend->ui, rects[i].x, rects[i].y,
         rects[i].w, rects[i].h, rects[i].color);
}

void host_video_text(void *opaque, int x, int y, const char *text,
   uint16_t color)
{
   struct js2300_frontend *frontend = opaque;

   if (text && text[0] && (!frontend || !frontend->suppress_video_text_log))
      printf("js2300 video_text text=%s\n", text);
   if (!frontend || !frontend->ui_open || !text)
      return;
   unifrog_ui_text_clipped(&frontend->ui, x, y, 44, text, color, 1);
}

int host_video_image(void *opaque, const char *path, int x, int y, int w,
   int h)
{
   struct js2300_frontend *frontend = opaque;
   unifrog_image image;
   struct unifrog_surface surface;

   if (!frontend || !path || !path[0])
      return -1;
   if (!frontend->ui_open) {
      if (unifrog_ui_open(&frontend->ui, 0) != 0)
         return -1;
      frontend->ui_open = 1;
   }
   if (!frontend->ui.frame_open)
      unifrog_ui_begin(&frontend->ui, 0);
   memset(&image, 0, sizeof(image));
   if (unifrog_image_load_file(path, &image) != 0)
      return -1;
   surface = unifrog_ui_surface(&frontend->ui);
   if (!frontend->ui.ge_ready || !unifrog_png_is_opaque(&image)) {
      unifrog_png_draw(&surface, &image, x, y, w, h);
   } else {
      struct unifrog_ge_surface dst = unifrog_fb_ge_surface_for_buffer(
         &frontend->ui.fb, frontend->ui.draw_buffer);

      if (unifrog_png_draw_ge(&frontend->ui.ge, &dst, &image,
          x, y, w, h) != 0)
         unifrog_png_draw(&surface, &image, x, y, w, h);
   }
   unifrog_image_free(&image);
   return 0;
}

void host_video_present(void *opaque)
{
   struct js2300_frontend *frontend = opaque;

   if (frontend && frontend->ui_open)
      unifrog_ui_present(&frontend->ui);
}

int host_video_font(void *opaque, const char *path)
{
   (void)opaque;
   return path && path[0] ? unifrog_gfx_load_font5x7_file(path) : -1;
}

uint32_t host_input_poll(void *opaque)
{
   uint32_t buttons;
   uint32_t result = 0;

   (void)opaque;
   unifrog_input_poll();
   buttons = unifrog_input_buttons();
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP)) result |= 1u << 0;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN)) result |= 1u << 1;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT)) result |= 1u << 2;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT)) result |= 1u << 3;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A)) result |= 1u << 4;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B)) result |= 1u << 5;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_X)) result |= 1u << 6;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_Y)) result |= 1u << 7;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_L)) result |= 1u << 8;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_R)) result |= 1u << 9;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT)) result |= 1u << 10;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START)) result |= 1u << 11;
   return result;
}

void host_battery(void *opaque, struct js2300_battery_status *status)
{
   struct js2300_frontend *frontend = opaque;

   if (!status)
      return;
   status->percent = -1;
   status->charging = 0;
   status->low = 0;
   if (frontend && unifrog_battery_update(&frontend->battery, 0) == 0) {
      status->percent = frontend->battery.available ?
         (int)frontend->battery.percent : -1;
      status->low = frontend->battery.low;
   }
}

int host_backlight(void *opaque, int level, int *out_level)
{
   unsigned current = 0;
   int ret = 0;

   (void)opaque;
   if (level >= 0)
      ret = unifrog_backlight_set((unsigned)level);
   if (unifrog_backlight_get(&current) != 0)
      return ret == 0 ? -1 : ret;
   if (out_level)
      *out_level = (int)current;
   return ret;
}

int host_av_output(void *opaque, int mode, int *out_mode)
{
   int current = 0;
   int ret = 0;

   (void)opaque;
   if (mode >= 0)
      ret = unifrog_av_set_mode(mode);
   if (unifrog_av_get_mode(&current) != 0)
      return ret == 0 ? -1 : ret;
   if (out_mode)
      *out_mode = current;
   return ret;
}

int host_fs_list(void *opaque, const char *path,
   struct js2300_fs_entry *entries, size_t max_entries)
{
   DIR *dir;
   struct dirent *entry;
   size_t count = 0;

   (void)opaque;
   if (!path || !entries || max_entries == 0)
      return -1;
   dir = unifrog_storage_opendir_resilient(path, "js2300_list", 3, 150);
   if (!dir)
      return -1;
   while ((entry = readdir(dir)) != NULL && count < max_entries) {
      char full[JS2300_FRONTEND_MAX_PATH];
      struct stat st;

      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0)
         continue;
      unifrog_text_copy(entries[count].name, sizeof(entries[count].name),
         entry->d_name);
      if (snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >=
          (int)sizeof(full)) {
         entries[count].is_dir = 0;
         count++;
         continue;
      }
      entries[count].is_dir =
         unifrog_storage_stat_resilient(full, &st, "js2300_list_stat", 3,
            150) == 0 && S_ISDIR(st.st_mode) ? 1 : 0;
      count++;
   }
   closedir(dir);
   return (int)count;
}

int host_fs_read_text(void *opaque, const char *path,
   char *out, size_t out_size)
{
   FILE *file;
   size_t got;

   (void)opaque;
   if (!path || !out || out_size == 0)
      return -1;
   file = unifrog_storage_fopen_resilient(path, "rb", "js2300_read_text",
      3, 150);
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

int host_fs_read_bytes(void *opaque, const char *path,
   uint8_t *out, size_t out_size)
{
   FILE *file;
   size_t got;

   (void)opaque;
   if (!path || !out || out_size == 0)
      return -1;
   file = unifrog_storage_fopen_resilient(path, "rb", "js2300_read_bytes",
      3, 150);
   if (!file)
      return -1;
   got = fread(out, 1, out_size, file);
   if (ferror(file)) {
      fclose(file);
      return -1;
   }
   fclose(file);
   return (int)got;
}

int host_fs_write_text(void *opaque, const char *path,
   const char *text, size_t size)
{
   (void)opaque;
   if (!path || !text)
      return -1;
   return unifrog_storage_write_atomic(path, NULL, text, size,
      "js2300_write_text", 3, 150);
}

int host_fs_write_bytes(void *opaque, const char *path,
   const uint8_t *data, size_t size)
{
   (void)opaque;
   if (!path || !data)
      return -1;
   return unifrog_storage_write_atomic(path, NULL, data, size,
      "js2300_write_bytes", 3, 150);
}

int host_fs_stat(void *opaque, const char *path,
   struct js2300_fs_stat *status)
{
   struct stat st;

   (void)opaque;
   if (!path || !status)
      return -1;
   memset(status, 0, sizeof(*status));
   if (unifrog_storage_stat_resilient(path, &st, "js2300_stat", 3, 150) != 0)
      return -1;
   status->exists = 1;
   status->is_dir = S_ISDIR(st.st_mode) ? 1u : 0u;
   status->is_file = S_ISREG(st.st_mode) ? 1u : 0u;
   status->size = st.st_size > 0 ? (uint64_t)st.st_size : 0;
   status->modified_time = (int64_t)st.st_mtime;
   return 0;
}

int host_fs_mkdir(void *opaque, const char *path)
{
   struct stat st;

   (void)opaque;
   if (!path || !path[0])
      return -1;
   if (mkdir(path, 0777) == 0)
      return 0;
   return errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode) ?
      0 : -1;
}

int host_fs_remove(void *opaque, const char *path)
{
   struct stat st;

   (void)opaque;
   if (!path || !path[0] || stat(path, &st) != 0)
      return -1;
   return S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path);
}

int host_fs_rename(void *opaque, const char *from, const char *to)
{
   (void)opaque;
   return from && from[0] && to && to[0] ? rename(from, to) : -1;
}

int host_cpu_clock(void *opaque, int mhz, int *out_mhz)
{
   struct js2300_frontend *frontend = opaque;
   int ret = 0;

   if (!frontend || !unifrog_scpu_supported())
      return -1;
   if (mhz > 0) {
      if (!frontend->scpu_restore_valid)
         frontend->scpu_restore_valid =
            unifrog_scpu_capture(&frontend->scpu_restore) == 0 &&
            frontend->scpu_restore.valid;
      ret = unifrog_scpu_apply_mhz((unsigned)mhz);
   }
   if (out_mhz)
      *out_mhz = (int)unifrog_scpu_current_mhz();
   return ret;
}

static int fs_index_scan(const char *dir, unsigned depth,
   struct js2300_fs_index_result *result)
{
   DIR *handle;
   struct dirent *entry;

   if (depth > 12)
      return 0;
   handle = opendir(dir);
   if (!handle)
      return -1;
   result->dirs++;
   while ((entry = readdir(handle)) != NULL) {
      char full[JS2300_FRONTEND_MAX_PATH];
      struct stat st;

      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0)
         continue;
      if (snprintf(full, sizeof(full), "%s/%s", dir, entry->d_name) >=
          (int)sizeof(full)) {
         result->truncated = 1;
         continue;
      }
      if (stat(full, &st) != 0)
         continue;
      if (S_ISDIR(st.st_mode)) {
         (void)fs_index_scan(full, depth + 1, result);
      } else if (S_ISREG(st.st_mode)) {
         result->files++;
         result->games++;
      }
   }
   closedir(handle);
   return 0;
}

int host_fs_index(void *opaque, const char *root,
   const char *game_index_path, const char *media_index_path,
   struct js2300_fs_index_result *result)
{
   uint32_t start;

   (void)opaque;
   (void)game_index_path;
   (void)media_index_path;
   if (!root || !result)
      return -1;
   memset(result, 0, sizeof(*result));
   start = unifrog_perf_time_ms();
   if (fs_index_scan(root, 0, result) != 0)
      return -1;
   result->ms = unifrog_perf_time_ms() - start;
   return 0;
}
