#include <unifrog/log.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <unifrog/perf.h>
#include <unifrog/paths.h>

#define MEMLOG_FALLBACK_BYTES (4 * 1024)
#define MEMLOG_FILE_MAX_BYTES (4 * 1024 * 1024)
#define MEMLOG_PERSIST_BASE 0x87ee7000u
#define MEMLOG_PERSIST_BYTES (1024 * 1024)
#define MEMLOG_PERSIST_MAGIC 0x524c4655u
#define MEMLOG_PERSIST_MAGIC_XOR 0xad8c99bau
#define MEMLOG_PERSIST_VERSION 1u
#ifndef UNIFROG_LOG_AUTO_FLUSH_BYTES
#define UNIFROG_LOG_AUTO_FLUSH_BYTES (4 * 1024)
#endif
#ifndef UNIFROG_LOG_FLUSH_EVERY
#define UNIFROG_LOG_FLUSH_EVERY 0
#endif
#ifndef UNIFROG_LOG_DISK_WRITES
#define UNIFROG_LOG_DISK_WRITES 1
#endif
#define MEMLOG_OPEN_RETRIES 5u
#define MEMLOG_OPEN_RETRY_US 50000u
#ifndef UNIFROG_LOG_FLUSH_FSYNC
#define UNIFROG_LOG_FLUSH_FSYNC 0
#endif
#ifndef UNIFROG_LOG_SYNC_FSYNC
#define UNIFROG_LOG_SYNC_FSYNC 0
#endif
#ifndef UNIFROG_LOG_RECOVERY_FSYNC
#define UNIFROG_LOG_RECOVERY_FSYNC 0
#endif
#ifndef UNIFROG_LOG_RESET_FSYNC
#define UNIFROG_LOG_RESET_FSYNC 0
#endif
#ifndef UNIFROG_GIT_COMMIT
#define UNIFROG_GIT_COMMIT "unknown"
#endif
#ifndef UNIFROG_GIT_TAG
#define UNIFROG_GIT_TAG ""
#endif

static const char *memlog_build_id(void)
{
   static char id[96];

   if (!id[0]) {
      if (UNIFROG_GIT_TAG[0])
         snprintf(id, sizeof(id), "%s_%s", UNIFROG_GIT_COMMIT,
            UNIFROG_GIT_TAG);
      else
         snprintf(id, sizeof(id), "%s", UNIFROG_GIT_COMMIT);
   }
   return id;
}

struct memlog_location {
   const char *unifrog_dir;
   const char *logs_dir;
   const char *crash_dir;
   const char *rotated_dir;
   const char *legacy_log;
   const char *legacy_prev;
   const char *legacy_crash;
};

struct memlog_persist_header {
   uint32_t magic;
   uint32_t magic_xor;
   uint32_t version;
   uint32_t header_bytes;
   uint32_t capacity;
   uint32_t capacity_xor;
   uint32_t len;
   uint32_t len_xor;
   uint32_t sequence;
   uint32_t sequence_xor;
   uint32_t flags;
   uint32_t boot_count;
};

static char memlog_fallback[MEMLOG_FALLBACK_BYTES];
static char *memlog = memlog_fallback;
static size_t memlog_capacity = MEMLOG_FALLBACK_BYTES;
static size_t memlog_len;
static size_t memlog_auto_flush_bytes = UNIFROG_LOG_FLUSH_EVERY ?
   1u : UNIFROG_LOG_AUTO_FLUSH_BYTES;
static int memlog_flush_every = UNIFROG_LOG_FLUSH_EVERY ? 1 : 0;
static int memlog_dropped;
static int memlog_flushing;
static int memlog_syncing;
static int memlog_disk_suspended;
static unsigned memlog_defer_depth;
static int memlog_flush_deferred;
static const char *memlog_last_path;
static int memlog_last_result;
static uint32_t memlog_quiet_until_ms;
static uint32_t memlog_sequence;
static uint32_t memlog_last_ms;
static int memlog_initialized;
static int memlog_using_persist;
static int memlog_recovery_pending;
static int memlog_disk_available;
static struct memlog_persist_header *memlog_persist;
static char memlog_current_path[160];
static char memlog_crash_path[160];
static char memlog_rotated_path[192];

static const struct memlog_location memlog_locations[] = {
   {
      UNIFROG_DATA_ROOT,
      UNIFROG_LOG_ROOT,
      UNIFROG_LOG_ROOT "/crashlogs",
      UNIFROG_LOG_ROOT "/rotatedlogs",
      "/media/mmcblk0/log.txt",
      "/media/mmcblk0/log-prev.txt",
      "/media/mmcblk0/log-recovery.txt",
   },
   {
      "/media/mmcblk0p1/unifrog_data",
      "/media/mmcblk0p1/unifrog_data/logs",
      "/media/mmcblk0p1/unifrog_data/logs/crashlogs",
      "/media/mmcblk0p1/unifrog_data/logs/rotatedlogs",
      "/media/mmcblk0p1/log.txt",
      "/media/mmcblk0p1/log-prev.txt",
      "/media/mmcblk0p1/log-recovery.txt",
   },
   {
      "/media/mmcblk0p2/unifrog_data",
      "/media/mmcblk0p2/unifrog_data/logs",
      "/media/mmcblk0p2/unifrog_data/logs/crashlogs",
      "/media/mmcblk0p2/unifrog_data/logs/rotatedlogs",
      "/media/mmcblk0p2/log.txt",
      "/media/mmcblk0p2/log-prev.txt",
      "/media/mmcblk0p2/log-recovery.txt",
   },
   {
      "/unifrog_data",
      "/unifrog_data/logs",
      "/unifrog_data/logs/crashlogs",
      "/unifrog_data/logs/rotatedlogs",
      "/log.txt",
      "/log-prev.txt",
      "/log-recovery.txt",
   },
};

static int memlog_disk_writes_enabled(void)
{
   return UNIFROG_LOG_DISK_WRITES ? 1 : 0;
}

static int memlog_disk_ready(void)
{
   return memlog_disk_writes_enabled() && memlog_disk_available;
}

static int memlog_disk_disabled_result(void)
{
   memlog_flush_deferred = 1;
   memlog_last_path = "retained-ram";
   memlog_last_result = UNIFROG_LOG_ERR_DISABLED;
   return UNIFROG_LOG_ERR_DISABLED;
}

static int memlog_disk_unavailable_result(void)
{
   memlog_flush_deferred = 1;
   memlog_last_path = "retained-ram-pending-sd";
   memlog_last_result = UNIFROG_LOG_ERR_UNAVAILABLE;
   return UNIFROG_LOG_ERR_UNAVAILABLE;
}

static int memlog_persist_valid(const struct memlog_persist_header *header)
{
   uint32_t capacity = MEMLOG_PERSIST_BYTES - sizeof(*header);

   if (header->magic != MEMLOG_PERSIST_MAGIC ||
       header->magic_xor != MEMLOG_PERSIST_MAGIC_XOR ||
       header->version != MEMLOG_PERSIST_VERSION ||
       header->header_bytes != sizeof(*header))
      return 0;
   if (header->capacity != capacity ||
       header->capacity_xor != ~capacity)
      return 0;
   if (header->len > capacity || header->len_xor != ~header->len)
      return 0;
   if (header->sequence_xor != ~header->sequence)
      return 0;
   return 1;
}

static void memlog_persist_commit_header(void)
{
   if (memlog_using_persist && memlog_persist)
      unifrog_perf_cache_flush(memlog_persist, sizeof(*memlog_persist));
}

static void memlog_persist_store(void)
{
   if (!memlog_using_persist || !memlog_persist)
      return;
   memlog_persist->len = (uint32_t)memlog_len;
   memlog_persist->len_xor = ~memlog_persist->len;
   memlog_persist->sequence = memlog_sequence;
   memlog_persist->sequence_xor = ~memlog_persist->sequence;
   memlog_persist_commit_header();
}

static void memlog_persist_clear(void)
{
   if (!memlog_using_persist || !memlog_persist)
      return;
   memlog_len = 0;
   memlog_dropped = 0;
   memlog_persist_store();
}

static void memlog_persist_init_header(struct memlog_persist_header *header)
{
   uint32_t capacity = MEMLOG_PERSIST_BYTES - sizeof(*header);

   memset(header, 0, sizeof(*header));
   header->magic = MEMLOG_PERSIST_MAGIC;
   header->magic_xor = MEMLOG_PERSIST_MAGIC_XOR;
   header->version = MEMLOG_PERSIST_VERSION;
   header->header_bytes = sizeof(*header);
   header->capacity = capacity;
   header->capacity_xor = ~capacity;
   header->len = 0;
   header->len_xor = ~header->len;
   header->sequence = 0;
   header->sequence_xor = ~header->sequence;
   header->boot_count = 1;
   memlog_persist_commit_header();
}

static void memlog_init(void)
{
   struct memlog_persist_header *header;
   uint32_t capacity;

   if (memlog_initialized)
      return;
   memlog_initialized = 1;

   header = (struct memlog_persist_header *)(uintptr_t)MEMLOG_PERSIST_BASE;
   capacity = MEMLOG_PERSIST_BYTES - sizeof(*header);
   if (memlog_persist_valid(header)) {
      memlog_persist = header;
      memlog = (char *)(header + 1);
      memlog_capacity = header->capacity;
      memlog_len = header->len;
      memlog_sequence = header->sequence;
      memlog_using_persist = 1;
      memlog_recovery_pending = memlog_len > 0;
      header->boot_count++;
      memlog_persist_commit_header();
      return;
   }

   memlog_persist = header;
   memlog = (char *)(header + 1);
   memlog_capacity = capacity;
   memlog_using_persist = 1;
   memlog_len = 0;
   memlog_sequence = 0;
   memlog_persist_init_header(header);
}

static void memlog_ensure_dirs(const struct memlog_location *loc)
{
   (void)mkdir(loc->unifrog_dir, 0777);
   (void)mkdir(loc->logs_dir, 0777);
   (void)mkdir(loc->crash_dir, 0777);
   (void)mkdir(loc->rotated_dir, 0777);
}

static void memlog_build_current_path(const struct memlog_location *loc,
   char *path, size_t path_size)
{
   snprintf(path, path_size, "%s/log_%s.txt", loc->logs_dir,
      memlog_build_id());
}

static void memlog_build_crash_path(const struct memlog_location *loc,
   char *path, size_t path_size)
{
   snprintf(path, path_size, "%s/crashlogs_%s.txt", loc->crash_dir,
      memlog_build_id());
}

static void memlog_build_rotated_path(const struct memlog_location *loc,
   char *path, size_t path_size)
{
   unsigned long boot = memlog_persist ? memlog_persist->boot_count : 0;

   snprintf(path, path_size, "%s/log_%s_boot%08lu_seq%08lu.txt",
      loc->rotated_dir, memlog_build_id(), boot,
      (unsigned long)memlog_sequence);
}

static int memlog_open(int flags)
{
   int fd = -1;

   memlog_last_path = NULL;
   if (!memlog_disk_writes_enabled()) {
      memlog_last_path = "retained-ram";
      return -1;
   }
   if (!memlog_disk_available) {
      memlog_last_path = "retained-ram-pending-sd";
      return -1;
   }
   for (size_t i = 0; i < sizeof(memlog_locations) /
         sizeof(memlog_locations[0]); i++) {
      memlog_ensure_dirs(&memlog_locations[i]);
      memlog_build_current_path(&memlog_locations[i], memlog_current_path,
         sizeof(memlog_current_path));
      fd = open(memlog_current_path, flags, 0666);
      if (fd >= 0) {
         memlog_last_path = memlog_current_path;
         break;
      }
      fd = open(memlog_locations[i].legacy_log, flags, 0666);
      if (fd >= 0) {
         memlog_last_path = memlog_locations[i].legacy_log;
         break;
      }
   }

   return fd;
}

static int memlog_open_retry(int flags)
{
   int fd;

   for (unsigned i = 0; i < MEMLOG_OPEN_RETRIES; i++) {
      fd = memlog_open(flags);
      if (fd >= 0)
         return fd;
      usleep(MEMLOG_OPEN_RETRY_US);
   }
   return -1;
}

static int memlog_recovery_open(void)
{
   int fd = -1;

   if (!memlog_disk_writes_enabled()) {
      memlog_last_path = "retained-ram";
      return -1;
   }
   if (!memlog_disk_available) {
      memlog_last_path = "retained-ram-pending-sd";
      return -1;
   }
   for (size_t i = 0; i < sizeof(memlog_locations) /
         sizeof(memlog_locations[0]); i++) {
      memlog_ensure_dirs(&memlog_locations[i]);
      memlog_build_crash_path(&memlog_locations[i], memlog_crash_path,
         sizeof(memlog_crash_path));
      fd = open(memlog_crash_path, O_CREAT | O_WRONLY | O_APPEND, 0666);
      if (fd >= 0)
         return fd;
      fd = open(memlog_locations[i].legacy_crash,
         O_CREAT | O_WRONLY | O_APPEND, 0666);
      if (fd >= 0)
         return fd;
   }

   return -1;
}

static int memlog_recovery_open_retry(void)
{
   int fd;

   for (unsigned i = 0; i < MEMLOG_OPEN_RETRIES; i++) {
      fd = memlog_recovery_open();
      if (fd >= 0)
         return fd;
      usleep(MEMLOG_OPEN_RETRY_US);
   }
   return -1;
}

static int memlog_flush_recovery(void)
{
   int fd;
   char marker[192];
   int marker_len;
   size_t pending;

   memlog_init();
   if (!memlog_recovery_pending || memlog_len == 0)
      return 0;
   if (!memlog_disk_writes_enabled())
      return memlog_disk_disabled_result();
   if (!memlog_disk_available)
      return memlog_disk_unavailable_result();
   if (memlog_disk_suspended) {
      memlog_flush_deferred = 1;
      memlog_last_result = 0;
      return 0;
   }

   pending = memlog_len;
   fd = memlog_recovery_open_retry();
   if (fd < 0) {
      memlog_last_result = UNIFROG_LOG_ERR_OPEN;
      return UNIFROG_LOG_ERR_OPEN;
   }

   marker_len = snprintf(marker, sizeof(marker),
      "\nUFLOG type=recovery ts_ms=%lu cyc=%08lx seq=%lu base=0x%08lx bytes=%lu event=recovered_persistent_buffer\n",
      (unsigned long)unifrog_perf_time_ms(),
      (unsigned long)unifrog_perf_count(),
      (unsigned long)++memlog_sequence,
      (unsigned long)MEMLOG_PERSIST_BASE,
      (unsigned long)pending);
   if (marker_len <= 0 || marker_len >= (int)sizeof(marker)) {
      close(fd);
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }
   if (write(fd, marker, (size_t)marker_len) != (ssize_t)marker_len ||
       write(fd, memlog, pending) != (ssize_t)pending ||
       write(fd, "UFLOG type=recovery_end\n",
          sizeof("UFLOG type=recovery_end\n") - 1) !=
          (ssize_t)(sizeof("UFLOG type=recovery_end\n") - 1)) {
      close(fd);
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }
#if UNIFROG_LOG_RECOVERY_FSYNC
   if (fsync(fd) != 0) {
      close(fd);
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }
#endif

   close(fd);
   memlog_recovery_pending = 0;
   memlog_persist_clear();
   memlog_last_result = 0;
   return 0;
}

static int memlog_storage_quiet(void)
{
   uint32_t now_ms;

   if (!memlog_quiet_until_ms)
      return 0;
   now_ms = unifrog_perf_time_ms();
   if ((int32_t)(memlog_quiet_until_ms - now_ms) > 0)
      return 1;
   memlog_quiet_until_ms = 0;
   return 0;
}

static void memlog_rotate_large_files(void)
{
   struct stat st;

   if (!memlog_disk_ready())
      return;

   for (size_t i = 0; i < sizeof(memlog_locations) /
         sizeof(memlog_locations[0]); i++) {
      memlog_ensure_dirs(&memlog_locations[i]);
      memlog_build_current_path(&memlog_locations[i], memlog_current_path,
         sizeof(memlog_current_path));
      if (stat(memlog_current_path, &st) == 0 &&
          st.st_size > (off_t)MEMLOG_FILE_MAX_BYTES) {
         memlog_build_rotated_path(&memlog_locations[i], memlog_rotated_path,
            sizeof(memlog_rotated_path));
         (void)unlink(memlog_rotated_path);
         if (rename(memlog_current_path, memlog_rotated_path) != 0)
            (void)unlink(memlog_current_path);
      }
      if (stat(memlog_locations[i].legacy_log, &st) == 0 &&
          st.st_size > (off_t)MEMLOG_FILE_MAX_BYTES) {
         (void)unlink(memlog_locations[i].legacy_prev);
         if (rename(memlog_locations[i].legacy_log,
             memlog_locations[i].legacy_prev) != 0)
            (void)unlink(memlog_locations[i].legacy_log);
      }
   }
}

int unifrog_log(const char *fmt, ...)
{
   va_list ap;
   char line[768];
   char out[896];
   int len;
   int out_len;
   uint32_t now_ms;
   uint32_t count;
   size_t old_len;

   memlog_init();

   va_start(ap, fmt);
   len = vsnprintf(line, sizeof(line), fmt, ap);
   va_end(ap);

   if (len <= 0)
      return len;
   if (len >= (int)sizeof(line))
      len = (int)sizeof(line) - 1;

   now_ms = unifrog_perf_time_ms();
   count = unifrog_perf_count();
   out_len = snprintf(out, sizeof(out),
      "UFLOG ts_ms=%lu dt_ms=%lu cyc=%08lx seq=%lu %.*s",
      (unsigned long)now_ms, (unsigned long)(now_ms - memlog_last_ms),
      (unsigned long)count, (unsigned long)++memlog_sequence, len, line);
   memlog_last_ms = now_ms;
   if (out_len <= 0)
      return len;
   if (out_len >= (int)sizeof(out))
      out_len = (int)sizeof(out) - 1;

   if (memlog_len + (size_t)out_len > memlog_capacity) {
      if (!memlog_flushing && memlog_defer_depth == 0)
         (void)unifrog_log_flush();
      if (memlog_len + (size_t)out_len > memlog_capacity) {
         memlog_dropped = 1;
         memlog_persist_store();
         return len;
      }
   }

   old_len = memlog_len;
   memcpy(memlog + memlog_len, out, out_len);
   memlog_len += (size_t)out_len;
   if (memlog_using_persist)
      unifrog_perf_cache_flush(memlog + old_len, (size_t)out_len);
   memlog_persist_store();

   if (!memlog_flushing && (memlog_flush_every ||
       (memlog_auto_flush_bytes > 0 && memlog_len >= memlog_auto_flush_bytes)))
      unifrog_log_flush();

   return len;
}

int unifrog_log_sync(const char *fmt, ...)
{
   va_list ap;
   char line[896];
   char out[960];
   int len;
   int out_len;
   int fd;
   int ret = 0;
   uint32_t start_ms;
   uint32_t write_ms;
   uint32_t fsync_ms;
   uint32_t end_ms;
   int fsync_ret = 0;

   memlog_init();

   va_start(ap, fmt);
   len = vsnprintf(line, sizeof(line), fmt, ap);
   va_end(ap);

   if (len <= 0)
      return len;
   if (len >= (int)sizeof(line))
      len = (int)sizeof(line) - 1;

   if (!memlog_syncing)
      (void)unifrog_log("sync_write ts_ms=%lu cyc=%08lx %.*s%s",
         (unsigned long)unifrog_perf_time_ms(),
         (unsigned long)unifrog_perf_count(), len, line,
         line[len - 1] == '\n' ? "" : "\n");

   if (memlog_syncing)
      return 0;
   if (memlog_disk_suspended)
      return 0;
   if (!memlog_disk_writes_enabled()) {
      (void)memlog_disk_disabled_result();
      return 0;
   }
   if (!memlog_disk_available) {
      (void)memlog_disk_unavailable_result();
      return 0;
   }

   memlog_syncing = 1;
   out_len = snprintf(out, sizeof(out),
      "UFLOG type=sync ts_ms=%lu cyc=%08lx seq=%lu %.*s%s",
      (unsigned long)unifrog_perf_time_ms(),
      (unsigned long)unifrog_perf_count(),
      (unsigned long)++memlog_sequence, len, line,
      line[len - 1] == '\n' ? "" : "\n");
   if (out_len < 0) {
      memlog_syncing = 0;
      return -1;
   }
   if (out_len >= (int)sizeof(out))
      out_len = (int)sizeof(out) - 1;

   start_ms = unifrog_perf_time_ms();
   fd = memlog_open_retry(O_CREAT | O_WRONLY | O_APPEND);
   if (fd < 0) {
      memlog_last_result = UNIFROG_LOG_ERR_OPEN;
      memlog_syncing = 0;
      return UNIFROG_LOG_ERR_OPEN;
   }

   if (write(fd, out, (size_t)out_len) != out_len) {
      ret = UNIFROG_LOG_ERR_WRITE;
      memlog_last_result = ret;
   }
   write_ms = unifrog_perf_time_ms();
#if UNIFROG_LOG_SYNC_FSYNC
   if (ret == 0)
      fsync_ret = fsync(fd);
#else
   fsync_ret = 0;
#endif
   fsync_ms = unifrog_perf_time_ms();
   if (ret == 0 && fsync_ret != 0) {
      ret = UNIFROG_LOG_ERR_WRITE;
      memlog_last_result = ret;
   } else if (ret == 0) {
      memlog_last_result = 0;
   }
   close(fd);
   end_ms = unifrog_perf_time_ms();
   memlog_syncing = 0;
   memlog_defer_depth++;
   (void)unifrog_log(
      "unifrog log sync_done ret=%d bytes=%lu total_ms=%lu write_ms=%lu fsync_ms=%lu close_ms=%lu fsync=%d\n",
      ret, (unsigned long)out_len,
      (unsigned long)(end_ms - start_ms),
      (unsigned long)(write_ms - start_ms),
      (unsigned long)(fsync_ms - write_ms),
      (unsigned long)(end_ms - fsync_ms),
      UNIFROG_LOG_SYNC_FSYNC);
   memlog_defer_depth--;
   return ret;
}

int unifrog_log_reset(void)
{
   int fd;
   struct unifrog_perf_caps caps;
   char marker[192];
   int marker_len;

   if (memlog_flushing)
      return 0;

   memlog_init();
   if (memlog_disk_suspended) {
      memlog_flush_deferred = 1;
      memlog_last_result = 0;
      return 0;
   }
   if (!memlog_disk_writes_enabled()) {
      (void)unifrog_log(
         "unifrog log reset retained_only disk_writes=0 pending=%lu recovery=%d\n",
         (unsigned long)memlog_len, memlog_recovery_pending);
      return memlog_disk_disabled_result();
   }
   if (!memlog_disk_available) {
      (void)unifrog_log(
         "unifrog log reset deferred reason=storage_unavailable pending=%lu recovery=%d\n",
         (unsigned long)memlog_len, memlog_recovery_pending);
      return memlog_disk_unavailable_result();
   }
   (void)memlog_flush_recovery();

   memlog_flushing = 1;
   memlog_rotate_large_files();
   memlog_flushing = 0;

   if (memlog_len > 0 || memlog_dropped)
      (void)unifrog_log_flush();

   memlog_flushing = 1;
   fd = memlog_open_retry(O_CREAT | O_WRONLY | O_APPEND);
   if (fd < 0) {
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_OPEN;
      return UNIFROG_LOG_ERR_OPEN;
   }

   memset(&caps, 0, sizeof(caps));
   (void)unifrog_perf_query_caps(&caps);
   marker_len = snprintf(marker, sizeof(marker),
      "UFLOG type=reset ts_ms=%lu cyc=%08lx seq=%lu scpu=%u selector=%u caps=0x%08lx event=boot_start\n",
      (unsigned long)unifrog_perf_time_ms(),
      (unsigned long)unifrog_perf_count(), (unsigned long)++memlog_sequence,
      caps.scpu_mhz_est,
      caps.scpu_selector, (unsigned long)caps.caps);
   if (marker_len <= 0) {
      close(fd);
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }
   if (marker_len >= (int)sizeof(marker))
      marker_len = (int)sizeof(marker) - 1;

   if (write(fd, marker, (size_t)marker_len) != (ssize_t)marker_len) {
      close(fd);
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }
#if UNIFROG_LOG_RESET_FSYNC
   if (fsync(fd) != 0) {
      close(fd);
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }
#endif

   close(fd);
   memlog_len = 0;
   memlog_dropped = 0;
   memlog_flush_deferred = 0;
   memlog_flushing = 0;
   memlog_last_result = 0;
   memlog_persist_store();
   return 0;
}

int unifrog_log_flush(void)
{
   int fd;
   char header[256];
   int header_len;
   uint32_t start_ms;
   uint32_t write_ms;
   uint32_t fsync_ms;
   uint32_t end_ms;
   int fsync_ret = 0;
   size_t pending = memlog_len;
   int dropped = memlog_dropped;

   memlog_init();
   pending = memlog_len;
   dropped = memlog_dropped;

   if (memlog_defer_depth > 0) {
      memlog_flush_deferred = 1;
      memlog_last_result = 0;
      return 0;
   }
   if (memlog_disk_suspended) {
      memlog_flush_deferred = 1;
      memlog_last_result = 0;
      return 0;
   }
   if (memlog_storage_quiet()) {
      memlog_flush_deferred = 1;
      memlog_last_result = 0;
      return 0;
   }
   if (!memlog_disk_writes_enabled())
      return memlog_disk_disabled_result();
   if (!memlog_disk_available)
      return memlog_disk_unavailable_result();
   if (memlog_flushing)
      return 0;
   if (memlog_len == 0 && !memlog_dropped) {
      memlog_last_result = 0;
      return 0;
   }
   if (memlog_recovery_pending)
      return memlog_flush_recovery();

   memlog_flushing = 1;
   start_ms = unifrog_perf_time_ms();
   fd = memlog_open_retry(O_CREAT | O_WRONLY | O_APPEND);
   if (fd < 0) {
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_OPEN;
      return UNIFROG_LOG_ERR_OPEN;
   }

   header_len = snprintf(header, sizeof(header),
      "\nUFLOG type=flush ts_ms=%lu cyc=%08lx seq=%lu path=%s pending=%lu dropped=%d\n",
      (unsigned long)unifrog_perf_time_ms(),
      (unsigned long)unifrog_perf_count(), (unsigned long)++memlog_sequence,
      memlog_last_path ? memlog_last_path : "?",
      (unsigned long)pending,
      dropped);
   if (header_len <= 0 || header_len >= (int)sizeof(header) ||
       write(fd, header, (size_t)header_len) != (ssize_t)header_len) {
      close(fd);
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }
   if (memlog_dropped)
      write(fd, "UFLOG type=drop reason=memory_buffer_full\n",
         sizeof("UFLOG type=drop reason=memory_buffer_full\n") - 1);
   if (memlog_len > 0 && write(fd, memlog, memlog_len) != (ssize_t)memlog_len) {
      close(fd);
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }
   write_ms = unifrog_perf_time_ms();
#if UNIFROG_LOG_FLUSH_FSYNC
   fsync_ret = fsync(fd);
#else
   fsync_ret = 0;
#endif
   fsync_ms = unifrog_perf_time_ms();
   if (fsync_ret != 0) {
      close(fd);
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }

   close(fd);
   end_ms = unifrog_perf_time_ms();
   memlog_len = 0;
   memlog_dropped = 0;
   memlog_flush_deferred = 0;
   memlog_flushing = 0;
   memlog_last_result = 0;
   memlog_persist_store();
   memlog_defer_depth++;
   (void)unifrog_log(
      "unifrog log flush_done pending=%lu dropped=%d total_ms=%lu write_ms=%lu fsync_ms=%lu close_ms=%lu fsync=%d\n",
      (unsigned long)pending, dropped,
      (unsigned long)(end_ms - start_ms),
      (unsigned long)(write_ms - start_ms),
      (unsigned long)(fsync_ms - write_ms),
      (unsigned long)(end_ms - fsync_ms),
      UNIFROG_LOG_FLUSH_FSYNC);
   memlog_defer_depth--;
   return 0;
}

int unifrog_log_flush_force(void)
{
   unsigned saved_defer_depth = memlog_defer_depth;
   int ret;

   memlog_defer_depth = 0;
   ret = unifrog_log_flush();
   memlog_defer_depth = saved_defer_depth;
   return ret;
}

void unifrog_log_note_storage_quiet(unsigned ms)
{
   uint32_t until;

   if (!ms)
      return;
   until = unifrog_perf_time_ms() + ms;
   if (!memlog_quiet_until_ms ||
       (int32_t)(until - memlog_quiet_until_ms) > 0)
      memlog_quiet_until_ms = until;
   memlog_flush_deferred = 1;
}

void unifrog_log_set_disk_suspended(int suspended)
{
   memlog_init();
   memlog_disk_suspended = suspended ? 1 : 0;
   if (!memlog_disk_suspended && memlog_flush_deferred)
      (void)unifrog_log_flush();
}

void unifrog_log_defer_begin(void)
{
   memlog_defer_depth++;
}

void unifrog_log_defer_end(void)
{
   if (memlog_defer_depth > 0)
      memlog_defer_depth--;
}

int unifrog_log_flush_deferred(void)
{
   return memlog_flush_deferred;
}

int unifrog_log_disk_writes_enabled(void)
{
   return memlog_disk_writes_enabled();
}

int unifrog_log_disk_available(void)
{
   return memlog_disk_available;
}

void unifrog_log_set_disk_available(int available)
{
   memlog_disk_available = available ? 1 : 0;
   if (!memlog_disk_available && memlog_disk_writes_enabled())
      memlog_last_path = "retained-ram-pending-sd";
}

const char *unifrog_log_last_path(void)
{
   return memlog_last_path;
}

int unifrog_log_last_result(void)
{
   return memlog_last_result;
}

size_t unifrog_log_auto_flush_bytes(void)
{
   return memlog_auto_flush_bytes;
}

void unifrog_log_set_auto_flush_bytes(size_t bytes)
{
   memlog_auto_flush_bytes = bytes;
   memlog_flush_every = bytes == 1u;
}

size_t unifrog_log_capacity(void)
{
   memlog_init();
   return memlog_capacity;
}

size_t unifrog_log_pending(void)
{
   memlog_init();
   return memlog_len;
}
