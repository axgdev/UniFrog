#include "js2300_frontend_internal.h"

static void frontend_fb_set_handoff_buffers(struct unifrog_fb *fb,
   int preserve_logo)
{
   if (preserve_logo && fb->buffer_count >= 2)
      return;
   if (unifrog_fb_set_buffer_count(fb, 2) != 0)
      (void)unifrog_fb_set_buffer_count(fb, 1);
}

static void frontend_fb_clear_buffers(struct unifrog_fb *fb)
{
   for (unsigned i = 0; i < fb->buffer_count; i++) {
      struct unifrog_surface surface = unifrog_fb_surface_for_buffer(fb, i);

      unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height, 0);
      unifrog_fb_flush_buffer(fb, i);
   }
}

static void frontend_video_state_reset(struct js2300_frontend *frontend)
{
   frontend->draw_buffer = frontend->fb.current_buffer;
   frontend->frame_open = 0;
   frontend->frame_draw_ops = 0;
   frontend->frame_has_visible_content = 0;
   frontend->boot_logo_present_skips = 0;
   frontend->pending_backlight_valid = 0;
   frontend->pending_av_valid = 0;
   frontend->video_present_count = 0;
   frontend->video_present_log_count = 0;
}

int frontend_fb_open(struct js2300_frontend *frontend)
{
   int preserve_logo;
   unsigned flags;

   preserve_logo = unifrog_boot_logo_is_active();
   flags = preserve_logo ? UNIFROG_FB_OPEN_PRESERVE : UNIFROG_FB_OPEN_DEFAULT;
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_FB_OPEN_BEGIN,
      unifrog_perf_time_ms(), (uint32_t)preserve_logo, 0);
   if (unifrog_fb_open(&frontend->fb, flags) != 0) {
      printf("unifrog js fb open failed\n");
      return -1;
   }
   frontend_fb_set_handoff_buffers(&frontend->fb, preserve_logo);
   if (!preserve_logo) {
      frontend_fb_clear_buffers(&frontend->fb);
      (void)unifrog_fb_pan(&frontend->fb, frontend->fb.current_buffer);
   }
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_FB_CLEAR_DONE,
      unifrog_perf_time_ms(), frontend->fb.width,
      (frontend->fb.height << 16) | (uint32_t)preserve_logo);
   unifrog_boot_trace_log("boot.fb_clear_done");
   frontend_video_state_reset(frontend);
   if (preserve_logo) {
      unifrog_boot_logo_release_early();
      printf("unifrog boot_logo preserved current=%u buffers=%u\n",
         frontend->fb.current_buffer, frontend->fb.buffer_count);
   } else {
      (void)unifrog_boot_logo_present(&frontend->fb, "frontend");
   }
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
   frontend->frame_draw_ops = 0;
   frontend->frame_has_visible_content = 0;
}

static void frontend_apply_deferred_display(struct js2300_frontend *frontend)
{
   if (!frontend)
      return;
   if (frontend->pending_av_valid) {
      int mode = frontend->pending_av_mode;
      int ret;

      frontend->pending_av_valid = 0;
      ret = unifrog_av_set_mode(mode);
      printf("unifrog boot_logo deferred av_output mode=%d ret=%d\n",
         mode, ret);
   }
   if (frontend->pending_backlight_valid) {
      unsigned level = frontend->pending_backlight_level;
      int ret;

      frontend->pending_backlight_valid = 0;
      ret = unifrog_backlight_set(level);
      printf("unifrog boot_logo deferred backlight level=%u ret=%d\n",
         level, ret);
   }
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
   frontend->frame_draw_ops++;
   if (color != 0)
      frontend->frame_has_visible_content = 1;
   surface = frontend_surface(frontend);
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height, color);
}

void host_video_rects(void *opaque, const struct js2300_rect *rects,
   size_t count)
{
   struct js2300_frontend *frontend = opaque;
   struct unifrog_surface surface;

   frontend_begin_frame(frontend);
   frontend->frame_draw_ops += (unsigned)count;
   surface = frontend_surface(frontend);
   for (size_t i = 0; i < count; i++) {
      if (rects[i].w > 0 && rects[i].h > 0 && rects[i].color != 0)
         frontend->frame_has_visible_content = 1;
      unifrog_gfx_fill_rect(&surface, rects[i].x, rects[i].y,
         rects[i].w, rects[i].h, rects[i].color);
   }
}

void host_video_text(void *opaque, int x, int y, const char *text,
   uint16_t color)
{
   struct js2300_frontend *frontend = opaque;
   struct unifrog_surface surface;

   frontend_begin_frame(frontend);
   frontend->frame_draw_ops++;
   if (text && text[0] && color != 0)
      frontend->frame_has_visible_content = 1;
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
   frontend->frame_draw_ops++;
   frontend->frame_has_visible_content = 1;
   surface = frontend_surface(frontend);
   unifrog_png_draw(&surface, &icon->image, x, y, w, h);
   return 0;
}

void host_video_present(void *opaque)
{
   struct js2300_frontend *frontend = opaque;
   uint32_t start_ms;
   uint32_t flush_ms;
   uint32_t wait_ms;
   uint32_t pan_ms;
   uint32_t done_ms;
   int pan_ret = -1;
   int replaced = 0;
   int logo_active;
   unsigned present_index;

   if (!frontend->frame_open) {
      unifrog_fb_wait_vsync(&frontend->fb);
      return;
   }
   present_index = ++frontend->video_present_count;
   if (unifrog_boot_logo_is_active() &&
       !frontend->frame_has_visible_content) {
      if (frontend->boot_logo_present_skips < 4)
         printf("unifrog boot_logo keep skip_present=%u ops=%u buffer=%u\n",
            frontend->boot_logo_present_skips + 1u,
            frontend->frame_draw_ops, frontend->draw_buffer);
      frontend->boot_logo_present_skips++;
      frontend->frame_open = 0;
      return;
   }
   start_ms = unifrog_perf_time_ms();
   logo_active = unifrog_boot_logo_is_active();
   unifrog_fb_flush_buffer(&frontend->fb, frontend->draw_buffer);
   flush_ms = unifrog_perf_time_ms();
   unifrog_fb_wait_vsync(&frontend->fb);
   wait_ms = unifrog_perf_time_ms();
   pan_ret = unifrog_fb_pan(&frontend->fb, frontend->draw_buffer);
   pan_ms = unifrog_perf_time_ms();
   if (pan_ret == 0 && unifrog_boot_logo_is_active()) {
      uint32_t logo_ms = unifrog_boot_logo_shown_ms();

      unifrog_boot_logo_mark_replaced();
      replaced = 1;
      printf("unifrog boot_logo replaced buffer=%u ops=%u\n",
         frontend->draw_buffer, frontend->frame_draw_ops);
      printf("unifrog boot_perf phase=logo_to_ui logo_age_ms=%lu "
         "frontend_ms=%lu ops=%u buffer=%u\n",
         logo_ms ? (unsigned long)(pan_ms - logo_ms) : 0ul,
         (unsigned long)(pan_ms - frontend->frontend_start_ms),
         frontend->frame_draw_ops, frontend->draw_buffer);
      frontend_apply_deferred_display(frontend);
   }
   done_ms = unifrog_perf_time_ms();
   if (replaced || frontend->video_present_log_count < 4) {
      printf("unifrog video_present n=%u ops=%u visible=%d buffer=%u "
         "logo_before=%d replaced=%d ret=%d total_ms=%lu flush_ms=%lu "
         "wait_ms=%lu pan_ms=%lu deferred_ms=%lu frontend_ms=%lu\n",
         present_index, frontend->frame_draw_ops,
         frontend->frame_has_visible_content, frontend->draw_buffer,
         logo_active, replaced, pan_ret,
         (unsigned long)(done_ms - start_ms),
         (unsigned long)(flush_ms - start_ms),
         (unsigned long)(wait_ms - flush_ms),
         (unsigned long)(pan_ms - wait_ms),
         (unsigned long)(done_ms - pan_ms),
         (unsigned long)(done_ms - frontend->frontend_start_ms));
      frontend->video_present_log_count++;
   }
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
   struct js2300_frontend *frontend = opaque;
   unsigned current = 0;
   int ret = 0;

   if (level >= 0) {
      if (level > 100)
         level = 100;
      if (frontend && unifrog_boot_logo_is_active()) {
         frontend->pending_backlight_level = (unsigned)level;
         frontend->pending_backlight_valid = 1;
      } else {
         ret = unifrog_backlight_set((unsigned)level);
      }
   }
   if (unifrog_backlight_get(&current) != 0) {
      if (level >= 0)
         printf("js2300 backlight request=%d ret=%d get_failed=1\n",
            level, ret);
      return -1;
   }
   if (out_level)
      *out_level = (int)current;
   if (level >= 0) {
      if (frontend && frontend->pending_backlight_valid &&
          frontend->pending_backlight_level == (unsigned)level)
         printf("js2300 backlight request=%d ret=%d current=%u deferred=1\n",
            level, ret, current);
      else
         printf("js2300 backlight request=%d ret=%d current=%u\n",
            level, ret, current);
   }
   return ret;
}

int host_av_output(void *opaque, int mode, int *out_mode)
{
   struct js2300_frontend *frontend = opaque;
   int current = 0;
   int ret = 0;

   if (mode >= 0) {
      if (frontend && unifrog_boot_logo_is_active()) {
         frontend->pending_av_mode = mode;
         frontend->pending_av_valid = 1;
      } else {
         ret = unifrog_av_set_mode(mode);
      }
   }
   if (unifrog_av_get_mode(&current) != 0) {
      if (mode >= 0)
         printf("js2300 av_output request=%d ret=%d get_failed=1\n",
            mode, ret);
      return -1;
   }
   if (out_mode)
      *out_mode = current;
   if (mode >= 0) {
      if (frontend && frontend->pending_av_valid &&
          frontend->pending_av_mode == mode)
         printf("js2300 av_output request=%d ret=%d current=%d deferred=1\n",
            mode, ret, current);
      else
         printf("js2300 av_output request=%d ret=%d current=%d\n",
            mode, ret, current);
   }
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
