#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <unifrog/abi.h>

#undef clearerr
#undef ferror
#undef fgetc
#undef fgets
#undef getc

#define MODULE_FD_BASE 3
#define MODULE_FD_COUNT 16
#define MODULE_DIR_BASE 100
#define MODULE_DIR_COUNT 8
#define MODULE_PRINTF_BUFFER 512u
#define MODULE_PATH_BUFFER 512u
#define MODULE_XLOG_LIMIT 128u
#define MODULE_HEAP_ALIGN 16u
#define MODULE_HEAP_MAGIC 0x55464850u
#define MODULE_HEAP_OOM_LOG_LIMIT 4u

#define MIPS_CACHE_INDEX_BASE 0x80000000u
#define MIPS_CACHE_INDEX_END  0x80004000u
#define MIPS_CACHE_LINE_BYTES 16u

extern char __unifrog_module_end[];

struct module_heap_block {
   size_t size;
   struct module_heap_block *next;
   uint32_t magic;
   uint32_t free;
};

const struct unifrog_abi *unifrog_core_module_abi;
void *__dso_handle __attribute__((visibility("hidden"))) = &__dso_handle;
static struct _reent module_reent;
struct _reent *_impure_ptr = &module_reent;
struct _reent *const _global_impure_ptr = &module_reent;
static FILE *module_fd_table[MODULE_FD_COUNT];
static struct {
   DIR *dir;
   unsigned style;
} module_dir_table[MODULE_DIR_COUNT];
/* Keep large core-private allocations inside the external module arena. */
static struct module_heap_block *module_heap_head;
static uintptr_t module_heap_base;
static uintptr_t module_heap_end;
static int module_heap_checked;
static unsigned module_heap_oom_logs;
static unsigned module_xlog_count;

enum {
   MODULE_DIR_STYLE_FIRMWARE = 0,
   MODULE_DIR_STYLE_NAME_AT_4 = 1,
};

static const struct unifrog_abi *module_abi(void)
{
   const struct unifrog_abi *abi = unifrog_core_module_abi;

   if (!abi || abi->magic != UNIFROG_ABI_MAGIC ||
       abi->size < sizeof(struct unifrog_abi))
      return NULL;
   return abi;
}

void *__real_memcpy(void *dest, const void *src, size_t len)
{
   return memcpy(dest, src, len);
}

void *__real_memmove(void *dest, const void *src, size_t len)
{
   return memmove(dest, src, len);
}

void *__real_memset(void *dest, int c, size_t len)
{
   return memset(dest, c, len);
}

void unifrog_core_load_progress(const char *stage, unsigned current,
   unsigned total)
{
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->core_load_progress)
      abi->core_load_progress(stage, current, total);
}

static size_t module_align_size(size_t value, size_t alignment)
{
   size_t rem;
   size_t add;

   if (alignment <= 1u)
      return value;
   rem = value % alignment;
   if (!rem)
      return value;
   add = alignment - rem;
   if (value > (size_t)-1 - add)
      return 0;
   return value + add;
}

static uintptr_t module_align_up_uintptr(uintptr_t value, size_t alignment)
{
   uintptr_t rem;
   uintptr_t add;

   if (alignment <= 1u)
      return value;
   rem = value % (uintptr_t)alignment;
   if (!rem)
      return value;
   add = (uintptr_t)alignment - rem;
   if (value > (uintptr_t)-1 - add)
      return 0;
   return value + add;
}

static uintptr_t module_align_down_uintptr(uintptr_t value, size_t alignment)
{
   if (alignment <= 1u)
      return value;
   return value - value % (uintptr_t)alignment;
}

static int module_is_power_of_two(size_t value)
{
   return value && !(value & (value - 1u));
}

static size_t module_heap_header_size(void)
{
   return module_align_size(sizeof(struct module_heap_block),
      MODULE_HEAP_ALIGN);
}

static size_t module_heap_min_block_size(void)
{
   return module_heap_header_size() + MODULE_HEAP_ALIGN;
}

static int module_heap_init(void)
{
   const struct unifrog_abi *abi;
   struct unifrog_abi_memory_slot slot;
   uintptr_t start;
   uintptr_t end;
   size_t header_size;

   if (module_heap_checked)
      return module_heap_head != NULL;

   abi = module_abi();
   if (!abi || !abi->application_memory_slot)
      return 0;

   module_heap_checked = 1;
   memset(&slot, 0, sizeof(slot));
   if (abi->application_memory_slot(&slot) != 0)
      return 0;

   if (slot.base > (uintptr_t)-1 - slot.bytes)
      return 0;
   start = module_align_up_uintptr((uintptr_t)__unifrog_module_end,
      MODULE_HEAP_ALIGN);
   end = module_align_down_uintptr(slot.base + slot.bytes, MODULE_HEAP_ALIGN);
   if (!start || !end || end <= start)
      return 0;
   if (start < slot.base)
      start = module_align_up_uintptr(slot.base, MODULE_HEAP_ALIGN);
   if (!start || end <= start)
      return 0;

   header_size = module_heap_header_size();
   if (end - start <= header_size + MODULE_HEAP_ALIGN)
      return 0;

   module_heap_base = start;
   module_heap_end = end;
   module_heap_head = (struct module_heap_block *)start;
   module_heap_head->size = end - start - header_size;
   module_heap_head->next = NULL;
   module_heap_head->magic = MODULE_HEAP_MAGIC;
   module_heap_head->free = 1;
   printf("unifrog module_heap init base=0x%08lx end=0x%08lx bytes=%u module_end=0x%08lx\n",
      (unsigned long)module_heap_base, (unsigned long)module_heap_end,
      (unsigned)(module_heap_end - module_heap_base),
      (unsigned long)__unifrog_module_end);
   return 1;
}

static int module_heap_contains(const void *ptr)
{
   uintptr_t addr = (uintptr_t)ptr;

   return module_heap_base && addr >= module_heap_base &&
      addr < module_heap_end;
}

static int module_heap_maybe_contains(const void *ptr)
{
   if (!ptr)
      return 0;
   if (!module_heap_checked)
      (void)module_heap_init();
   return module_heap_contains(ptr);
}

static struct module_heap_block *module_heap_block_from_ptr(void *ptr)
{
   size_t header_size = module_heap_header_size();
   uintptr_t payload = (uintptr_t)ptr;
   uintptr_t header;
   struct module_heap_block *block;

   if (!module_heap_contains(ptr) || payload < module_heap_base + header_size)
      return NULL;
   header = payload - header_size;
   if (header < module_heap_base || header >= module_heap_end)
      return NULL;
   block = (struct module_heap_block *)header;
   if (block->magic != MODULE_HEAP_MAGIC || block->free ||
       header + header_size != payload)
      return NULL;
   return block;
}

static void module_heap_coalesce(void)
{
   size_t header_size = module_heap_header_size();
   struct module_heap_block *block = module_heap_head;

   while (block && block->next) {
      uintptr_t block_end = (uintptr_t)block + header_size + block->size;

      if (block->free && block->next->free &&
          block_end == (uintptr_t)block->next &&
          block->next->magic == MODULE_HEAP_MAGIC) {
         block->size += header_size + block->next->size;
         block->next = block->next->next;
      } else {
         block = block->next;
      }
   }
}

static void module_heap_log_oom(size_t size, size_t alignment)
{
   size_t header_size = module_heap_header_size();
   size_t total_free = 0;
   size_t largest_free = 0;
   struct module_heap_block *block;

   if (module_heap_oom_logs >= MODULE_HEAP_OOM_LOG_LIMIT)
      return;
   module_heap_oom_logs++;

   for (block = module_heap_head; block; block = block->next) {
      if (block->magic == MODULE_HEAP_MAGIC && block->free) {
         total_free += block->size;
         if (block->size > largest_free)
            largest_free = block->size;
      }
   }
   printf("unifrog module_heap oom size=%u align=%u total_free=%u largest=%u heap=0x%08lx..0x%08lx header=%u\n",
      (unsigned)size, (unsigned)alignment, (unsigned)total_free,
      (unsigned)largest_free, (unsigned long)module_heap_base,
      (unsigned long)module_heap_end, (unsigned)header_size);
}

static void *module_heap_alloc_aligned(size_t size, size_t alignment)
{
   size_t header_size;
   size_t min_block_size;
   struct module_heap_block **link;
   struct module_heap_block *block;

   if (!size)
      size = 1u;
   if (alignment < MODULE_HEAP_ALIGN)
      alignment = MODULE_HEAP_ALIGN;
   if (!module_is_power_of_two(alignment))
      return NULL;

   size = module_align_size(size, MODULE_HEAP_ALIGN);
   if (!size || !module_heap_init())
      return NULL;

   header_size = module_heap_header_size();
   min_block_size = module_heap_min_block_size();
   for (link = &module_heap_head; (block = *link) != NULL; link = &block->next) {
      struct module_heap_block *old_next;
      uintptr_t block_start;
      uintptr_t block_end;
      uintptr_t payload;
      uintptr_t header;
      uintptr_t alloc_end;
      size_t front_bytes;
      size_t tail_bytes;

      if (block->magic != MODULE_HEAP_MAGIC || !block->free)
         continue;

      block_start = (uintptr_t)block;
      if (block_start > module_heap_end ||
          module_heap_end - block_start < header_size ||
          block->size > module_heap_end - block_start - header_size)
         break;
      block_end = block_start + header_size + block->size;
      payload = module_align_up_uintptr(block_start + header_size, alignment);
      if (!payload || payload < block_start + header_size)
         continue;
      header = payload - header_size;
      front_bytes = header - block_start;
      if (front_bytes > 0 && front_bytes < min_block_size) {
         payload = module_align_up_uintptr(block_start + min_block_size +
            header_size, alignment);
         if (!payload || payload < block_start + header_size)
            continue;
         header = payload - header_size;
         if (header < block_start)
            continue;
         front_bytes = header - block_start;
         if (front_bytes > 0 && front_bytes < min_block_size)
            continue;
      }
      if (payload > block_end || size > block_end - payload)
         continue;

      old_next = block->next;
      if (front_bytes >= min_block_size) {
         struct module_heap_block *front = block;

         block = (struct module_heap_block *)header;
         front->size = front_bytes - header_size;
         front->next = block;
         front->magic = MODULE_HEAP_MAGIC;
         front->free = 1;
      } else {
         header = block_start;
         payload = block_start + header_size;
      }

      alloc_end = payload + size;
      tail_bytes = block_end - alloc_end;
      if (tail_bytes >= min_block_size) {
         struct module_heap_block *tail =
            (struct module_heap_block *)alloc_end;

         tail->size = tail_bytes - header_size;
         tail->next = old_next;
         tail->magic = MODULE_HEAP_MAGIC;
         tail->free = 1;
         block->size = size;
         block->next = tail;
      } else {
         block->size = block_end - payload;
         block->next = old_next;
      }
      block->magic = MODULE_HEAP_MAGIC;
      block->free = 0;
      return (void *)payload;
   }

   module_heap_log_oom(size, alignment);
   return NULL;
}

static void module_heap_free(void *ptr)
{
   struct module_heap_block *block = module_heap_block_from_ptr(ptr);

   if (!block)
      return;
   block->free = 1;
   module_heap_coalesce();
}

void *malloc(size_t size)
{
   const struct unifrog_abi *abi = module_abi();
   void *ptr = module_heap_alloc_aligned(size, MODULE_HEAP_ALIGN);

   return ptr ? ptr : (abi && abi->malloc ? abi->malloc(size) : NULL);
}

void free(void *ptr)
{
   const struct unifrog_abi *abi = module_abi();

   if (module_heap_maybe_contains(ptr)) {
      module_heap_free(ptr);
      return;
   }
   if (abi && abi->free)
      abi->free(ptr);
}

void *calloc(size_t nmemb, size_t size)
{
   size_t bytes;
   void *ptr;

   if (size && nmemb > (size_t)-1 / size)
      return NULL;
   bytes = nmemb * size;
   ptr = malloc(bytes);
   if (ptr)
      memset(ptr, 0, bytes);
   return ptr;
}

void *realloc(void *ptr, size_t size)
{
   const struct unifrog_abi *abi = module_abi();
   struct module_heap_block *block;
   void *new_ptr;
   size_t copy_size;

   if (!ptr)
      return malloc(size);
   if (!size) {
      free(ptr);
      return NULL;
   }

   block = module_heap_block_from_ptr(ptr);
   if (!block)
      return abi && abi->realloc ? abi->realloc(ptr, size) : NULL;
   if (size <= block->size)
      return ptr;

   new_ptr = malloc(size);
   if (!new_ptr)
      return NULL;
   copy_size = block->size < size ? block->size : size;
   memcpy(new_ptr, ptr, copy_size);
   free(ptr);
   return new_ptr;
}

void *memalign(size_t alignment, size_t size)
{
   const struct unifrog_abi *abi = module_abi();
   void *ptr;

   if (alignment < sizeof(void *))
      alignment = sizeof(void *);
   if (!module_is_power_of_two(alignment))
      return NULL;
   ptr = module_heap_alloc_aligned(size, alignment);

   return ptr ? ptr : (abi && abi->memalign ? abi->memalign(alignment, size) :
      NULL);
}

void *aligned_alloc(size_t alignment, size_t size)
{
   return memalign(alignment, size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
   void *ptr;

   if (!memptr)
      return EINVAL;
   if (alignment < sizeof(void *) || !module_is_power_of_two(alignment))
      return EINVAL;
   ptr = memalign(alignment, size);
   if (!ptr)
      return ENOMEM;
   *memptr = ptr;
   return 0;
}

void *_malloc_r(struct _reent *r, size_t size)
{
   (void)r;
   return malloc(size);
}

void _free_r(struct _reent *r, void *ptr)
{
   (void)r;
   free(ptr);
}

void *_calloc_r(struct _reent *r, size_t nmemb, size_t size)
{
   (void)r;
   return calloc(nmemb, size);
}

void *_realloc_r(struct _reent *r, void *ptr, size_t size)
{
   (void)r;
   return realloc(ptr, size);
}

char *strdup(const char *s)
{
   size_t len;
   char *copy;

   if (!s)
      return NULL;
   len = strlen(s) + 1u;
   copy = malloc(len);
   if (!copy)
      return NULL;
   memcpy(copy, s, len);
   return copy;
}

char *_strdup_r(struct _reent *r, const char *s)
{
   (void)r;
   return strdup(s);
}

char *strndup(const char *s, size_t n)
{
   size_t len;
   char *copy;

   if (!s)
      return NULL;
   for (len = 0; len < n && s[len]; len++)
      ;
   copy = malloc(len + 1u);
   if (!copy)
      return NULL;
   memcpy(copy, s, len);
   copy[len] = '\0';
   return copy;
}

static int module_path_prefix_matches(const char *path, const char *prefix)
{
   size_t len = strlen(prefix);

   return strncmp(path, prefix, len) == 0 &&
      (path[len] == '\0' || path[len] == '/');
}

static const char *module_map_path(const char *path, char *out,
   size_t out_size)
{
   const char *prefix = NULL;
   const char *base = NULL;
   const char *suffix;
   size_t base_len;
   size_t suffix_len;

   if (!path || !out || out_size == 0)
      return path;

   if (module_path_prefix_matches(path, "/mnt/sda1/cores/config")) {
      prefix = "/mnt/sda1/cores/config";
      base = "/media/mmcblk0/unifrog/config";
   } else if (module_path_prefix_matches(path, "/mnt/sda1/ROMS/SAVE")) {
      prefix = "/mnt/sda1/ROMS/SAVE";
      base = "/media/mmcblk0/unifrog/saves";
   } else if (module_path_prefix_matches(path, "/mnt/sda1")) {
      prefix = "/mnt/sda1";
      base = "/media/mmcblk0";
   }

   if (!prefix)
      return path;

   suffix = path + strlen(prefix);
   base_len = strlen(base);
   suffix_len = strlen(suffix);
   if (base_len + suffix_len + 1u > out_size)
      return path;
   memcpy(out, base, base_len);
   memcpy(out + base_len, suffix, suffix_len + 1u);
   return out;
}

static unsigned module_dir_style_for_path(const char *path)
{
   if (path && module_path_prefix_matches(path,
       "/media/mmcblk0/unifrog/config"))
      return MODULE_DIR_STYLE_NAME_AT_4;
   return MODULE_DIR_STYLE_FIRMWARE;
}

FILE *fopen(const char *path, const char *mode)
{
   const struct unifrog_abi *abi = module_abi();
   char mapped[MODULE_PATH_BUFFER];

   return abi && abi->fopen ?
      abi->fopen(module_map_path(path, mapped, sizeof(mapped)), mode) : NULL;
}

FILE *_fopen_r(struct _reent *r, const char *path, const char *mode)
{
   (void)r;
   return fopen(path, mode);
}

int fclose(FILE *stream)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->fclose ? abi->fclose(stream) : -1;
}

int _fclose_r(struct _reent *r, FILE *stream)
{
   (void)r;
   return fclose(stream);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->fread ? abi->fread(ptr, size, nmemb, stream) : 0;
}

size_t _fread_r(struct _reent *r, void *ptr, size_t size, size_t nmemb,
   FILE *stream)
{
   (void)r;
   return fread(ptr, size, nmemb, stream);
}

int fgetc(FILE *stream)
{
   unsigned char ch;

   return fread(&ch, 1, 1, stream) == 1 ? (int)ch : EOF;
}

int _fgetc_r(struct _reent *r, FILE *stream)
{
   (void)r;
   return fgetc(stream);
}

int getc(FILE *stream)
{
   return fgetc(stream);
}

char *fgets(char *s, int size, FILE *stream)
{
   int len = 0;

   if (!s || size <= 0 || !stream)
      return NULL;
   while (len + 1 < size) {
      int ch = fgetc(stream);

      if (ch == EOF)
         break;
      s[len++] = (char)ch;
      if (ch == '\n')
         break;
   }
   if (len == 0)
      return NULL;
   s[len] = '\0';
   return s;
}

char *_fgets_r(struct _reent *r, char *s, int size, FILE *stream)
{
   (void)r;
   return fgets(s, size, stream);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->fwrite ? abi->fwrite(ptr, size, nmemb, stream) : 0;
}

int fseek(FILE *stream, long offset, int whence)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->fseek ? abi->fseek(stream, offset, whence) : -1;
}

int _fseek_r(struct _reent *r, FILE *stream, long offset, int whence)
{
   (void)r;
   return fseek(stream, offset, whence);
}

long ftell(FILE *stream)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->ftell ? abi->ftell(stream) : -1L;
}

long _ftell_r(struct _reent *r, FILE *stream)
{
   (void)r;
   return ftell(stream);
}

int fseeko(FILE *stream, off_t offset, int whence)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->fseeko ? abi->fseeko(stream, offset, whence) : -1;
}

int _fseeko_r(struct _reent *r, FILE *stream, off_t offset, int whence)
{
   (void)r;
   return fseeko(stream, offset, whence);
}

off_t ftello(FILE *stream)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->ftello ? abi->ftello(stream) : (off_t)-1;
}

off_t _ftello_r(struct _reent *r, FILE *stream)
{
   (void)r;
   return ftello(stream);
}

int fflush(FILE *stream)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->fflush ? abi->fflush(stream) : 0;
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->setvbuf ? abi->setvbuf(stream, buf, mode, size) : 0;
}

int remove(const char *path)
{
   const struct unifrog_abi *abi = module_abi();
   char mapped[MODULE_PATH_BUFFER];

   return abi && abi->remove ?
      abi->remove(module_map_path(path, mapped, sizeof(mapped))) : -1;
}

int _remove_r(struct _reent *r, const char *path)
{
   (void)r;
   return remove(path);
}

int rename(const char *oldpath, const char *newpath)
{
   const struct unifrog_abi *abi = module_abi();
   char mapped_old[MODULE_PATH_BUFFER];
   char mapped_new[MODULE_PATH_BUFFER];

   return abi && abi->rename ?
      abi->rename(module_map_path(oldpath, mapped_old, sizeof(mapped_old)),
      module_map_path(newpath, mapped_new, sizeof(mapped_new))) : -1;
}

int _rename_r(struct _reent *r, const char *oldpath, const char *newpath)
{
   (void)r;
   return rename(oldpath, newpath);
}

int mkdir(const char *path, mode_t mode)
{
   const struct unifrog_abi *abi = module_abi();
   char mapped[MODULE_PATH_BUFFER];

   return abi && abi->mkdir ?
      abi->mkdir(module_map_path(path, mapped, sizeof(mapped)), mode) : -1;
}

int _mkdir_r(struct _reent *r, const char *path, mode_t mode)
{
   (void)r;
   return mkdir(path, mode);
}

static const char *open_flags_to_mode(int flags)
{
   int access = flags & O_ACCMODE;

   if (access == O_RDWR) {
      if (flags & O_TRUNC)
         return "w+b";
      if (flags & O_CREAT)
         return "a+b";
      return "r+b";
   }
   if (access == O_WRONLY) {
      if (flags & O_APPEND)
         return "ab";
      return "wb";
   }
   return "rb";
}

static FILE *module_file_from_fd(int fd)
{
   if (fd < MODULE_FD_BASE || fd >= MODULE_FD_BASE + MODULE_FD_COUNT)
      return NULL;
   return module_fd_table[fd - MODULE_FD_BASE];
}

static int module_fill_file_stat(FILE *file, struct stat *st)
{
   off_t saved;
   off_t size;

   if (!file || !st)
      return -1;
   saved = ftello(file);
   if (saved < 0)
      return -1;
   if (fseeko(file, 0, SEEK_END) != 0)
      return -1;
   size = ftello(file);
   (void)fseeko(file, saved, SEEK_SET);
   if (size < 0)
      return -1;
   memset(st, 0, sizeof(*st));
   st->st_mode = S_IFREG | 0644;
   st->st_size = size;
   return 0;
}

int open(const char *path, int flags, ...)
{
   FILE *file = fopen(path, open_flags_to_mode(flags));

   if (!file)
      return -1;
   for (int i = 0; i < MODULE_FD_COUNT; i++) {
      if (!module_fd_table[i]) {
         module_fd_table[i] = file;
         return i + MODULE_FD_BASE;
      }
   }
   fclose(file);
   return -1;
}

int close(int fd)
{
   FILE *file = module_file_from_fd(fd);
   int ret;

   if (!file)
      return -1;
   ret = fclose(file);
   module_fd_table[fd - MODULE_FD_BASE] = NULL;
   return ret;
}

int read(int fd, void *buf, size_t count)
{
   FILE *file = module_file_from_fd(fd);

   if (!file)
      return -1;
   return (int)fread(buf, 1, count, file);
}

int write(int fd, const void *buf, size_t count)
{
   FILE *file = module_file_from_fd(fd);

   if (!file)
      return -1;
   return (int)fwrite(buf, 1, count, file);
}

off_t lseek(int fd, off_t offset, int whence)
{
   FILE *file = module_file_from_fd(fd);

   if (!file || fseeko(file, offset, whence) != 0)
      return (off_t)-1;
   return ftello(file);
}

int fileno(FILE *stream)
{
   for (int i = 0; i < MODULE_FD_COUNT; i++) {
      if (module_fd_table[i] == stream)
         return i + MODULE_FD_BASE;
   }
   return -1;
}

int stat(const char *path, struct stat *st)
{
   const struct unifrog_abi *abi = module_abi();
   char mapped[MODULE_PATH_BUFFER];
   FILE *file = fopen(path, "rb");
   int ret;

   if (abi && abi->stat) {
      ret = abi->stat(module_map_path(path, mapped, sizeof(mapped)), st);
      if (ret == 0)
         return 0;
   }
   if (!file)
      return -1;
   ret = module_fill_file_stat(file, st);
   fclose(file);
   return ret;
}

int _stat_r(struct _reent *r, const char *path, struct stat *st)
{
   (void)r;
   return stat(path, st);
}

int fstat(int fd, struct stat *st)
{
   return module_fill_file_stat(module_file_from_fd(fd), st);
}

int _fstat_r(struct _reent *r, int fd, struct stat *st)
{
   (void)r;
   return fstat(fd, st);
}

static int module_posix_flags_from_fs_flags(int flags)
{
   int out = flags & O_ACCMODE;

   if (flags & 0x0008)
      out |= O_APPEND;
   if (flags & 0x0100)
      out |= O_CREAT;
   if (flags & 0x0200)
      out |= O_TRUNC;
   return out;
}

int fs_open(const char *path, int flags, int perms)
{
   (void)perms;
   return open(path, module_posix_flags_from_fs_flags(flags));
}

int fs_close(int fd)
{
   return close(fd);
}

int64_t fs_lseek(int fd, int64_t offset, int whence)
{
   return (int64_t)lseek(fd, (off_t)offset, whence);
}

ssize_t fs_read(int fd, void *buf, size_t count)
{
   return (ssize_t)read(fd, buf, count);
}

ssize_t fs_write(int fd, const void *buf, size_t count)
{
   return (ssize_t)write(fd, buf, count);
}

int fs_mkdir(const char *path, int mode)
{
   return mkdir(path, (mode_t)mode);
}

int fs_sync(const char *path)
{
   (void)path;
   return 0;
}

static int module_dir_index(int fd)
{
   if (fd < MODULE_DIR_BASE || fd >= MODULE_DIR_BASE + MODULE_DIR_COUNT)
      return -1;
   return fd - MODULE_DIR_BASE;
}

int fs_opendir(const char *path)
{
   const struct unifrog_abi *abi = module_abi();
   char mapped[MODULE_PATH_BUFFER];
   const char *actual;
   DIR *dir;

   if (!abi || !abi->opendir || !abi->closedir)
      return -1;

   actual = module_map_path(path, mapped, sizeof(mapped));
   dir = abi->opendir(actual);
   if (!dir)
      return -1;

   for (int i = 0; i < MODULE_DIR_COUNT; i++) {
      if (!module_dir_table[i].dir) {
         module_dir_table[i].dir = dir;
         module_dir_table[i].style = module_dir_style_for_path(actual);
         return MODULE_DIR_BASE + i;
      }
   }

   abi->closedir(dir);
   return -1;
}

int fs_closedir(int fd)
{
   const struct unifrog_abi *abi = module_abi();
   int idx = module_dir_index(fd);
   DIR *dir;
   int ret;

   if (!abi || !abi->closedir || idx < 0 || !module_dir_table[idx].dir)
      return -1;

   dir = module_dir_table[idx].dir;
   module_dir_table[idx].dir = NULL;
   ret = abi->closedir(dir);
   return ret;
}

static uint32_t module_dirent_mode(const struct dirent *entry)
{
#ifdef DT_DIR
   if (entry && entry->d_type == DT_DIR)
      return S_IFDIR | 0755u;
#endif
#ifdef DT_REG
   if (entry && entry->d_type == DT_REG)
      return S_IFREG | 0644u;
#endif
   return S_IFREG | 0644u;
}

ssize_t fs_readdir(int fd, void *buffer)
{
   const struct unifrog_abi *abi = module_abi();
   int idx = module_dir_index(fd);
   struct dirent *entry;
   uint8_t *bytes = (uint8_t *)buffer;
   uint32_t mode;

   if (!abi || !abi->readdir || idx < 0 || !module_dir_table[idx].dir ||
       !buffer)
      return -1;

   entry = abi->readdir(module_dir_table[idx].dir);
   if (!entry)
      return -1;

   if (module_dir_table[idx].style == MODULE_DIR_STYLE_NAME_AT_4) {
      memset(bytes, 0, 260u);
      strncpy((char *)bytes + 4, entry->d_name, 255u);
      bytes[259] = '\0';
      return 1;
   }

   memset(bytes, 0, 0x428u);
   mode = module_dirent_mode(entry);
   memcpy(bytes + 0x10, &mode, sizeof(mode));
   strncpy((char *)bytes + 0x22, entry->d_name, 0x224u);
   bytes[0x22 + 0x224] = '\0';
   return 1;
}

int ferror(FILE *stream)
{
   (void)stream;
   return 0;
}

void clearerr(FILE *stream)
{
   (void)stream;
}

void rewind(FILE *stream)
{
   (void)fseek(stream, 0, SEEK_SET);
}

char *setlocale(int category, const char *locale)
{
   (void)category;
   (void)locale;
   return "C";
}

time_t time(time_t *tloc)
{
   const struct unifrog_abi *abi = module_abi();
   time_t value = 0;

   if (abi && abi->time)
      return abi->time(tloc);
   if (tloc)
      *tloc = value;
   return value;
}

struct tm *localtime(const time_t *timer)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->localtime ? abi->localtime(timer) : NULL;
}

int gettimeofday(struct timeval *tv, void *tz)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->gettimeofday ? abi->gettimeofday(tv, tz) : -1;
}

int _gettimeofday(struct timeval *tv, void *tz)
{
   return gettimeofday(tv, tz);
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
   const struct unifrog_abi *abi = module_abi();

   if (!abi || !abi->vsnprintf)
      return -1;
   return abi->vsnprintf(str, size, format, ap);
}

int snprintf(char *str, size_t size, const char *format, ...)
{
   va_list ap;
   int ret;

   va_start(ap, format);
   ret = vsnprintf(str, size, format, ap);
   va_end(ap);
   return ret;
}

int vsprintf(char *str, const char *format, va_list ap)
{
   return vsnprintf(str, 0x7fffffffu, format, ap);
}

int sprintf(char *str, const char *format, ...)
{
   va_list ap;
   int ret;

   va_start(ap, format);
   ret = vsprintf(str, format, ap);
   va_end(ap);
   return ret;
}

int vfprintf(FILE *stream, const char *format, va_list ap)
{
   const struct unifrog_abi *abi = module_abi();

   if (!stream)
      return vprintf(format, ap);
   if (!abi || !abi->vfprintf)
      return -1;
   return abi->vfprintf(stream, format, ap);
}

int fprintf(FILE *stream, const char *format, ...)
{
   va_list ap;
   int ret;

   va_start(ap, format);
   ret = vfprintf(stream, format, ap);
   va_end(ap);
   return ret;
}

int vprintf(const char *format, va_list ap)
{
   char buffer[MODULE_PRINTF_BUFFER];
   int ret = vsnprintf(buffer, sizeof(buffer), format, ap);
   const struct unifrog_abi *abi = module_abi();

   if (ret >= 0 && abi && abi->log_message)
      abi->log_message(buffer);
   return ret;
}

int printf(const char *format, ...)
{
   va_list ap;
   int ret;

   va_start(ap, format);
   ret = vprintf(format, ap);
   va_end(ap);
   return ret;
}

int puts(const char *s)
{
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->log_message)
      abi->log_message(s ? s : "");
   return s ? (int)strlen(s) : 0;
}

int fputs(const char *s, FILE *stream)
{
   return fprintf(stream, "%s", s ? s : "");
}

int putchar(int c)
{
   char text[2];

   text[0] = (char)c;
   text[1] = '\0';
   return puts(text) >= 0 ? c : -1;
}

void xlog(const char *format, ...)
{
   const struct unifrog_abi *abi = module_abi();
   char buffer[MODULE_PRINTF_BUFFER];
   va_list ap;
   int ret;

   if (module_xlog_count >= MODULE_XLOG_LIMIT) {
      if (module_xlog_count == MODULE_XLOG_LIMIT && abi && abi->log_message)
         abi->log_message("unifrog core module xlog suppressed\n");
      module_xlog_count++;
      return;
   }
   module_xlog_count++;

   va_start(ap, format);
   ret = vsnprintf(buffer, sizeof(buffer), format ? format : "", ap);
   va_end(ap);
   if (ret >= 0 && abi && abi->log_message)
      abi->log_message(buffer);
}

void xlog_clear(void)
{
   module_xlog_count = 0;
}

uint32_t os_get_tick_count(void)
{
   struct timeval tv;

   if (gettimeofday(&tv, NULL) != 0)
      return 0;
   return (uint32_t)((uint32_t)tv.tv_sec * 1000u +
      (uint32_t)(tv.tv_usec / 1000u));
}

int getpid(void)
{
   return 1;
}

int isatty(int fd)
{
   (void)fd;
   return 0;
}

void _exit(int status) __attribute__((noreturn));
void _exit(int status)
{
   char message[64];
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->log_message) {
      (void)snprintf(message, sizeof(message),
         "unifrog core module exit status=%d\n", status);
      abi->log_message(message);
   }
   for (;;)
      ;
}

void exit(int status) __attribute__((noreturn));
void exit(int status)
{
   _exit(status);
}

void __assert_func(const char *file, int line, const char *func,
   const char *expr) __attribute__((noreturn));
void __assert_func(const char *file, int line, const char *func,
   const char *expr)
{
   char message[192];
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->log_message) {
      (void)snprintf(message, sizeof(message),
         "unifrog core module assert %s:%d %s: %s\n",
         file ? file : "?", line, func ? func : "?", expr ? expr : "?");
      abi->log_message(message);
   }
   abort();
}

void abort(void)
{
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->log_message)
      abi->log_message("unifrog core module abort\n");
   for (;;)
      ;
}

static void mips_cache_sync_all(void)
{
   uintptr_t idx;

   for (idx = MIPS_CACHE_INDEX_BASE; idx < MIPS_CACHE_INDEX_END;
        idx += MIPS_CACHE_LINE_BYTES) {
      __asm__ volatile(
         ".set push\n"
         ".set noreorder\n"
         ".set mips3\n"
         "cache 0x01, 0(%0)\n"
         "cache 0x01, 0(%0)\n"
         ".set pop\n"
         :
         : "r"(idx)
         : "memory");
   }

   __asm__ volatile(
      ".set push\n"
      ".set noreorder\n"
      ".set mips32\n"
      "sync\n"
      ".set pop\n"
      ::: "memory");

   for (idx = MIPS_CACHE_INDEX_BASE; idx < MIPS_CACHE_INDEX_END;
        idx += MIPS_CACHE_LINE_BYTES) {
      __asm__ volatile(
         ".set push\n"
         ".set noreorder\n"
         ".set mips3\n"
         "cache 0x00, 0(%0)\n"
         "cache 0x00, 0(%0)\n"
         ".set pop\n"
         :
         : "r"(idx)
         : "memory");
   }

   __asm__ volatile("nop; nop; nop; nop; nop" ::: "memory");
}

void _flush_cache(void *ptr, int len, int cache)
{
   (void)ptr;
   (void)len;
   (void)cache;
   mips_cache_sync_all();
}

void unifrog_exception_panic(uint32_t cause, uint32_t epc, uint32_t badvaddr,
   uint32_t ra)
{
   const struct unifrog_abi *abi = module_abi();

   (void)cause;
   (void)epc;
   (void)badvaddr;
   (void)ra;
   if (abi && abi->log_message)
      (void)abi->log_message("unifrog core module exception\n");
   for (;;)
      ;
}
