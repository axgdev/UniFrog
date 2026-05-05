#include <unifrog/log.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#define MEMLOG_BYTES (1024 * 1024)
#define MEMLOG_AUTO_FLUSH_BYTES (4 * 1024)
#define MEMLOG_FILE_MAX_BYTES (1024 * 1024)

struct memlog_path {
   const char *path;
   const char *prev_path;
};

static char memlog[MEMLOG_BYTES];
static size_t memlog_len;
static size_t memlog_auto_flush_bytes = MEMLOG_AUTO_FLUSH_BYTES;
static int memlog_dropped;
static int memlog_flushing;
static unsigned memlog_defer_depth;
static int memlog_flush_deferred;
static const char *memlog_last_path;
static int memlog_last_result;

static const struct memlog_path memlog_paths[] = {
   { "/media/mmcblk0/log.txt", "/media/mmcblk0/log-prev.txt" },
   { "/media/mmcblk0p1/log.txt", "/media/mmcblk0p1/log-prev.txt" },
   { "/media/mmcblk0p2/log.txt", "/media/mmcblk0p2/log-prev.txt" },
   { "/log.txt", "/log-prev.txt" },
};

static int memlog_open(int flags)
{
   int fd = -1;

   memlog_last_path = NULL;
   for (size_t i = 0; i < sizeof(memlog_paths) / sizeof(memlog_paths[0]); i++) {
      fd = open(memlog_paths[i].path, flags, 0666);
      if (fd >= 0) {
         memlog_last_path = memlog_paths[i].path;
         break;
      }
   }

   return fd;
}

static void memlog_rotate_large_files(void)
{
   struct stat st;

   for (size_t i = 0; i < sizeof(memlog_paths) / sizeof(memlog_paths[0]); i++) {
      if (stat(memlog_paths[i].path, &st) != 0)
         continue;
      if (st.st_size <= (off_t)MEMLOG_FILE_MAX_BYTES)
         continue;
      (void)unlink(memlog_paths[i].prev_path);
      if (rename(memlog_paths[i].path, memlog_paths[i].prev_path) != 0)
         (void)unlink(memlog_paths[i].path);
   }
}

int unifrog_log(const char *fmt, ...)
{
   va_list ap;
   char line[768];
   int len;

   va_start(ap, fmt);
   len = vsnprintf(line, sizeof(line), fmt, ap);
   va_end(ap);

   if (len <= 0)
      return len;
   if (len >= (int)sizeof(line))
      len = (int)sizeof(line) - 1;

   if (memlog_len + (size_t)len > sizeof(memlog)) {
      if (!memlog_flushing && memlog_defer_depth == 0)
         (void)unifrog_log_flush();
      if (memlog_len + (size_t)len > sizeof(memlog)) {
         memlog_dropped = 1;
         return len;
      }
   }

   memcpy(memlog + memlog_len, line, len);
   memlog_len += (size_t)len;

   if (!memlog_flushing && memlog_auto_flush_bytes > 0 &&
       memlog_len >= memlog_auto_flush_bytes)
      unifrog_log_flush();

   return len;
}

int unifrog_log_reset(void)
{
   int fd;
   const char marker[] = "--- unifrog log reset: boot start ---\n";

   if (memlog_flushing)
      return 0;

   memlog_flushing = 1;
   memlog_rotate_large_files();
   memlog_flushing = 0;

   if (memlog_len > 0 || memlog_dropped)
      (void)unifrog_log_flush();

   memlog_flushing = 1;
   fd = memlog_open(O_CREAT | O_WRONLY | O_APPEND);
   if (fd < 0) {
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_OPEN;
      return UNIFROG_LOG_ERR_OPEN;
   }

   if (write(fd, marker, sizeof(marker) - 1) != (ssize_t)(sizeof(marker) - 1)) {
      close(fd);
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }

   close(fd);
   memlog_len = 0;
   memlog_dropped = 0;
   memlog_flush_deferred = 0;
   memlog_flushing = 0;
   memlog_last_result = 0;
   return 0;
}

int unifrog_log_flush(void)
{
   int fd;
   char header[128];
   size_t pending = memlog_len;
   int dropped = memlog_dropped;

   if (memlog_defer_depth > 0) {
      memlog_flush_deferred = 1;
      memlog_last_result = 0;
      return 0;
   }
   if (memlog_flushing)
      return 0;
   if (memlog_len == 0 && !memlog_dropped) {
      memlog_last_result = 0;
      return 0;
   }

   memlog_flushing = 1;
   fd = memlog_open(O_CREAT | O_WRONLY | O_APPEND);
   if (fd < 0) {
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_OPEN;
      return UNIFROG_LOG_ERR_OPEN;
   }

   snprintf(header, sizeof(header),
      "\n--- unifrog memory log flush path=%s pending=%lu dropped=%d ---\n",
      memlog_last_path ? memlog_last_path : "?",
      (unsigned long)pending,
      dropped);
   if (write(fd, header, strlen(header)) != (ssize_t)strlen(header)) {
      close(fd);
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }
   if (memlog_dropped)
      write(fd, "\n--- log lines dropped while memory buffer was full ---\n",
         sizeof("\n--- log lines dropped while memory buffer was full ---\n") - 1);
   if (memlog_len > 0 && write(fd, memlog, memlog_len) != (ssize_t)memlog_len) {
      close(fd);
      memlog_flushing = 0;
      memlog_last_result = UNIFROG_LOG_ERR_WRITE;
      return UNIFROG_LOG_ERR_WRITE;
   }

   close(fd);
   memlog_len = 0;
   memlog_dropped = 0;
   memlog_flush_deferred = 0;
   memlog_flushing = 0;
   memlog_last_result = 0;
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
}

size_t unifrog_log_capacity(void)
{
   return sizeof(memlog);
}

size_t unifrog_log_pending(void)
{
   return memlog_len;
}
