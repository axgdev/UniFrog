#include <unifrog/storage_probe.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <kernel/delay.h>

#include <unifrog/build_info.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>
#include <unifrog/platform.h>
#include <unifrog/text.h>

#define FAST_PROBE_REPORT_BYTES 32768u
#define FAST_PROBE_MAX_FILES 6u
#define FAST_PROBE_CYCLES 5u
#define FAST_PROBE_ROUNDS 3u
#define FAST_PROBE_READ_LIMIT (2u * 1024u * 1024u)
#define FAST_PROBE_READ_CHUNK (64u * 1024u)
#define FAST_PROBE_SWITCH_ATTEMPTS 10u
#define FAST_PROBE_SWITCH_DELAY_MS 100u
#define FAST_PROBE_FILEUART_BURST_LINES 2u
#define FAST_PROBE_SCRATCH_PATH \
   "/media/mmcblk0/unifrog/storage-fast-probe-scratch.txt"
#define WRITE_PROBE_REPORT_BYTES 16384u
#define WRITE_PROBE_SCRATCH_PATH \
   UNIFROG_DATA_ROOT "/storage-write-probe-scratch.bin"
#define WRITE_PROBE_SMALL_DIR \
   UNIFROG_DATA_ROOT "/storage-write-probe-small"
#define WRITE_PROBE_CHUNK_BYTES 4096u
#define WRITE_PROBE_CHUNKS 128u
#define WRITE_PROBE_SMALL_FILES 48u

extern void fileuart_get_debug_status(uint32_t *pending, uint32_t *suspended,
   uint32_t *quiet_ticks, uint32_t *dirty_bytes);

struct fast_probe_read {
   const char *path;
   unsigned long bytes;
   unsigned long ms;
   unsigned long kib_s;
   uint32_t checksum;
   int ret;
   int err;
};

struct fast_probe_path {
   char path[256];
   unsigned long size;
};

static void fast_probe_append(char *dst, size_t dst_size, size_t *used,
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
      *used = dst_size - 1u;
   else
      *used += (size_t)wrote;
}

static void fast_probe_progress(unifrog_storage_probe_progress_cb progress,
   void *userdata, const char *line1, const char *line2)
{
   if (progress)
      progress(userdata, line1, line2);
   unifrog_platform_storage_diag_note(line1 ? line1 : "fast_probe",
      line2 ? line2 : "");
}

static int fast_probe_write_report(const char *report, size_t report_used)
{
   char tmp[256];
   FILE *file;
   size_t wrote;
   int close_ret;
   int ret = -1;

   if (!report || report_used == 0)
      return -1;
   snprintf(tmp, sizeof(tmp), "%s.tmp", UNIFROG_STORAGE_FAST_PROBE_REPORT);
   file = fopen(tmp, "wb");
   if (!file)
      return -1;
   wrote = fwrite(report, 1, report_used, file);
   close_ret = fclose(file);
   if (wrote == report_used && close_ret == 0) {
      unlink(UNIFROG_STORAGE_FAST_PROBE_REPORT);
      if (rename(tmp, UNIFROG_STORAGE_FAST_PROBE_REPORT) == 0)
         ret = 0;
   }
   if (ret != 0)
      unlink(tmp);
   return ret;
}

static int write_probe_write_report(const char *report, size_t report_used)
{
   char tmp[256];
   FILE *file;
   size_t wrote;
   int close_ret;
   int ret = -1;

   if (!report || report_used == 0)
      return -1;
   snprintf(tmp, sizeof(tmp), "%s.tmp", UNIFROG_STORAGE_WRITE_PROBE_REPORT);
   file = fopen(tmp, "wb");
   if (!file)
      return -1;
   wrote = fwrite(report, 1, report_used, file);
   close_ret = fclose(file);
   if (wrote == report_used && close_ret == 0) {
      unlink(UNIFROG_STORAGE_WRITE_PROBE_REPORT);
      if (rename(tmp, UNIFROG_STORAGE_WRITE_PROBE_REPORT) == 0)
         ret = 0;
   }
   if (ret != 0)
      unlink(tmp);
   return ret;
}

static void write_probe_fill(unsigned char *buffer, size_t size,
   unsigned phase)
{
   for (size_t i = 0; i < size; i++)
      buffer[i] = (unsigned char)((i * 33u + phase * 17u) & 0xffu);
}

static int write_probe_write_chunks(int fd, unsigned char *buffer,
   unsigned chunks, int fsync_each, uint32_t *write_ms, uint32_t *fsync_ms)
{
   uint32_t write_total = 0;
   uint32_t fsync_total = 0;

   for (unsigned i = 0; i < chunks; i++) {
      uint32_t start = unifrog_perf_time_ms();

      write_probe_fill(buffer, WRITE_PROBE_CHUNK_BYTES, i);
      if (write(fd, buffer, WRITE_PROBE_CHUNK_BYTES) !=
          (ssize_t)WRITE_PROBE_CHUNK_BYTES)
         return errno ? -errno : -EIO;
      write_total += unifrog_perf_time_ms() - start;
      if (fsync_each) {
         start = unifrog_perf_time_ms();
         if (fsync(fd) != 0)
            return errno ? -errno : -EIO;
         fsync_total += unifrog_perf_time_ms() - start;
      }
   }
   if (write_ms)
      *write_ms = write_total;
   if (fsync_ms)
      *fsync_ms = fsync_total;
   return 0;
}

static int write_probe_run_phase(unifrog_storage_probe_progress_cb progress,
   void *userdata, const char *phase, const char *detail, int flags,
   int fsync_each, int fsync_end, char *report, size_t report_size,
   size_t *report_used)
{
   unsigned char *buffer;
   uint32_t start_ms;
   uint32_t write_ms = 0;
   uint32_t fsync_ms = 0;
   uint32_t end_fsync_ms = 0;
   uint32_t close_ms;
   int fd;
   int ret = 0;
   int close_ret;

   fast_probe_progress(progress, userdata, phase, detail);
   unifrog_log("storage_write_probe phase_begin phase=%s detail=\"%s\" fsync_each=%d fsync_end=%d\n",
      phase ? phase : "", detail ? detail : "", fsync_each, fsync_end);
   buffer = malloc(WRITE_PROBE_CHUNK_BYTES);
   if (!buffer)
      return -ENOMEM;
   start_ms = unifrog_perf_time_ms();
   fd = open(WRITE_PROBE_SCRATCH_PATH, flags, 0666);
   if (fd < 0) {
      ret = errno ? -errno : -EIO;
      free(buffer);
      return ret;
   }
   ret = write_probe_write_chunks(fd, buffer, WRITE_PROBE_CHUNKS,
      fsync_each, &write_ms, &fsync_ms);
   if (ret == 0 && fsync_end) {
      uint32_t fsync_start = unifrog_perf_time_ms();

      if (fsync(fd) != 0)
         ret = errno ? -errno : -EIO;
      end_fsync_ms = unifrog_perf_time_ms() - fsync_start;
   }
   {
      uint32_t close_start = unifrog_perf_time_ms();

      close_ret = close(fd);
      close_ms = unifrog_perf_time_ms() - close_start;
   }
   if (close_ret != 0 && ret == 0)
      ret = errno ? -errno : -EIO;
   free(buffer);
   fast_probe_append(report, report_size, report_used,
      "item|%s|%s|ret=%d total=%lums write=%lums fsync_each=%lums fsync_end=%lums close=%lums|%s\n",
      ret == 0 ? "OK" : "FAIL", phase ? phase : "phase", ret,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)write_ms, (unsigned long)fsync_ms,
      (unsigned long)end_fsync_ms, (unsigned long)close_ms,
      detail ? detail : "");
   (void)write_probe_write_report(report, *report_used);
   unifrog_log("storage_write_probe phase_done phase=%s ret=%d total_ms=%lu write_ms=%lu fsync_each_ms=%lu fsync_end_ms=%lu close_ms=%lu\n",
      phase ? phase : "", ret,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)write_ms, (unsigned long)fsync_ms,
      (unsigned long)end_fsync_ms, (unsigned long)close_ms);
   return ret;
}

static int write_probe_small_files(unifrog_storage_probe_progress_cb progress,
   void *userdata, char *report, size_t report_size, size_t *report_used)
{
   unsigned char buffer[512];
   uint32_t start_ms = unifrog_perf_time_ms();
   uint32_t close_ms = 0;
   unsigned ok = 0;
   int ret = 0;

   fast_probe_progress(progress, userdata, "small files",
      "create/write/close loop");
   unifrog_log("storage_write_probe phase_begin phase=small_files files=%u\n",
      WRITE_PROBE_SMALL_FILES);
   (void)mkdir(WRITE_PROBE_SMALL_DIR, 0777);
   for (unsigned i = 0; i < WRITE_PROBE_SMALL_FILES; i++) {
      char path[256];
      uint32_t close_start;
      int fd;

      snprintf(path, sizeof(path), "%s/file%03u.bin",
         WRITE_PROBE_SMALL_DIR, i);
      write_probe_fill(buffer, sizeof(buffer), i);
      fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
      if (fd < 0) {
         ret = errno ? -errno : -EIO;
         break;
      }
      if (write(fd, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer) &&
          ret == 0)
         ret = errno ? -errno : -EIO;
      close_start = unifrog_perf_time_ms();
      if (close(fd) != 0 && ret == 0)
         ret = errno ? -errno : -EIO;
      close_ms += unifrog_perf_time_ms() - close_start;
      if (ret != 0)
         break;
      ok++;
   }
   fast_probe_append(report, report_size, report_used,
      "item|%s|small files|ret=%d ok=%u/%u total=%lums close=%lums|512-byte files\n",
      ret == 0 ? "OK" : "FAIL", ret, ok, WRITE_PROBE_SMALL_FILES,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)close_ms);
   (void)write_probe_write_report(report, *report_used);
   unifrog_log("storage_write_probe phase_done phase=small_files ret=%d ok=%u total_ms=%lu close_ms=%lu\n",
      ret, ok, (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)close_ms);
   return ret;
}

static void fast_probe_fileuart_status(const char *tag)
{
   uint32_t pending = 0;
   uint32_t suspended = 0;
   uint32_t quiet_ticks = 0;
   uint32_t dirty_bytes = 0;

   fileuart_get_debug_status(&pending, &suspended, &quiet_ticks,
      &dirty_bytes);
   printf("unifrog fast_sd_probe fileuart tag=%s pending=%lu suspended=%lu quiet=%lu dirty=%lu\n",
      tag ? tag : "", (unsigned long)pending, (unsigned long)suspended,
      (unsigned long)quiet_ticks, (unsigned long)dirty_bytes);
}

static int fast_probe_fileuart_write(const char *profile, unsigned round,
   unsigned file_index, const char *phase)
{
   char text[192];
   int fd;
   int ret = 0;
   int err = 0;
   int len;

   len = snprintf(text, sizeof(text),
      "fileuart_probe profile=%s round=%u file=%u phase=%s ms=%lu\n",
      profile ? profile : "", round, file_index, phase ? phase : "",
      (unsigned long)unifrog_perf_time_ms());
   if (len <= 0)
      return -1;

   fd = open("/dev/fileuart", O_WRONLY);
   if (fd < 0) {
      ret = -1;
      err = errno;
   } else {
      for (unsigned i = 0; i < FAST_PROBE_FILEUART_BURST_LINES; i++) {
         if (write(fd, text, (size_t)len) != len) {
            ret = -1;
            err = errno ? errno : EIO;
            break;
         }
      }
      if (close(fd) != 0 && ret == 0) {
         ret = -1;
         err = errno;
      }
   }

   fast_probe_fileuart_status(phase);
   printf("unifrog fast_sd_probe fileuart_write profile=%s round=%u file=%u phase=%s ret=%d errno=%d\n",
      profile ? profile : "", round, file_index, phase ? phase : "",
      ret, err);
   return ret == 0 ? 0 : -err;
}

static int fast_probe_path_exists(const struct fast_probe_path *paths,
   unsigned count, const char *path)
{
   for (unsigned i = 0; i < count; i++) {
      if (strcmp(paths[i].path, path) == 0)
         return 1;
   }
   return 0;
}

static int fast_probe_add_path(struct fast_probe_path *paths, unsigned *count,
   const char *path)
{
   struct stat st;

   if (!paths || !count || *count >= FAST_PROBE_MAX_FILES || !path ||
       !path[0])
      return -1;
   if (fast_probe_path_exists(paths, *count, path))
      return -1;
   if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
      return -1;
   unifrog_text_copy(paths[*count].path, sizeof(paths[*count].path), path);
   paths[*count].size = (unsigned long)st.st_size;
   (*count)++;
   return 0;
}

static unsigned fast_probe_build_paths(struct fast_probe_path *paths,
   unsigned max_paths)
{
   static const char *known_paths[] = {
      "/media/mmcblk0/ROMS/probes/test20M.md",
      "/media/mmcblk0/ROMS/probes/test10M.md",
      "/media/mmcblk0/ROMS/probes/test5M.md",
      "/media/mmcblk0/ROMS/probes/test1M.md",
      "/media/mmcblk0/firmware/unifrog.bin",
      "/media/mmcblk0/unifrog/cores/quicknes.bin",
      "/media/mmcblk0/unifrog/cores/pce-fast.bin",
      "/media/mmcblk0/unifrog/cores/picodrive.bin",
      "/media/mmcblk0/bios/bisrv.asd",
   };
   FILE *index;
   char line[640];
   unsigned count = 0;

   if (!paths || max_paths == 0)
      return 0;
   memset(paths, 0, sizeof(paths[0]) * max_paths);
   for (unsigned i = 0; i < sizeof(known_paths) / sizeof(known_paths[0]); i++) {
      if (count >= max_paths)
         return count;
      (void)fast_probe_add_path(paths, &count, known_paths[i]);
   }

   index = fopen("/media/mmcblk0/unifrog/game-index.txt", "rb");
   if (!index)
      return count;
   while (fgets(line, sizeof(line), index)) {
      char *p1;
      char *p2;
      char *p3;
      char *p4;

      if (count >= max_paths)
         break;
      if (strncmp(line, "game|", 5) != 0)
         continue;
      p1 = strchr(line, '|');
      p2 = p1 ? strchr(p1 + 1, '|') : NULL;
      p3 = p2 ? strchr(p2 + 1, '|') : NULL;
      p4 = p3 ? strchr(p3 + 1, '|') : NULL;
      if (!p3 || !p4)
         continue;
      *p4 = '\0';
      (void)fast_probe_add_path(paths, &count, p3 + 1);
   }
   fclose(index);
   return count;
}

static int fast_probe_read_file(const char *path, struct fast_probe_read *out)
{
   unsigned char *buffer;
   uint32_t start_ms;
   uint32_t checksum = 2166136261u;
   unsigned long total = 0;
   int fd;
   int ret = 0;
   int err = 0;

   if (!path || !out)
      return -1;
   memset(out, 0, sizeof(*out));
   out->path = path;
   buffer = malloc(FAST_PROBE_READ_CHUNK);
   if (!buffer) {
      out->ret = -1;
      out->err = ENOMEM;
      return -1;
   }
   fd = open(path, O_RDONLY);
   if (fd < 0) {
      out->ret = -1;
      out->err = errno;
      free(buffer);
      return -1;
   }

   start_ms = unifrog_perf_time_ms();
   while (total < FAST_PROBE_READ_LIMIT) {
      size_t want = FAST_PROBE_READ_CHUNK;
      ssize_t got;

      if ((unsigned long)want > FAST_PROBE_READ_LIMIT - total)
         want = (size_t)(FAST_PROBE_READ_LIMIT - total);
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
   }
   if (close(fd) != 0 && ret == 0) {
      ret = -1;
      err = errno;
   }
   free(buffer);

   out->ms = unifrog_perf_time_ms() - start_ms;
   out->bytes = total;
   out->kib_s = out->ms ? ((total / 1024ul) * 1000ul) / out->ms : 0ul;
   out->checksum = checksum;
   out->ret = ret;
   out->err = err;
   return ret;
}

static int fast_probe_restore(unifrog_storage_probe_progress_cb progress,
   void *userdata, char *detail, size_t detail_size)
{
   int ret;

   fast_probe_progress(progress, userdata, "restore boot", "safe profile");
   ret = unifrog_platform_sd_restore_boot(FAST_PROBE_SWITCH_ATTEMPTS,
      FAST_PROBE_SWITCH_DELAY_MS, detail, detail_size);
   if (ret != 0) {
      msleep(500);
      ret = unifrog_platform_sd_restore_boot(FAST_PROBE_SWITCH_ATTEMPTS,
         FAST_PROBE_SWITCH_DELAY_MS, detail, detail_size);
   }
   return ret;
}

static int fast_probe_write_scratch(const char *profile, unsigned round,
   unsigned file_index)
{
   char text[192];
   uint32_t now = unifrog_perf_time_ms();
   int fd;
   int ret = 0;
   int err = 0;
   int len;

   len = snprintf(text, sizeof(text),
      "fast_sd_probe profile=%s round=%u file=%u ms=%lu\n",
      profile ? profile : "", round, file_index, (unsigned long)now);
   if (len <= 0)
      return -1;
   fd = open(FAST_PROBE_SCRATCH_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
   if (fd < 0)
      return -errno;
   if (write(fd, text, (size_t)len) != len) {
      ret = -1;
      err = errno ? errno : EIO;
   }
   if (close(fd) != 0 && ret == 0) {
      ret = -1;
      err = errno;
   }
   return ret == 0 ? 0 : -err;
}

static int fast_probe_run_stress(unifrog_storage_probe_progress_cb progress,
   void *userdata, const char *profile, const struct fast_probe_path *paths,
   unsigned path_count, char *report, size_t report_size, size_t *report_used,
   unsigned long *out_kib_s)
{
   unsigned ok_reads = 0;
   unsigned failed_reads = 0;
   unsigned ok_writes = 0;
   unsigned failed_writes = 0;
   unsigned long total_bytes = 0;
   unsigned long total_ms = 0;
   uint32_t start_ms = unifrog_perf_time_ms();
   char line[64];

   if (out_kib_s)
      *out_kib_s = 0;
   for (unsigned round = 0; round < FAST_PROBE_ROUNDS; round++) {
      for (unsigned i = 0; i < path_count; i++) {
         struct fast_probe_read read_result;
         int write_ret;

         snprintf(line, sizeof(line), "%s r%u/%u f%u/%u",
            profile, round + 1u, FAST_PROBE_ROUNDS, i + 1u, path_count);
         fast_probe_progress(progress, userdata, line, "read");
         (void)fast_probe_fileuart_write(profile, round + 1u, i + 1u,
            "before_read");
         (void)fast_probe_read_file(paths[i].path, &read_result);
         printf("unifrog fast_sd_probe profile=%s round=%u file=%u read_ret=%d errno=%d bytes=%lu ms=%lu kib_s=%lu checksum=0x%08lx path=%s\n",
            profile, round + 1u, i + 1u, read_result.ret, read_result.err,
            read_result.bytes, read_result.ms, read_result.kib_s,
            (unsigned long)read_result.checksum, paths[i].path);
         (void)fast_probe_fileuart_write(profile, round + 1u, i + 1u,
            "after_read");
         if (read_result.ret == 0) {
            ok_reads++;
            total_bytes += read_result.bytes;
            total_ms += read_result.ms;
         } else {
            failed_reads++;
         }

         fast_probe_progress(progress, userdata, line, "scratch write");
         write_ret = fast_probe_write_scratch(profile, round + 1u, i + 1u);
         (void)fast_probe_fileuart_write(profile, round + 1u, i + 1u,
            "after_scratch");
         printf("unifrog fast_sd_probe profile=%s round=%u file=%u write_ret=%d path=%s\n",
            profile, round + 1u, i + 1u, write_ret,
            FAST_PROBE_SCRATCH_PATH);
         if (write_ret == 0)
            ok_writes++;
         else
            failed_writes++;

         if (read_result.ret != 0 || write_ret != 0) {
            fast_probe_append(report, report_size, report_used,
               "item|FAIL|%s r%u f%u|read=%d/%d write=%d|%s\n",
               profile, round + 1u, i + 1u, read_result.ret,
               read_result.err, write_ret, paths[i].path);
            return -1;
         }
      }
   }

   if (out_kib_s && total_ms)
      *out_kib_s = ((total_bytes / 1024ul) * 1000ul) / total_ms;
   fast_probe_append(report, report_size, report_used,
      "item|OK|%s stress|reads=%u writes=%u %lu KiB/s|%lu bytes %lu ms total=%lums\n",
      profile, ok_reads, ok_writes, out_kib_s ? *out_kib_s : 0ul,
      total_bytes, total_ms,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   if (failed_reads || failed_writes) {
      fast_probe_append(report, report_size, report_used,
         "item|FAIL|%s errors|read_fail=%u write_fail=%u|mixed stress\n",
         profile, failed_reads, failed_writes);
      return -1;
   }
   return 0;
}

int unifrog_storage_fast_probe_run(unifrog_storage_probe_progress_cb progress,
   void *userdata, char *summary, size_t summary_size)
{
   static const char *profiles[] = {
      "wide1",
      "wide2",
      "wide4",
      "wide8",
   };
   struct fast_probe_path paths[FAST_PROBE_MAX_FILES];
   char *report;
   size_t report_used = 0;
   size_t old_auto_flush;
   unsigned ok_profiles = 0;
   unsigned failed_profiles = 0;
   int runtime_supported;
   int storage_safe = 1;
   uint32_t start_ms;
   unsigned path_count;

   if (summary && summary_size)
      summary[0] = '\0';
   report = malloc(FAST_PROBE_REPORT_BYTES);
   if (!report)
      return -1;
   report[0] = '\0';

   fast_probe_progress(progress, userdata, "fast SD probe", "finding files");
   runtime_supported = unifrog_platform_sd_runtime_supported();
   path_count = fast_probe_build_paths(paths, FAST_PROBE_MAX_FILES);
   if (path_count == 0) {
      fast_probe_append(report, FAST_PROBE_REPORT_BYTES, &report_used,
         "show=1\n"
         "title=FAST SD PROBE\n"
         "detail=No readable test file\n"
         "item|FAIL|Input file|Expected probes, firmware, core, or indexed game|none\n");
      (void)fast_probe_write_report(report, report_used);
      free(report);
      return -1;
   }

   fast_probe_append(report, FAST_PROBE_REPORT_BYTES, &report_used,
      "show=1\n"
      "title=FAST SD PROBE\n"
      "item|OK|Build|mode=%s read=%s commit=%s|runtime=%d\n"
      "item|OK|Soak plan|%u profiles  %u cycles|low 4-bit 1-8 MHz only\n"
      "item|OK|Stress plan|%u files  %u rounds  2 MiB/read|scratch and fileuart writes around each read\n",
      UNIFROG_SD_MODE, UNIFROG_SD_READ_MODE, UNIFROG_GIT_COMMIT,
      runtime_supported,
      (unsigned)(sizeof(profiles) / sizeof(profiles[0])),
      FAST_PROBE_CYCLES, path_count, FAST_PROBE_ROUNDS);
   for (unsigned i = 0; i < path_count; i++) {
      fast_probe_append(report, FAST_PROBE_REPORT_BYTES, &report_used,
         "item|OK|File %u|%lu bytes|%s\n",
         i + 1u, paths[i].size, paths[i].path);
   }
   (void)fast_probe_write_report(report, report_used);
   (void)unifrog_log_flush_force();

   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_log_defer_begin();
   start_ms = unifrog_perf_time_ms();
   fast_probe_fileuart_status("probe_begin");

   {
      unsigned long boot_kib_s = 0;

      if (fast_probe_run_stress(progress, userdata, "boot", paths, path_count,
          report, FAST_PROBE_REPORT_BYTES, &report_used, &boot_kib_s) != 0)
         failed_profiles++;
   }

   for (unsigned cycle = 0; cycle < FAST_PROBE_CYCLES && storage_safe;
        cycle++) {
      for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
         char detail[192];
         char restore_detail[192];
         char line[80];
         char profile_label[64];
         unsigned long kib_s = 0;
         int stress_ret = -1;
         int switch_ret;
         int restore_ret;

         if (!unifrog_platform_sd_profile_allowed(profiles[i]))
            continue;
         snprintf(profile_label, sizeof(profile_label), "%s c%u/%u",
            profiles[i], cycle + 1u, FAST_PROBE_CYCLES);
         snprintf(line, sizeof(line), "switch %s", profile_label);
         fast_probe_progress(progress, userdata, line, "storage unmounted");
         detail[0] = '\0';
         switch_ret = unifrog_platform_sd_apply_profile(profiles[i],
            FAST_PROBE_SWITCH_ATTEMPTS, FAST_PROBE_SWITCH_DELAY_MS,
            detail, sizeof(detail));
         if (switch_ret != 0) {
            failed_profiles++;
            fast_probe_append(report, FAST_PROBE_REPORT_BYTES, &report_used,
               "item|FAIL|Switch %s|ret=%d|%s\n",
               profile_label, switch_ret, detail);
            restore_ret = fast_probe_restore(progress, userdata,
               restore_detail, sizeof(restore_detail));
            if (restore_ret != 0) {
               storage_safe = 0;
               break;
            }
            (void)fast_probe_fileuart_write(profile_label, cycle + 1u, 0,
               "after_switch_fail_restore");
            (void)unifrog_log_flush_force();
            (void)fast_probe_write_report(report, report_used);
            continue;
         }

         stress_ret = fast_probe_run_stress(progress, userdata, profile_label,
            paths, path_count, report, FAST_PROBE_REPORT_BYTES, &report_used,
            &kib_s);

         restore_ret = fast_probe_restore(progress, userdata, restore_detail,
            sizeof(restore_detail));
         if (restore_ret != 0) {
            storage_safe = 0;
            failed_profiles++;
            fast_probe_append(report, FAST_PROBE_REPORT_BYTES, &report_used,
               "item|FAIL|Restore after %s|ret=%d|%s\n",
               profile_label, restore_ret, restore_detail);
            break;
         }
         (void)fast_probe_fileuart_write(profile_label, cycle + 1u, 0,
            "after_restore");

         if (stress_ret == 0) {
            ok_profiles++;
            fast_probe_append(report, FAST_PROBE_REPORT_BYTES, &report_used,
               "item|OK|Restore after %s|%lu KiB/s|%s restore=%s\n",
               profile_label, kib_s, detail, restore_detail);
         } else {
            failed_profiles++;
            fast_probe_append(report, FAST_PROBE_REPORT_BYTES, &report_used,
               "item|FAIL|Restore after %s|stress failed|%s restore=%s\n",
               profile_label, detail, restore_detail);
         }

         (void)unifrog_log_flush_force();
         (void)fast_probe_write_report(report, report_used);
      }
   }

   fast_probe_append(report, FAST_PROBE_REPORT_BYTES, &report_used,
      "detail=%u ok  %u failed  safe=%d  %lums\n",
      ok_profiles, failed_profiles, storage_safe,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));

   unifrog_log_defer_end();
   unifrog_log_set_auto_flush_bytes(old_auto_flush);
   if (storage_safe) {
      fast_probe_fileuart_status("probe_end");
      fast_probe_progress(progress, userdata, "fast SD probe", "writing report");
      (void)fast_probe_write_report(report, report_used);
      (void)unifrog_log_flush_force();
   } else {
      printf("unifrog fast_sd_probe unsafe_storage report_checkpoint=%s\n",
         UNIFROG_STORAGE_FAST_PROBE_REPORT);
   }

   if (summary && summary_size) {
      snprintf(summary, summary_size, "%u ok %u failed%s",
         ok_profiles, failed_profiles, storage_safe ? "" : " unsafe");
   }
   printf("unifrog fast_sd_probe done ok=%u fail=%u safe=%d report=%s\n",
      ok_profiles, failed_profiles, storage_safe,
      UNIFROG_STORAGE_FAST_PROBE_REPORT);
   free(report);
   return storage_safe && failed_profiles == 0 ? 0 : -1;
}

int unifrog_storage_write_probe_run(unifrog_storage_probe_progress_cb progress,
   void *userdata, char *summary, size_t summary_size)
{
   char *report;
   size_t report_used = 0;
   size_t old_auto_flush;
   unsigned ok = 0;
   unsigned fail = 0;
   uint32_t start_ms = unifrog_perf_time_ms();

   if (summary && summary_size)
      summary[0] = '\0';
   report = malloc(WRITE_PROBE_REPORT_BYTES);
   if (!report)
      return -1;
   report[0] = '\0';

   fast_probe_progress(progress, userdata, "write probe", "preparing report");
   fast_probe_append(report, WRITE_PROBE_REPORT_BYTES, &report_used,
      "show=1\n"
      "title=STORAGE WRITE PROBE\n"
      "item|OK|Build|mode=%s read=%s commit=%s|chunk=%u chunks=%u\n"
      "item|OK|Purpose|separate write, close, fsync|report checkpoints after each phase\n",
      UNIFROG_SD_MODE, UNIFROG_SD_READ_MODE, UNIFROG_GIT_COMMIT,
      WRITE_PROBE_CHUNK_BYTES, WRITE_PROBE_CHUNKS);
   (void)write_probe_write_report(report, report_used);
   (void)unifrog_log_flush_force();

   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_log_defer_begin();

   if (write_probe_run_phase(progress, userdata, "single open",
       "512 KiB sequential, close once",
       O_CREAT | O_WRONLY | O_TRUNC, 0, 0, report,
       WRITE_PROBE_REPORT_BYTES, &report_used) == 0)
      ok++;
   else
      fail++;

   if (write_probe_run_phase(progress, userdata, "append open",
       "512 KiB append, close once",
       O_CREAT | O_WRONLY | O_APPEND, 0, 0, report,
       WRITE_PROBE_REPORT_BYTES, &report_used) == 0)
      ok++;
   else
      fail++;

   if (write_probe_small_files(progress, userdata, report,
       WRITE_PROBE_REPORT_BYTES, &report_used) == 0)
      ok++;
   else
      fail++;

   if (write_probe_run_phase(progress, userdata, "fsync at end",
       "512 KiB then explicit fsync",
       O_CREAT | O_WRONLY | O_TRUNC, 0, 1, report,
       WRITE_PROBE_REPORT_BYTES, &report_used) == 0)
      ok++;
   else
      fail++;

   if (write_probe_run_phase(progress, userdata, "fsync each",
       "16 KiB with fsync after each 4 KiB",
       O_CREAT | O_WRONLY | O_TRUNC, 1, 0, report,
       WRITE_PROBE_REPORT_BYTES, &report_used) == 0)
      ok++;
   else
      fail++;

   fast_probe_append(report, WRITE_PROBE_REPORT_BYTES, &report_used,
      "detail=%u ok  %u failed  %lums\n",
      ok, fail, (unsigned long)(unifrog_perf_time_ms() - start_ms));

   unifrog_log_defer_end();
   unifrog_log_set_auto_flush_bytes(old_auto_flush);
   fast_probe_progress(progress, userdata, "write probe", "saving report");
   (void)write_probe_write_report(report, report_used);
   (void)unifrog_log_flush_force();

   if (summary && summary_size)
      snprintf(summary, summary_size, "%u ok %u failed", ok, fail);
   unifrog_log("storage_write_probe done ok=%u fail=%u total_ms=%lu report=%s\n",
      ok, fail, (unsigned long)(unifrog_perf_time_ms() - start_ms),
      UNIFROG_STORAGE_WRITE_PROBE_REPORT);
   free(report);
   return fail == 0 ? 0 : -1;
}
