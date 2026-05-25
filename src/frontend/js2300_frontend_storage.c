#include "js2300_frontend_internal.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
#define STORAGE_STRESS_CHUNK_BYTES (32u * 1024u)
#define STORAGE_STRESS_DURATION_MS 60000u
#define STORAGE_STRESS_WRITE_WINDOW_BYTES (8u * 1024u * 1024u)

static const char *storage_test_active_title = "Storage test";

struct storage_stress_context {
   volatile int start;
   volatile int stop;
   volatile int reader_done;
   volatile int writer_done;
   const char *read_path;
   char write_path[JS2300_FRONTEND_MAX_PATH];
   uint32_t duration_ms;
   volatile unsigned long read_bytes;
   volatile unsigned long read_ops;
   volatile unsigned long write_bytes;
   volatile unsigned long write_ops;
   volatile unsigned read_errors;
   volatile unsigned write_errors;
   volatile int read_errno;
   volatile int write_errno;
   volatile int read_debug_dumped;
   volatile int write_debug_dumped;
   volatile uint32_t read_checksum;
   volatile uint32_t write_checksum;
};

static void storage_stress_reader_task(void *arg)
{
   struct storage_stress_context *ctx = arg;
   unsigned char *buffer;
   int fd = -1;
   uint32_t checksum = 2166136261u;

   buffer = malloc(STORAGE_STRESS_CHUNK_BYTES);
   if (!buffer) {
      ctx->read_errors++;
      ctx->read_errno = ENOMEM;
      ctx->reader_done = 1;
      vTaskDelete(NULL);
      return;
   }
   while (!ctx->start && !ctx->stop)
      vTaskDelay(pdMS_TO_TICKS(10));
   fd = open(ctx->read_path, O_RDONLY);
   if (fd < 0) {
      ctx->read_errors++;
      ctx->read_errno = errno;
      free(buffer);
      ctx->reader_done = 1;
      vTaskDelete(NULL);
      return;
   }
   while (!ctx->stop) {
      ssize_t got = read(fd, buffer, STORAGE_STRESS_CHUNK_BYTES);

      if (got < 0) {
         ctx->read_errors++;
         ctx->read_errno = errno;
         if (!ctx->read_debug_dumped) {
            ctx->read_debug_dumped = 1;
            printf("unifrog storage_stress read_error path=%s errno=%d bytes=%lu ops=%lu active=%s\n",
               ctx->read_path, ctx->read_errno, ctx->read_bytes,
               ctx->read_ops, unifrog_platform_sd_active_profile());
            unifrog_platform_sd_debug_dump("storage_stress_read_error");
         }
         break;
      }
      if (got == 0) {
         if (lseek(fd, 0, SEEK_SET) < 0) {
            ctx->read_errors++;
            ctx->read_errno = errno;
            if (!ctx->read_debug_dumped) {
               ctx->read_debug_dumped = 1;
               printf("unifrog storage_stress read_seek_error path=%s errno=%d bytes=%lu ops=%lu active=%s\n",
                  ctx->read_path, ctx->read_errno, ctx->read_bytes,
                  ctx->read_ops, unifrog_platform_sd_active_profile());
               unifrog_platform_sd_debug_dump("storage_stress_read_seek_error");
            }
            break;
         }
         continue;
      }
      for (ssize_t i = 0; i < got; i++) {
         checksum ^= buffer[i];
         checksum *= 16777619u;
      }
      ctx->read_bytes += (unsigned long)got;
      ctx->read_ops++;
      ctx->read_checksum = checksum;
   }
   if (fd >= 0)
      close(fd);
   free(buffer);
   ctx->reader_done = 1;
   vTaskDelete(NULL);
}

static void storage_stress_writer_task(void *arg)
{
   struct storage_stress_context *ctx = arg;
   unsigned char *buffer;
   uint32_t checksum = 2166136261u;
   unsigned sequence = 0;

   buffer = malloc(STORAGE_STRESS_CHUNK_BYTES);
   if (!buffer) {
      ctx->write_errors++;
      ctx->write_errno = ENOMEM;
      ctx->writer_done = 1;
      vTaskDelete(NULL);
      return;
   }
   while (!ctx->start && !ctx->stop)
      vTaskDelay(pdMS_TO_TICKS(10));
   while (!ctx->stop) {
      int fd = open(ctx->write_path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
      unsigned long window = 0;

      if (fd < 0) {
         ctx->write_errors++;
         ctx->write_errno = errno;
         if (!ctx->write_debug_dumped) {
            ctx->write_debug_dumped = 1;
            printf("unifrog storage_stress write_open_error path=%s errno=%d bytes=%lu ops=%lu active=%s\n",
               ctx->write_path, ctx->write_errno, ctx->write_bytes,
               ctx->write_ops, unifrog_platform_sd_active_profile());
            unifrog_platform_sd_debug_dump("storage_stress_write_open_error");
         }
         break;
      }
      while (!ctx->stop && window < STORAGE_STRESS_WRITE_WINDOW_BYTES) {
         for (unsigned i = 0; i < STORAGE_STRESS_CHUNK_BYTES; i++) {
            buffer[i] = (unsigned char)(sequence + i + (i >> 8));
            checksum ^= buffer[i];
            checksum *= 16777619u;
         }
         if (write(fd, buffer, STORAGE_STRESS_CHUNK_BYTES) !=
             STORAGE_STRESS_CHUNK_BYTES) {
            ctx->write_errors++;
            ctx->write_errno = errno;
            if (!ctx->write_debug_dumped) {
               ctx->write_debug_dumped = 1;
               printf("unifrog storage_stress write_error path=%s errno=%d bytes=%lu ops=%lu active=%s\n",
                  ctx->write_path, ctx->write_errno, ctx->write_bytes,
                  ctx->write_ops, unifrog_platform_sd_active_profile());
               unifrog_platform_sd_debug_dump("storage_stress_write_error");
            }
            break;
         }
         sequence++;
         window += STORAGE_STRESS_CHUNK_BYTES;
         ctx->write_bytes += STORAGE_STRESS_CHUNK_BYTES;
         ctx->write_ops++;
         ctx->write_checksum = checksum;
      }
      if (close(fd) != 0 && ctx->write_errors == 0) {
         ctx->write_errors++;
         ctx->write_errno = errno;
         if (!ctx->write_debug_dumped) {
            ctx->write_debug_dumped = 1;
            printf("unifrog storage_stress write_close_error path=%s errno=%d bytes=%lu ops=%lu active=%s\n",
               ctx->write_path, ctx->write_errno, ctx->write_bytes,
               ctx->write_ops, unifrog_platform_sd_active_profile());
            unifrog_platform_sd_debug_dump("storage_stress_write_close_error");
         }
      }
      if (ctx->write_errors)
         break;
   }
   free(buffer);
   ctx->writer_done = 1;
   vTaskDelete(NULL);
}

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
      "wide1",
      "wide2",
      "wide4",
      "wide8",
      "wide10",
      "wide12",
      "wide14",
      "wide16",
      "wide18",
      "wide20",
      "wide22",
      "wide24",
      "wide25",
      "wide37",
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

static unsigned long storage_test_kib_s(unsigned long bytes, uint32_t ms)
{
   if (!ms)
      return 0;
   return ((bytes / 1024ul) * 1000ul) / (unsigned long)ms;
}

static int storage_quick_write_file(const char *path, unsigned long bytes,
   unsigned long *written, uint32_t *write_ms, uint32_t *fsync_ms,
   uint32_t *total_ms, uint32_t *checksum_out)
{
   unsigned char *buffer;
   uint32_t start_ms;
   uint32_t before_fsync_ms;
   uint32_t checksum = 2166136261u;
   unsigned long done = 0;
   unsigned sequence = 0;
   int fd;
   int ret = 0;

   *written = 0;
   *write_ms = 0;
   *fsync_ms = 0;
   *total_ms = 0;
   *checksum_out = checksum;
   buffer = malloc(65536u);
   if (!buffer)
      return -ENOMEM;

   start_ms = unifrog_perf_time_ms();
   fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
   if (fd < 0) {
      ret = -errno;
      free(buffer);
      return ret;
   }

   while (done < bytes) {
      size_t want = 65536u;

      if ((unsigned long)want > bytes - done)
         want = (size_t)(bytes - done);
      for (size_t i = 0; i < want; i++) {
         buffer[i] = (unsigned char)(sequence + i + (i >> 8));
         checksum ^= buffer[i];
         checksum *= 16777619u;
      }
      if (write(fd, buffer, want) != (ssize_t)want) {
         ret = -errno;
         break;
      }
      sequence++;
      done += (unsigned long)want;
   }

   before_fsync_ms = unifrog_perf_time_ms();
   *write_ms = before_fsync_ms - start_ms;
   if (ret == 0 && fsync(fd) != 0)
      ret = -errno;
   *fsync_ms = unifrog_perf_time_ms() - before_fsync_ms;
   if (close(fd) != 0 && ret == 0)
      ret = -errno;
   *total_ms = unifrog_perf_time_ms() - start_ms;
   *written = done;
   *checksum_out = checksum;
   free(buffer);
   return ret;
}

int run_storage_quick_benchmark(struct js2300_frontend *frontend)
{
   struct storage_test_case tests[STORAGE_TEST_MAX_CASES];
   struct storage_test_case read_case;
   struct storage_test_result read_result;
   struct storage_test_result reread_result;
   char unique_paths[4][JS2300_FRONTEND_MAX_PATH];
   char indexed_path[JS2300_FRONTEND_MAX_PATH];
   char state_before[384];
   char state_after[384];
   char temp_path[JS2300_FRONTEND_MAX_PATH];
   char report[4096];
   size_t used = 0;
   unsigned known_count = 0;
   unsigned test_count;
   unsigned long indexed_size = 0;
   unsigned long written = 0;
   uint32_t write_ms = 0;
   uint32_t fsync_ms = 0;
   uint32_t total_ms = 0;
   uint32_t write_checksum = 0;
   int write_ret;

   memset(unique_paths, 0, sizeof(unique_paths));
   test_count = storage_test_build_cases(tests, STORAGE_TEST_MAX_CASES,
      unique_paths, &known_count, indexed_path, sizeof(indexed_path),
      &indexed_size, 0);
   (void)unifrog_platform_sd_describe(state_before, sizeof(state_before));
   unifrog_platform_sd_debug_dump("storage_quick_before");
   storage_test_progress(frontend, "Storage quick bench", "Measuring read",
      test_count ? tests[0].path : "no source");

   storage_test_append(report, sizeof(report), &used,
      "show=1\n"
      "title=STORAGE QUICK BENCH\n"
      "detail=Short synced benchmark with actual SD host parameters\n"
      "item|OK|State before|%s|runtime_supported=%d\n",
      state_before, unifrog_platform_sd_runtime_supported());

   if (test_count == 0) {
      storage_test_append(report, sizeof(report), &used,
         "item|FAIL|Read source|No package or ROM source found|known=%u indexed=%s\n",
         known_count, indexed_path);
      storage_test_write_file(JS2300_FRONTEND_STORAGE_QUICK_BENCH_REPORT,
         report, used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, used);
      return 0;
   }

   read_case = tests[0];
   read_case.chunk_size = 65536u;
   read_case.byte_limit = 4u * 1024u * 1024u;
   read_case.pause_ms = 0;
   if (storage_test_read_file(&read_case, &read_result) != 0)
      unifrog_platform_sd_debug_dump("storage_quick_read_error");
   storage_test_append(report, sizeof(report), &used,
      "item|%s|Source read|%lu KiB/s|bytes=%lu ms=%lu chunk=%lu ret=%d errno=%d crc=%08lx path=%s\n",
      read_result.ret == 0 ? "OK" : "FAIL",
      storage_test_kib_s(read_result.bytes, read_result.ms),
      read_result.bytes, (unsigned long)read_result.ms,
      (unsigned long)read_result.chunk_size, read_result.ret,
      read_result.err, (unsigned long)read_result.checksum,
      read_result.path);

   snprintf(temp_path, sizeof(temp_path),
      UNIFROG_DATA_ROOT "/storage-quick-benchmark.bin");
   storage_test_progress(frontend, "Storage quick bench", "Measuring write",
      temp_path);
   write_ret = storage_quick_write_file(temp_path, 4u * 1024u * 1024u,
      &written, &write_ms, &fsync_ms, &total_ms, &write_checksum);
   if (write_ret != 0)
      unifrog_platform_sd_debug_dump("storage_quick_write_error");
   storage_test_append(report, sizeof(report), &used,
      "item|%s|Synced temp write|%lu KiB/s total  %lu KiB/s loop|bytes=%lu write_ms=%lu fsync_ms=%lu total_ms=%lu ret=%d crc=%08lx path=%s\n",
      write_ret == 0 ? "OK" : "FAIL",
      storage_test_kib_s(written, total_ms),
      storage_test_kib_s(written, write_ms),
      written, (unsigned long)write_ms, (unsigned long)fsync_ms,
      (unsigned long)total_ms, write_ret, (unsigned long)write_checksum,
      temp_path);

   storage_test_progress(frontend, "Storage quick bench", "Measuring readback",
      temp_path);
   memset(&read_case, 0, sizeof(read_case));
   read_case.label = "readback";
   read_case.path = temp_path;
   read_case.chunk_size = 65536u;
   read_case.byte_limit = 4u * 1024u * 1024u;
   if (storage_test_read_file(&read_case, &reread_result) != 0)
      unifrog_platform_sd_debug_dump("storage_quick_readback_error");
   storage_test_append(report, sizeof(report), &used,
      "item|%s|Temp readback|%lu KiB/s|bytes=%lu ms=%lu chunk=%lu ret=%d errno=%d crc=%08lx path=%s\n",
      reread_result.ret == 0 ? "OK" : "FAIL",
      storage_test_kib_s(reread_result.bytes, reread_result.ms),
      reread_result.bytes, (unsigned long)reread_result.ms,
      (unsigned long)reread_result.chunk_size, reread_result.ret,
      reread_result.err, (unsigned long)reread_result.checksum,
      reread_result.path);
   (void)unlink(temp_path);

   {
      unsigned long source_kib_s =
         storage_test_kib_s(read_result.bytes, read_result.ms);
      unsigned long reread_kib_s =
         storage_test_kib_s(reread_result.bytes, reread_result.ms);
      unsigned long sustained_kib_s = source_kib_s;
      unsigned long window = 524288ul;
      unsigned slots = 16u;
      unsigned long prefill = 2097152ul;
      unsigned long preload = 0ul;

      if (reread_kib_s != 0 &&
          (sustained_kib_s == 0 || reread_kib_s < sustained_kib_s))
         sustained_kib_s = reread_kib_s;
      if (sustained_kib_s < 512ul) {
         window = 262144ul;
         prefill = 1048576ul;
      } else if (sustained_kib_s >= 4096ul) {
         window = 1048576ul;
         prefill = 4194304ul;
         preload = 33554432ul;
      } else if (sustained_kib_s >= 1536ul) {
         prefill = 4194304ul;
         preload = 16777216ul;
      }
      storage_test_append(report, sizeof(report), &used,
         "item|OK|Media buffer suggestion|%lu KiB/s sustained|MEDIA_VIDEO_READAHEAD_SIZE=%lu MEDIA_VIDEO_READAHEAD_SLOTS=%u MEDIA_VIDEO_PREFILL_MAX_BYTES=%lu MEDIA_VIDEO_PRELOAD_MAX_BYTES=%lu\n",
         sustained_kib_s, window, slots, prefill, preload);
   }

   (void)unifrog_platform_sd_describe(state_after, sizeof(state_after));
   unifrog_platform_sd_debug_dump("storage_quick_after");
   storage_test_append(report, sizeof(report), &used,
      "item|OK|State after|%s|active=%s\n"
      "detail=read=%lu KiB/s write_synced=%lu KiB/s readback=%lu KiB/s actual_state=%s\n",
      state_after, unifrog_platform_sd_active_profile(),
      storage_test_kib_s(read_result.bytes, read_result.ms),
      storage_test_kib_s(written, total_ms),
      storage_test_kib_s(reread_result.bytes, reread_result.ms),
      state_after);

   storage_test_write_file(JS2300_FRONTEND_STORAGE_QUICK_BENCH_REPORT,
      report, used);
   storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
      report, used);
   (void)unifrog_log_flush();
   return 0;
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

int run_storage_test(struct js2300_frontend *frontend)
{
   static const char *runtime_profiles[] = {
      "wide1",
      "wide2",
      "wide4",
      "wide8",
      "wide10",
      "wide12",
      "wide14",
      "wide16",
      "wide18",
      "wide20",
      "wide22",
      "wide24",
      "wide25",
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
      return -1;
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

int run_storage_full_test(struct js2300_frontend *frontend)
{
   static const char *runtime_profiles[] = {
      "wide1",
      "wide2",
      "wide4",
      "wide8",
      "wide10",
      "wide12",
      "wide14",
      "wide16",
      "wide18",
      "wide20",
      "wide22",
      "wide24",
      "wide25",
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

int run_storage_mode_test(struct js2300_frontend *frontend,
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

int run_storage_stress_test(struct js2300_frontend *frontend,
   const char *profile)
{
   struct storage_test_case tests[STORAGE_TEST_MAX_CASES];
   char unique_paths[4][JS2300_FRONTEND_MAX_PATH];
   char indexed_path[JS2300_FRONTEND_MAX_PATH];
   struct storage_stress_context ctx;
   char *report;
   size_t report_used = 0;
   size_t old_auto_flush;
   unsigned known_count = 0;
   unsigned test_count;
   unsigned long indexed_size = 0;
   int runtime_supported;
   int runtime_sweep;
   int switch_ret = 0;
   int restore_ret = 0;
   int storage_safe = 1;
   int task_ok = 1;
   uint32_t start_ms;
   uint32_t last_ui_ms = 0;
   char detail[192];

   if (!storage_test_profile_is_allowed(profile))
      profile = "safe";
   memset(&ctx, 0, sizeof(ctx));

   storage_test_active_title = "Storage stress";
   report = malloc(STORAGE_TEST_REPORT_BYTES);
   if (!report) {
      storage_test_progress(frontend, storage_test_active_title,
         "Out of memory", "Cannot allocate report buffer");
      return -1;
   }
   report[0] = '\0';

   memset(unique_paths, 0, sizeof(unique_paths));
   test_count = storage_test_build_cases(tests, STORAGE_TEST_MAX_CASES,
      unique_paths, &known_count, indexed_path, sizeof(indexed_path),
      &indexed_size, 0);
   if (test_count == 0) {
      storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
         "show=1\n"
         "title=STORAGE STRESS\n"
         "detail=No readable probe source found\n"
         "item|FAIL|Probe source|Need firmware, cores, /ROMS/test.md, or indexed ROMs|profile=%s\n",
         profile);
      storage_test_write_file(JS2300_FRONTEND_STORAGE_STRESS_REPORT,
         report, report_used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
      free(report);
      return 0;
   }

   runtime_supported = unifrog_platform_sd_runtime_supported();
   runtime_sweep = runtime_supported &&
      strcmp(UNIFROG_SD_MODE, "safe") == 0;
   printf("unifrog storage_stress begin profile=%s mode=%s runtime_supported=%d runtime_sweep=%d read=%s indexed=%s indexed_bytes=%lu\n",
      profile, UNIFROG_SD_MODE, runtime_supported, runtime_sweep,
      tests[0].path ? tests[0].path : "", indexed_path, indexed_size);

   storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
      "show=1\n"
      "title=STORAGE STRESS\n"
      "item|OK|Profile %s|Concurrent read/write for %u ms|boot=%s runtime_supported=%d runtime_sweep=%d\n",
      profile, STORAGE_STRESS_DURATION_MS, UNIFROG_SD_MODE,
      runtime_supported, runtime_sweep);
   storage_test_write_file(JS2300_FRONTEND_STORAGE_STRESS_REPORT,
      report, report_used);
   storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
      report, report_used);
   (void)unifrog_log_flush();

   if (strcmp(profile, "safe") != 0 && !runtime_sweep) {
      storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
         "detail=Runtime switching unavailable from SD_MODE=%s\n"
         "item|FAIL|Profile %s|Build with SD_MODE=safe to sweep runtime profiles|runtime_supported=%d\n",
         UNIFROG_SD_MODE, profile, runtime_supported);
      storage_test_write_file(JS2300_FRONTEND_STORAGE_STRESS_REPORT,
         report, report_used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
      (void)unifrog_log_flush();
      free(report);
      return 0;
   }

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
         "Concurrent read/write next");
      switch_ret = unifrog_platform_sd_apply_profile(profile,
         STORAGE_TEST_SWITCH_ATTEMPTS, STORAGE_TEST_SWITCH_DELAY_MS,
         detail, sizeof(detail));
      if (switch_ret != 0) {
         storage_safe = 0;
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|FAIL|Switch %s|Stress skipped|%s\n", profile, detail);
      }
   } else {
      snprintf(detail, sizeof(detail), "mode %s", UNIFROG_SD_MODE);
   }

   if (switch_ret == 0) {
      ctx.read_path = tests[0].path;
      ctx.duration_ms = STORAGE_STRESS_DURATION_MS;
      snprintf(ctx.write_path, sizeof(ctx.write_path),
         UNIFROG_DATA_ROOT "/storage-stress-%s.bin", profile);
      storage_test_progress(frontend, storage_test_active_title,
         "Running concurrent read/write", profile);
      unifrog_platform_storage_diag_note(profile, "stress read/write start");
      printf("unifrog storage_stress run profile=%s read=%s write=%s duration=%u detail=%s\n",
         profile, ctx.read_path, ctx.write_path, ctx.duration_ms, detail);

      if (xTaskCreate(storage_stress_reader_task, "sdstress_r",
          configTASK_STACK_DEPTH, &ctx, portPRI_TASK_NORMAL, NULL) != pdPASS)
         task_ok = 0;
      if (xTaskCreate(storage_stress_writer_task, "sdstress_w",
          configTASK_STACK_DEPTH, &ctx, portPRI_TASK_NORMAL, NULL) != pdPASS)
         task_ok = 0;
      if (!task_ok) {
         ctx.stop = 1;
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|FAIL|Task create %s|Unable to create reader/writer tasks|read_done=%d write_done=%d\n",
            profile, ctx.reader_done, ctx.writer_done);
      } else {
         ctx.start = 1;
         while ((unifrog_perf_time_ms() - start_ms) < ctx.duration_ms) {
            uint32_t now = unifrog_perf_time_ms();

            if (ctx.read_errors || ctx.write_errors)
               break;
            if ((now - last_ui_ms) >= 500u) {
               char line1[96];
               char line2[96];

               snprintf(line1, sizeof(line1), "%s %lu/%lu ms", profile,
                  (unsigned long)(now - start_ms),
                  (unsigned long)ctx.duration_ms);
               snprintf(line2, sizeof(line2), "R %luK/%lu ops  W %luK/%lu ops",
                  ctx.read_bytes / 1024ul, ctx.read_ops,
                  ctx.write_bytes / 1024ul, ctx.write_ops);
               storage_test_progress(frontend, storage_test_active_title,
                  line1, line2);
               printf("unifrog storage_stress progress profile=%s elapsed=%lu read=%lu write=%lu rops=%lu wops=%lu rerr=%u werr=%u\n",
                  profile, (unsigned long)(now - start_ms),
                  ctx.read_bytes, ctx.write_bytes, ctx.read_ops,
                  ctx.write_ops, ctx.read_errors, ctx.write_errors);
               last_ui_ms = now;
            }
            msleep(50);
         }
         ctx.stop = 1;
         for (unsigned i = 0; i < 100u &&
             (!ctx.reader_done || !ctx.writer_done); i++)
            msleep(20);
         storage_test_append(report, STORAGE_TEST_REPORT_BYTES, &report_used,
            "item|%s|Stress %s|read=%lu bytes %lu ops write=%lu bytes %lu ops %lu ms|rerr=%u errno=%d werr=%u errno=%d rsum=%08lx wsum=%08lx\n",
            (!ctx.read_errors && !ctx.write_errors &&
             ctx.reader_done && ctx.writer_done) ? "OK" : "FAIL",
            profile,
            ctx.read_bytes, ctx.read_ops, ctx.write_bytes, ctx.write_ops,
            (unsigned long)(unifrog_perf_time_ms() - start_ms),
            ctx.read_errors, ctx.read_errno,
            ctx.write_errors, ctx.write_errno,
            (unsigned long)ctx.read_checksum,
            (unsigned long)ctx.write_checksum);
      }
      (void)unlink(ctx.write_path);
   }

   if (strcmp(profile, "safe") != 0) {
      char restore_detail[192];

      msleep(100);
      restore_ret = storage_test_restore_safe(frontend,
         "Restoring boot mode", profile, restore_detail,
         sizeof(restore_detail));
      if (restore_ret != 0) {
         storage_safe = 0;
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
      "detail=stress %s read=%luK write=%luK rerr=%u werr=%u switch=%d restore=%d\n",
      profile, ctx.read_bytes / 1024ul, ctx.write_bytes / 1024ul,
      ctx.read_errors, ctx.write_errors, switch_ret, restore_ret);

   unifrog_log_defer_end();
   unifrog_log_set_auto_flush_bytes(old_auto_flush);
   unifrog_platform_set_storage_stage_callback(NULL, NULL);

   if (storage_safe) {
      unifrog_platform_set_storage_log_suspended(0);
      storage_test_progress(frontend, storage_test_active_title,
         "Writing final report", profile);
      storage_test_write_file(JS2300_FRONTEND_STORAGE_STRESS_REPORT,
         report, report_used);
      storage_test_write_file(JS2300_FRONTEND_SYSTEM_CHECK_REPORT,
         report, report_used);
      (void)unifrog_log_flush();
   }

   printf("unifrog storage_stress done profile=%s safe=%d switch=%d restore=%d read=%lu write=%lu rerr=%u werr=%u ms=%lu report=%s\n",
      profile, storage_safe, switch_ret, restore_ret,
      ctx.read_bytes, ctx.write_bytes, ctx.read_errors, ctx.write_errors,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      JS2300_FRONTEND_STORAGE_STRESS_REPORT);
   free(report);
   return storage_safe && switch_ret == 0 && restore_ret == 0 &&
      ctx.read_errors == 0 && ctx.write_errors == 0 ? 0 : -1;
}
