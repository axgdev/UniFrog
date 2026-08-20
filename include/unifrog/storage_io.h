#ifndef UNIFROG_STORAGE_IO_H
#define UNIFROG_STORAGE_IO_H

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

int unifrog_storage_path_is_storage(const char *path);
int unifrog_storage_recover_after_io_error(const char *tag, unsigned attempts,
   unsigned delay_ms);
DIR *unifrog_storage_opendir_resilient(const char *path, const char *tag,
   unsigned attempts, unsigned delay_ms);
int unifrog_storage_stat_resilient(const char *path, struct stat *st,
   const char *tag, unsigned attempts, unsigned delay_ms);
FILE *unifrog_storage_fopen_resilient(const char *path, const char *mode,
   const char *tag, unsigned attempts, unsigned delay_ms);
int unifrog_storage_reopen_file_after_io_error(FILE **file_io,
   const char *path, long pos, const char *tag, unsigned attempts,
   unsigned delay_ms);
int unifrog_storage_write_atomic(const char *path, const char *tmp_path,
   const void *data, size_t size, const char *tag, unsigned attempts,
   unsigned delay_ms);

#ifdef __cplusplus
}
#endif

#endif
