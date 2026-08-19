#include "internal.h"

#include <unifrog/hc_mmc_host_swap.h>

static void draw_native_toast(const char *message)
{
   struct unifrog_fb fb;
   struct unifrog_surface surface;
   unsigned buffer;
   char text[72];

   if (!message || !message[0])
      return;
   memset(&fb, 0, sizeof(fb));
   if (unifrog_fb_open(&fb, UNIFROG_FB_OPEN_PRESERVE) != 0)
      return;
   buffer = fb.current_buffer;
   surface = unifrog_fb_surface_for_buffer(&fb, buffer);
   unifrog_text_copy(text, sizeof(text), message);
   unifrog_gfx_fill_rect(&surface, 8, (int)surface.height - 42,
      (int)surface.width - 16, 34, UNIFROG_RGB565(0, 0, 0));
   unifrog_gfx_fill_rect(&surface, 10, (int)surface.height - 40,
      (int)surface.width - 20, 30, UNIFROG_RGB565(22, 28, 34));
   unifrog_gfx_fill_rect(&surface, 10, (int)surface.height - 40, 4, 30,
      UNIFROG_RGB565(68, 188, 136));
   unifrog_gfx_draw_text(&surface, 20, (int)surface.height - 31, text,
      UNIFROG_RGB565(236, 241, 246), 1);
   unifrog_fb_flush_buffer(&fb, buffer);
   (void)unifrog_fb_pan(&fb, buffer);
   unifrog_fb_close(&fb);
}

static int run_storage_stress_with_mmc_diag(struct js2300_frontend *frontend,
   const char *profile)
{
   int ret;

   unifrog_platform_sd_mmc_diag_begin("storage_stress");
   ret = run_storage_stress_test(frontend, profile);
   unifrog_platform_sd_mmc_diag_end("storage_stress");
   return ret;
}

static int run_storage_quick_benchmark_with_mmc_diag(
   struct js2300_frontend *frontend)
{
   int ret;

   unifrog_platform_sd_mmc_diag_begin("storage_quick_benchmark");
   ret = run_storage_quick_benchmark(frontend);
   unifrog_platform_sd_mmc_diag_end("storage_quick_benchmark");
   return ret;
}

static int run_storage_quick_modes_with_mmc_diag(
   struct js2300_frontend *frontend)
{
   int ret;

   unifrog_platform_sd_mmc_diag_begin("storage_quick_modes");
   ret = run_storage_quick_modes(frontend);
   unifrog_platform_sd_mmc_diag_end("storage_quick_modes");
   return ret;
}

static int run_storage_speed_matrix_with_mmc_diag(
   struct js2300_frontend *frontend)
{
   int ret;

   unifrog_platform_sd_mmc_diag_begin("storage_speed_matrix");
   ret = run_storage_speed_matrix(frontend);
   unifrog_platform_sd_mmc_diag_end("storage_speed_matrix");
   return ret;
}

static int run_storage_test_with_mmc_diag(struct js2300_frontend *frontend)
{
   int ret;

   unifrog_platform_sd_mmc_diag_begin("storage_test");
   ret = run_storage_test(frontend);
   unifrog_platform_sd_mmc_diag_end("storage_test");
   return ret;
}

static int run_storage_full_test_with_mmc_diag(
   struct js2300_frontend *frontend)
{
   int ret;

   unifrog_platform_sd_mmc_diag_begin("storage_full_test");
   ret = run_storage_full_test(frontend);
   unifrog_platform_sd_mmc_diag_end("storage_full_test");
   return ret;
}

static int run_storage_mode_test_with_mmc_diag(
   struct js2300_frontend *frontend, const char *profile)
{
   int ret;

   unifrog_platform_sd_mmc_diag_begin("storage_mode_test");
   ret = run_storage_mode_test(frontend, profile);
   unifrog_platform_sd_mmc_diag_end("storage_mode_test");
   return ret;
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

static int run_system_check(void)
{
   static const char *required_files[] = {
      "/media/mmcblk0/bios/bisrv.asd",
      "/media/mmcblk0/firmware/unifrog.bin",
      JS2300_FRONTEND_APP_ROOT "/manifest.ini",
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
      JS2300_FRONTEND_APP_ROOT "/cores/js2300.bin",
#if UNIFROG_HCRTOS_MEDIA_MODULE
      JS2300_FRONTEND_HCRTOS_MEDIA_MODULE,
#endif
   };
   char manifest[2048];
   char dirty[16];
   struct system_check_report report;
   unsigned stale = 0;
   unsigned missing = 0;
   int manifest_ret;

   memset(&report, 0, sizeof(report));

   printf("unifrog system_check begin running firmware=%s dirty=%d sdk=%s cores=%s js2300=%s frontend=%s\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY, UNIFROG_SDK_GIT_COMMIT,
      UNIFROG_CORES_GIT_COMMIT, UNIFROG_JS2300_GIT_COMMIT,
      UNIFROG_FRONTEND_GIT_COMMIT);

   for (unsigned i = 0; i < FRONTEND_ARRAY_SIZE(required_files); i++)
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
      system_check_manifest_key(manifest, "hcrtos_media",
         UNIFROG_HCRTOS_MEDIA, &stale, &report);
   }

   printf("unifrog system_check result missing=%u stale=%u\n",
      missing, stale);
   system_check_write_report(&report, missing, stale);
   (void)unifrog_log_flush();
   return 0;
}

static unsigned parse_unsigned_option(const char *text, unsigned fallback)
{
   char *end;
   unsigned long value;

   if (!text || !text[0])
      return fallback;
   value = strtoul(text, &end, 10);
   if (end == text)
      return fallback;
   return value > UINT32_MAX ? UINT32_MAX : (unsigned)value;
}

static void parse_run_option(struct unifrog_libretro_run_options *options,
   const char *key, const char *value)
{
   if (!options || !key || !value)
      return;

   if (strcmp(key, "core") == 0) {
      unifrog_text_copy(options->core_id, sizeof(options->core_id), value);
   } else if (strcmp(key, "audio") == 0) {
      options->audio_enabled = parse_unsigned_option(value,
         options->audio_enabled) ? 1 : 0;
   } else if (strcmp(key, "frames") == 0 ||
       strcmp(key, "max_frames") == 0) {
      options->max_frames = parse_unsigned_option(value,
         options->max_frames);
   } else if (strcmp(key, "frameskip") == 0) {
      options->frameskip = (int)parse_unsigned_option(value,
         (unsigned)options->frameskip);
   } else if (strcmp(key, "scpu") == 0 || strcmp(key, "cpu") == 0) {
      options->scpu_mhz = parse_unsigned_option(value, options->scpu_mhz);
   }
}

static int run_libretro_action(const char *id)
{
   struct unifrog_libretro_run_options options;
   const char *path;
   const char *colon;
   char option_text[160];
   size_t option_len;
   char *part;
   int ret;

   if (!id || strncmp(id, "run+", 4) != 0)
      return -1;
   colon = strchr(id + 4, ':');
   if (!colon || !colon[1])
      return -1;
   option_len = (size_t)(colon - (id + 4));
   if (option_len >= sizeof(option_text))
      option_len = sizeof(option_text) - 1;
   memcpy(option_text, id + 4, option_len);
   option_text[option_len] = '\0';
   path = colon + 1;

   unifrog_libretro_run_options_init(&options);
   for (part = option_text; part && part[0];) {
      char *next = strchr(part, ',');
      char *eq;

      if (next)
         *next++ = '\0';
      eq = strchr(part, '=');
      if (eq) {
         *eq++ = '\0';
         parse_run_option(&options, part, eq);
      }
      part = next;
   }

   printf("js2300 action run begin core=%s audio=%d frames=%u path=%s\n",
      options.core_id[0] ? options.core_id : "auto",
      options.audio_enabled, options.max_frames, path);
   (void)unifrog_log_flush();
   ret = unifrog_libretro_run_path_ex(path, &options);
   printf("js2300 action run end core=%s ret=%d path=%s\n",
      options.core_id[0] ? options.core_id : "auto", ret, path);
   (void)unifrog_log_flush();
   return ret == 0 ? 1 : -1;
}

static int run_media_action(const char *id)
{
   struct unifrog_media_video_options options;
   const char *colon;
   const char *path;
   char option_text[160];
   size_t option_len;
   char *part;
   int mode_valid = 1;
   int ret;

   if (!id || strncmp(id, "media+", 6) != 0)
      return -1;
   colon = strchr(id + 6, ':');
   if (!colon || !colon[1])
      return -1;
   option_len = (size_t)(colon - (id + 6));
   if (option_len >= sizeof(option_text))
      option_len = sizeof(option_text) - 1;
   memcpy(option_text, id + 6, option_len);
   option_text[option_len] = '\0';
   path = colon + 1;

   memset(&options, 0, sizeof(options));
   options.preset = -1;
   for (part = option_text; part && part[0];) {
      char *next = strchr(part, ',');
      char *eq;

      if (next)
         *next++ = '\0';
      eq = strchr(part, '=');
      if (eq) {
         *eq++ = '\0';
         if (strcmp(part, "ms") == 0 || strcmp(part, "max_ms") == 0) {
            options.max_play_ms = parse_unsigned_option(eq, 15000u);
         } else if (strcmp(part, "mode") == 0) {
            if (unifrog_media_route_parse(eq, &options.route) != 0)
               mode_valid = 0;
         } else if (strcmp(part, "audio") == 0) {
            options.disable_audio = parse_unsigned_option(eq, 1u) ? 0 : 1;
         } else if (strcmp(part, "video") == 0) {
            options.disable_video = parse_unsigned_option(eq, 1u) ? 0 : 1;
         }
      }
      part = next;
   }

   if (!mode_valid)
      return -1;
#if !UNIFROG_ENABLE_HCPLAYER
   if (options.route == UNIFROG_MEDIA_ROUTE_HCPLAYER ||
       options.route == UNIFROG_MEDIA_ROUTE_HCPLAYER_AUDIO ||
       options.route == UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED) {
      printf("js2300 action media unavailable mode=hcplayer path=%s\n", path);
      return -1;
   }
#endif
   printf("js2300 action media begin route=%s audio=%d video=%d max_ms=%u path=%s\n",
      unifrog_media_route_name(options.route),
      !options.disable_audio, !options.disable_video, options.max_play_ms,
      path);
   (void)unifrog_log_flush();
   ret = unifrog_media_play_video_ex(path, &options);
   printf("js2300 action media end ret=%d path=%s\n", ret, path);
   (void)unifrog_log_flush();
   return ret == 0 ? 1 : -1;
}

int host_action(void *opaque, const char *id)
{
   struct js2300_frontend *frontend = opaque;

   if (!id || !*id)
      return -1;

   if (strcmp(id, "storage:experimental") == 0)
      return UNIFROG_SD_EXPERIMENTAL ? 1 : 0;
   if (strcmp(id, "storage:recover") == 0)
      return unifrog_platform_recover_storage("js_action", 4, 100) == 0 ?
         1 : -1;
   if (strncmp(id, "storage:swap_driver:", 20) == 0) {
      const char *target = id + 20;
      int ret = unifrog_platform_mmc_host_swap(target);
      printf("js2300 action storage:swap_driver target=%s ret=%d\n",
         target, ret);
      /* status returns 0=vendor, 1=source; swap returns 1=ok, -1=fail */
      if (strcmp(target, "status") == 0)
         return ret >= 0 ? ret : -1;
      return ret >= 0 ? 1 : -1;
   }
   if (strncmp(id, "toast:", 6) == 0) {
      draw_native_toast(id + 6);
      printf("js2300 action toast message=%s\n", id + 6);
      return 1;
   }
   if (strncmp(id, "run+", 4) == 0)
      return run_libretro_action(id);
   if (strncmp(id, "media+", 6) == 0)
      return run_media_action(id);
   if (strcmp(id, "developer:system_check") == 0)
      return run_system_check() == 0 ? 1 : -1;
   if (strcmp(id, "developer:display_benchmark") == 0) {
      char summary[64];
      int ret = unifrog_display_benchmark_run(summary, sizeof(summary));

      printf("js2300 action developer display_benchmark ret=%d summary=%s\n",
         ret, summary);
      return ret == 0 ? 1 : -1;
   }
   if (strcmp(id, "developer:display_color_test") == 0) {
      char summary[64];
      int ret = unifrog_display_color_test_run(summary, sizeof(summary));

      printf("js2300 action developer display_color_test ret=%d summary=%s\n",
         ret, summary);
      return ret == 0 ? 1 : -1;
   }
   if (strcmp(id, "developer:audio_test") == 0) {
      char summary[96];
      int ret;

      summary[0] = '\0';
      ret = unifrog_media_run_audio_diagnostics(summary, sizeof(summary));
      printf("js2300 action developer audio_test ret=%d summary=%s\n",
         ret, summary);
      return ret == 0 ? 1 : -1;
   }
   if (strncmp(id, "developer:storage_stress:", 25) == 0) {
      const char *profile = id + 25;
      int ret = run_storage_stress_with_mmc_diag(frontend, profile);

      printf("js2300 action developer storage_stress profile=%s ret=%d\n",
         profile, ret);
      return ret == 0 ? 1 : -1;
   }
   if (strcmp(id, "developer:storage_runtime_sweep_supported") == 0) {
      int ret = unifrog_platform_sd_runtime_supported();

      printf("js2300 action developer storage_runtime_sweep_supported ret=%d mode=%s\n",
         ret, UNIFROG_SD_MODE);
      return ret ? 1 : 0;
   }
   if (strcmp(id, "developer:storage_quick_benchmark") == 0) {
      int ret = run_storage_quick_benchmark_with_mmc_diag(frontend);

      printf("js2300 action developer storage_quick_benchmark ret=%d\n",
         ret);
      return ret == 0 ? 1 : -1;
   }
   if (strcmp(id, "developer:storage_quick_modes") == 0) {
      int ret = run_storage_quick_modes_with_mmc_diag(frontend);

      printf("js2300 action developer storage_quick_modes ret=%d\n", ret);
      return ret == 0 ? 1 : -1;
   }
   if (strcmp(id, "developer:storage_speed_matrix") == 0) {
      int ret = run_storage_speed_matrix_with_mmc_diag(frontend);

      printf("js2300 action developer storage_speed_matrix ret=%d\n", ret);
      return ret == 0 ? 1 : -1;
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
   if (strcmp(id, "developer:storage_test") == 0) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action),
         "storage_test");
      return 0;
   }
   if (strcmp(id, "developer:storage_full_test") == 0) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action),
         "storage_full_test");
      return 0;
   }
   if (strncmp(id, "developer:storage_mode_test:", 28) == 0) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action),
         "storage_mode_test");
      unifrog_text_copy(frontend->path, sizeof(frontend->path), id + 28);
      return 0;
   }
   return -1;
}

void host_exit(void *opaque, const char *reason)
{
   (void)opaque;
   printf("js2300 exit reason=%s\n", reason ? reason : "");
}

int run_requested_action(struct js2300_frontend *frontend)
{
   if (!frontend || !frontend->action[0])
      return -1;
   if (strcmp(frontend->action, "script") == 0) {
      char script_path[JS2300_FRONTEND_MAX_PATH];
      int ret;

      unifrog_text_copy(script_path, sizeof(script_path), frontend->path);
      frontend->action[0] = '\0';
      frontend->path[0] = '\0';
      ret = run_js_script_file(frontend, script_path);
      if (ret == 0 && frontend->action[0])
         return run_requested_action(frontend);
      return ret;
   }
   if (strcmp(frontend->action, "storage_test") == 0)
      return run_storage_test_with_mmc_diag(frontend);
   if (strcmp(frontend->action, "storage_full_test") == 0)
      return run_storage_full_test_with_mmc_diag(frontend);
   if (strcmp(frontend->action, "storage_mode_test") == 0) {
      char profile[32];

      unifrog_text_copy(profile, sizeof(profile), frontend->path);
      return run_storage_mode_test_with_mmc_diag(frontend, profile);
   }
   return -1;
}
