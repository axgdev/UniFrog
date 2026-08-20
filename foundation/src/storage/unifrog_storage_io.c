#include <unifrog/storage_io.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <unifrog/config.h>
#include <unifrog/log.h>
#include <unifrog/paths.h>
#include <unifrog/platform.h>

int unifrog_storage_path_is_storage(const char *path)
{
   size_t root_len = strlen(UNIFROG_SD_ROOT);

   if (!path || !path[0])
      return 0;
   if (strncmp(path, UNIFROG_SD_ROOT, root_len) == 0 &&
       (path[root_len] == '\0' || path[root_len] == '/'))
      return 1;
   return strncmp(path, "/media/", 7) == 0;
}

int unifrog_storage_recover_after_io_error(const char *tag, unsigned attempts,
   unsigned delay_ms)
{
   return unifrog_platform_recover_storage_after_io_error(
      tag && tag[0] ? tag : "storage_io", attempts, delay_ms);
}

DIR *unifrog_storage_opendir_resilient(const char *path, const char *tag,
   unsigned attempts, unsigned delay_ms)
{
   DIR *dir;
   int open_errno;

   if (!path) {
      errno = EINVAL;
      return NULL;
   }
   errno = 0;
   dir = opendir(path);
   open_errno = errno;
   if (dir || !unifrog_storage_path_is_storage(path))
      return dir;
   if (open_errno == ENOENT || open_errno == ENOTDIR)
      return NULL;

   unifrog_log("unifrog storage_io opendir_failed tag=%s path=%s errno=%d recover=1\n",
      tag ? tag : "", path ? path : "", open_errno);
   if (unifrog_storage_recover_after_io_error(tag ? tag : "opendir",
       attempts, delay_ms) != 0) {
      errno = open_errno;
      return NULL;
   }
   errno = 0;
   dir = opendir(path);
   unifrog_log("unifrog storage_io opendir_retry tag=%s path=%s ret=%d errno=%d\n",
      tag ? tag : "", path ? path : "", dir ? 0 : -1, errno);
   return dir;
}

int unifrog_storage_stat_resilient(const char *path, struct stat *st,
   const char *tag, unsigned attempts, unsigned delay_ms)
{
   int stat_errno;

   if (!path || !st) {
      errno = EINVAL;
      return -1;
   }
   errno = 0;
   if (stat(path, st) == 0)
      return 0;
   stat_errno = errno;
   if (!unifrog_storage_path_is_storage(path))
      return -1;
   if (stat_errno == ENOENT || stat_errno == ENOTDIR)
      return -1;

   unifrog_log("unifrog storage_io stat_failed tag=%s path=%s errno=%d recover=1\n",
      tag ? tag : "", path ? path : "", stat_errno);
   if (unifrog_storage_recover_after_io_error(tag ? tag : "stat",
       attempts, delay_ms) != 0) {
      errno = stat_errno;
      return -1;
   }
   errno = 0;
   if (stat(path, st) == 0) {
      unifrog_log("unifrog storage_io stat_retry tag=%s path=%s ret=0\n",
         tag ? tag : "", path ? path : "");
      return 0;
   }
   unifrog_log("unifrog storage_io stat_retry tag=%s path=%s ret=-1 errno=%d\n",
      tag ? tag : "", path ? path : "", errno);
   return -1;
}

FILE *unifrog_storage_fopen_resilient(const char *path, const char *mode,
   const char *tag, unsigned attempts, unsigned delay_ms)
{
   FILE *file;
   int open_errno;

   if (!path || !mode) {
      errno = EINVAL;
      return NULL;
   }
   errno = 0;
   file = fopen(path, mode);
   open_errno = errno;
   if (file || !unifrog_storage_path_is_storage(path))
      return file;
   if (open_errno == ENOENT || open_errno == ENOTDIR)
      return NULL;

   unifrog_log("unifrog storage_io fopen_failed tag=%s path=%s mode=%s errno=%d recover=1\n",
      tag ? tag : "", path ? path : "", mode ? mode : "", open_errno);
   if (unifrog_storage_recover_after_io_error(tag ? tag : "fopen",
       attempts, delay_ms) != 0) {
      errno = open_errno;
      return NULL;
   }
   errno = 0;
   file = fopen(path, mode);
   unifrog_log("unifrog storage_io fopen_retry tag=%s path=%s mode=%s ret=%d errno=%d\n",
      tag ? tag : "", path ? path : "", mode ? mode : "",
      file ? 0 : -1, errno);
   return file;
}

int unifrog_storage_reopen_file_after_io_error(FILE **file_io,
   const char *path, long pos, const char *tag, unsigned attempts,
   unsigned delay_ms)
{
   FILE *file;

   if (!file_io || !path || !path[0] || pos < 0)
      return -1;
   if (*file_io) {
      fclose(*file_io);
      *file_io = NULL;
   }
   if (unifrog_storage_recover_after_io_error(tag ? tag : "reopen",
       attempts, delay_ms) != 0)
      return -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   if (fseek(file, pos, SEEK_SET) != 0) {
      fclose(file);
      return -1;
   }
   *file_io = file;
   unifrog_log("unifrog storage_io reopen tag=%s pos=%ld path=%s\n",
      tag ? tag : "", pos, path);
   return 0;
}

int unifrog_storage_write_atomic(const char *path, const char *tmp_path,
   const void *data, size_t size, const char *tag, unsigned attempts,
   unsigned delay_ms)
{
   char tmp[288];

   if (!path || !path[0] || (!data && size > 0))
      return -1;
   if (!tmp_path || !tmp_path[0]) {
      if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
         return -1;
      tmp_path = tmp;
   }
   if (attempts == 0)
      attempts = 1;

   for (unsigned attempt = 0; attempt < attempts; attempt++) {
      unsigned recover_attempts = attempts < 8u ? 24u : attempts;
      FILE *file;
      int ok = 0;
      int saved_errno = 0;

      errno = 0;
      file = fopen(tmp_path, "wb");
      if (file) {
         errno = 0;
         ok = (size == 0 || fwrite(data, 1, size, file) == size) &&
            fflush(file) == 0;
         saved_errno = errno;
         if (fclose(file) != 0) {
            ok = 0;
            if (!saved_errno)
               saved_errno = errno;
         }
         if (ok) {
            errno = 0;
            if (unifrog_config_commit(tmp_path, path) == 0) {
               if (attempt > 0)
                  unifrog_log("unifrog storage_io write_recovered tag=%s path=%s attempts=%u\n",
                     tag ? tag : "", path, attempt + 1u);
               return 0;
            }
            saved_errno = errno;
         }
      } else {
         saved_errno = errno;
      }

      (void)unlink(tmp_path);
      if (saved_errno == ENOENT || saved_errno == ENOTDIR) {
         errno = saved_errno;
         return -1;
      }
      unifrog_log("unifrog storage_io write_failed tag=%s path=%s attempt=%u errno=%d\n",
         tag ? tag : "", path, attempt + 1u, saved_errno);
      if (attempt + 1u < attempts)
         (void)unifrog_storage_recover_after_io_error(tag ? tag : "write",
            recover_attempts, delay_ms);
   }

   return -1;
}
