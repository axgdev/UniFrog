#include <unifrog/abi.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>
#include <unifrog/runtime.h>

#include <stdlib.h>
#include <string.h>

#define UNIFROG_APPMEM_PATH "/hcrtos/memory-mapping/appmem"
#define UNIFROG_SYSMEM_PATH "/hcrtos/memory-mapping/sysmem"
#define UNIFROG_MMZ0_PATH "/hcrtos/memory-mapping/mmz0"
#define UNIFROG_MIPS_CACHED_ADDR(addr) \
   ((uintptr_t)((((uintptr_t)(addr)) | 0x80000000u) & 0x9fffffffu))

static uintptr_t appmem_reserved_top;
static size_t appmem_reserved_bytes;

extern int fdt_get_node_offset_by_path(const char *path);
extern int fdt_get_property_u_32_index(int offset, const char *name, int index,
                                       uint32_t *outval);

int unifrog_abi_log_message_cb(const char *message)
{
   return unifrog_log("%s", message ? message : "");
}

extern const char *unifrog_abi_runtime_name_trampoline(void);
extern unsigned unifrog_abi_runtime_api_version_trampoline(void);
extern int unifrog_abi_log_message_trampoline(const char *message);
extern int unifrog_abi_log_flush_trampoline(void);
extern uint32_t unifrog_abi_perf_count_trampoline(void);
extern uint32_t unifrog_abi_perf_elapsed_trampoline(uint32_t start,
                                                    uint32_t end);
extern void unifrog_abi_cache_flush_trampoline(const void *ptr, size_t len);
extern void unifrog_abi_cache_invalidate_trampoline(const void *ptr,
                                                    size_t len);
extern void unifrog_abi_cache_flush_invalidate_trampoline(const void *ptr,
                                                         size_t len);
extern int unifrog_abi_memory_layout_trampoline(
   struct unifrog_abi_memory_layout *layout);
extern int unifrog_abi_application_memory_slot_trampoline(
   struct unifrog_abi_memory_slot *slot);
extern void unifrog_abi_core_load_progress_trampoline(const char *stage,
   unsigned current, unsigned total);
extern void *unifrog_abi_malloc_trampoline(size_t size);
extern void unifrog_abi_free_trampoline(void *ptr);
extern void *unifrog_abi_calloc_trampoline(size_t nmemb, size_t size);
extern void *unifrog_abi_realloc_trampoline(void *ptr, size_t size);
extern void *unifrog_abi_memalign_trampoline(size_t alignment, size_t size);
extern FILE *unifrog_abi_fopen_trampoline(const char *path, const char *mode);
extern int unifrog_abi_fclose_trampoline(FILE *stream);
extern size_t unifrog_abi_fread_trampoline(void *ptr, size_t size,
   size_t nmemb, FILE *stream);
extern size_t unifrog_abi_fwrite_trampoline(const void *ptr, size_t size,
   size_t nmemb, FILE *stream);
extern int unifrog_abi_fseek_trampoline(FILE *stream, long offset, int whence);
extern long unifrog_abi_ftell_trampoline(FILE *stream);
extern int unifrog_abi_fseeko_trampoline(FILE *stream, off_t offset,
   int whence);
extern off_t unifrog_abi_ftello_trampoline(FILE *stream);
extern int unifrog_abi_fflush_trampoline(FILE *stream);
extern int unifrog_abi_setvbuf_trampoline(FILE *stream, char *buf, int mode,
   size_t size);
extern int unifrog_abi_remove_trampoline(const char *path);
extern int unifrog_abi_rename_trampoline(const char *oldpath,
   const char *newpath);
extern time_t unifrog_abi_time_trampoline(time_t *tloc);
extern struct tm *unifrog_abi_localtime_trampoline(const time_t *timer);
extern int unifrog_abi_gettimeofday_trampoline(struct timeval *tv, void *tz);
extern int unifrog_abi_vsnprintf_trampoline(char *str, size_t size,
   const char *format, va_list ap);
extern int unifrog_abi_vfprintf_trampoline(FILE *stream, const char *format,
   va_list ap);
extern int unifrog_abi_stat_trampoline(const char *path, struct stat *st);
extern int unifrog_abi_mkdir_trampoline(const char *path, mode_t mode);
extern DIR *unifrog_abi_opendir_trampoline(const char *path);
extern struct dirent *unifrog_abi_readdir_trampoline(DIR *dirp);
extern int unifrog_abi_closedir_trampoline(DIR *dirp);
extern size_t unifrog_abi_log_auto_flush_bytes_trampoline(void);
extern void unifrog_abi_log_set_auto_flush_bytes_trampoline(size_t bytes);
extern void unifrog_abi_audio_set_system_output_enabled_trampoline(
   int enabled);
extern void unifrog_abi_audio_debug_dump_trampoline(void *audio,
   const char *tag);
extern void unifrog_abi_input_save_previous_trampoline(void);
extern void unifrog_abi_input_poll_with_wireless_divisor_trampoline(
   unsigned wireless_divisor);
extern uint32_t unifrog_abi_input_menu_buttons_trampoline(void);

static int fdt_read_region(const char *path, uintptr_t *cached_base,
                           uintptr_t *physical_base, size_t *bytes)
{
   uint32_t reg_base;
   uint32_t reg_size;
   int node;

   node = fdt_get_node_offset_by_path(path);
   if (node < 0)
      return -1;
   if (fdt_get_property_u_32_index(node, "reg", 0, &reg_base) < 0)
      return -1;
   if (fdt_get_property_u_32_index(node, "reg", 1, &reg_size) < 0)
      return -1;

   *cached_base = UNIFROG_MIPS_CACHED_ADDR(reg_base);
   *physical_base = (uintptr_t)reg_base;
   *bytes = (size_t)reg_size;
   return 0;
}

static int is_power_of_two_size(size_t value)
{
   return value && ((value & (value - 1u)) == 0);
}

static uintptr_t align_down_uintptr(uintptr_t value, size_t alignment)
{
   return value & ~(uintptr_t)(alignment - 1u);
}

static int read_app_region(uintptr_t *base, uintptr_t *phys, size_t *bytes)
{
   if (!base || !phys || !bytes)
      return -1;
   if (fdt_read_region(UNIFROG_APPMEM_PATH, base, phys, bytes) < 0)
      return -1;
   if (*bytes < UNIFROG_APP_ARENA_MIN_BYTES)
      return -1;
   return 0;
}

static int read_app_arena(struct unifrog_abi_memory_slot *slot)
{
   uintptr_t base;
   uintptr_t phys;
   size_t bytes;

   if (!slot)
      return -1;

   if (read_app_region(&base, &phys, &bytes) < 0)
      return -1;
   if (appmem_reserved_bytes) {
      if (appmem_reserved_bytes >= bytes)
         return -1;
      bytes -= appmem_reserved_bytes;
      if (bytes < UNIFROG_APP_ARENA_MIN_BYTES)
         return -1;
   }

   slot->size = sizeof(*slot);
   slot->base = base;
   slot->bytes = bytes;
   slot->flags = UNIFROG_ABI_MEMORY_EXECUTABLE |
                 UNIFROG_ABI_MEMORY_CACHEABLE |
                 UNIFROG_ABI_MEMORY_RESERVED |
                 UNIFROG_ABI_MEMORY_RECLAIMABLE |
                 UNIFROG_ABI_MEMORY_APPLICATION;
   return 0;
}

int unifrog_abi_application_memory_slot(struct unifrog_abi_memory_slot *slot)
{
   return read_app_arena(slot);
}

int unifrog_abi_application_memory_reserve_top(size_t bytes,
   size_t alignment, void **out_ptr)
{
   uintptr_t base;
   uintptr_t phys;
   uintptr_t arena_end;
   uintptr_t reserve_end;
   uintptr_t reserve_start;
   size_t arena_bytes;

   if (!out_ptr || bytes == 0)
      return -1;
   *out_ptr = NULL;
   if (appmem_reserved_bytes)
      return -1;
   if (alignment < 32u)
      alignment = 32u;
   if (!is_power_of_two_size(alignment))
      return -1;
   if (read_app_region(&base, &phys, &arena_bytes) < 0)
      return -1;
   if (bytes > arena_bytes - UNIFROG_APP_ARENA_MIN_BYTES)
      return -1;

   arena_end = base + arena_bytes;
   reserve_end = align_down_uintptr(arena_end, alignment);
   if (reserve_end < base || bytes > reserve_end - base)
      return -1;
   reserve_start = align_down_uintptr(reserve_end - bytes, alignment);
   if (reserve_start < base ||
       reserve_start - base < UNIFROG_APP_ARENA_MIN_BYTES)
      return -1;

   appmem_reserved_top = reserve_start;
   appmem_reserved_bytes = arena_end - reserve_start;
   *out_ptr = (void *)reserve_start;
   unifrog_log("unifrog abi appmem reserve_top ptr=0x%08lx bytes=%u requested=%u remaining=%u\n",
      (unsigned long)reserve_start, (unsigned)appmem_reserved_bytes,
      (unsigned)bytes, (unsigned)(reserve_start - base));
   return 0;
}

void unifrog_abi_application_memory_release_top(void *ptr)
{
   if (!appmem_reserved_bytes)
      return;
   if (ptr && (uintptr_t)ptr != appmem_reserved_top) {
      unifrog_log("unifrog abi appmem release_top mismatch ptr=0x%08lx reserved=0x%08lx bytes=%u\n",
         (unsigned long)(uintptr_t)ptr, (unsigned long)appmem_reserved_top,
         (unsigned)appmem_reserved_bytes);
      return;
   }
   unifrog_log("unifrog abi appmem release_top ptr=0x%08lx bytes=%u\n",
      (unsigned long)appmem_reserved_top, (unsigned)appmem_reserved_bytes);
   appmem_reserved_top = 0;
   appmem_reserved_bytes = 0;
}

static void fill_region(struct unifrog_abi_memory_region *region,
                        uintptr_t cached_base, uintptr_t physical_base,
                        size_t bytes, uint32_t flags)
{
   region->size = sizeof(*region);
   region->cached_base = cached_base;
   region->physical_base = physical_base;
   region->bytes = bytes;
   region->flags = flags;
}

int unifrog_abi_memory_layout(struct unifrog_abi_memory_layout *layout)
{
   uintptr_t cached;
   uintptr_t physical;
   size_t bytes;

   if (!layout)
      return -1;

   memset(layout, 0, sizeof(*layout));
   layout->size = sizeof(*layout);
   layout->version = UNIFROG_ABI_VERSION;

   if (fdt_read_region(UNIFROG_SYSMEM_PATH, &cached, &physical, &bytes) == 0) {
      fill_region(&layout->runtime, cached, physical, bytes,
                  UNIFROG_ABI_MEMORY_CACHEABLE |
                  UNIFROG_ABI_MEMORY_RUNTIME);
      layout->region_count++;
   }

   if (read_app_region(&cached, &physical, &bytes) == 0) {
      if (appmem_reserved_bytes && appmem_reserved_bytes < bytes)
         bytes -= appmem_reserved_bytes;
      fill_region(&layout->external, cached, physical, bytes,
                  UNIFROG_ABI_MEMORY_EXECUTABLE |
                  UNIFROG_ABI_MEMORY_CACHEABLE |
                  UNIFROG_ABI_MEMORY_RESERVED |
                  UNIFROG_ABI_MEMORY_RECLAIMABLE |
                  UNIFROG_ABI_MEMORY_APPLICATION);
      layout->region_count++;
   }

   if (fdt_read_region(UNIFROG_MMZ0_PATH, &cached, &physical, &bytes) == 0) {
      fill_region(&layout->media, cached, physical, bytes,
                  UNIFROG_ABI_MEMORY_CACHEABLE |
                  UNIFROG_ABI_MEMORY_MEDIA);
      layout->region_count++;
   }

   return layout->external.bytes >= UNIFROG_APP_ARENA_MIN_BYTES ? 0 : -1;
}

static const struct unifrog_abi unifrog_abi_table = {
   .magic = UNIFROG_ABI_MAGIC,
   .size = sizeof(struct unifrog_abi),
   .version = UNIFROG_ABI_VERSION,
   .flags = 0,
   .runtime_name = unifrog_abi_runtime_name_trampoline,
   .runtime_api_version = unifrog_abi_runtime_api_version_trampoline,
   .log_message = unifrog_abi_log_message_trampoline,
   .log_flush = unifrog_abi_log_flush_trampoline,
   .perf_count = unifrog_abi_perf_count_trampoline,
   .perf_elapsed = unifrog_abi_perf_elapsed_trampoline,
   .cache_flush = unifrog_abi_cache_flush_trampoline,
   .cache_invalidate = unifrog_abi_cache_invalidate_trampoline,
   .cache_flush_invalidate = unifrog_abi_cache_flush_invalidate_trampoline,
   .memory_layout = unifrog_abi_memory_layout_trampoline,
   .application_memory_slot = unifrog_abi_application_memory_slot_trampoline,
   .core_load_progress = unifrog_abi_core_load_progress_trampoline,
   .malloc = unifrog_abi_malloc_trampoline,
   .free = unifrog_abi_free_trampoline,
   .calloc = unifrog_abi_calloc_trampoline,
   .realloc = unifrog_abi_realloc_trampoline,
   .memalign = unifrog_abi_memalign_trampoline,
   .fopen = unifrog_abi_fopen_trampoline,
   .fclose = unifrog_abi_fclose_trampoline,
   .fread = unifrog_abi_fread_trampoline,
   .fwrite = unifrog_abi_fwrite_trampoline,
   .fseek = unifrog_abi_fseek_trampoline,
   .ftell = unifrog_abi_ftell_trampoline,
   .fseeko = unifrog_abi_fseeko_trampoline,
   .ftello = unifrog_abi_ftello_trampoline,
   .fflush = unifrog_abi_fflush_trampoline,
   .setvbuf = unifrog_abi_setvbuf_trampoline,
   .remove = unifrog_abi_remove_trampoline,
   .rename = unifrog_abi_rename_trampoline,
   .time = unifrog_abi_time_trampoline,
   .localtime = unifrog_abi_localtime_trampoline,
   .gettimeofday = unifrog_abi_gettimeofday_trampoline,
   .vsnprintf = unifrog_abi_vsnprintf_trampoline,
   .vfprintf = unifrog_abi_vfprintf_trampoline,
   .stat = unifrog_abi_stat_trampoline,
   .mkdir = unifrog_abi_mkdir_trampoline,
   .opendir = unifrog_abi_opendir_trampoline,
   .readdir = unifrog_abi_readdir_trampoline,
   .closedir = unifrog_abi_closedir_trampoline,
   .log_auto_flush_bytes = unifrog_abi_log_auto_flush_bytes_trampoline,
   .log_set_auto_flush_bytes =
      unifrog_abi_log_set_auto_flush_bytes_trampoline,
   .audio_set_system_output_enabled =
      unifrog_abi_audio_set_system_output_enabled_trampoline,
   .audio_debug_dump = unifrog_abi_audio_debug_dump_trampoline,
   .input_save_previous = unifrog_abi_input_save_previous_trampoline,
   .input_poll_with_wireless_divisor =
      unifrog_abi_input_poll_with_wireless_divisor_trampoline,
   .input_menu_buttons = unifrog_abi_input_menu_buttons_trampoline,
};

const struct unifrog_abi *unifrog_abi_get(void)
{
   return &unifrog_abi_table;
}

int unifrog_abi_compatible(uint32_t required_version)
{
   uint32_t required_major = UNIFROG_ABI_VERSION_GET_MAJOR(required_version);
   uint32_t required_minor = UNIFROG_ABI_VERSION_GET_MINOR(required_version);

   if (required_major != UNIFROG_ABI_VERSION_MAJOR_VALUE)
      return 0;
   if (required_minor > UNIFROG_ABI_VERSION_MINOR_VALUE)
      return 0;
   return 1;
}

int unifrog_abi_table_compatible(const struct unifrog_abi *abi,
   uint32_t required_version, size_t required_size)
{
   uint32_t runtime_major;
   uint32_t runtime_minor;
   uint32_t required_major;
   uint32_t required_minor;

   if (!abi || abi->magic != UNIFROG_ABI_MAGIC)
      return 0;
   if (abi->size < required_size)
      return 0;
   runtime_major = UNIFROG_ABI_VERSION_GET_MAJOR(abi->version);
   runtime_minor = UNIFROG_ABI_VERSION_GET_MINOR(abi->version);
   required_major = UNIFROG_ABI_VERSION_GET_MAJOR(required_version);
   required_minor = UNIFROG_ABI_VERSION_GET_MINOR(required_version);
   if (runtime_major != required_major)
      return 0;
   if (runtime_minor < required_minor)
      return 0;
   return 1;
}
