#include "js2300_frontend_internal.h"

#define BOOT_LOGO_WIDTH 256u
#define BOOT_LOGO_HEIGHT 100u
#define BOOT_LOGO_BACKLIGHT 70u

#include "../../assets/boot/unifrog-logo-rgb565.inc"

static void frontend_present_boot_logo(struct js2300_frontend *frontend)
{
   struct unifrog_surface surface;
   unsigned x0;
   unsigned y0;
   unsigned x;
   unsigned y;
   unsigned pos = 0;
   unsigned i;
   uint16_t *dst;
   int ret;

   if (!frontend || !frontend->fb.pixels ||
       frontend->fb.width < BOOT_LOGO_WIDTH ||
       frontend->fb.height < BOOT_LOGO_HEIGHT)
      return;

   frontend->draw_buffer = frontend->fb.current_buffer;
   surface = unifrog_fb_surface_for_buffer(&frontend->fb, frontend->draw_buffer);
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height, 0);
   x0 = (surface.width - BOOT_LOGO_WIDTH) / 2u;
   y0 = (surface.height - BOOT_LOGO_HEIGHT) / 2u;
   for (i = 0; i + 1 < UNIFROG_BOOT_LOGO_RLE_WORDS; i += 2u) {
      uint16_t color = unifrog_boot_logo_rle[i];
      unsigned count = unifrog_boot_logo_rle[i + 1u];
      while (count-- && pos < BOOT_LOGO_WIDTH * BOOT_LOGO_HEIGHT) {
         x = pos % BOOT_LOGO_WIDTH;
         y = pos / BOOT_LOGO_WIDTH;
         dst = surface.pixels + (y0 + y) * surface.stride + x0;
         dst[x] = color;
         pos++;
      }
   }

   unifrog_fb_flush_buffer(&frontend->fb, frontend->draw_buffer);
   (void)unifrog_fb_pan(&frontend->fb, frontend->draw_buffer);
   frontend->frame_open = 0;
   (void)unifrog_av_set_mode(0);
   ret = unifrog_backlight_set(BOOT_LOGO_BACKLIGHT);
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_BOOT_LOGO_DONE,
      unifrog_perf_time_ms(), BOOT_LOGO_BACKLIGHT, (uint32_t)ret);
   unifrog_boot_trace_log("boot.logo_done");
   printf("unifrog boot_logo shown %ux%u backlight=%u ret=%d\n",
      BOOT_LOGO_WIDTH, BOOT_LOGO_HEIGHT, BOOT_LOGO_BACKLIGHT, ret);
}

int frontend_fb_open(struct js2300_frontend *frontend)
{
   unsigned i;

   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_FB_OPEN_BEGIN,
      unifrog_perf_time_ms(), 0, 0);
   if (unifrog_fb_open(&frontend->fb, UNIFROG_FB_OPEN_DEFAULT) != 0) {
      printf("unifrog js fb open failed\n");
      return -1;
   }
   if (unifrog_fb_set_buffer_count(&frontend->fb, 2) != 0)
      (void)unifrog_fb_set_buffer_count(&frontend->fb, 1);
   for (i = 0; i < frontend->fb.buffer_count; i++) {
      struct unifrog_surface surface =
         unifrog_fb_surface_for_buffer(&frontend->fb, i);
      unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height, 0);
      unifrog_fb_flush_buffer(&frontend->fb, i);
   }
   (void)unifrog_fb_pan(&frontend->fb, frontend->fb.current_buffer);
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_FB_CLEAR_DONE,
      unifrog_perf_time_ms(), frontend->fb.width, frontend->fb.height);
   unifrog_boot_trace_log("boot.fb_clear_done");
   frontend->draw_buffer = frontend->fb.current_buffer;
   frontend->frame_open = 0;
   frontend_present_boot_logo(frontend);
   printf("unifrog js fb ready %ux%u stride=%u buffers=%u\n",
      frontend->fb.width, frontend->fb.height, frontend->fb.stride_pixels,
      frontend->fb.buffer_count);
   return 0;
}

void frontend_fb_reopen(struct js2300_frontend *frontend, const char *tag)
{
   unifrog_fb_close(&frontend->fb);
   if (frontend_fb_open(frontend) == 0) {
      printf("unifrog js fb reopen tag=%s ret=0\n", tag ? tag : "none");
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

void host_log(void *opaque, const char *message)
{
   (void)opaque;
   printf("js2300: %s\n", message ? message : "");
}

static unsigned frontend_storage_attempts(void)
{
   return UNIFROG_SD_EXPERIMENTAL ? 3u : 1u;
}

static int frontend_recover_storage(const char *tag)
{
   if (!UNIFROG_SD_EXPERIMENTAL)
      return -1;
   return unifrog_platform_recover_storage(tag, 4, 100);
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
   if (ms > 1000)
      ms = 1000;
   if (ms)
      msleep(ms);
}

void host_video_clear(void *opaque, uint16_t color)
{
   struct js2300_frontend *frontend = opaque;
   struct unifrog_surface surface;

   frontend_begin_frame(frontend);
   surface = frontend_surface(frontend);
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height, color);
}

void host_video_rects(void *opaque, const struct js2300_rect *rects,
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

void host_video_text(void *opaque, int x, int y, const char *text,
   uint16_t color)
{
   struct js2300_frontend *frontend = opaque;
   struct unifrog_surface surface;

   frontend_begin_frame(frontend);
   surface = frontend_surface(frontend);
   unifrog_gfx_draw_text(&surface, x, y, text, color, 1);
}

void frontend_icon_cache_clear(struct js2300_frontend *frontend)
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

int host_video_image(void *opaque, const char *path,
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

void host_video_present(void *opaque)
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

int host_video_font(void *opaque, const char *path)
{
   int ret;
   (void)opaque;

   ret = unifrog_gfx_load_font5x7_file(path);
   printf("js2300 font load path=%s ret=%d\n", path ? path : "?", ret);
   return ret;
}

uint32_t host_input_poll(void *opaque)
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

void host_battery(void *opaque, struct js2300_battery_status *status)
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

int host_backlight(void *opaque, int level, int *out_level)
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

int host_av_output(void *opaque, int mode, int *out_mode)
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

int host_fs_list(void *opaque, const char *path,
   struct js2300_fs_entry *entries, size_t max_entries)
{
   DIR *dir;
   struct dirent *entry;
   int count = 0;
   uint32_t start = unifrog_perf_time_ms();
   unsigned attempts = frontend_storage_attempts();
   (void)opaque;

   if (!path || !entries || max_entries == 0)
      return -1;
   dir = NULL;
   for (unsigned attempt = 0; attempt < attempts && !dir; attempt++) {
      dir = opendir(path);
      if (!dir && attempt + 1u < attempts) {
         printf("js2300 fs list open_fail path=%s attempt=%u errno=%d\n",
            path, attempt + 1u, errno);
         (void)frontend_recover_storage("js_fs_list");
      }
   }
   if (!dir) {
      printf("js2300 fs list open_fail path=%s attempts=%u errno=%d\n",
         path, attempts, errno);
      return -1;
   }
   while ((entry = readdir(dir)) != NULL && (size_t)count < max_entries) {
      char full[JS2300_FRONTEND_MAX_PATH];
      int is_dir;

      if (frontend_dirent_is_dot(entry))
         continue;
      if (frontend_path_join_checked(full, sizeof(full), path,
          entry->d_name) != 0)
         continue;
      is_dir = frontend_dirent_is_dir(entry, full);
      if (is_dir < 0)
         continue;
      unifrog_text_copy(entries[count].name, sizeof(entries[count].name),
         entry->d_name);
      entries[count].is_dir = is_dir ? 1 : 0;
      count++;
   }
   closedir(dir);
   if ((uint32_t)(unifrog_perf_time_ms() - start) >= 50u ||
       (size_t)count == max_entries)
      printf("js2300 fs list path=%s count=%d ms=%lu capped=%d\n",
         path, count, (unsigned long)(unifrog_perf_time_ms() - start),
         (size_t)count == max_entries ? 1 : 0);
   return count;
}

int host_fs_read_text(void *opaque, const char *path,
   char *out, size_t out_size)
{
   FILE *file;
   size_t got;
   uint32_t start = unifrog_perf_time_ms();
   unsigned attempts = frontend_storage_attempts();
   (void)opaque;

   if (!path || !out || out_size == 0)
      return -1;

   for (unsigned attempt = 0; attempt < attempts; attempt++) {
      file = fopen(path, "rb");
      if (!file) {
         printf("js2300 fs read_text open_fail path=%s attempt=%u errno=%d ms=%lu\n",
            path, attempt + 1u, errno,
            (unsigned long)(unifrog_perf_time_ms() - start));
         if (attempt + 1u < attempts)
            (void)frontend_recover_storage("js_read_text_open");
         continue;
      }
      got = fread(out, 1, out_size - 1, file);
      if (!ferror(file)) {
         out[got] = '\0';
         fclose(file);
         printf("js2300 fs read_text path=%s bytes=%u cap=%u ms=%lu attempts=%u\n",
            path, (unsigned)got, (unsigned)out_size,
            (unsigned long)(unifrog_perf_time_ms() - start),
            attempt + 1u);
         return (int)got;
      }
      fclose(file);
      printf("js2300 fs read_text read_fail path=%s bytes=%u attempt=%u ms=%lu\n",
         path, (unsigned)got, attempt + 1u,
         (unsigned long)(unifrog_perf_time_ms() - start));
      if (attempt + 1u < attempts)
         (void)frontend_recover_storage("js_read_text_read");
   }

   return -1;
}

static int frontend_write_text_once(const char *path,
   const char *text, size_t size)
{
   char tmp[JS2300_FRONTEND_MAX_PATH + 8];
   FILE *file;
   int ret = -1;

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

int host_fs_write_text(void *opaque, const char *path,
   const char *text, size_t size)
{
   uint32_t start = unifrog_perf_time_ms();
   unsigned attempts = frontend_storage_attempts();
   int ret = -1;
   (void)opaque;

   if (!path || !path[0] || !text)
      return -1;

   for (unsigned attempt = 0; attempt < attempts; attempt++) {
      ret = frontend_write_text_once(path, text, size);
      if (ret == 0) {
         printf("js2300 fs write_text path=%s bytes=%u ms=%lu attempts=%u ret=0\n",
            path, (unsigned)size,
            (unsigned long)(unifrog_perf_time_ms() - start),
            attempt + 1u);
         return 0;
      }
      if (attempt + 1u < attempts)
         (void)frontend_recover_storage("js_write_text");
   }

   printf("js2300 fs write_text path=%s bytes=%u ms=%lu attempts=%u ret=%d\n",
      path, (unsigned)size,
      (unsigned long)(unifrog_perf_time_ms() - start), attempts, ret);
   return ret;
}

static int frontend_tmp_path(char *tmp, size_t tmp_size, const char *path)
{
   int len;

   if (!tmp || tmp_size == 0 || !path || !path[0])
      return -1;
   len = snprintf(tmp, tmp_size, "%s.tmp", path);
   return len > 0 && (size_t)len < tmp_size ? 0 : -1;
}

static int host_fs_index_once(const char *root,
   const char *game_index_path, const char *media_index_path,
   struct js2300_fs_index_result *result)
{
   char game_tmp[JS2300_FRONTEND_MAX_PATH + 8u] = "";
   char media_tmp[JS2300_FRONTEND_MAX_PATH + 8u] = "";
   FILE *game_file = NULL;
   FILE *media_file = NULL;
   struct frontend_index_scan scan;
   uint32_t start = unifrog_perf_time_ms();
   int ret = -1;

   if (!root || !root[0] || !game_index_path || !media_index_path || !result)
      return -1;
   memset(result, 0, sizeof(*result));
   if (frontend_tmp_path(game_tmp, sizeof(game_tmp), game_index_path) != 0 ||
       frontend_tmp_path(media_tmp, sizeof(media_tmp), media_index_path) != 0)
      goto done;

   game_file = fopen(game_tmp, "wb");
   if (!game_file) {
      printf("js2300 fs index game_open_fail path=%s errno=%d\n",
         game_tmp, errno);
      goto done;
   }
   media_file = fopen(media_tmp, "wb");
   if (!media_file) {
      printf("js2300 fs index media_open_fail path=%s errno=%d\n",
         media_tmp, errno);
      goto done;
   }

   memset(&scan, 0, sizeof(scan));
   scan.game_file = game_file;
   scan.media_file = media_file;
   scan.result = result;
   ret = frontend_index_scan_dir(root, 0, &scan);
   if (fclose(game_file) != 0)
      ret = -1;
   game_file = NULL;
   if (fclose(media_file) != 0)
      ret = -1;
   media_file = NULL;
   if (ret != 0)
      goto done;

   unlink(game_index_path);
   if (rename(game_tmp, game_index_path) != 0) {
      printf("js2300 fs index game_rename_fail path=%s errno=%d\n",
         game_index_path, errno);
      ret = -1;
      goto done;
   }
   unlink(media_index_path);
   if (rename(media_tmp, media_index_path) != 0) {
      printf("js2300 fs index media_rename_fail path=%s errno=%d\n",
         media_index_path, errno);
      ret = -1;
      goto done;
   }

done:
   if (game_file && fclose(game_file) != 0)
      ret = -1;
   if (media_file && fclose(media_file) != 0)
      ret = -1;
   if (ret != 0) {
      if (game_tmp[0])
         unlink(game_tmp);
      if (media_tmp[0])
         unlink(media_tmp);
   }
   result->ms = unifrog_perf_time_ms() - start;
   printf("js2300 fs index root=%s games=%lu media=%lu files=%lu dirs=%lu ms=%lu truncated=%lu ret=%d\n",
      root ? root : "?",
      (unsigned long)result->games, (unsigned long)result->media,
      (unsigned long)result->files, (unsigned long)result->dirs,
      (unsigned long)result->ms, (unsigned long)result->truncated, ret);
   return ret;
}

int host_fs_index(void *opaque, const char *root,
   const char *game_index_path, const char *media_index_path,
   struct js2300_fs_index_result *result)
{
   uint32_t start = unifrog_perf_time_ms();
   unsigned attempts = frontend_storage_attempts();
   int ret = -1;
   (void)opaque;

   for (unsigned attempt = 0; attempt < attempts; attempt++) {
      ret = host_fs_index_once(root, game_index_path, media_index_path,
         result);
      if (ret == 0) {
         if (attempt > 0)
            printf("js2300 fs index recovered attempts=%u total_ms=%lu\n",
               attempt + 1u,
               (unsigned long)(unifrog_perf_time_ms() - start));
         return 0;
      }
      if (attempt + 1u < attempts)
         (void)frontend_recover_storage("js_fs_index");
   }

   printf("js2300 fs index failed attempts=%u total_ms=%lu ret=%d\n",
      attempts, (unsigned long)(unifrog_perf_time_ms() - start), ret);
   return ret;
}
