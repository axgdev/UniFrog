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

struct storage_test_case {
   const char *label;
   const char *path;
   size_t chunk_size;
   size_t byte_limit;
   unsigned pause_ms;
};

struct storage_test_result {
   const char *label;
   const char *path;
   size_t chunk_size;
   size_t byte_limit;
   unsigned pause_ms;
   uint32_t ms;
   unsigned long bytes;
   unsigned long chunks;
   uint32_t checksum;
   int ret;
   int err;
};

struct storage_probe_file {
   const char *label;
   const char *path;
   size_t chunk_size;
};

static void storage_test_append(char *dst, size_t dst_size, size_t *used,
   const char *fmt, ...)
{
   va_list ap;
   int wrote;

   if (!dst || !dst_size || !used || *used >= dst_size)
      return;
   va_start(ap, fmt);
   wrote = vsnprintf(dst + *used, dst_size - *used, fmt, ap);
   va_end(ap);
   if (wrote <= 0)
      return;
   if ((size_t)wrote >= dst_size - *used)
      *used = dst_size - 1;
   else
      *used += (size_t)wrote;
}

static int storage_test_write_file(const char *path, const char *text,
   size_t size)
{
   char tmp[JS2300_FRONTEND_MAX_PATH];
   FILE *file;
   size_t wrote;
   int close_ret;
   int ret = -1;

   if (!path || !text)
      return -1;
   snprintf(tmp, sizeof(tmp), "%s.tmp", path);
   file = fopen(tmp, "wb");
   if (!file) {
      printf("unifrog storage_test report open_fail path=%s errno=%d\n",
         tmp, errno);
      return -1;
   }
   wrote = fwrite(text, 1, size, file);
   close_ret = fclose(file);
   if (wrote == size && close_ret == 0) {
      unlink(path);
      if (rename(tmp, path) == 0)
         ret = 0;
   }
   if (ret != 0) {
      printf("unifrog storage_test report write_fail path=%s errno=%d\n",
         path, errno);
      unlink(tmp);
   }
   return ret;
}

static int storage_test_path_is_duplicate(char paths[][JS2300_FRONTEND_MAX_PATH],
   unsigned count, const char *path)
{
   for (unsigned i = 0; i < count; i++) {
      if (strcmp(paths[i], path) == 0)
         return 1;
   }
   return 0;
}

static int storage_test_index_path(char *out, size_t out_size,
   unsigned long *out_size_bytes)
{
   FILE *file;
   char line[640];
   char best[JS2300_FRONTEND_MAX_PATH];
   unsigned long best_size = 0;

   if (!out || out_size == 0)
      return -1;
   file = fopen("/media/mmcblk0/unifrog/game-index.txt", "rb");
   if (!file)
      return -1;

   best[0] = '\0';
   while (fgets(line, sizeof(line), file)) {
      char *p1;
      char *p2;
      char *p3;
      char *p4;
      struct stat st;

      if (strncmp(line, "game|", 5) != 0)
         continue;
      p1 = strchr(line, '|');
      p2 = p1 ? strchr(p1 + 1, '|') : NULL;
      p3 = p2 ? strchr(p2 + 1, '|') : NULL;
      p4 = p3 ? strchr(p3 + 1, '|') : NULL;
      if (!p3 || !p4)
         continue;
      *p4 = '\0';
      if (stat(p3 + 1, &st) != 0 || !S_ISREG(st.st_mode))
         continue;
      if ((unsigned long)st.st_size > best_size) {
         best_size = (unsigned long)st.st_size;
         unifrog_text_copy(best, sizeof(best), p3 + 1);
      }
   }
   fclose(file);

   if (!best[0])
      return -1;
   unifrog_text_copy(out, out_size, best);
   if (out_size_bytes)
      *out_size_bytes = best_size;
   return 0;
}

static int storage_test_read_file(const struct storage_test_case *test,
   struct storage_test_result *result)
{
   unsigned char *buffer;
   uint32_t start_ms;
   uint32_t checksum = 2166136261u;
   unsigned long total = 0;
   unsigned long chunks = 0;
   size_t chunk_size;
   int fd;
   int ret = 0;
   int err = 0;

   memset(result, 0, sizeof(*result));
   result->label = test->label;
   result->path = test->path;
   result->chunk_size = test->chunk_size;
   result->byte_limit = test->byte_limit;
   result->pause_ms = test->pause_ms;

   chunk_size = test->chunk_size ? test->chunk_size : 32768u;
   buffer = malloc(chunk_size);
   if (!buffer) {
      result->ret = -1;
      result->err = ENOMEM;
      return -1;
   }

   fd = open(test->path, O_RDONLY);
   if (fd < 0) {
      result->ret = -1;
      result->err = errno;
      free(buffer);
      return -1;
   }

   start_ms = unifrog_perf_time_ms();
   while (1) {
      size_t want = chunk_size;
      ssize_t got;

      if (test->byte_limit && total >= (unsigned long)test->byte_limit)
         break;
      if (test->byte_limit &&
          (unsigned long)want > (unsigned long)test->byte_limit - total)
         want = (size_t)((unsigned long)test->byte_limit - total);
      got = read(fd, buffer, want);
      if (got < 0) {
         ret = -1;
         err = errno;
         break;
      }
      if (got == 0)
         break;
      for (ssize_t i = 0; i < got; i++) {
         checksum ^= buffer[i];
         checksum *= 16777619u;
      }
      total += (unsigned long)got;
      chunks++;
      if (test->pause_ms)
         msleep(test->pause_ms);
   }

   if (close(fd) != 0 && ret == 0) {
      ret = -1;
      err = errno;
   }
   free(buffer);

   result->ms = unifrog_perf_time_ms() - start_ms;
   result->bytes = total;
   result->chunks = chunks;
   result->checksum = checksum;
   result->ret = ret;
   result->err = err;
   return ret;
}

#define STORAGE_TEST_MAX_CASES 12u
#define STORAGE_TEST_REPORT_BYTES 49152u
#define STORAGE_TEST_PROFILE_REPORT_BYTES 4096u
#define STORAGE_TEST_SWITCH_ATTEMPTS 10u
#define STORAGE_TEST_SWITCH_DELAY_MS 100u
#define STORAGE_TEST_SWEEP_KNOWN_BYTES (128u * 1024u)
#define STORAGE_TEST_SWEEP_INDEX_PAUSE_BYTES (256u * 1024u)
#define STORAGE_TEST_SWEEP_INDEX_BYTES (512u * 1024u)
#define STORAGE_TEST_FULL_INDEX_BYTES (16u * 1024u * 1024u)
#define STORAGE_TEST_FULL_INDEX_LARGE_BYTES (32u * 1024u * 1024u)

static const char *storage_test_active_title = "Storage test";

static void storage_test_progress(struct js2300_frontend *frontend,
   const char *title, const char *line1, const char *line2)
{
   if (!frontend)
      return;
   host_video_clear(frontend, 0x0000);
   host_video_text(frontend, 16, 18, title ? title : "Storage test", 0xffff);
   if (line1 && line1[0])
      host_video_text(frontend, 16, 52, line1, 0xbdf7);
   if (line2 && line2[0])
      host_video_text(frontend, 16, 76, line2, 0x7bef);
   host_video_text(frontend, 16, 204, "Logs are buffered until safe mode returns", 0x7bef);
   host_video_present(frontend);
}

static void storage_test_platform_stage(void *userdata,
   const char *operation, const char *stage)
{
   struct js2300_frontend *frontend = userdata;
   char line[96];

   snprintf(line, sizeof(line), "%s: %s",
      operation ? operation : "sd",
      stage ? stage : "");
   storage_test_progress(frontend, storage_test_active_title, line,
      "Screen shows the last risky stage");
}

static unsigned storage_test_build_cases(struct storage_test_case *tests,
   unsigned max_tests, char unique_paths[][JS2300_FRONTEND_MAX_PATH],
   unsigned *out_known_count, char *indexed_path, size_t indexed_path_size,
   unsigned long *out_indexed_size, int compact)
{
   static const char *known_paths[] = {
      "/media/mmcblk0/ROMS/test.md",
      "/media/mmcblk0/firmware/unifrog.bin",
      "/media/mmcblk0/unifrog/cores/gpsp.bin",
      "/media/mmcblk0/bios/bisrv.asd",
   };
   unsigned known_count = 0;
   unsigned test_count = 0;
   unsigned long indexed_size = 0;

   if (!tests || !max_tests || !unique_paths || !indexed_path)
      return 0;

   memset(tests, 0, sizeof(*tests) * max_tests);
   indexed_path[0] = '\0';

   for (unsigned i = 0; i < sizeof(known_paths) / sizeof(known_paths[0]); i++) {
      struct stat st;

      if (stat(known_paths[i], &st) != 0 || !S_ISREG(st.st_mode))
         continue;
      if (storage_test_path_is_duplicate(unique_paths, known_count,
          known_paths[i]))
         continue;
      unifrog_text_copy(unique_paths[known_count],
         sizeof(unique_paths[known_count]), known_paths[i]);
      known_count++;
   }

   if (compact && known_count > 0 &&
       strcmp(unique_paths[0], "/media/mmcblk0/ROMS/test.md") == 0) {
      tests[0].label = "test.md 1K";
      tests[0].path = unique_paths[0];
      tests[0].chunk_size = 1024u;
      tests[0].byte_limit = 1024u;
      if (out_known_count)
         *out_known_count = known_count;
      if (out_indexed_size)
         *out_indexed_size = 0;
      return 1;
   }

   if (known_count > 0 && test_count < max_tests) {
      tests[test_count].label = "firmware/core 64K";
      tests[test_count].path = unique_paths[0];
      tests[test_count].chunk_size = 64u * 1024u;
      if (compact)
         tests[test_count].byte_limit = STORAGE_TEST_SWEEP_KNOWN_BYTES;
      test_count++;
   }
   if (known_count > 1 && test_count < max_tests) {
      tests[test_count].label = "second file 64K";
      tests[test_count].path = unique_paths[1];
      tests[test_count].chunk_size = 64u * 1024u;
      if (compact)
         tests[test_count].byte_limit = STORAGE_TEST_SWEEP_KNOWN_BYTES;
      test_count++;
   }

   if (storage_test_index_path(indexed_path, indexed_path_size,
       &indexed_size) == 0) {
      if (test_count < max_tests) {
         tests[test_count].label = "indexed 32K pause";
         tests[test_count].path = indexed_path;
         tests[test_count].chunk_size = 32u * 1024u;
         tests[test_count].byte_limit = compact ?
            STORAGE_TEST_SWEEP_INDEX_PAUSE_BYTES : 4u * 1024u * 1024u;
         tests[test_count].pause_ms = compact ? 1u : 2u;
         test_count++;
      }

      if (test_count < max_tests) {
         tests[test_count].label = "indexed 256K";
         tests[test_count].path = indexed_path;
         tests[test_count].chunk_size = 256u * 1024u;
         tests[test_count].byte_limit =
            indexed_size <
               (compact ? STORAGE_TEST_SWEEP_INDEX_BYTES :
                  STORAGE_TEST_FULL_INDEX_BYTES) ?
            (size_t)indexed_size :
               (compact ? STORAGE_TEST_SWEEP_INDEX_BYTES :
                  STORAGE_TEST_FULL_INDEX_BYTES);
         test_count++;
      }

      if (!compact && test_count < max_tests) {
         tests[test_count].label = "indexed 512K";
         tests[test_count].path = indexed_path;
         tests[test_count].chunk_size = 512u * 1024u;
         tests[test_count].byte_limit =
            indexed_size < STORAGE_TEST_FULL_INDEX_LARGE_BYTES ?
            (size_t)indexed_size : STORAGE_TEST_FULL_INDEX_LARGE_BYTES;
         test_count++;
      }
   }

   if (out_known_count)
      *out_known_count = known_count;
   if (out_indexed_size)
      *out_indexed_size = indexed_size;
   return test_count;
}

static unsigned storage_test_build_probe_cases(struct storage_test_case *tests,
   unsigned max_tests, unsigned *out_known_count, unsigned long *out_total_bytes)
{
   static const struct storage_probe_file probes[] = {
      { "probe 1K", "/media/mmcblk0/ROMS/probes/test.md", 1024u },
      { "probe 128K", "/media/mmcblk0/ROMS/probes/test128.md", 32u * 1024u },
      { "probe 512K", "/media/mmcblk0/ROMS/probes/test512.md", 64u * 1024u },
      { "probe 1M", "/media/mmcblk0/ROMS/probes/test1M.md", 128u * 1024u },
      { "probe 2M", "/media/mmcblk0/ROMS/probes/test2M.md", 128u * 1024u },
      { "probe 5M", "/media/mmcblk0/ROMS/probes/test5M.md", 256u * 1024u },
      { "probe 10M", "/media/mmcblk0/ROMS/probes/test10M.md", 256u * 1024u },
      { "probe 20M", "/media/mmcblk0/ROMS/probes/test20M.md", 256u * 1024u },
      { "probe 50M", "/media/mmcblk0/ROMS/probes/test50M.md", 256u * 1024u },
   };
   unsigned test_count = 0;
   unsigned long total_bytes = 0;

   if (!tests || !max_tests)
      return 0;
   memset(tests, 0, sizeof(*tests) * max_tests);

   for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
      struct stat st;

      if (test_count >= max_tests)
         break;
      if (stat(probes[i].path, &st) != 0 || !S_ISREG(st.st_mode))
         continue;
      tests[test_count].label = probes[i].label;
      tests[test_count].path = probes[i].path;
      tests[test_count].chunk_size = probes[i].chunk_size;
      tests[test_count].byte_limit = (size_t)st.st_size;
      total_bytes += (unsigned long)st.st_size;
      test_count++;
   }

   if (out_known_count)
      *out_known_count = test_count;
   if (out_total_bytes)
      *out_total_bytes = total_bytes;
   return test_count;
}

static int storage_test_profile_is_allowed(const char *profile)
{
   static const char *profiles[] = {
      "safe",
      "hs1",
      "uhs12",
      "uhs25",
      "wide",
      "uhs",
      "wide50",
   };

   if (!profile || !profile[0])
      return 0;
   for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
      if (strcmp(profile, profiles[i]) == 0)
         return 1;
   }
   return 0;
}

static void storage_test_write_probe_file(
   const struct storage_test_case *tests, unsigned test_count,
   unsigned known_count, const char *indexed_path, unsigned long indexed_size,
   int runtime_supported, int runtime_sweep)
{
   char probe[2048];
   size_t used = 0;

   storage_test_append(probe, sizeof(probe), &used,
      "storage_test_probe=1\n"
      "mode=%s\n"
      "runtime_supported=%d\n"
      "runtime_sweep=%d\n"
      "known=%u\n"
      "indexed=%s\n"
      "indexed_bytes=%lu\n"
      "note=Runtime switch stages are shown on screen; long tests checkpoint the report only after returning to safe storage.\n",
      UNIFROG_SD_MODE,
      runtime_supported,
      runtime_sweep,
      known_count,
      indexed_path && indexed_path[0] ? indexed_path : "",
      indexed_size);

   for (unsigned i = 0; i < test_count; i++) {
      storage_test_append(probe, sizeof(probe), &used,
         "case|%u|%s|chunk=%lu|limit=%lu|pause=%u|%s\n",
         i + 1u,
         tests[i].label ? tests[i].label : "",
         (unsigned long)tests[i].chunk_size,
         (unsigned long)tests[i].byte_limit,
         tests[i].pause_ms,
         tests[i].path ? tests[i].path : "");
   }

   (void)storage_test_write_file(JS2300_FRONTEND_STORAGE_TEST_PROBE,
      probe, used);
}

static void storage_test_checkpoint_report(const char *report_path,
   const char *report, size_t report_used, int update_frontend_report)
{
   if (!report_path || !report || report_used == 0)
      return;

   unifrog_platform_set_storage_log_suspended(0);
   (void)unifrog_log_flush_force();
   (void)storage_test_write_file(report_path, report, report_used);
   if (update_frontend_report)
      (void)storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
   (void)unifrog_log_flush_force();
   unifrog_platform_set_storage_log_suspended(1);
}

static int storage_test_restore_safe(struct js2300_frontend *frontend,
   const char *line1, const char *line2, char *detail, size_t detail_size)
{
   int ret;

   if (detail && detail_size)
      detail[0] = '\0';
   storage_test_progress(frontend, storage_test_active_title, line1, line2);
   ret = unifrog_platform_sd_restore_boot(STORAGE_TEST_SWITCH_ATTEMPTS,
      STORAGE_TEST_SWITCH_DELAY_MS, detail, detail_size);
   if (ret != 0) {
      msleep(500);
      ret = unifrog_platform_sd_restore_boot(STORAGE_TEST_SWITCH_ATTEMPTS,
         STORAGE_TEST_SWITCH_DELAY_MS, detail, detail_size);
   }
   return ret;
}

static void storage_test_run_profile(struct js2300_frontend *frontend,
   const char *profile,
   const char *switch_detail, const struct storage_test_case *tests,
   unsigned test_count, char *report, size_t report_size, size_t *report_used,
   unsigned *total_pass, unsigned *total_fail)
{
   char case_report[STORAGE_TEST_PROFILE_REPORT_BYTES];
   size_t case_used = 0;
   unsigned pass_count = 0;
   unsigned fail_count = 0;
   uint32_t start_ms = unifrog_perf_time_ms();

   case_report[0] = '\0';
   for (unsigned i = 0; i < test_count; i++) {
      struct storage_test_result result;
      char progress_line[96];
      unsigned long kib_s;

      snprintf(progress_line, sizeof(progress_line), "%s  %u/%u",
         profile, i + 1u, test_count);
      storage_test_progress(frontend, storage_test_active_title, progress_line,
         tests[i].label);
      unifrog_platform_storage_diag_note(profile, tests[i].label);

      if (storage_test_read_file(&tests[i], &result) == 0) {
         pass_count++;
         if (total_pass)
            (*total_pass)++;
      } else {
         fail_count++;
         if (total_fail)
            (*total_fail)++;
      }

      printf("unifrog storage_test profile=%s case=%s ret=%d errno=%d bytes=%lu chunks=%lu chunk=%lu pause=%u ms=%lu checksum=0x%08lx path=%s\n",
         profile,
         result.label,
         result.ret,
         result.err,
         result.bytes,
         result.chunks,
         (unsigned long)result.chunk_size,
         result.pause_ms,
         (unsigned long)result.ms,
         (unsigned long)result.checksum,
         result.path);

      kib_s = result.ms ?
         ((result.bytes / 1024ul) * 1000ul) / result.ms : 0ul;
      storage_test_append(case_report, sizeof(case_report), &case_used,
         "item|%s|%s %s|%lu KiB/s|%lu bytes %lu ms chunk=%lu pause=%u crc=%08lx %s\n",
         result.ret == 0 ? "OK" : "FAIL",
         profile,
         result.label,
         kib_s,
         result.bytes,
         (unsigned long)result.ms,
         (unsigned long)result.chunk_size,
         result.pause_ms,
         (unsigned long)result.checksum,
         result.path);

      if (result.ret != 0)
         (void)unifrog_platform_recover_storage(profile, 2, 100);
   }

   storage_test_append(report, report_size, report_used,
      "item|%s|Profile %s|%u ok  %u failed  %lu ms|%s\n",
      fail_count == 0 ? "OK" : "FAIL",
      profile,
      pass_count,
      fail_count,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      switch_detail ? switch_detail : "");
   storage_test_append(report, report_size, report_used, "%s", case_report);
}

static int run_storage_test(struct js2300_frontend *frontend)
{
   static const char *runtime_profiles[] = {
      "hs1",
      "wide50",
      "uhs12",
      "uhs25",
      "wide",
      "uhs",
   };
   struct storage_test_case tests[STORAGE_TEST_MAX_CASES];
   char unique_paths[4][JS2300_FRONTEND_MAX_PATH];
   char indexed_path[JS2300_FRONTEND_MAX_PATH];
   char *report;
   size_t report_used = 0;
   size_t old_auto_flush;
   unsigned known_count = 0;
   unsigned test_count = 0;
   unsigned pass_count = 0;
   unsigned fail_count = 0;
   unsigned long indexed_size = 0;
   int runtime_sweep;
   int runtime_supported;
   uint32_t start_ms;

   storage_test_active_title = "Storage test";
   report = malloc(STORAGE_TEST_REPORT_BYTES);
   if (!report) {
      storage_test_progress(frontend, storage_test_active_title,
         "Out of memory", "Cannot allocate report buffer");
      return -1;
   }
   report[0] = '\0';

   storage_test_progress(frontend, storage_test_active_title,
      "Preparing safe baseline",
      "Runtime sweep uses short reads");
   runtime_supported = unifrog_platform_sd_runtime_supported();
   runtime_sweep = runtime_supported &&
      strcmp(UNIFROG_SD_MODE, "safe") == 0;
   test_count = storage_test_build_cases(tests, STORAGE_TEST_MAX_CASES,
      unique_paths, &known_count, indexed_path, sizeof(indexed_path),
      &indexed_size, runtime_sweep);

   if (test_count == 0) {
      storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
         "show=1\n"
         "title=STORAGE TEST\n"
         "detail=No readable benchmark files\n"
         "item|FAIL|Storage files|Expected firmware or indexed games|No files found\n");
      storage_test_write_file(JS2300_FRONTEND_STORAGE_TEST_REPORT,
         report, report_used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
      (void)unifrog_log_flush();
      free(report);
      return 0;
   }

   printf("unifrog storage_test begin mode=%s runtime_supported=%d runtime_sweep=%d tests=%u known=%u indexed=%s indexed_bytes=%lu\n",
      UNIFROG_SD_MODE, runtime_supported, runtime_sweep, test_count,
      known_count, indexed_path[0] ? indexed_path : "", indexed_size);

   storage_test_write_probe_file(tests, test_count, known_count,
      indexed_path, indexed_size, runtime_supported, runtime_sweep);
   (void)unifrog_log_flush();
   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_log_defer_begin();
   unifrog_platform_set_storage_stage_callback(storage_test_platform_stage,
      frontend);
   unifrog_platform_set_storage_log_suspended(1);
   start_ms = unifrog_perf_time_ms();

   storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
      "show=1\n"
      "title=STORAGE TEST\n");

   if (runtime_sweep) {
      char restore_detail[192];
      int safe_restart_ret;
      int restore_ret;

      storage_test_run_profile(frontend, "boot", "safe boot", tests, test_count,
         report, STORAGE_TEST_REPORT_BYTES, &report_used, &pass_count,
         &fail_count);

      restore_detail[0] = '\0';
      storage_test_progress(frontend, storage_test_active_title, "Safe restart",
         "Unmount and remount safe mode");
      safe_restart_ret = unifrog_platform_sd_restore_boot(
         STORAGE_TEST_SWITCH_ATTEMPTS, STORAGE_TEST_SWITCH_DELAY_MS,
         restore_detail, sizeof(restore_detail));
      if (safe_restart_ret != 0) {
         fail_count++;
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|FAIL|Safe restart|Skipping experimental profiles|%s\n",
            restore_detail);
      } else {
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|OK|Safe restart|Unmount/remount path works|%s\n",
            restore_detail);
         storage_test_run_profile(frontend, "safe-restart", restore_detail,
            tests, test_count, report, STORAGE_TEST_REPORT_BYTES, &report_used,
            &pass_count, &fail_count);

         for (unsigned i = 0;
              i < sizeof(runtime_profiles) / sizeof(runtime_profiles[0]); i++) {
            char switch_detail[192];
            char progress_line[96];
            int switch_ret;

            switch_detail[0] = '\0';
            snprintf(progress_line, sizeof(progress_line), "Switch to %s",
               runtime_profiles[i]);
            storage_test_progress(frontend, storage_test_active_title,
               progress_line,
               "Storage is unmounted during switch");
            switch_ret = unifrog_platform_sd_apply_profile(runtime_profiles[i],
               STORAGE_TEST_SWITCH_ATTEMPTS, STORAGE_TEST_SWITCH_DELAY_MS,
               switch_detail, sizeof(switch_detail));
            if (switch_ret != 0) {
               fail_count++;
               storage_test_append(report, STORAGE_TEST_REPORT_BYTES,
                  &report_used,
                  "item|FAIL|Profile %s|switch failed|%s\n",
                  runtime_profiles[i], switch_detail);
               (void)unifrog_platform_sd_restore_boot(
                  STORAGE_TEST_SWITCH_ATTEMPTS, STORAGE_TEST_SWITCH_DELAY_MS,
                  restore_detail, sizeof(restore_detail));
               continue;
            }

            storage_test_run_profile(frontend, runtime_profiles[i],
               switch_detail, tests, test_count, report,
               STORAGE_TEST_REPORT_BYTES, &report_used, &pass_count,
               &fail_count);
         }
      }

      restore_detail[0] = '\0';
      restore_ret = storage_test_restore_safe(frontend, "Restoring safe mode",
         "Final report will be written after this",
         restore_detail, sizeof(restore_detail));
      if (restore_ret != 0) {
         fail_count++;
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|FAIL|Restore safe boot|storage may still be unavailable|%s\n",
            restore_detail);
      } else {
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|OK|Restore safe boot|Ready for report flush|%s\n",
            restore_detail);
      }
   } else {
      char detail[96];

      snprintf(detail, sizeof(detail), "mode %s%s", UNIFROG_SD_MODE,
         runtime_supported ? "" : " runtime switch unavailable");
      storage_test_run_profile(frontend, UNIFROG_SD_MODE, detail, tests,
         test_count, report, STORAGE_TEST_REPORT_BYTES, &report_used,
         &pass_count, &fail_count);
   }

   storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
      "detail=%s  %u ok  %u failed  %lu ms\n",
      runtime_sweep ? "safe boot runtime sweep" : "single profile",
      pass_count, fail_count,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));

   unifrog_log_defer_end();
   unifrog_log_set_auto_flush_bytes(old_auto_flush);
   unifrog_platform_set_storage_log_suspended(0);
   unifrog_platform_set_storage_stage_callback(NULL, NULL);

   storage_test_progress(frontend, storage_test_active_title, "Writing report",
      runtime_sweep ? "Safe runtime sweep complete" : "Single profile complete");
   storage_test_write_file(JS2300_FRONTEND_STORAGE_TEST_REPORT,
      report, report_used);
   storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
      report, report_used);
   printf("unifrog storage_test done mode=%s runtime_sweep=%d ok=%u fail=%u ms=%lu report=%s\n",
      UNIFROG_SD_MODE, runtime_sweep, pass_count, fail_count,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      JS2300_FRONTEND_STORAGE_TEST_REPORT);
   (void)unifrog_log_flush();
   free(report);
   return 0;
}

static int run_storage_full_test(struct js2300_frontend *frontend)
{
   static const char *runtime_profiles[] = {
      "hs1",
      "uhs12",
      "uhs25",
      "wide",
      "uhs",
      "wide50",
   };
   struct storage_test_case tests[STORAGE_TEST_MAX_CASES];
   char *report;
   size_t report_used = 0;
   size_t old_auto_flush;
   unsigned probe_count = 0;
   unsigned test_count = 0;
   unsigned pass_count = 0;
   unsigned fail_count = 0;
   unsigned long probe_bytes = 0;
   int runtime_sweep;
   int runtime_supported;
   int storage_safe = 1;
   int abort_sweep = 0;
   uint32_t start_ms;

   storage_test_active_title = "Storage full test";
   report = malloc(STORAGE_TEST_REPORT_BYTES);
   if (!report) {
      storage_test_progress(frontend, storage_test_active_title,
         "Out of memory", "Cannot allocate report buffer");
      return -1;
   }
   report[0] = '\0';

   storage_test_progress(frontend, storage_test_active_title,
      "Finding probe files", "/ROMS/probes");
   runtime_supported = unifrog_platform_sd_runtime_supported();
   runtime_sweep = runtime_supported &&
      strcmp(UNIFROG_SD_MODE, "safe") == 0;
   test_count = storage_test_build_probe_cases(tests, STORAGE_TEST_MAX_CASES,
      &probe_count, &probe_bytes);

   if (test_count == 0) {
      storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
         "show=1\n"
         "title=STORAGE FULL TEST\n"
         "detail=No /ROMS/probes files found\n"
         "item|FAIL|Probe files|Expected /ROMS/probes/test*.md|No files found\n");
      storage_test_write_file(JS2300_FRONTEND_STORAGE_FULL_TEST_REPORT,
         report, report_used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
      (void)unifrog_log_flush();
      free(report);
      return 0;
   }

   printf("unifrog storage_full_test begin mode=%s runtime_supported=%d runtime_sweep=%d tests=%u probe_bytes=%lu\n",
      UNIFROG_SD_MODE, runtime_supported, runtime_sweep, test_count,
      probe_bytes);

   storage_test_write_probe_file(tests, test_count, probe_count, "",
      probe_bytes, runtime_supported, runtime_sweep);
   (void)unifrog_log_flush();
   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_log_defer_begin();
   unifrog_platform_set_storage_stage_callback(storage_test_platform_stage,
      frontend);
   unifrog_platform_set_storage_log_suspended(1);
   start_ms = unifrog_perf_time_ms();

   storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
      "show=1\n"
      "title=STORAGE FULL TEST\n"
      "item|OK|Probe files|%u files  %lu bytes|Checkpoint after each safe restore\n",
      test_count, probe_bytes);

   if (runtime_sweep) {
      char restore_detail[192];
      int safe_restart_ret;

      storage_test_run_profile(frontend, "boot", "safe boot", tests,
         test_count, report, STORAGE_TEST_REPORT_BYTES, &report_used,
         &pass_count, &fail_count);
      storage_test_checkpoint_report(JS2300_FRONTEND_STORAGE_FULL_TEST_REPORT,
         report, report_used, 1);

      safe_restart_ret = storage_test_restore_safe(frontend, "Safe restart",
         "Unmount and remount safe mode", restore_detail,
         sizeof(restore_detail));
      if (safe_restart_ret != 0) {
         fail_count++;
         storage_safe = 0;
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|FAIL|Safe restart|Skipping experimental profiles|%s\n",
            restore_detail);
      } else {
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|OK|Safe restart|Unmount/remount path works|%s\n",
            restore_detail);
         storage_test_run_profile(frontend, "safe-restart", restore_detail,
            tests, test_count, report, STORAGE_TEST_REPORT_BYTES, &report_used,
            &pass_count, &fail_count);
         storage_test_checkpoint_report(
            JS2300_FRONTEND_STORAGE_FULL_TEST_REPORT, report, report_used, 1);

         for (unsigned i = 0;
              i < sizeof(runtime_profiles) / sizeof(runtime_profiles[0]) &&
                 !abort_sweep;
              i++) {
            for (unsigned j = 0; j < test_count; j++) {
               char switch_detail[192];
               char restore_line[192];
               char progress_line[96];
               unsigned fail_before_read;
               int read_failed;
               int switch_ret;
               int restore_ret;

               switch_detail[0] = '\0';
               snprintf(progress_line, sizeof(progress_line), "%s %s",
                  runtime_profiles[i], tests[j].label);
               storage_test_progress(frontend, storage_test_active_title,
                  progress_line, "Switching from safe mode");
               switch_ret = unifrog_platform_sd_apply_profile(
                  runtime_profiles[i], STORAGE_TEST_SWITCH_ATTEMPTS,
                  STORAGE_TEST_SWITCH_DELAY_MS, switch_detail,
                  sizeof(switch_detail));
               if (switch_ret != 0) {
                  fail_count++;
                  storage_test_append(report, STORAGE_TEST_REPORT_BYTES,
                     &report_used,
                     "item|FAIL|Switch %s|Skipping remaining %s probes|%s\n",
                     runtime_profiles[i], runtime_profiles[i], switch_detail);
                  restore_ret = storage_test_restore_safe(frontend,
                     "Restoring safe mode", runtime_profiles[i],
                     restore_line, sizeof(restore_line));
                  if (restore_ret != 0) {
                     storage_safe = 0;
                     abort_sweep = 1;
                     storage_test_append(report, STORAGE_TEST_REPORT_BYTES,
                        &report_used,
                        "item|FAIL|Restore after switch fail|Aborting sweep|%s\n",
                        restore_line);
                  } else {
                     storage_test_append(report, STORAGE_TEST_REPORT_BYTES,
                        &report_used,
                        "item|OK|Restore after switch fail|Continuing sweep|%s\n",
                        restore_line);
                     storage_test_checkpoint_report(
                        JS2300_FRONTEND_STORAGE_FULL_TEST_REPORT, report,
                        report_used, 1);
                  }
                  break;
               }

               fail_before_read = fail_count;
               storage_test_run_profile(frontend, runtime_profiles[i],
                  switch_detail, &tests[j], 1u, report,
                  STORAGE_TEST_REPORT_BYTES, &report_used, &pass_count,
                  &fail_count);
               read_failed = fail_count != fail_before_read;

               restore_ret = storage_test_restore_safe(frontend,
                  "Restoring safe mode", tests[j].label, restore_line,
                  sizeof(restore_line));
               if (restore_ret != 0) {
                  fail_count++;
                  storage_safe = 0;
                  abort_sweep = 1;
                  storage_test_append(report, STORAGE_TEST_REPORT_BYTES,
                     &report_used,
                     "item|FAIL|Restore after %s|Aborting sweep|%s\n",
                     runtime_profiles[i], restore_line);
                  break;
               }

               storage_test_append(report, STORAGE_TEST_REPORT_BYTES,
                  &report_used,
                  "item|OK|Restore after %s|Checkpoint safe|%s\n",
                  runtime_profiles[i], restore_line);
               if (read_failed) {
                  storage_test_append(report, STORAGE_TEST_REPORT_BYTES,
                     &report_used,
                     "item|FAIL|Profile %s|Stopping profile after first read failure|%s\n",
                     runtime_profiles[i], tests[j].label);
               }
               storage_test_checkpoint_report(
                  JS2300_FRONTEND_STORAGE_FULL_TEST_REPORT, report,
                  report_used, 1);
               if (read_failed)
                  break;
            }
         }
      }
   } else {
      char detail[96];

      snprintf(detail, sizeof(detail), "mode %s%s", UNIFROG_SD_MODE,
         runtime_supported ? "" : " runtime switch unavailable");
      storage_test_run_profile(frontend, UNIFROG_SD_MODE, detail, tests,
         test_count, report, STORAGE_TEST_REPORT_BYTES, &report_used,
         &pass_count, &fail_count);
   }

   if (runtime_sweep && !storage_safe) {
      char restore_detail[192];

      if (storage_test_restore_safe(frontend, "Final safe recovery",
          "Trying to restore storage", restore_detail,
          sizeof(restore_detail)) == 0) {
         storage_safe = 1;
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|OK|Final safe recovery|Ready for report flush|%s\n",
            restore_detail);
      } else {
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|FAIL|Final safe recovery|Report may stop at last checkpoint|%s\n",
            restore_detail);
      }
   }

   storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
      "detail=%s  %u ok  %u failed  %lu ms\n",
      runtime_sweep ? "full runtime sweep" : "single profile",
      pass_count, fail_count,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));

   unifrog_log_defer_end();
   unifrog_log_set_auto_flush_bytes(old_auto_flush);
   unifrog_platform_set_storage_stage_callback(NULL, NULL);

   if (storage_safe) {
      unifrog_platform_set_storage_log_suspended(0);
      storage_test_progress(frontend, storage_test_active_title,
         "Writing final report",
         runtime_sweep ? "Full runtime sweep complete" :
            "Single profile complete");
      storage_test_write_file(JS2300_FRONTEND_STORAGE_FULL_TEST_REPORT,
         report, report_used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
      (void)unifrog_log_flush();
   }

   printf("unifrog storage_full_test done mode=%s runtime_sweep=%d ok=%u fail=%u safe=%d ms=%lu report=%s\n",
      UNIFROG_SD_MODE, runtime_sweep, pass_count, fail_count, storage_safe,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      JS2300_FRONTEND_STORAGE_FULL_TEST_REPORT);
   free(report);
   return 0;
}

static int run_storage_mode_test(struct js2300_frontend *frontend,
   const char *profile)
{
   struct storage_test_case tests[STORAGE_TEST_MAX_CASES];
   char *report;
   size_t report_used = 0;
   size_t old_auto_flush;
   unsigned probe_count = 0;
   unsigned test_count = 0;
   unsigned pass_count = 0;
   unsigned fail_count = 0;
   unsigned long probe_bytes = 0;
   int runtime_supported;
   int runtime_sweep;
   int storage_safe = 1;
   int switch_ret = 0;
   int restore_ret = 0;
   uint32_t start_ms;
   const char *run_profile;
   char detail[192];

   if (!storage_test_profile_is_allowed(profile))
      profile = "safe";
   run_profile = strcmp(profile, "safe") == 0 ? UNIFROG_SD_MODE : profile;

   storage_test_active_title = "Storage mode test";
   report = malloc(STORAGE_TEST_REPORT_BYTES);
   if (!report) {
      storage_test_progress(frontend, storage_test_active_title,
         "Out of memory", "Cannot allocate report buffer");
      return -1;
   }
   report[0] = '\0';

   storage_test_progress(frontend, storage_test_active_title,
      "Finding probe files", profile);
   runtime_supported = unifrog_platform_sd_runtime_supported();
   runtime_sweep = runtime_supported &&
      strcmp(UNIFROG_SD_MODE, "safe") == 0;
   test_count = storage_test_build_probe_cases(tests, STORAGE_TEST_MAX_CASES,
      &probe_count, &probe_bytes);

   if (test_count == 0) {
      storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
         "show=1\n"
         "title=STORAGE MODE TEST\n"
         "detail=No /ROMS/probes files found\n"
         "item|FAIL|Probe files|Expected /ROMS/probes/test*.md|No files found\n");
      storage_test_write_file(JS2300_FRONTEND_STORAGE_MODE_TEST_REPORT,
         report, report_used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
      (void)unifrog_log_flush();
      free(report);
      return 0;
   }

   printf("unifrog storage_mode_test begin requested=%s run_profile=%s mode=%s runtime_supported=%d runtime_sweep=%d tests=%u probe_bytes=%lu\n",
      profile, run_profile, UNIFROG_SD_MODE, runtime_supported, runtime_sweep,
      test_count, probe_bytes);

   storage_test_write_probe_file(tests, test_count, probe_count, "",
      probe_bytes, runtime_supported, runtime_sweep);
   storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
      "show=1\n"
      "title=STORAGE MODE TEST\n"
      "item|OK|Probe files|%u files  %lu bytes|%s\n",
      test_count, probe_bytes, profile);

   if (strcmp(profile, "safe") != 0 && !runtime_sweep) {
      storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
         "detail=Runtime switching unavailable from SD_MODE=%s\n"
         "item|FAIL|Profile %s|Build with SD_MODE=safe to switch at runtime|runtime_supported=%d\n",
         UNIFROG_SD_MODE, profile, runtime_supported);
      storage_test_write_file(JS2300_FRONTEND_STORAGE_MODE_TEST_REPORT,
         report, report_used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
      (void)unifrog_log_flush();
      free(report);
      return 0;
   }

   storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
      "item|OK|Mode %s|Single-switch sustained read|Final disk checkpoint after safe restore\n",
      profile);
   storage_test_write_file(JS2300_FRONTEND_STORAGE_MODE_TEST_REPORT,
      report, report_used);
   storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
      report, report_used);
   (void)unifrog_log_flush();

   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_log_defer_begin();
   unifrog_platform_set_storage_stage_callback(storage_test_platform_stage,
      frontend);
   unifrog_platform_set_storage_log_suspended(1);
   start_ms = unifrog_perf_time_ms();

   detail[0] = '\0';
   if (strcmp(profile, "safe") != 0) {
      char progress_line[96];

      snprintf(progress_line, sizeof(progress_line), "Switch to %s", profile);
      storage_test_progress(frontend, storage_test_active_title, progress_line,
         "Reads stay in this mode");
      switch_ret = unifrog_platform_sd_apply_profile(profile,
         STORAGE_TEST_SWITCH_ATTEMPTS, STORAGE_TEST_SWITCH_DELAY_MS,
         detail, sizeof(detail));
      if (switch_ret != 0) {
         fail_count++;
         storage_safe = 0;
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|FAIL|Switch %s|No reads run|%s\n", profile, detail);
      }
   } else {
      snprintf(detail, sizeof(detail), "mode %s", UNIFROG_SD_MODE);
   }

   if (switch_ret == 0) {
      storage_test_run_profile(frontend, run_profile, detail, tests,
         test_count, report, STORAGE_TEST_REPORT_BYTES, &report_used,
         &pass_count, &fail_count);
   }

   if (strcmp(profile, "safe") != 0) {
      char restore_detail[192];

      msleep(100);
      restore_ret = storage_test_restore_safe(frontend,
         "Restoring safe mode", profile, restore_detail,
         sizeof(restore_detail));
      if (restore_ret != 0) {
         storage_safe = 0;
         fail_count++;
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|FAIL|Restore after %s|Final report may stay at start checkpoint|%s\n",
            profile, restore_detail);
      } else {
         storage_safe = 1;
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|OK|Restore after %s|Ready for report flush|%s\n",
            profile, restore_detail);
      }
   }

   storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
      "detail=mode %s  %u ok  %u failed  %lu ms\n",
      profile, pass_count, fail_count,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));

   unifrog_log_defer_end();
   unifrog_log_set_auto_flush_bytes(old_auto_flush);
   unifrog_platform_set_storage_stage_callback(NULL, NULL);

   if (storage_safe) {
      unifrog_platform_set_storage_log_suspended(0);
      storage_test_progress(frontend, storage_test_active_title,
         "Writing final report", profile);
      storage_test_write_file(JS2300_FRONTEND_STORAGE_MODE_TEST_REPORT,
         report, report_used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
      (void)unifrog_log_flush();
   }

   printf("unifrog storage_mode_test done profile=%s ok=%u fail=%u safe=%d switch=%d restore=%d ms=%lu report=%s\n",
      profile, pass_count, fail_count, storage_safe, switch_ret, restore_ret,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      JS2300_FRONTEND_STORAGE_MODE_TEST_REPORT);
   free(report);
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

   if (strcmp(id, "storage:experimental") == 0)
      return UNIFROG_SD_EXPERIMENTAL ? 1 : 0;
   if (strcmp(id, "storage:recover") == 0)
      return unifrog_platform_recover_storage("js_action", 4, 100) == 0 ?
         1 : -1;

   run_path = parse_run_action(frontend, id);
   if (run_path) {
      char path[JS2300_FRONTEND_MAX_PATH];
      size_t old_auto_flush = 0;
      uint32_t start_ms;
      int storage_quiet = 0;
      int ret;

      unifrog_text_copy(path, sizeof(path), run_path);
      if (UNIFROG_SD_EXPERIMENTAL) {
         old_auto_flush = unifrog_log_auto_flush_bytes();
         unifrog_log_set_auto_flush_bytes(0);
         unifrog_log_defer_begin();
         unifrog_platform_set_storage_log_suspended(1);
         storage_quiet = 1;
      }
      printf("js2300 action run warm path=%s core=%s corefile=%s audio=%d gain=%u scpu=%u ge=%d backlight=%d fs=%d display=%d\n",
         path,
         frontend->run_options.core_id[0] ?
            frontend->run_options.core_id : "auto",
         frontend->run_options.core_path,
         frontend->run_options.audio_enabled,
         frontend->run_options.audio_gain,
         frontend->run_options.scpu_mhz, frontend->run_options.ge_clock,
         frontend->run_options.backlight_level,
         frontend->run_options.frameskip,
         frontend->run_options.display_mode);
      unifrog_diag_memory_snapshot("frontend.warm_run_start");
      if (!UNIFROG_SD_EXPERIMENTAL)
         (void)unifrog_log_flush();
      start_ms = unifrog_perf_time_ms();
      ret = unifrog_libretro_run_path_ex(path, &frontend->run_options);
      printf("js2300 action run warm ret=%d ms=%lu path=%s\n",
         ret, (unsigned long)(unifrog_perf_time_ms() - start_ms), path);
      unifrog_diag_memory_snapshot("frontend.warm_run_return");
      if (storage_quiet) {
         unifrog_platform_set_storage_log_suspended(0);
         unifrog_log_defer_end();
         unifrog_log_set_auto_flush_bytes(old_auto_flush);
      }
      frontend_fb_reopen(frontend, "warm_libretro_return");
      unifrog_input_clear();
      frontend->input_recovered = 1;
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
   if (strcmp(id, "developer:storage_test") == 0) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action),
         "storage_test");
      printf("js2300 action developer storage_test\n");
      return 0;
   }
   if (strcmp(id, "developer:storage_full_test") == 0) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action),
         "storage_full_test");
      printf("js2300 action developer storage_full_test\n");
      return 0;
   }
   if (strncmp(id, "developer:storage_mode_test:", 28) == 0) {
      const char *profile = id + 28;

      unifrog_text_copy(frontend->action, sizeof(frontend->action),
         "storage_mode_test");
      unifrog_text_copy(frontend->path, sizeof(frontend->path), profile);
      printf("js2300 action developer storage_mode_test profile=%s\n",
         frontend->path);
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
   if (strcmp(id, "reboot") == 0) {
      unifrog_text_copy(frontend->action, sizeof(frontend->action), "reboot");
      printf("js2300 action reboot\n");
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
   if (strcmp(frontend->action, "reboot") == 0) {
      unifrog_boot_reboot();
      return 0;
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
   if (strcmp(frontend->action, "storage_test") == 0) {
      int ret = run_storage_test(frontend);

      frontend_fb_reopen(frontend, "storage_test_return");
      return ret;
   }
   if (strcmp(frontend->action, "storage_full_test") == 0) {
      int ret = run_storage_full_test(frontend);

      frontend_fb_reopen(frontend, "storage_full_test_return");
      return ret;
   }
   if (strcmp(frontend->action, "storage_mode_test") == 0) {
      char profile[32];
      int ret;

      unifrog_text_copy(profile, sizeof(profile), frontend->path);
      ret = run_storage_mode_test(frontend, profile);
      frontend_fb_reopen(frontend, "storage_mode_test_return");
      return ret;
   }
   return -1;
}
