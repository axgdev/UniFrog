#ifndef UNIFROG_ABI_H
#define UNIFROG_ABI_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_ABI_MAGIC 0x55464142u

#define UNIFROG_ABI_VERSION_MAJOR_VALUE 0u
#define UNIFROG_ABI_VERSION_MINOR_VALUE 4u
#define UNIFROG_ABI_VERSION_PATCH_VALUE 0u

#define UNIFROG_ABI_VERSION_ENCODE(major, minor, patch) \
   ((((uint32_t)(major) & 0xffu) << 16) | \
    (((uint32_t)(minor) & 0xffu) << 8) | \
    ((uint32_t)(patch) & 0xffu))

#define UNIFROG_ABI_VERSION_GET_MAJOR(version) (((uint32_t)(version) >> 16) & 0xffu)
#define UNIFROG_ABI_VERSION_GET_MINOR(version) (((uint32_t)(version) >> 8) & 0xffu)
#define UNIFROG_ABI_VERSION_GET_PATCH(version) ((uint32_t)(version) & 0xffu)

#define UNIFROG_ABI_VERSION \
   UNIFROG_ABI_VERSION_ENCODE(UNIFROG_ABI_VERSION_MAJOR_VALUE, \
                              UNIFROG_ABI_VERSION_MINOR_VALUE, \
                              UNIFROG_ABI_VERSION_PATCH_VALUE)

#define UNIFROG_ABI_MEMORY_EXECUTABLE (1u << 0)
#define UNIFROG_ABI_MEMORY_CACHEABLE (1u << 1)
#define UNIFROG_ABI_MEMORY_RESERVED (1u << 2)
#define UNIFROG_ABI_MEMORY_RECLAIMABLE (1u << 3)
#define UNIFROG_ABI_MEMORY_RUNTIME (1u << 4)
#define UNIFROG_ABI_MEMORY_MEDIA (1u << 5)
#define UNIFROG_ABI_MEMORY_APPLICATION (1u << 6)

#define UNIFROG_APP_ARENA_MIN_BYTES (2u * 1024u * 1024u)
#define UNIFROG_APP_STREAM_BUFFER_DEFAULT_BYTES (1u * 1024u * 1024u)

struct unifrog_abi_memory_slot {
   uint32_t size;
   uintptr_t base;
   size_t bytes;
   uint32_t flags;
};

struct unifrog_abi_memory_region {
   uint32_t size;
   uintptr_t cached_base;
   uintptr_t physical_base;
   size_t bytes;
   uint32_t flags;
};

struct unifrog_abi_memory_layout {
   uint32_t size;
   uint32_t version;
   uint32_t region_count;
   uint32_t flags;
   struct unifrog_abi_memory_region runtime;
   struct unifrog_abi_memory_region external;
   struct unifrog_abi_memory_region media;
};

struct unifrog_abi {
   uint32_t magic;
   uint32_t size;
   uint32_t version;
   uint32_t flags;

   const char *(*runtime_name)(void);
   unsigned (*runtime_api_version)(void);

   int (*log_message)(const char *message);
   int (*log_flush)(void);

   uint32_t (*perf_count)(void);
   uint32_t (*perf_elapsed)(uint32_t start, uint32_t end);
   void (*cache_flush)(const void *ptr, size_t len);
   void (*cache_invalidate)(const void *ptr, size_t len);
   void (*cache_flush_invalidate)(const void *ptr, size_t len);

   int (*memory_layout)(struct unifrog_abi_memory_layout *layout);
   int (*application_memory_slot)(struct unifrog_abi_memory_slot *slot);

   void (*core_load_progress)(const char *stage, unsigned current,
                              unsigned total);

   void *(*malloc)(size_t size);
   void (*free)(void *ptr);
   void *(*calloc)(size_t nmemb, size_t size);
   void *(*realloc)(void *ptr, size_t size);
   void *(*memalign)(size_t alignment, size_t size);

   FILE *(*fopen)(const char *path, const char *mode);
   int (*fclose)(FILE *stream);
   size_t (*fread)(void *ptr, size_t size, size_t nmemb, FILE *stream);
   size_t (*fwrite)(const void *ptr, size_t size, size_t nmemb,
                    FILE *stream);
   int (*fseek)(FILE *stream, long offset, int whence);
   long (*ftell)(FILE *stream);
   int (*fseeko)(FILE *stream, off_t offset, int whence);
   off_t (*ftello)(FILE *stream);
   int (*fflush)(FILE *stream);
   int (*setvbuf)(FILE *stream, char *buf, int mode, size_t size);
   int (*remove)(const char *path);
   int (*rename)(const char *oldpath, const char *newpath);

   time_t (*time)(time_t *tloc);
   struct tm *(*localtime)(const time_t *timer);
   int (*gettimeofday)(struct timeval *tv, void *tz);

   int (*vsnprintf)(char *str, size_t size, const char *format, va_list ap);
   int (*vfprintf)(FILE *stream, const char *format, va_list ap);

   int (*stat)(const char *path, struct stat *st);
   int (*mkdir)(const char *path, mode_t mode);
   DIR *(*opendir)(const char *path);
   struct dirent *(*readdir)(DIR *dirp);
   int (*closedir)(DIR *dirp);
};

const struct unifrog_abi *unifrog_abi_get(void);
int unifrog_abi_compatible(uint32_t required_version);
int unifrog_abi_application_memory_slot(struct unifrog_abi_memory_slot *slot);
int unifrog_abi_memory_layout(struct unifrog_abi_memory_layout *layout);
int unifrog_abi_application_memory_reserve_top(size_t bytes,
   size_t alignment, void **out_ptr);
void unifrog_abi_application_memory_release_top(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
