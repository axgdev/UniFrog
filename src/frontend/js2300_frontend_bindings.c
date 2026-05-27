#include "js2300_frontend_internal.h"

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
   usleep((useconds_t)ms * 1000u);
}

void host_video_clear(void *opaque, uint16_t color)
{
   (void)opaque;
   (void)color;
}

void host_video_rects(void *opaque, const struct js2300_rect *rects,
   size_t count)
{
   (void)opaque;
   (void)rects;
   (void)count;
}

void host_video_text(void *opaque, int x, int y, const char *text,
   uint16_t color)
{
   (void)opaque;
   (void)x;
   (void)y;
   (void)color;
   if (text && text[0])
      printf("js2300 video_text text=%s\n", text);
}

int host_video_image(void *opaque, const char *path, int x, int y, int w,
   int h)
{
   (void)opaque;
   (void)path;
   (void)x;
   (void)y;
   (void)w;
   (void)h;
   return -1;
}

void host_video_present(void *opaque)
{
   (void)opaque;
}

int host_video_font(void *opaque, const char *path)
{
   (void)opaque;
   (void)path;
   return 0;
}

uint32_t host_input_poll(void *opaque)
{
   (void)opaque;
   return 0;
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
         (int)(frontend->battery.bars * 25u) : -1;
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
   dir = opendir(path);
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
         stat(full, &st) == 0 && S_ISDIR(st.st_mode) ? 1 : 0;
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

int host_fs_write_text(void *opaque, const char *path,
   const char *text, size_t size)
{
   FILE *file;
   size_t wrote;

   (void)opaque;
   if (!path || !text)
      return -1;
   file = fopen(path, "wb");
   if (!file)
      return -1;
   wrote = fwrite(text, 1, size, file);
   if (fclose(file) != 0)
      return -1;
   return wrote == size ? 0 : -1;
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
