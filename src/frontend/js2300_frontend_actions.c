#include "js2300_frontend_internal.h"

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

static int run_system_check(void)
{
   static const char *required_files[] = {
      "/media/mmcblk0/bios/bisrv.asd",
      "/media/mmcblk0/firmware/unifrog.bin",
      JS2300_FRONTEND_APP_ROOT "/main.js",
      JS2300_FRONTEND_APP_ROOT "/main.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/quick-menu.js",
      JS2300_FRONTEND_APP_ROOT "/quick-menu.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/bytecode-manifest.txt",
      JS2300_FRONTEND_APP_ROOT "/app/theme.js",
      JS2300_FRONTEND_APP_ROOT "/app/theme.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/constants.js",
      JS2300_FRONTEND_APP_ROOT "/app/constants.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/catalog.js",
      JS2300_FRONTEND_APP_ROOT "/app/catalog.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/text.js",
      JS2300_FRONTEND_APP_ROOT "/app/text.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/navigation.js",
      JS2300_FRONTEND_APP_ROOT "/app/navigation.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/content.js",
      JS2300_FRONTEND_APP_ROOT "/app/content.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/settings.js",
      JS2300_FRONTEND_APP_ROOT "/app/settings.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/index.js",
      JS2300_FRONTEND_APP_ROOT "/app/index.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/views.js",
      JS2300_FRONTEND_APP_ROOT "/app/views.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/actions.js",
      JS2300_FRONTEND_APP_ROOT "/app/actions.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/app/app.js",
      JS2300_FRONTEND_APP_ROOT "/app/app.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/manifest.ini",
      JS2300_FRONTEND_APP_ROOT "/scripts/smoke-test.js",
      JS2300_FRONTEND_APP_ROOT "/scripts/smoke-test.js.mqbc",
      JS2300_FRONTEND_APP_ROOT "/scripts/perf-test.js",
      JS2300_FRONTEND_APP_ROOT "/scripts/perf-test.js.mqbc",
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
   return 0;
}

int host_action(void *opaque, const char *id)
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

void host_exit(void *opaque, const char *reason)
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
   uint32_t start_ms;
   uint32_t create_start_ms;
   uint32_t run_start_ms;
   int ret;

   if (!frontend || split_script_path(path, root, sizeof(root),
       entry, sizeof(entry)) != 0)
      return -1;

   js2300_config_init(&config);
   config.app_root = root;
   config.entry_script = entry;
   config.heap_bytes = 8u * 1024u * 1024u;
   frontend_configure_host(frontend, &host);
   start_ms = unifrog_perf_time_ms();
   printf("js2300 script launch root=%s script=%s heap=%u\n",
      root, entry, (unsigned)config.heap_bytes);
   unifrog_diag_memory_snapshot("script.launch");
   (void)unifrog_log_flush();
   create_start_ms = unifrog_perf_time_ms();
   ret = js2300_runtime_create(&config, &host, &runtime);
   printf("js2300 script phase=runtime_create ms=%lu ret=%d\n",
      (unsigned long)(unifrog_perf_time_ms() - create_start_ms), ret);
   unifrog_diag_memory_snapshot("script.created");
   if (ret == 0) {
      run_start_ms = unifrog_perf_time_ms();
      ret = js2300_runtime_run(runtime);
      printf("js2300 script phase=runtime_run ms=%lu ret=%d\n",
         (unsigned long)(unifrog_perf_time_ms() - run_start_ms), ret);
   }
   unifrog_diag_memory_snapshot("script.after_run");
   js2300_runtime_destroy(runtime);
   unifrog_diag_memory_snapshot("script.destroyed");
   printf("js2300 script done ret=%d ms=%lu path=%s action=%s\n",
      ret, (unsigned long)(unifrog_perf_time_ms() - start_ms), path,
      frontend->action);
   (void)unifrog_log_flush();
   return ret;
}

int run_requested_action(struct js2300_frontend *frontend)
{
   frontend->relaunch = 1;
   printf("js2300 run action dispatch action=%s path=%s preset=%d\n",
      frontend->action, frontend->path, frontend->video_preset);
   unifrog_diag_memory_snapshot("action.dispatch");
   (void)unifrog_log_flush();
   if (strcmp(frontend->action, "run") == 0) {
      uint32_t action_start_ms = unifrog_perf_time_ms();
      int ret = unifrog_libretro_run_path_ex(frontend->path,
         &frontend->run_options);

      printf("js2300 run action core ret=%d ms=%lu path=%s\n",
         ret, (unsigned long)(unifrog_perf_time_ms() - action_start_ms),
         frontend->path);
      unifrog_diag_memory_snapshot("action.core_return");
      (void)unifrog_log_flush();
      frontend_fb_reopen(frontend, "libretro_return");
      unifrog_diag_memory_snapshot("action.fb_reopen");
      frontend->input_recovered = 1;
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
      int ret = run_system_check();

      frontend_fb_reopen(frontend, "system_check_return");
      return ret;
   }
   return -1;
}
