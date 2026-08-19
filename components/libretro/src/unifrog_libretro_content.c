#include "unifrog_libretro_internal.h"

#define LIBRETRO_CONTENT_ERR_ALLOC (-2)

static int read_file_aligned(FILE *file, const char *path,
   uint8_t **out_data, size_t *out_size, const char *label);
static int read_file_aligned_timeout(FILE *file, const char *path,
   uint8_t **out_data, size_t *out_size, const char *label,
   unsigned timeout_ms);

static uint16_t read_le16(const uint8_t *data)
{
   return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
   return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
      ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static const char *path_extension(const char *path)
{
   const char *slash = strrchr(path, '/');
   const char *dot = strrchr(path, '.');

   if (!dot || (slash && dot < slash) || dot[1] == '\0')
      return NULL;
   return dot + 1;
}

static void *rom_alloc_aligned(size_t size)
{
   uintptr_t aligned;
   uint8_t *raw;
   struct rom_alloc_header *header;
   size_t total;

   if (size == 0)
      return NULL;
   if (size > SIZE_MAX - 31u - sizeof(*header))
      return NULL;
   total = size + 31u + sizeof(*header);

   if (host.content_alloc_appmem &&
       unifrog_abi_application_memory_reserve_top(total, 32u,
       (void **)&raw) == 0) {
      aligned = ((uintptr_t)(raw + sizeof(*header)) + 31u) &
         ~(uintptr_t)31u;
      header = ((struct rom_alloc_header *)aligned) - 1;
      header->magic = ROM_ALLOC_MAGIC;
      header->kind = ROM_ALLOC_APPMEM;
      header->raw = raw;
      header->reserved_bytes = total;
      header->payload_size = size;
      printf("unifrog libretro rom alloc appmem size=%u total=%u ptr=0x%08lx aligned=%lu\n",
         (unsigned)size, (unsigned)total, (unsigned long)aligned,
         (unsigned long)(aligned & 31u));
      unifrog_diag_memory_snapshot("libretro.rom_alloc_appmem");
      return (void *)aligned;
   }

   unifrog_diag_memory_snapshot("libretro.before_rom_alloc_heap");
   raw = malloc(total);
   if (!raw) {
      printf("unifrog libretro rom alloc heap failed size=%u total=%u\n",
         (unsigned)size, (unsigned)total);
      unifrog_diag_memory_snapshot("libretro.rom_alloc_heap_failed");
      return NULL;
   }
   aligned = ((uintptr_t)(raw + sizeof(*header)) + 31u) & ~(uintptr_t)31u;
   header = ((struct rom_alloc_header *)aligned) - 1;
   header->magic = ROM_ALLOC_MAGIC;
   header->kind = ROM_ALLOC_HEAP;
   header->raw = raw;
   header->reserved_bytes = total;
   header->payload_size = size;
   printf("unifrog libretro rom alloc heap size=%u total=%u raw=0x%08lx ptr=0x%08lx aligned=%lu\n",
      (unsigned)size, (unsigned)total, (unsigned long)(uintptr_t)raw,
      (unsigned long)aligned, (unsigned long)(aligned & 31u));
   unifrog_diag_memory_snapshot("libretro.rom_alloc_heap");
   return (void *)aligned;
}

void rom_free_aligned(void *ptr)
{
   struct rom_alloc_header *header;

   if (!ptr)
      return;
   header = ((struct rom_alloc_header *)ptr) - 1;
   if (header->magic != ROM_ALLOC_MAGIC) {
      printf("unifrog libretro rom free bad_header ptr=0x%08lx magic=0x%08lx\n",
         (unsigned long)(uintptr_t)ptr, (unsigned long)header->magic);
      return;
   }
   if (header->kind == ROM_ALLOC_APPMEM)
      unifrog_abi_application_memory_release_top(header->raw);
   else
      free(header->raw);
   unifrog_diag_memory_snapshot("libretro.rom_free");
}

static int file_size(FILE *file, size_t *out_size)
{
   long size;

   if (!file || !out_size)
      return -1;
   if (fseek(file, 0, SEEK_END) != 0)
      return -1;
   size = ftell(file);
   if (size < 0 || fseek(file, 0, SEEK_SET) != 0)
      return -1;
   *out_size = (size_t)size;
   return 0;
}

void probe_rom_seek_path(const char *path)
{
   static unsigned probe_count;
   struct stat st;
   uint8_t fd_first[LIBRETRO_FS_PROBE_SAMPLE];
   uint8_t fd_again[LIBRETRO_FS_PROBE_SAMPLE];
   uint8_t std_first[LIBRETRO_FS_PROBE_SAMPLE];
   uint8_t std_again[LIBRETRO_FS_PROBE_SAMPLE];
   off_t fd_pos_after = (off_t)-1;
   off_t fd_seek_cur = (off_t)-1;
   off_t fd_seek_mid = (off_t)-1;
   off_t fd_seek_zero = (off_t)-1;
   long std_tell_after = -1;
   long std_tell_mid = -1;
   ssize_t fd_read_first = -1;
   ssize_t fd_read_mid = -1;
   ssize_t fd_read_again = -1;
   size_t std_read_first = 0;
   size_t std_read_mid = 0;
   size_t std_read_again = 0;
   off_t mid;
   int fd = -1;
   FILE *file = NULL;
   int fd_same = 0;
   int std_same = 0;

   if (!path || probe_count >= LIBRETRO_FS_PROBE_MAX_LOGS ||
       stat(path, &st) != 0 || st.st_size < (off_t)LIBRETRO_FS_PROBE_MIN_SIZE)
      return;
   probe_count++;
   mid = st.st_size / 2;

   fd = open(path, O_RDONLY);
   if (fd >= 0) {
      fd_read_first = read(fd, fd_first, sizeof(fd_first));
      fd_pos_after = lseek(fd, 0, SEEK_CUR);
      fd_seek_cur = lseek(fd, 128, SEEK_CUR);
      fd_seek_mid = lseek(fd, mid, SEEK_SET);
      if (fd_seek_mid >= 0)
         fd_read_mid = read(fd, fd_again, sizeof(fd_again));
      fd_seek_zero = lseek(fd, 0, SEEK_SET);
      if (fd_seek_zero == 0)
         fd_read_again = read(fd, fd_again, sizeof(fd_again));
      fd_same = fd_read_first == (ssize_t)sizeof(fd_first) &&
         fd_read_again == (ssize_t)sizeof(fd_again) &&
         memcmp(fd_first, fd_again, sizeof(fd_first)) == 0;
      close(fd);
   }

   file = fopen(path, "rb");
   if (file) {
      std_read_first = fread(std_first, 1, sizeof(std_first), file);
      std_tell_after = ftell(file);
      if (fseek(file, (long)mid, SEEK_SET) == 0) {
         std_read_mid = fread(std_again, 1, sizeof(std_again), file);
         std_tell_mid = ftell(file);
      }
      if (fseek(file, 0, SEEK_SET) == 0)
         std_read_again = fread(std_again, 1, sizeof(std_again), file);
      std_same = std_read_first == sizeof(std_first) &&
         std_read_again == sizeof(std_again) &&
         memcmp(std_first, std_again, sizeof(std_first)) == 0;
      fclose(file);
   }

   printf("unifrog fs_probe path=%s size=%u fd_read0=%d fd_pos_after=%ld fd_seek_cur=%ld fd_seek_mid=%ld fd_read_mid=%d fd_seek0=%ld fd_read0b=%d fd_same0=%d std_read0=%u std_tell_after=%ld std_read_mid=%u std_tell_mid=%ld std_read0b=%u std_same0=%d\n",
      path, (unsigned)st.st_size, (int)fd_read_first, (long)fd_pos_after,
      (long)fd_seek_cur, (long)fd_seek_mid, (int)fd_read_mid,
      (long)fd_seek_zero, (int)fd_read_again, fd_same,
      (unsigned)std_read_first, std_tell_after, (unsigned)std_read_mid,
      std_tell_mid, (unsigned)std_read_again, std_same);
   (void)unifrog_log_flush();
}

static int read_fd_fully_to_buffer(int fd, const char *path, uint8_t *data,
   size_t size, const char *title, const char *label, unsigned progress_base,
   unsigned progress_span)
{
   size_t done = 0;
   unsigned last_progress = 0xffffffffu;
   uint64_t start_us = host_time_us();

   if (fd < 0 || !data || size == 0)
      return -1;
   while (done < size) {
      size_t chunk = size - done;
      ssize_t got;
      unsigned progress;
      size_t max_chunk = libretro_content_read_chunk();

      if (chunk > max_chunk)
         chunk = max_chunk;
      got = read(fd, data + done, chunk);
      if (got <= 0) {
         printf("unifrog libretro file read failed path=%s size=%u done=%u got=%d errno=%d label=%s\n",
            path ? path : "", (unsigned)size, (unsigned)done, (int)got,
            errno, label ? label : "");
         unifrog_log_sync("content_fd_read fail path=%s size=%u done=%u got=%d errno=%d label=%s",
            path ? path : "", (unsigned)size, (unsigned)done, (int)got,
            errno, label ? label : "");
         return -1;
      }
      done += (size_t)got;
      progress = progress_base + (unsigned)((done * progress_span) /
         (size ? size : 1u));
      if (progress != last_progress) {
         loading_draw(title ? title : "LOADING", label ? label : "READING",
            progress);
         last_progress = progress;
      }
   }
   unifrog_log_sync("content_fd_read done path=%s size=%u elapsed=%u label=%s",
      path ? path : "", (unsigned)size,
      host_elapsed_ms(start_us, host_time_us()), label ? label : "");
   return 0;
}

static int read_path_aligned_direct(const char *path, uint8_t **out_data,
   size_t *out_size, const char *label)
{
   unsigned attempts = libretro_storage_attempts();

   if (!path || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;

   for (unsigned attempt = 0; attempt < attempts; attempt++) {
      struct stat st;
      uint8_t *data = NULL;
      size_t size;
      uint64_t start_us;
      uint64_t end_us;
      int fd = -1;

      if (stat(path, &st) != 0 || st.st_size <= 0) {
         printf("unifrog libretro rom stat failed path=%s errno=%d attempt=%u label=%s\n",
            path, errno, attempt + 1u, label ? label : "");
         if (attempt + 1u < attempts)
            (void)libretro_recover_storage("rom_stat");
         continue;
      }
      size = (size_t)st.st_size;
      data = rom_alloc_aligned(size);
      if (!data) {
         printf("unifrog libretro rom alloc failed path=%s size=%u label=%s\n",
            path, (unsigned)size, label ? label : "");
         return -1;
      }
      fd = open(path, O_RDONLY);
      if (fd < 0) {
         printf("unifrog libretro rom open failed path=%s errno=%d attempt=%u label=%s\n",
            path, errno, attempt + 1u, label ? label : "");
         rom_free_aligned(data);
         if (attempt + 1u < attempts)
            (void)libretro_recover_storage("rom_open");
         continue;
      }
      start_us = host_time_us();
      errno = 0;
      if (read_fd_fully_to_buffer(fd, path, data, size, "LOADING ROM",
          label ? label : "READING", 12, 58) == 0) {
         end_us = host_time_us();
         close(fd);
         *out_data = data;
         *out_size = size;
         printf("unifrog load_time stage=file_read mode=fd_aligned ms=%u bytes=%u chunk=%u label=%s attempts=%u path=%s\n",
            host_elapsed_ms(start_us, end_us), (unsigned)size,
            (unsigned)libretro_content_read_chunk(), label ? label : "",
            attempt + 1u, path);
         return 0;
      }
      {
         int read_errno = errno;

         close(fd);
         rom_free_aligned(data);
         if (read_errno == ETIMEDOUT) {
            errno = ETIMEDOUT;
            return -1;
         }
      }
      if (attempt + 1u < attempts)
         (void)libretro_recover_storage("rom_read");
   }

   return -1;
}

static uint32_t hash_u32(uint32_t hash, uint32_t value)
{
   for (unsigned i = 0; i < 4; i++) {
      hash ^= value & 0xffu;
      hash *= 16777619u;
      value >>= 8;
   }
   return hash;
}

static uint32_t hash_text(uint32_t hash, const char *text)
{
   while (text && *text) {
      hash ^= (uint8_t)*text++;
      hash *= 16777619u;
   }
   return hash;
}

static uint32_t hash_bytes(uint32_t hash, const uint8_t *bytes, size_t len)
{
   for (size_t i = 0; bytes && i < len; i++) {
      hash ^= bytes[i];
      hash *= 16777619u;
   }
   return hash;
}

static uint32_t hash_source_file_sample(uint32_t hash, const char *path)
{
   FILE *file;
   struct stat st;
   uint8_t sample[256];
   size_t got;

   if (!path || stat(path, &st) != 0 || st.st_size <= 0)
      return hash;

   hash = hash_u32(hash, (uint32_t)st.st_size);
   hash = hash_u32(hash, (uint32_t)(st.st_size >> 32));
   file = fopen(path, "rb");
   if (!file)
      return hash;

   got = fread(sample, 1, sizeof(sample), file);
   hash = hash_bytes(hash, sample, got);
   if (st.st_size > (long)sizeof(sample) &&
       fseek(file, (long)(st.st_size - (long)sizeof(sample)), SEEK_SET) == 0) {
      got = fread(sample, 1, sizeof(sample), file);
      hash = hash_bytes(hash, sample, got);
   }
   fclose(file);
   return hash;
}

static int path_is_lz4(const char *path)
{
   return unifrog_text_ends_with_ci(path, ".lz4");
}

static int path_is_zstd(const char *path)
{
   return unifrog_text_ends_with_ci(path, ".zst") ||
      unifrog_text_ends_with_ci(path, ".zstd");
}

int path_is_zip(const char *path)
{
   return unifrog_text_ends_with_ci(path, ".zip");
}

int path_is_wrapped_compressed(const char *path)
{
#ifdef UNIFROG_LIBRETRO_NO_COMPRESSED
   (void)path;
   return 0;
#else
   return path_is_lz4(path) || path_is_zstd(path);
#endif
}

int copy_path_without_last_extension(const char *path, char *out,
   size_t out_size)
{
   const char *slash;
   const char *dot;
   size_t len;

   if (!path || !out || out_size == 0)
      return -1;
   slash = strrchr(path, '/');
   dot = strrchr(path, '.');
   if (!dot || (slash && dot < slash) || dot == path)
      return -1;
   len = (size_t)(dot - path);
   if (len >= out_size)
      len = out_size - 1u;
   memcpy(out, path, len);
   out[len] = '\0';
   return 0;
}

static int first_valid_extension(const char *valid_extensions, char *out,
   size_t out_size)
{
   const char *cursor = valid_extensions;
   size_t len;

   if (!out || out_size == 0)
      return -1;
   out[0] = '\0';
   if (!cursor || !cursor[0])
      return -1;
   while (*cursor == '.')
      cursor++;
   len = 0;
   while (cursor[len] && cursor[len] != '|' && len + 1u < out_size)
      len++;
   if (len == 0)
      return -1;
   memcpy(out, cursor, len);
   out[len] = '\0';
   return 0;
}

static int content_extension_for_cache(const char *source_path,
   const char *entry_name, const char *valid_extensions,
   char *out, size_t out_size)
{
   char stripped[256];
   const char *ext = NULL;

   if (!out || out_size == 0)
      return -1;
   out[0] = '\0';
   if (entry_name && entry_name[0])
      ext = path_extension(entry_name);
   if (!ext && path_is_wrapped_compressed(source_path) &&
       copy_path_without_last_extension(source_path, stripped,
       sizeof(stripped)) == 0)
      ext = path_extension(stripped);
   if (ext && ext[0]) {
      unifrog_text_copy(out, out_size, ext);
      return 0;
   }
   return first_valid_extension(valid_extensions, out, out_size);
}

static int ensure_content_cache_dir(void)
{
   if (mkdir(UNIFROG_DIST_ROOT, 0777) != 0 && errno != EEXIST)
      return -1;
   if (mkdir(LIBRETRO_CONTENT_CACHE_DIR, 0777) != 0 && errno != EEXIST)
      return -1;
   return 0;
}

static int content_cache_path(const char *source_path, const char *entry_name,
   const char *valid_extensions, char *out, size_t out_size)
{
   uint32_t hash = 2166136261u;
   char ext[16];

   if (!source_path || !out || out_size == 0)
      return -1;
   if (content_extension_for_cache(source_path, entry_name, valid_extensions,
       ext, sizeof(ext)) != 0)
      return -1;
   hash = hash_text(hash, source_path);
   hash = hash_text(hash, entry_name ? entry_name : "");
   hash = hash_source_file_sample(hash, source_path);
   if (ensure_content_cache_dir() != 0)
      return -1;
   snprintf(out, out_size, "%s/uf%08x.%s",
      LIBRETRO_CONTENT_CACHE_DIR, (unsigned)hash, ext);
   return 0;
}

static int libretro_valid_extension_matches(const char *path,
   const char *valid_extensions)
{
   const char *ext = path_extension(path);
   const char *cursor = valid_extensions;
   size_t ext_len;

   if (!ext)
      return 0;
   if (strcasecmp(ext, "zip") == 0 ||
       strcasecmp(ext, "lz4") == 0 ||
       strcasecmp(ext, "zst") == 0 ||
       strcasecmp(ext, "zstd") == 0)
      return 0;
   if (!valid_extensions || !valid_extensions[0])
      return 1;
   ext_len = strlen(ext);
   while (*cursor) {
      const char *begin = cursor;
      size_t len;

      while (*cursor && *cursor != '|')
         cursor++;
      while (*begin == '.')
         begin++;
      len = (size_t)(cursor - begin);
      if (len == ext_len && strncasecmp(begin, ext, len) == 0)
         return 1;
      if (*cursor == '|')
         cursor++;
   }
   return 0;
}

/* Private libretro content loading, archive, and compressed-ROM helpers. */
static int zip_entry_name_is_dir(const char *name)
{
   size_t len = name ? strlen(name) : 0;

   return len > 0 && name[len - 1] == '/';
}

static int zip_find_eocd(const uint8_t *zip, size_t zip_size,
   size_t *eocd_offset)
{
   size_t min_pos;
   size_t pos;

   if (!zip || !eocd_offset || zip_size < 22)
      return -1;
   min_pos = zip_size > (0xffffu + 22u) ? zip_size - (0xffffu + 22u) : 0;
   pos = zip_size - 22u;
   for (;;) {
      if (read_le32(zip + pos) == 0x06054b50u) {
         uint16_t comment_len = read_le16(zip + pos + 20);

         if (pos + 22u + comment_len == zip_size) {
            *eocd_offset = pos;
            return 0;
         }
      }
      if (pos == min_pos)
         break;
      pos--;
   }
   return -1;
}

static int zip_select_rom_entry(const uint8_t *zip, size_t zip_size,
   const char *valid_extensions, struct zip_rom_entry *selected)
{
   size_t eocd;
   size_t cursor;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries;

   if (!selected || zip_find_eocd(zip, zip_size, &eocd) != 0)
      return -1;
   entries = read_le16(zip + eocd + 10);
   cd_size = read_le32(zip + eocd + 12);
   cd_offset = read_le32(zip + eocd + 16);
   if ((size_t)cd_offset + cd_size > zip_size)
      return -1;

   cursor = cd_offset;
   memset(selected, 0, sizeof(*selected));
   for (uint16_t i = 0; i < entries && cursor + 46u <= zip_size; i++) {
      const uint8_t *header = zip + cursor;
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      uint16_t method;
      uint16_t flags;
      uint32_t compressed_size;
      uint32_t uncompressed_size;
      char name[sizeof(selected->name)];
      size_t copy_len;

      if (read_le32(header) != 0x02014b50u)
         return -1;
      flags = read_le16(header + 8);
      method = read_le16(header + 10);
      compressed_size = read_le32(header + 20);
      uncompressed_size = read_le32(header + 24);
      name_len = read_le16(header + 28);
      extra_len = read_le16(header + 30);
      comment_len = read_le16(header + 32);
      if (cursor + 46u + name_len + extra_len + comment_len > zip_size)
         return -1;
      copy_len = name_len;
      if (copy_len >= sizeof(name))
         copy_len = sizeof(name) - 1u;
      memcpy(name, header + 46, copy_len);
      name[copy_len] = '\0';

      if (!zip_entry_name_is_dir(name) &&
          (method == 0 || method == 8) &&
          !(flags & 1u) &&
          uncompressed_size > 0 &&
          uncompressed_size <= LIBRETRO_ZIP_MAX_UNCOMPRESSED &&
          libretro_valid_extension_matches(name, valid_extensions)) {
         unifrog_text_copy(selected->name, sizeof(selected->name), name);
         selected->flags = flags;
         selected->method = method;
         selected->crc32 = read_le32(header + 16);
         selected->compressed_size = compressed_size;
         selected->uncompressed_size = uncompressed_size;
         selected->local_offset = read_le32(header + 42);
         return 0;
      }

      cursor += 46u + name_len + extra_len + comment_len;
   }
   return -1;
}

static int zip_inflate_raw(uint8_t *out, size_t out_size,
   const uint8_t *in, size_t in_size)
{
   z_stream stream;
   int zret;

   memset(&stream, 0, sizeof(stream));
   stream.next_in = (Bytef *)in;
   stream.avail_in = (uInt)in_size;
   stream.next_out = out;
   stream.avail_out = (uInt)out_size;
   zret = inflateInit2(&stream, -MAX_WBITS);
   if (zret != Z_OK)
      return zret;
   zret = inflate(&stream, Z_FINISH);
   (void)inflateEnd(&stream);
   if (zret != Z_STREAM_END || stream.total_out != out_size)
      return zret == Z_STREAM_END ? Z_BUF_ERROR : zret;
   return Z_OK;
}

static int zip_select_rom_entry_stream(FILE *file, const char *zip_path,
   size_t zip_size, const char *valid_extensions,
   struct zip_rom_entry *selected)
{
   uint8_t *tail = NULL;
   uint8_t *cd = NULL;
   size_t tail_size;
   size_t tail_eocd;
   uint32_t cd_offset;
   uint32_t cd_size;
   size_t cursor = 0;
   uint16_t entries;
   int out_ret = -1;

   if (!file || !zip_path || !selected || zip_size < 22)
      return -1;
   tail_size = zip_size > (0xffffu + 22u) ?
      (0xffffu + 22u) : zip_size;
   tail = malloc(tail_size);
   if (!tail)
      return -1;
   if (fseek(file, (long)(zip_size - tail_size), SEEK_SET) != 0 ||
       fread(tail, 1, tail_size, file) != tail_size)
      goto out;
   if (zip_find_eocd(tail, tail_size, &tail_eocd) != 0)
      goto out;
   entries = read_le16(tail + tail_eocd + 10);
   cd_size = read_le32(tail + tail_eocd + 12);
   cd_offset = read_le32(tail + tail_eocd + 16);
   if ((size_t)cd_offset + cd_size > zip_size)
      goto out;
   if (cd_size == 0 || cd_size > 4u * 1024u * 1024u)
      goto out;
   cd = malloc(cd_size);
   if (!cd)
      goto out;
   if (fseek(file, (long)cd_offset, SEEK_SET) != 0 ||
       fread(cd, 1, cd_size, file) != cd_size)
      goto out;

   memset(selected, 0, sizeof(*selected));
   for (uint16_t i = 0; i < entries && cursor + 46u <= cd_size; i++) {
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      uint16_t flags;
      uint16_t method;
      uint32_t compressed_size;
      uint32_t uncompressed_size;
      uint32_t local_offset;
      char name[sizeof(selected->name)];
      size_t copy_len;
      size_t record_len;

      if (read_le32(cd + cursor) != 0x02014b50u)
         goto out;
      flags = read_le16(cd + cursor + 8);
      method = read_le16(cd + cursor + 10);
      compressed_size = read_le32(cd + cursor + 20);
      uncompressed_size = read_le32(cd + cursor + 24);
      name_len = read_le16(cd + cursor + 28);
      extra_len = read_le16(cd + cursor + 30);
      comment_len = read_le16(cd + cursor + 32);
      local_offset = read_le32(cd + cursor + 42);
      if ((size_t)local_offset >= zip_size)
         goto out;
      record_len = 46u + (size_t)name_len + extra_len + comment_len;
      if (record_len > cd_size - cursor)
         goto out;
      copy_len = name_len;
      if (copy_len >= sizeof(name))
         copy_len = sizeof(name) - 1u;
      memcpy(name, cd + cursor + 46u, copy_len);
      name[copy_len] = '\0';
      cursor += record_len;

      if (!zip_entry_name_is_dir(name) &&
          (method == 0 || method == 8) &&
          !(flags & 1u) &&
          uncompressed_size > 0 &&
          uncompressed_size <= LIBRETRO_ZIP_MAX_UNCOMPRESSED &&
          libretro_valid_extension_matches(name, valid_extensions)) {
         unifrog_text_copy(selected->name, sizeof(selected->name), name);
         selected->flags = flags;
         selected->method = method;
         selected->crc32 = read_le32(cd + cursor - record_len + 16);
         selected->compressed_size = compressed_size;
         selected->uncompressed_size = uncompressed_size;
         selected->local_offset = local_offset;
         out_ret = 0;
         break;
      }
   }

out:
   free(cd);
   free(tail);
   if (out_ret != 0)
      printf("unifrog libretro zip stream select failed path=%s\n",
         zip_path);
   return out_ret;
}

static int zip_locate_entry_data(FILE *file, const char *zip_path,
   size_t zip_size, const struct zip_rom_entry *entry, size_t *data_offset)
{
   uint8_t local[30];
   uint16_t local_name_len;
   uint16_t local_extra_len;
   size_t offset;

   if (!file || !entry || !data_offset ||
       (size_t)entry->local_offset + sizeof(local) > zip_size)
      return -1;
   if (fseek(file, (long)entry->local_offset, SEEK_SET) != 0 ||
       fread(local, 1, sizeof(local), file) != sizeof(local) ||
       read_le32(local) != 0x04034b50u) {
      printf("unifrog libretro zip local header failed path=%s entry=%s\n",
         zip_path, entry->name);
      return -1;
   }
   if (read_le16(local + 8) != entry->method) {
      printf("unifrog libretro zip method mismatch path=%s entry=%s\n",
         zip_path, entry->name);
      return -1;
   }
   local_name_len = read_le16(local + 26);
   local_extra_len = read_le16(local + 28);
   offset = (size_t)entry->local_offset + sizeof(local) +
      local_name_len + local_extra_len;
   if (offset > zip_size || entry->compressed_size > zip_size - offset) {
      printf("unifrog libretro zip data offset failed path=%s entry=%s\n",
         zip_path, entry->name);
      return -1;
   }
   *data_offset = offset;
   return 0;
}

static int zip_read_stored_stream(FILE *file, const char *zip_path,
   const struct zip_rom_entry *entry, uint8_t *out, size_t out_size)
{
   size_t done = 0;

   if (entry->compressed_size != entry->uncompressed_size ||
       out_size != entry->uncompressed_size)
      return -1;
   loading_draw("LOADING ZIP", "COPYING", 36);
   while (done < out_size) {
      size_t chunk = out_size - done;

      if (chunk > LIBRETRO_CONTENT_STREAM_IN)
         chunk = LIBRETRO_CONTENT_STREAM_IN;
      if (fread(out + done, 1, chunk, file) != chunk) {
         printf("unifrog libretro zip stored read failed path=%s entry=%s done=%u\n",
            zip_path, entry->name, (unsigned)done);
         return -1;
      }
      done += chunk;
      loading_draw("LOADING ZIP", "COPYING",
         36u + (unsigned)((done * 30u) /
         (out_size ? out_size : 1u)));
   }
   return 0;
}

static int zip_inflate_raw_stream(FILE *file, const char *zip_path,
   const struct zip_rom_entry *entry, uint8_t *out, size_t out_size)
{
   z_stream stream;
   uint8_t *in_buf = NULL;
   size_t remaining;
   int zret = Z_OK;
   int out_ret = -1;
   unsigned last_percent = 0xffu;
   size_t old_auto_flush = 0;
   int storage_quiet = 0;

   if (!file || !entry || !out || out_size == 0)
      return -1;
   in_buf = malloc(LIBRETRO_CONTENT_STREAM_IN);
   if (!in_buf)
      return -1;
   memset(&stream, 0, sizeof(stream));
   zret = inflateInit2(&stream, -MAX_WBITS);
   if (zret != Z_OK)
      goto out;

   remaining = entry->compressed_size;
   loading_draw("LOADING ZIP", "INFLATE 0%", 36);
   unifrog_log_sync("zip_inflate begin path=%s entry=%s compressed=%u uncompressed=%u",
      zip_path, entry->name, entry->compressed_size,
      entry->uncompressed_size);
   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_log_defer_begin();
   unifrog_platform_set_storage_log_suspended(1);
   storage_quiet = 1;
   unifrog_platform_sd_debug_dump("zip_inflate_start");
   while (remaining > 0 && zret != Z_STREAM_END) {
      size_t chunk = remaining;
      unsigned percent;

      if (chunk > LIBRETRO_CONTENT_STREAM_IN)
         chunk = LIBRETRO_CONTENT_STREAM_IN;
      if (fread(in_buf, 1, chunk, file) != chunk) {
         printf("unifrog libretro zip entry read failed path=%s entry=%s\n",
            zip_path, entry->name);
         goto out_inflate;
      }
      remaining -= chunk;
      stream.next_in = in_buf;
      stream.avail_in = (uInt)chunk;
      while (stream.avail_in > 0 && zret != Z_STREAM_END) {
         if ((size_t)stream.total_out >= out_size)
            goto out_inflate;
         stream.next_out = out + stream.total_out;
         stream.avail_out = (uInt)(out_size - (size_t)stream.total_out);
         zret = inflate(&stream, Z_NO_FLUSH);
         if (zret != Z_OK && zret != Z_STREAM_END)
            goto out_inflate;
      }
      percent = entry->compressed_size ?
         (unsigned)(((entry->compressed_size - remaining) * 100u) /
         entry->compressed_size) : 100u;
      if (percent / 10u != last_percent / 10u || remaining == 0) {
         char detail[32];
         unsigned mapped_percent = 36u + (unsigned)(((uint64_t)percent * 30u) /
            100u);

         last_percent = percent;
         snprintf(detail, sizeof(detail), "INFLATE %u%%", percent);
         loading_draw("LOADING ZIP", detail, mapped_percent);
         libretro_watchdog_load_progress("zip inflate", percent, 100);
         unifrog_log_sync("zip_inflate progress path=%s entry=%s percent=%u in=%u out=%u",
            zip_path, entry->name, percent,
            (unsigned)(entry->compressed_size - remaining),
            (unsigned)stream.total_out);
      }
      unifrog_perf_delay_us(1000u);
   }

   if (zret != Z_STREAM_END || stream.total_out != out_size)
      goto out_inflate;
   out_ret = 0;
   unifrog_platform_sd_debug_dump("zip_inflate_done");
   unifrog_log_sync("zip_inflate done path=%s entry=%s out=%u",
      zip_path, entry->name, (unsigned)stream.total_out);

out_inflate:
   if (out_ret != 0)
      unifrog_platform_sd_debug_dump("zip_inflate_fail");
   if (storage_quiet) {
      unifrog_platform_set_storage_log_suspended(0);
      unifrog_log_defer_end();
      unifrog_log_set_auto_flush_bytes(old_auto_flush);
      if (out_ret == 0)
         (void)libretro_log_flush_force_if_safe();
   }
   (void)inflateEnd(&stream);
out:
   if (out_ret != 0)
      printf("unifrog libretro zip inflate failed path=%s entry=%s zret=%d out=%u expected=%u\n",
         zip_path, entry ? entry->name : "", zret,
         (unsigned)stream.total_out, (unsigned)out_size);
   free(in_buf);
   return out_ret;
}

static int zip_load_rom_data_stream_entry(FILE *file, const char *zip_path,
   size_t zip_size, const struct zip_rom_entry *entry, uint8_t **out_data,
   size_t *out_size, char *out_name, size_t out_name_size)
{
   uint8_t *rom = NULL;
   size_t data_offset;
   uint64_t start_us = host_time_us();
   uint64_t end_us;
   int ret = -1;

   if (!file || !zip_path || !entry || !out_data || !out_size)
      return -1;
   printf("unifrog libretro zip entry name=%s method=%u compressed=%u uncompressed=%u flags=0x%04x mode=stream_ram\n",
      entry->name, entry->method, entry->compressed_size,
      entry->uncompressed_size, entry->flags);
   if (zip_locate_entry_data(file, zip_path, zip_size, entry,
       &data_offset) != 0)
      return -1;

   rom = rom_alloc_aligned(entry->uncompressed_size);
   if (!rom) {
      printf("unifrog libretro zip data alloc failed path=%s entry=%s uncompressed=%u mode=stream_ram\n",
         zip_path, entry->name, entry->uncompressed_size);
      return LIBRETRO_CONTENT_ERR_ALLOC;
   }
   if (fseek(file, (long)data_offset, SEEK_SET) != 0)
      goto out;
   if (entry->method == 0)
      ret = zip_read_stored_stream(file, zip_path, entry, rom,
         entry->uncompressed_size);
   else
      ret = zip_inflate_raw_stream(file, zip_path, entry, rom,
         entry->uncompressed_size);
   if (ret != 0)
      goto out;
   if ((uint32_t)crc32(crc32(0L, Z_NULL, 0), rom,
       (uInt)entry->uncompressed_size) != entry->crc32) {
      printf("unifrog libretro zip crc mismatch path=%s entry=%s\n",
         zip_path, entry->name);
      ret = -1;
      goto out;
   }

   end_us = host_time_us();
   if (out_name && out_name_size)
      unifrog_text_copy(out_name, out_name_size, entry->name);
   *out_data = rom;
   *out_size = entry->uncompressed_size;
   rom = NULL;
   printf("unifrog libretro zip loaded entry=%s size=%u aligned=%lu mode=stream_ram\n",
      entry->name, (unsigned)*out_size,
      (unsigned long)((uintptr_t)*out_data & 31u));
   printf("unifrog load_time stage=zip_memory ms=%u compressed=%u uncompressed=%u mode=stream_ram method=%u\n",
      host_elapsed_ms(start_us, end_us), entry->compressed_size,
      entry->uncompressed_size, entry->method);
   loading_draw("LOADING ZIP", "READY", 68);
   ret = 0;

out:
   rom_free_aligned(rom);
   return ret;
}

static int zip_load_rom_data_stream(FILE *file, const char *zip_path,
   const struct retro_system_info *info, uint8_t **out_data,
   size_t *out_size, char *out_name, size_t out_name_size)
{
   struct zip_rom_entry entry;
   size_t zip_size;

   if (!file || !zip_path || !info || !out_data || !out_size)
      return -1;
   if (file_size(file, &zip_size) != 0)
      return -1;
   if (zip_select_rom_entry_stream(file, zip_path, zip_size,
       info->valid_extensions, &entry) != 0)
      return -1;
   return zip_load_rom_data_stream_entry(file, zip_path, zip_size, &entry,
      out_data, out_size, out_name, out_name_size);
}

int zip_load_rom_data_stream_path(const char *zip_path,
   const struct retro_system_info *info, uint8_t **out_data,
   size_t *out_size, char *out_name, size_t out_name_size)
{
   unsigned attempts = libretro_storage_attempts();

   if (!zip_path || !info || !out_data || !out_size)
      return -1;
   for (unsigned attempt = 0; attempt < attempts; attempt++) {
      FILE *file = fopen(zip_path, "rb");
      int ret;

      if (!file) {
         printf("unifrog libretro zip open failed path=%s attempt=%u errno=%d\n",
            zip_path, attempt + 1u, errno);
         ret = -1;
      } else {
         ret = zip_load_rom_data_stream(file, zip_path, info, out_data,
            out_size, out_name, out_name_size);
         fclose(file);
      }
      if (ret == 0) {
         if (attempt > 0)
            printf("unifrog libretro zip recovered path=%s attempts=%u\n",
               zip_path, attempt + 1u);
         return 0;
      }
      /*
       * Memory pressure is deterministic for the current launch context. Do
       * not retry it as an SD read error, because that only adds latency and
       * can obscure the real cause in retained logs.
       */
      if (ret == LIBRETRO_CONTENT_ERR_ALLOC) {
         printf("unifrog libretro zip alloc unrecoverable path=%s attempts=%u\n",
            zip_path, attempt + 1u);
         return -1;
      }
      if (attempt + 1u < attempts)
         (void)libretro_recover_storage("zip_memory");
   }

   return -1;
}

int read_path_memory_with_fallback(const char *path,
   uint8_t **out_data, size_t *out_size, const char *label)
{
   FILE *file;
   int ret;

   errno = 0;
   if (read_path_aligned_direct(path, out_data, out_size, label) == 0)
      return 0;
   if (errno == ETIMEDOUT)
      return -1;

   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro rom fallback open failed path=%s errno=%d label=%s\n",
         path ? path : "", errno, label ? label : "");
      return -1;
   }
   ret = read_file_aligned(file, path, out_data, out_size, label);
   fclose(file);
   return ret;
}

static int read_file_aligned(FILE *file, const char *path, uint8_t **out_data,
   size_t *out_size, const char *label)
{
   return read_file_aligned_timeout(file, path, out_data, out_size, label, 0);
}

static int read_file_aligned_timeout(FILE *file, const char *path,
   uint8_t **out_data, size_t *out_size, const char *label,
   unsigned timeout_ms)
{
   uint8_t *data = NULL;
   size_t size;
   uint64_t read_start_us;

   if (!file || !out_data || !out_size)
      return -1;
   if (file_size(file, &size) != 0)
      return -1;
   data = rom_alloc_aligned(size);
   if (!data) {
      printf("unifrog libretro rom alloc failed path=%s size=%u label=%s\n",
         path ? path : "", (unsigned)size, label ? label : "");
      return -1;
   }
   read_start_us = host_time_us();
   for (size_t done = 0; done < size;) {
      size_t chunk = size - done;
      size_t max_chunk = libretro_content_read_chunk();
      uint64_t now_us;

      if (chunk > max_chunk)
         chunk = max_chunk;
      if (fread(data + done, 1, chunk, file) != chunk) {
         printf("unifrog libretro rom read failed path=%s size=%u done=%u label=%s\n",
            path ? path : "", (unsigned)size, (unsigned)done,
            label ? label : "");
         unifrog_log_sync("content_stdio_read fail path=%s size=%u done=%u label=%s",
            path ? path : "", (unsigned)size, (unsigned)done,
            label ? label : "");
         rom_free_aligned(data);
         return -1;
      }
      done += chunk;
      now_us = host_time_us();
      if (timeout_ms &&
          host_elapsed_ms(read_start_us, now_us) > timeout_ms) {
         printf("unifrog libretro rom read timeout path=%s size=%u done=%u elapsed=%u timeout=%u label=%s\n",
            path ? path : "", (unsigned)size, (unsigned)done,
            host_elapsed_ms(read_start_us, now_us), timeout_ms,
            label ? label : "");
         unifrog_log_sync("content_stdio_read timeout path=%s size=%u done=%u elapsed=%u timeout=%u label=%s",
            path ? path : "", (unsigned)size, (unsigned)done,
            host_elapsed_ms(read_start_us, now_us), timeout_ms,
            label ? label : "");
         unifrog_platform_sd_debug_dump("content_stdio_read_timeout");
         rom_free_aligned(data);
         errno = ETIMEDOUT;
         return -1;
      }
      loading_draw("LOADING ROM", label ? label : "READING",
         12u + (unsigned)((done * 58u) / (size ? size : 1u)));
   }
   *out_data = data;
   *out_size = size;
   unifrog_log_sync("content_stdio_read done path=%s size=%u elapsed=%u label=%s",
      path ? path : "", (unsigned)size,
      host_elapsed_ms(read_start_us, host_time_us()), label ? label : "");
   return 0;
}

static int read_path_heap_sequential(const char *path, uint8_t **out_data,
   size_t *out_size, size_t max_size, const char *label)
{
   uint8_t *data = NULL;
   struct stat st;
   size_t size;
   uint64_t start_us;
   uint64_t end_us;
   unsigned attempts = libretro_storage_attempts();

   if (!path || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;
   for (unsigned attempt = 0; attempt < attempts; attempt++) {
      int fd = -1;
      int read_errno = 0;

      if (stat(path, &st) != 0 || st.st_size <= 0) {
         printf("unifrog libretro compressed stat failed path=%s errno=%d attempt=%u label=%s\n",
            path, errno, attempt + 1u, label ? label : "");
         if (attempt + 1u < attempts)
            (void)libretro_recover_storage("compressed_stat");
         continue;
      }
      size = (size_t)st.st_size;
      if (size > max_size) {
         printf("unifrog libretro compressed input too large path=%s size=%u max=%u label=%s\n",
            path, (unsigned)size, (unsigned)max_size, label ? label : "");
         return -1;
      }
      data = malloc(size);
      if (!data) {
         printf("unifrog libretro compressed input alloc failed path=%s size=%u label=%s\n",
            path, (unsigned)size, label ? label : "");
         return -1;
      }
      fd = open(path, O_RDONLY);
      if (fd < 0) {
         printf("unifrog libretro compressed open failed path=%s errno=%d attempt=%u label=%s\n",
            path, errno, attempt + 1u, label ? label : "");
         free(data);
         data = NULL;
         if (attempt + 1u < attempts)
            (void)libretro_recover_storage("compressed_open");
         continue;
      }
      start_us = host_time_us();
      errno = 0;
      if (read_fd_fully_to_buffer(fd, path, data, size,
          label ? label : "LOADING", "READING", 10, 20) == 0) {
         end_us = host_time_us();
         close(fd);
         *out_data = data;
         *out_size = size;
         printf("unifrog load_time stage=file_read mode=fd_heap ms=%u bytes=%u chunk=%u label=%s attempts=%u path=%s\n",
            host_elapsed_ms(start_us, end_us), (unsigned)size,
            (unsigned)libretro_content_read_chunk(), label ? label : "",
            attempt + 1u, path);
         return 0;
      }
      read_errno = errno;
      close(fd);
      free(data);
      data = NULL;
      if (read_errno == ETIMEDOUT) {
         errno = ETIMEDOUT;
         return -1;
      }
      if (attempt + 1u < attempts)
         (void)libretro_recover_storage("compressed_read");
   }
   return -1;
}

#ifndef UNIFROG_LIBRETRO_NO_COMPRESSED
static int decompress_zstd_memory(const char *path, const uint8_t *compressed,
   size_t compressed_size, uint8_t **out_data, size_t *out_size)
{
   unsigned long long frame_size;
   uint8_t *rom = NULL;
   size_t ret;

   frame_size = ZSTD_getFrameContentSize(compressed, compressed_size);
   if (frame_size == ZSTD_CONTENTSIZE_ERROR ||
       frame_size == ZSTD_CONTENTSIZE_UNKNOWN ||
       frame_size == 0 ||
       frame_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED) {
      printf("unifrog libretro zstd memory size unsupported path=%s compressed=%u frame=%llu\n",
         path, (unsigned)compressed_size, frame_size);
      return -1;
   }
   rom = rom_alloc_aligned((size_t)frame_size);
   if (!rom) {
      printf("unifrog libretro zstd memory alloc failed path=%s compressed=%u uncompressed=%u\n",
         path, (unsigned)compressed_size, (unsigned)frame_size);
      return -1;
   }
   loading_draw("LOADING ZSTD", "DECOMPRESS", 36);
   ret = ZSTD_decompress(rom, (size_t)frame_size, compressed, compressed_size);
   if (ZSTD_isError(ret) || ret != (size_t)frame_size) {
      printf("unifrog libretro zstd memory decode failed path=%s err=%s out=%u expected=%u\n",
         path, ZSTD_isError(ret) ? ZSTD_getErrorName(ret) : "short_output",
         (unsigned)ret, (unsigned)frame_size);
      rom_free_aligned(rom);
      return -1;
   }
   *out_data = rom;
   *out_size = (size_t)frame_size;
   return 0;
}

static int reserve_heap_output(uint8_t **data, size_t *capacity,
   size_t required, size_t max_size, const char *path, const char *type)
{
   size_t new_capacity;
   uint8_t *new_data;

   if (!data || !capacity || required > max_size)
      return -1;
   if (*capacity >= required)
      return 0;

   new_capacity = *capacity ? *capacity : LIBRETRO_COMPRESSED_GROW_INITIAL;
   if (new_capacity > max_size)
      new_capacity = max_size;
   while (new_capacity < required) {
      if (new_capacity > max_size / 2u) {
         new_capacity = max_size;
         break;
      }
      new_capacity *= 2u;
   }
   if (new_capacity < required)
      return -1;

   new_data = realloc(*data, new_capacity);
   if (!new_data) {
      printf("unifrog libretro %s memory grow failed path=%s required=%u capacity=%u max=%u\n",
         type ? type : "compressed", path ? path : "",
         (unsigned)required, (unsigned)new_capacity, (unsigned)max_size);
      return -1;
   }
   *data = new_data;
   *capacity = new_capacity;
   return 0;
}

static int align_heap_output(const char *path, const char *type,
   uint8_t *heap_data, size_t heap_size, uint8_t **out_data, size_t *out_size)
{
   uint8_t *rom;

   if (!heap_data || heap_size == 0 ||
       heap_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED)
      return -1;
   rom = rom_alloc_aligned(heap_size);
   if (!rom) {
      printf("unifrog libretro %s memory final alloc failed path=%s size=%u\n",
         type ? type : "compressed", path ? path : "", (unsigned)heap_size);
      return -1;
   }
   memcpy(rom, heap_data, heap_size);
   *out_data = rom;
   *out_size = heap_size;
   return 0;
}

static int decompress_zstd_memory_stream(const char *path,
   const uint8_t *compressed, size_t compressed_size, uint8_t **out_data,
   size_t *out_size)
{
   ZSTD_DStream *stream = NULL;
   ZSTD_inBuffer input;
   uint8_t *heap_data = NULL;
   size_t heap_capacity = 0;
   int done = 0;
   int out_ret = -1;

   stream = ZSTD_createDStream();
   if (!stream)
      return -1;
   if (ZSTD_isError(ZSTD_initDStream(stream)))
      goto out;

   input.src = compressed;
   input.size = compressed_size;
   input.pos = 0;
   loading_draw("LOADING ZSTD", "DECOMPRESS", 36);
   while (!done && input.pos < input.size) {
      ZSTD_outBuffer output;
      size_t ret;
      size_t before_in;
      size_t before_out;

      if (reserve_heap_output(&heap_data, &heap_capacity,
          *out_size + LIBRETRO_CONTENT_STREAM_OUT,
          LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED, path, "zstd") != 0)
         goto out;

      output.dst = heap_data + *out_size;
      output.size = heap_capacity - *out_size;
      output.pos = 0;
      before_in = input.pos;
      before_out = output.pos;
      ret = ZSTD_decompressStream(stream, &output, &input);
      if (ZSTD_isError(ret)) {
         printf("unifrog libretro zstd memory decode failed path=%s err=%s\n",
            path, ZSTD_getErrorName(ret));
         goto out;
      }
      *out_size += output.pos;
      if (ret == 0)
         done = 1;
      if (input.pos == before_in && output.pos == before_out && !done)
         goto out;
      loading_draw("LOADING ZSTD", "DECOMPRESS",
         36u + (unsigned)((input.pos * 30u) /
         (compressed_size ? compressed_size : 1u)));
   }

   if (!done || align_heap_output(path, "zstd", heap_data, *out_size,
       out_data, out_size) != 0)
      goto out;
   out_ret = 0;

out:
   free(heap_data);
   if (stream)
      ZSTD_freeDStream(stream);
   return out_ret;
}

static int zstd_file_frame_size(const char *path, size_t compressed_size,
   size_t *out_size)
{
   FILE *file = NULL;
   uint8_t *header = NULL;
   size_t got;
   size_t want = compressed_size;
   unsigned long long frame_size;
   int ret = -1;

   if (!path || !out_size || compressed_size == 0)
      return -1;
   *out_size = 0;
   if (want > LIBRETRO_CONTENT_STREAM_IN)
      want = LIBRETRO_CONTENT_STREAM_IN;
   header = malloc(want);
   if (!header)
      return -1;
   file = fopen(path, "rb");
   if (!file)
      goto out;
   got = fread(header, 1, want, file);
   if (got == 0)
      goto out;

   frame_size = ZSTD_getFrameContentSize(header, got);
   if (frame_size == ZSTD_CONTENTSIZE_ERROR ||
       frame_size == ZSTD_CONTENTSIZE_UNKNOWN ||
       frame_size == 0 ||
       frame_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED)
      goto out;
   *out_size = (size_t)frame_size;
   ret = 0;

out:
   if (file)
      fclose(file);
   free(header);
   return ret;
}

static int zstd_stream_count_output(const char *path, size_t compressed_size,
   size_t *out_size)
{
   FILE *file = NULL;
   ZSTD_DStream *stream = NULL;
   uint8_t *in_buf = NULL;
   uint8_t *scratch = NULL;
   size_t in_cap = ZSTD_DStreamInSize();
   size_t out_cap = ZSTD_DStreamOutSize();
   size_t read_total = 0;
   int done = 0;
   int out_ret = -1;

   if (!path || !out_size)
      return -1;
   *out_size = 0;
   if (in_cap == 0 || in_cap > LIBRETRO_CONTENT_STREAM_IN)
      in_cap = LIBRETRO_CONTENT_STREAM_IN;
   if (out_cap == 0 || out_cap > LIBRETRO_CONTENT_STREAM_OUT)
      out_cap = LIBRETRO_CONTENT_STREAM_OUT;
   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro zstd stream open failed path=%s mode=count\n",
         path);
      return -1;
   }
   stream = ZSTD_createDStream();
   if (!stream || ZSTD_isError(ZSTD_initDStream(stream)))
      goto out;
   in_buf = malloc(in_cap);
   scratch = malloc(out_cap);
   if (!in_buf || !scratch)
      goto out;

   loading_draw("LOADING ZSTD", "MEASURE", 34);
   while (!done) {
      size_t got = fread(in_buf, 1, in_cap, file);
      ZSTD_inBuffer input;

      if (got == 0) {
         if (ferror(file))
            goto out;
         break;
      }
      read_total += got;
      input.src = in_buf;
      input.size = got;
      input.pos = 0;
      while (input.pos < input.size) {
         ZSTD_outBuffer output;
         size_t before_in;
         size_t ret;

         output.dst = scratch;
         output.size = out_cap;
         output.pos = 0;
         before_in = input.pos;
         ret = ZSTD_decompressStream(stream, &output, &input);
         if (ZSTD_isError(ret)) {
            printf("unifrog libretro zstd stream count failed path=%s err=%s\n",
               path, ZSTD_getErrorName(ret));
            goto out;
         }
         if (output.pos > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED - *out_size) {
            printf("unifrog libretro zstd stream count too large path=%s out=%u max=%u\n",
               path, (unsigned)*out_size,
               (unsigned)LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED);
            goto out;
         }
         *out_size += output.pos;
         if (ret == 0) {
            done = 1;
            break;
         }
         if (input.pos == before_in && output.pos == 0)
            goto out;
      }
      loading_draw("LOADING ZSTD", "MEASURE",
         34u + (unsigned)((read_total * 18u) /
         (compressed_size ? compressed_size : 1u)));
   }

   out_ret = done && *out_size > 0 ? 0 : -1;

out:
   free(scratch);
   free(in_buf);
   if (stream)
      ZSTD_freeDStream(stream);
   if (file)
      fclose(file);
   return out_ret;
}

static int zstd_stream_to_buffer(const char *path, size_t compressed_size,
   uint8_t *out_data, size_t out_size)
{
   FILE *file = NULL;
   ZSTD_DStream *stream = NULL;
   uint8_t *in_buf = NULL;
   size_t in_cap = ZSTD_DStreamInSize();
   size_t read_total = 0;
   size_t dst_pos = 0;
   int done = 0;
   int out_ret = -1;

   if (!path || !out_data || out_size == 0)
      return -1;
   if (in_cap == 0 || in_cap > LIBRETRO_CONTENT_STREAM_IN)
      in_cap = LIBRETRO_CONTENT_STREAM_IN;
   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro zstd stream open failed path=%s mode=decode\n",
         path);
      return -1;
   }
   stream = ZSTD_createDStream();
   if (!stream || ZSTD_isError(ZSTD_initDStream(stream)))
      goto out;
   in_buf = malloc(in_cap);
   if (!in_buf)
      goto out;

   loading_draw("LOADING ZSTD", "DECOMPRESS", 52);
   while (!done) {
      size_t got = fread(in_buf, 1, in_cap, file);
      ZSTD_inBuffer input;

      if (got == 0) {
         if (ferror(file))
            goto out;
         break;
      }
      read_total += got;
      input.src = in_buf;
      input.size = got;
      input.pos = 0;
      while (input.pos < input.size) {
         ZSTD_outBuffer output;
         size_t before_in;
         size_t ret;

         if (dst_pos >= out_size)
            goto out;
         output.dst = out_data + dst_pos;
         output.size = out_size - dst_pos;
         output.pos = 0;
         before_in = input.pos;
         ret = ZSTD_decompressStream(stream, &output, &input);
         if (ZSTD_isError(ret)) {
            printf("unifrog libretro zstd stream decode failed path=%s err=%s\n",
               path, ZSTD_getErrorName(ret));
            goto out;
         }
         dst_pos += output.pos;
         if (ret == 0) {
            done = 1;
            break;
         }
         if (input.pos == before_in && output.pos == 0)
            goto out;
      }
      loading_draw("LOADING ZSTD", "DECOMPRESS",
         52u + (unsigned)((read_total * 16u) /
         (compressed_size ? compressed_size : 1u)));
   }

   if (!done || dst_pos != out_size) {
      printf("unifrog libretro zstd stream incomplete path=%s out=%u expected=%u\n",
         path, (unsigned)dst_pos, (unsigned)out_size);
      goto out;
   }
   out_ret = 0;

out:
   free(in_buf);
   if (stream)
      ZSTD_freeDStream(stream);
   if (file)
      fclose(file);
   return out_ret;
}

static int load_zstd_rom_data_stream(const char *path, uint64_t start_us,
   uint8_t **out_data, size_t *out_size)
{
   struct stat st;
   uint8_t *rom = NULL;
   uint64_t count_done_us;
   uint64_t alloc_done_us;
   uint64_t decode_done_us;
   size_t compressed_size;
   size_t expected_size = 0;
   int counted = 0;
   int out_ret = -1;

   if (!path || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;
   if (stat(path, &st) != 0 || st.st_size <= 0)
      return -1;
   compressed_size = (size_t)st.st_size;
   if (compressed_size > LIBRETRO_COMPRESSED_MAX_INPUT) {
      printf("unifrog libretro zstd stream input too large path=%s size=%u max=%u\n",
         path, (unsigned)compressed_size,
         (unsigned)LIBRETRO_COMPRESSED_MAX_INPUT);
      return -1;
   }

   if (zstd_file_frame_size(path, compressed_size, &expected_size) != 0) {
      if (zstd_stream_count_output(path, compressed_size, &expected_size) != 0)
         return -1;
      counted = 1;
   }
   count_done_us = host_time_us();
   printf("unifrog libretro zstd stream %s path=%s compressed=%u uncompressed=%u\n",
      counted ? "counted" : "sized", path, (unsigned)compressed_size,
      (unsigned)expected_size);

   rom = rom_alloc_aligned(expected_size);
   alloc_done_us = host_time_us();
   if (!rom) {
      printf("unifrog libretro zstd stream alloc failed path=%s compressed=%u uncompressed=%u\n",
         path, (unsigned)compressed_size, (unsigned)expected_size);
      goto out;
   }

   if (zstd_stream_to_buffer(path, compressed_size, rom, expected_size) != 0)
      goto out;
   decode_done_us = host_time_us();

   *out_data = rom;
   *out_size = expected_size;
   rom = NULL;
   printf("unifrog libretro compressed memory loaded path=%s type=zstd compressed=%u uncompressed=%u aligned=%lu mode=stream_ram\n",
      path, (unsigned)compressed_size, (unsigned)*out_size,
      (unsigned long)((uintptr_t)*out_data & 31u));
   printf("unifrog load_time stage=compressed_memory type=zstd count_ms=%u alloc_ms=%u decode_ms=%u total_ms=%u compressed=%u uncompressed=%u mode=stream_ram size_source=%s\n",
      host_elapsed_ms(start_us, count_done_us),
      host_elapsed_ms(count_done_us, alloc_done_us),
      host_elapsed_ms(alloc_done_us, decode_done_us),
      host_elapsed_ms(start_us, decode_done_us),
      (unsigned)compressed_size, (unsigned)*out_size,
      counted ? "scan" : "frame");
   loading_draw("LOADING ROM", "READY", 68);
   out_ret = 0;

out:
   rom_free_aligned(rom);
   return out_ret;
}

static int decompress_lz4_count_output(const char *path,
   const uint8_t *compressed, size_t compressed_size, LZ4F_dctx *ctx,
   size_t *out_size)
{
   uint8_t *scratch = NULL;
   size_t src_pos = 0;
   int done = 0;
   int out_ret = -1;

   if (!compressed || !ctx || !out_size)
      return -1;
   *out_size = 0;
   scratch = malloc(LIBRETRO_CONTENT_STREAM_OUT);
   if (!scratch)
      return -1;

   loading_draw("LOADING LZ4", "MEASURE", 34);
   while (!done && src_pos < compressed_size) {
      size_t src_size = compressed_size - src_pos;
      size_t dst_size = LIBRETRO_CONTENT_STREAM_OUT;
      size_t before_src = src_pos;
      size_t ret;

      ret = LZ4F_decompress(ctx, scratch, &dst_size,
         compressed + src_pos, &src_size, NULL);
      if (LZ4F_isError(ret)) {
         printf("unifrog libretro lz4 memory count failed path=%s err=%s\n",
            path, LZ4F_getErrorName(ret));
         goto out;
      }
      if (dst_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED - *out_size) {
         printf("unifrog libretro lz4 memory count too large path=%s out=%u max=%u\n",
            path ? path : "", (unsigned)*out_size,
            (unsigned)LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED);
         goto out;
      }
      src_pos += src_size;
      *out_size += dst_size;
      if (ret == 0)
         done = 1;
      if (src_pos == before_src && dst_size == 0 && !done)
         goto out;
      loading_draw("LOADING LZ4", "MEASURE",
         34u + (unsigned)((src_pos * 18u) /
         (compressed_size ? compressed_size : 1u)));
   }

   out_ret = done && *out_size > 0 ? 0 : -1;

out:
   free(scratch);
   return out_ret;
}

static int decompress_lz4_to_buffer(const char *path,
   const uint8_t *compressed, size_t compressed_size, LZ4F_dctx *ctx,
   uint8_t *out_data, size_t out_size)
{
   uint8_t overflow[64];
   size_t src_pos = 0;
   size_t dst_pos = 0;
   int done = 0;
   int out_ret = -1;

   if (!compressed || !ctx || !out_data || out_size == 0)
      return -1;

   loading_draw("LOADING LZ4", "DECOMPRESS", 52);
   while (!done && src_pos < compressed_size) {
      LZ4F_decompressOptions_t opts;
      size_t src_size = compressed_size - src_pos;
      size_t dst_size;
      size_t before_src = src_pos;
      size_t ret;
      uint8_t *dst;

      if (dst_pos >= out_size) {
         dst = overflow;
         dst_size = sizeof(overflow);
      } else {
         dst = out_data + dst_pos;
         dst_size = out_size - dst_pos;
      }
      memset(&opts, 0, sizeof(opts));
      opts.stableDst = 1;
      ret = LZ4F_decompress(ctx, dst, &dst_size,
         compressed + src_pos, &src_size, &opts);
      if (LZ4F_isError(ret)) {
         printf("unifrog libretro lz4 memory decode failed path=%s err=%s\n",
            path, LZ4F_getErrorName(ret));
         goto out;
      }
      src_pos += src_size;
      if (dst_pos >= out_size) {
         if (dst_size != 0) {
            printf("unifrog libretro lz4 memory output exceeded path=%s out=%u extra=%u expected=%u\n",
               path, (unsigned)dst_pos, (unsigned)dst_size,
               (unsigned)out_size);
            goto out;
         }
      } else {
         dst_pos += dst_size;
      }
      if (ret == 0)
         done = 1;
      if (src_pos == before_src && dst_size == 0 && !done)
         goto out;
      loading_draw("LOADING LZ4", "DECOMPRESS",
         52u + (unsigned)((src_pos * 16u) /
         (compressed_size ? compressed_size : 1u)));
   }

   if (!done || dst_pos != out_size) {
      printf("unifrog libretro lz4 memory incomplete path=%s out=%u expected=%u\n",
         path, (unsigned)dst_pos, (unsigned)out_size);
      goto out;
   }
   out_ret = 0;

out:
   return out_ret;
}

static int decompress_lz4_memory(const char *path, const uint8_t *compressed,
   size_t compressed_size, uint64_t start_us, uint64_t read_done_us,
   uint8_t **out_data, size_t *out_size)
{
   LZ4F_dctx *ctx = NULL;
   uint8_t *rom = NULL;
   uint64_t count_done_us;
   uint64_t decode_done_us;
   size_t expected_size = 0;
   int out_ret = -1;

   if (!compressed || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;

   if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION)))
      goto out;
   if (decompress_lz4_count_output(path, compressed, compressed_size, ctx,
       &expected_size) != 0)
      goto out;
   count_done_us = host_time_us();
   printf("unifrog libretro lz4 memory counted path=%s compressed=%u uncompressed=%u\n",
      path, (unsigned)compressed_size, (unsigned)expected_size);

   rom = rom_alloc_aligned(expected_size);
   if (!rom) {
      printf("unifrog libretro lz4 memory alloc failed path=%s compressed=%u uncompressed=%u\n",
         path, (unsigned)compressed_size, (unsigned)expected_size);
      goto out;
   }

   LZ4F_resetDecompressionContext(ctx);
   if (decompress_lz4_to_buffer(path, compressed, compressed_size, ctx,
       rom, expected_size) != 0)
      goto out;
   decode_done_us = host_time_us();

   *out_data = rom;
   *out_size = expected_size;
   rom = NULL;
   printf("unifrog libretro compressed memory loaded path=%s type=lz4 compressed=%u uncompressed=%u aligned=%lu mode=ram\n",
      path, (unsigned)compressed_size, (unsigned)*out_size,
      (unsigned long)((uintptr_t)*out_data & 31u));
   printf("unifrog load_time stage=compressed_memory type=lz4 read_ms=%u count_ms=%u decode_ms=%u total_ms=%u compressed=%u uncompressed=%u mode=ram\n",
      host_elapsed_ms(start_us, read_done_us),
      host_elapsed_ms(read_done_us, count_done_us),
      host_elapsed_ms(count_done_us, decode_done_us),
      host_elapsed_ms(start_us, decode_done_us),
      (unsigned)compressed_size, (unsigned)*out_size);
   loading_draw("LOADING ROM", "READY", 68);
   out_ret = 0;

out:
   rom_free_aligned(rom);
   if (ctx)
      LZ4F_freeDecompressionContext(ctx);
   return out_ret;
}

static int lz4_stream_count_output(const char *path, size_t compressed_size,
   size_t *out_size)
{
   FILE *file = NULL;
   LZ4F_dctx *ctx = NULL;
   uint8_t *in_buf = NULL;
   uint8_t *scratch = NULL;
   size_t read_total = 0;
   int done = 0;
   int out_ret = -1;

   if (!path || !out_size)
      return -1;
   *out_size = 0;
   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro lz4 stream open failed path=%s mode=count\n",
         path);
      return -1;
   }
   if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION)))
      goto out;
   in_buf = malloc(LIBRETRO_CONTENT_STREAM_IN);
   scratch = malloc(LIBRETRO_CONTENT_STREAM_OUT);
   if (!in_buf || !scratch)
      goto out;

   loading_draw("LOADING LZ4", "MEASURE", 34);
   while (!done) {
      size_t in_size = fread(in_buf, 1, LIBRETRO_CONTENT_STREAM_IN, file);
      size_t in_pos = 0;

      if (in_size == 0) {
         if (ferror(file))
            goto out;
         break;
      }
      read_total += in_size;
      while (in_pos < in_size) {
         size_t src_size = in_size - in_pos;
         size_t dst_size = LIBRETRO_CONTENT_STREAM_OUT;
         size_t before_in = in_pos;
         size_t ret = LZ4F_decompress(ctx, scratch, &dst_size,
            in_buf + in_pos, &src_size, NULL);

         if (LZ4F_isError(ret)) {
            printf("unifrog libretro lz4 stream count failed path=%s err=%s\n",
               path, LZ4F_getErrorName(ret));
            goto out;
         }
         if (dst_size > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED - *out_size) {
            printf("unifrog libretro lz4 stream count too large path=%s out=%u max=%u\n",
               path, (unsigned)*out_size,
               (unsigned)LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED);
            goto out;
         }
         in_pos += src_size;
         *out_size += dst_size;
         if (ret == 0) {
            done = 1;
            break;
         }
         if (in_pos == before_in && dst_size == 0)
            goto out;
      }
      loading_draw("LOADING LZ4", "MEASURE",
         34u + (unsigned)((read_total * 18u) /
         (compressed_size ? compressed_size : 1u)));
   }

   out_ret = done && *out_size > 0 ? 0 : -1;

out:
   free(scratch);
   free(in_buf);
   if (ctx)
      LZ4F_freeDecompressionContext(ctx);
   if (file)
      fclose(file);
   return out_ret;
}

static int lz4_file_frame_size(const char *path, size_t compressed_size,
   size_t *out_size)
{
   FILE *file = NULL;
   LZ4F_dctx *ctx = NULL;
   LZ4F_frameInfo_t info;
   uint8_t header[LZ4F_HEADER_SIZE_MAX];
   size_t got;
   size_t src_size;
   size_t ret;
   int out_ret = -1;

   if (!path || !out_size || compressed_size == 0)
      return -1;
   *out_size = 0;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   got = fread(header, 1, sizeof(header), file);
   if (got < LZ4F_HEADER_SIZE_MIN)
      goto out;
   if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION)))
      goto out;

   memset(&info, 0, sizeof(info));
   src_size = got;
   ret = LZ4F_getFrameInfo(ctx, &info, header, &src_size);
   if (LZ4F_isError(ret) || info.contentSize == 0 ||
       info.contentSize > LIBRETRO_COMPRESSED_MAX_UNCOMPRESSED)
      goto out;
   *out_size = (size_t)info.contentSize;
   out_ret = 0;

out:
   if (ctx)
      LZ4F_freeDecompressionContext(ctx);
   if (file)
      fclose(file);
   return out_ret;
}

static int lz4_stream_to_buffer(const char *path, size_t compressed_size,
   uint8_t *out_data, size_t out_size, LZ4F_dctx *ctx, uint8_t *in_buf,
   size_t in_cap)
{
   FILE *file = NULL;
   uint8_t overflow[64];
   size_t read_total = 0;
   size_t dst_pos = 0;
   int done = 0;
   int out_ret = -1;

   if (!path || !out_data || out_size == 0 || !ctx || !in_buf ||
       in_cap == 0)
      return -1;
   file = fopen(path, "rb");
   if (!file) {
      printf("unifrog libretro lz4 stream open failed path=%s mode=decode\n",
         path);
      return -1;
   }
   LZ4F_resetDecompressionContext(ctx);

   loading_draw("LOADING LZ4", "DECOMPRESS", 52);
   while (!done) {
      size_t in_size = fread(in_buf, 1, in_cap, file);
      size_t in_pos = 0;

      if (in_size == 0) {
         if (ferror(file)) {
            printf("unifrog libretro lz4 stream read failed path=%s read=%u\n",
               path, (unsigned)read_total);
            goto out;
         }
         break;
      }
      read_total += in_size;
      while (in_pos < in_size) {
         LZ4F_decompressOptions_t opts;
         size_t src_size = in_size - in_pos;
         size_t dst_size;
         size_t before_in = in_pos;
         size_t ret;
         uint8_t *dst;

         if (dst_pos >= out_size) {
            dst = overflow;
            dst_size = sizeof(overflow);
         } else {
            dst = out_data + dst_pos;
            dst_size = out_size - dst_pos;
         }
         memset(&opts, 0, sizeof(opts));
         opts.stableDst = 1;
         ret = LZ4F_decompress(ctx, dst, &dst_size,
            in_buf + in_pos, &src_size, &opts);
         if (LZ4F_isError(ret)) {
            printf("unifrog libretro lz4 stream decode failed path=%s err=%s\n",
               path, LZ4F_getErrorName(ret));
            goto out;
         }
         in_pos += src_size;
         if (dst_pos >= out_size) {
            if (dst_size != 0) {
               printf("unifrog libretro lz4 stream output exceeded path=%s out=%u extra=%u expected=%u\n",
                  path, (unsigned)dst_pos, (unsigned)dst_size,
                  (unsigned)out_size);
               goto out;
            }
         } else {
            dst_pos += dst_size;
         }
         if (ret == 0) {
            done = 1;
            break;
         }
         if (in_pos == before_in && dst_size == 0) {
            printf("unifrog libretro lz4 stream no progress path=%s read=%u in_pos=%u in_size=%u out=%u need=%u\n",
               path, (unsigned)read_total, (unsigned)in_pos,
               (unsigned)in_size, (unsigned)dst_pos, (unsigned)ret);
            goto out;
         }
      }
      loading_draw("LOADING LZ4", "DECOMPRESS",
         52u + (unsigned)((read_total * 16u) /
         (compressed_size ? compressed_size : 1u)));
   }

   if (!done || dst_pos != out_size) {
      printf("unifrog libretro lz4 stream incomplete path=%s out=%u expected=%u\n",
         path, (unsigned)dst_pos, (unsigned)out_size);
      goto out;
   }
   out_ret = 0;

out:
   if (file)
      fclose(file);
   return out_ret;
}

static int load_lz4_rom_data_stream(const char *path, uint64_t start_us,
   uint8_t **out_data, size_t *out_size)
{
   struct stat st;
   uint8_t *rom = NULL;
   uint8_t *decode_in = NULL;
   LZ4F_dctx *decode_ctx = NULL;
   uint64_t count_done_us;
   uint64_t alloc_done_us;
   uint64_t decode_done_us;
   size_t compressed_size;
   size_t expected_size = 0;
   int counted = 0;
   int out_ret = -1;

   if (!path || !out_data || !out_size)
      return -1;
   *out_data = NULL;
   *out_size = 0;
   if (stat(path, &st) != 0 || st.st_size <= 0)
      return -1;
   compressed_size = (size_t)st.st_size;
   if (compressed_size > LIBRETRO_COMPRESSED_MAX_INPUT) {
      printf("unifrog libretro lz4 stream input too large path=%s size=%u max=%u\n",
         path, (unsigned)compressed_size,
         (unsigned)LIBRETRO_COMPRESSED_MAX_INPUT);
      return -1;
   }

   if (lz4_file_frame_size(path, compressed_size, &expected_size) != 0) {
      if (lz4_stream_count_output(path, compressed_size, &expected_size) != 0)
         return -1;
      counted = 1;
   }
   count_done_us = host_time_us();
   printf("unifrog libretro lz4 stream %s path=%s compressed=%u uncompressed=%u\n",
      counted ? "counted" : "sized", path, (unsigned)compressed_size,
      (unsigned)expected_size);

   decode_in = malloc(LIBRETRO_CONTENT_STREAM_IN);
   if (!decode_in) {
      printf("unifrog libretro lz4 stream work alloc failed path=%s bytes=%u\n",
         path, (unsigned)LIBRETRO_CONTENT_STREAM_IN);
      goto out;
   }
   if (LZ4F_isError(LZ4F_createDecompressionContext(&decode_ctx,
       LZ4F_VERSION))) {
      printf("unifrog libretro lz4 stream context alloc failed path=%s\n",
         path);
      goto out;
   }

   rom = rom_alloc_aligned(expected_size);
   alloc_done_us = host_time_us();
   if (!rom) {
      printf("unifrog libretro lz4 stream alloc failed path=%s compressed=%u uncompressed=%u\n",
         path, (unsigned)compressed_size, (unsigned)expected_size);
      goto out;
   }

   if (lz4_stream_to_buffer(path, compressed_size, rom, expected_size,
       decode_ctx, decode_in, LIBRETRO_CONTENT_STREAM_IN) != 0)
      goto out;
   decode_done_us = host_time_us();

   *out_data = rom;
   *out_size = expected_size;
   rom = NULL;
   printf("unifrog libretro compressed memory loaded path=%s type=lz4 compressed=%u uncompressed=%u aligned=%lu mode=stream_ram\n",
      path, (unsigned)compressed_size, (unsigned)*out_size,
      (unsigned long)((uintptr_t)*out_data & 31u));
   printf("unifrog load_time stage=compressed_memory type=lz4 read_ms=%u count_ms=%u alloc_ms=%u decode_ms=%u total_ms=%u compressed=%u uncompressed=%u mode=stream_ram size_source=%s\n",
      0u,
      host_elapsed_ms(start_us, count_done_us),
      host_elapsed_ms(count_done_us, alloc_done_us),
      host_elapsed_ms(alloc_done_us, decode_done_us),
      host_elapsed_ms(start_us, decode_done_us),
      (unsigned)compressed_size, (unsigned)*out_size,
      counted ? "scan" : "frame");
   loading_draw("LOADING ROM", "READY", 68);
   out_ret = 0;

out:
   rom_free_aligned(rom);
   if (decode_ctx)
      LZ4F_freeDecompressionContext(decode_ctx);
   free(decode_in);
   return out_ret;
}

int load_wrapped_compressed_rom_data(const char *path,
   uint8_t **out_data, size_t *out_size)
{
   uint8_t *compressed = NULL;
   size_t compressed_size = 0;
   uint64_t start_us;
   uint64_t read_done_us;
   uint64_t decode_done_us;
   int ret;

   if (!path || !out_data || !out_size)
      return -1;
   *out_size = 0;
   if (path_is_lz4(path)) {
      struct stat st;

      start_us = host_time_us();
      if (stat(path, &st) == 0 &&
          st.st_size > (long)LIBRETRO_COMPRESSED_STREAM_FIRST_INPUT) {
         ret = load_lz4_rom_data_stream(path, start_us, out_data, out_size);
         if (ret == 0)
            return 0;
         printf("unifrog libretro lz4 stream_ram failed; trying heap path=%s\n",
            path);
      }
      ret = read_path_heap_sequential(path, &compressed, &compressed_size,
         LIBRETRO_COMPRESSED_MAX_INPUT, "LOADING LZ4");
      if (ret == 0) {
         read_done_us = host_time_us();
         ret = decompress_lz4_memory(path, compressed, compressed_size,
            start_us, read_done_us, out_data, out_size);
         free(compressed);
         if (ret == 0)
            return 0;
         printf("unifrog libretro lz4 heap path failed; trying stream_ram path=%s\n",
            path);
      } else {
         printf("unifrog libretro lz4 heap read failed; trying stream_ram path=%s\n",
            path);
      }
      return load_lz4_rom_data_stream(path, start_us, out_data, out_size);
   }

   start_us = host_time_us();
   if (path_is_zstd(path)) {
      struct stat st;

      if (stat(path, &st) == 0 &&
          st.st_size > (long)LIBRETRO_COMPRESSED_STREAM_FIRST_INPUT) {
         ret = load_zstd_rom_data_stream(path, start_us, out_data, out_size);
         if (ret == 0)
            return 0;
         printf("unifrog libretro zstd stream_ram failed; trying heap path=%s\n",
            path);
      }
   }

   ret = read_path_heap_sequential(path, &compressed, &compressed_size,
      LIBRETRO_COMPRESSED_MAX_INPUT, "LOADING ZSTD");
   if (ret != 0)
      return load_zstd_rom_data_stream(path, start_us, out_data, out_size);
   read_done_us = host_time_us();
   ret = decompress_zstd_memory(path, compressed, compressed_size, out_data,
      out_size);
   if (ret != 0) {
      *out_size = 0;
      ret = decompress_zstd_memory_stream(path, compressed, compressed_size,
         out_data, out_size);
   }
   decode_done_us = host_time_us();
   free(compressed);
   compressed = NULL;
   if (ret != 0) {
      printf("unifrog libretro zstd heap path failed; trying stream_ram path=%s\n",
         path);
      return load_zstd_rom_data_stream(path, start_us, out_data, out_size);
   }
   if (ret == 0) {
      printf("unifrog libretro compressed memory loaded path=%s type=%s compressed=%u uncompressed=%u aligned=%lu\n",
         path, "zstd", (unsigned)compressed_size, (unsigned)*out_size,
         (unsigned long)((uintptr_t)*out_data & 31u));
      printf("unifrog load_time stage=compressed_memory type=%s read_ms=%u decode_ms=%u total_ms=%u compressed=%u uncompressed=%u\n",
         "zstd", host_elapsed_ms(start_us, read_done_us),
         host_elapsed_ms(read_done_us, decode_done_us),
         host_elapsed_ms(start_us, decode_done_us),
         (unsigned)compressed_size, (unsigned)*out_size);
      loading_draw("LOADING ROM", "READY", 68);
   }
   return ret;
}
#else
int load_wrapped_compressed_rom_data(const char *path,
   uint8_t **out_data, size_t *out_size)
{
   (void)path;
   if (out_data)
      *out_data = NULL;
   if (out_size)
      *out_size = 0;
   return -1;
}
#endif

static int write_all(FILE *file, const void *data, size_t size)
{
   const uint8_t *bytes = data;

   while (size > 0) {
      size_t wrote = fwrite(bytes, 1, size, file);

      if (wrote == 0)
         return -1;
      bytes += wrote;
      size -= wrote;
   }
   return 0;
}

#ifndef UNIFROG_LIBRETRO_NO_COMPRESSED
static int stream_lz4_to_file(FILE *in, FILE *out, const char *path)
{
   LZ4F_dctx *ctx = NULL;
   uint8_t *in_buf = NULL;
   uint8_t *out_buf = NULL;
   size_t in_size = 0;
   size_t in_pos = 0;
   size_t ret;
   int done = 0;
   int out_ret = -1;

   if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION)))
      return -1;
   in_buf = malloc(LIBRETRO_CONTENT_STREAM_IN);
   out_buf = malloc(LIBRETRO_CONTENT_STREAM_OUT);
   if (!in_buf || !out_buf)
      goto out;

   loading_draw("LOADING LZ4", "EXTRACTING", 24);
   while (!done) {
      LZ4F_decompressOptions_t opts;
      size_t src_size;
      size_t dst_size;

      if (in_pos == in_size) {
         in_size = fread(in_buf, 1, LIBRETRO_CONTENT_STREAM_IN, in);
         in_pos = 0;
         if (in_size == 0) {
            if (ferror(in))
               goto out;
            break;
         }
      }

      memset(&opts, 0, sizeof(opts));
      src_size = in_size - in_pos;
      dst_size = LIBRETRO_CONTENT_STREAM_OUT;
      ret = LZ4F_decompress(ctx, out_buf, &dst_size,
         in_buf + in_pos, &src_size, &opts);
      if (LZ4F_isError(ret)) {
         printf("unifrog libretro lz4 decode failed path=%s err=%s\n",
            path, LZ4F_getErrorName(ret));
         goto out;
      }
      in_pos += src_size;
      if (dst_size && write_all(out, out_buf, dst_size) != 0)
         goto out;
      if (ret == 0)
         done = 1;
      if (src_size == 0 && dst_size == 0 && !done)
         goto out;
   }
   out_ret = done ? 0 : -1;

out:
   free(out_buf);
   free(in_buf);
   if (ctx)
      LZ4F_freeDecompressionContext(ctx);
   return out_ret;
}

static int stream_zstd_to_file(FILE *in, FILE *out, const char *path)
{
   ZSTD_DStream *stream = NULL;
   uint8_t *in_buf = NULL;
   uint8_t *out_buf = NULL;
   size_t in_cap = ZSTD_DStreamInSize();
   size_t out_cap = ZSTD_DStreamOutSize();
   int done = 0;
   int out_ret = -1;

   if (in_cap == 0 || in_cap > LIBRETRO_CONTENT_STREAM_IN)
      in_cap = LIBRETRO_CONTENT_STREAM_IN;
   if (out_cap == 0 || out_cap > LIBRETRO_CONTENT_STREAM_OUT)
      out_cap = LIBRETRO_CONTENT_STREAM_OUT;

   stream = ZSTD_createDStream();
   if (!stream)
      return -1;
   if (ZSTD_isError(ZSTD_initDStream(stream)))
      goto out;
   in_buf = malloc(in_cap);
   out_buf = malloc(out_cap);
   if (!in_buf || !out_buf)
      goto out;

   loading_draw("LOADING ZSTD", "EXTRACTING", 24);
   while (!done) {
      size_t got = fread(in_buf, 1, in_cap, in);
      ZSTD_inBuffer input;

      if (got == 0) {
         if (ferror(in))
            goto out;
         break;
      }
      input.src = in_buf;
      input.size = got;
      input.pos = 0;
      while (input.pos < input.size) {
         ZSTD_outBuffer output;
         size_t ret;

         output.dst = out_buf;
         output.size = out_cap;
         output.pos = 0;
         ret = ZSTD_decompressStream(stream, &output, &input);
         if (ZSTD_isError(ret)) {
            printf("unifrog libretro zstd decode failed path=%s err=%s\n",
               path, ZSTD_getErrorName(ret));
            goto out;
         }
         if (output.pos && write_all(out, out_buf, output.pos) != 0)
            goto out;
         if (ret == 0) {
            done = 1;
            break;
         }
         if (output.pos == 0 && input.pos == input.size)
            break;
      }
   }
   out_ret = done ? 0 : -1;

out:
   free(out_buf);
   free(in_buf);
   if (stream)
      ZSTD_freeDStream(stream);
   return out_ret;
}

int extract_wrapped_compressed_to_cache(const char *path,
   const struct retro_system_info *info, char *cache_path,
   size_t cache_path_size)
{
   FILE *in = NULL;
   FILE *out = NULL;
   uint64_t start_us = host_time_us();
   uint64_t end_us;
   int ret = -1;

   if (!path || !info || !cache_path)
      return -1;
   if (content_cache_path(path, NULL, info->valid_extensions, cache_path,
       cache_path_size) != 0) {
      printf("unifrog libretro compressed cache path failed path=%s exts=%s\n",
         path, info->valid_extensions ? info->valid_extensions : "?");
      return -1;
   }
   in = fopen(path, "rb");
   if (!in) {
      printf("unifrog libretro compressed open failed path=%s\n", path);
      return -1;
   }
   out = fopen(cache_path, "wb");
   if (!out) {
      printf("unifrog libretro compressed cache open failed path=%s cache=%s\n",
         path, cache_path);
      goto out;
   }
   printf("unifrog libretro compressed extract path=%s cache=%s type=%s\n",
      path, cache_path, path_is_lz4(path) ? "lz4" : "zstd");
   (void)unifrog_log_flush();
   ret = path_is_lz4(path) ?
      stream_lz4_to_file(in, out, path) :
      stream_zstd_to_file(in, out, path);
   if (fflush(out) != 0)
      ret = -1;

out:
   if (out)
      fclose(out);
   if (in)
      fclose(in);
   if (ret != 0)
      unlink(cache_path);
   else {
      end_us = host_time_us();
      printf("unifrog load_time stage=compressed_cache_extract type=%s ms=%u cache=%s\n",
         path_is_lz4(path) ? "lz4" : "zstd",
         host_elapsed_ms(start_us, end_us), cache_path);
      loading_draw("LOADING ROM", "EXTRACTED", 58);
   }
   return ret;
}
#else
int extract_wrapped_compressed_to_cache(const char *path,
   const struct retro_system_info *info, char *cache_path,
   size_t cache_path_size)
{
   (void)path;
   (void)info;
   if (cache_path && cache_path_size)
      cache_path[0] = '\0';
   return -1;
}
#endif

static int zip_load_rom_data(FILE *file, const char *zip_path,
   const struct retro_system_info *info, uint8_t **out_data,
   size_t *out_size, char *out_name, size_t out_name_size)
{
   struct zip_rom_entry entry;
   uint8_t local[30];
   uint8_t *zip = NULL;
   uint8_t *compressed = NULL;
   uint8_t *rom = NULL;
   size_t zip_size;
   uint16_t local_name_len;
   uint16_t local_extra_len;
   int ret = -1;

   if (!file || !zip_path || !info || !out_data || !out_size)
      return -1;
   if (zip_load_rom_data_stream(file, zip_path, info, out_data, out_size,
       out_name, out_name_size) == 0)
      return 0;
   (void)fseek(file, 0, SEEK_SET);
   printf("unifrog libretro zip stream_ram failed; trying heap path=%s\n",
      zip_path);
   if (file_size(file, &zip_size) != 0)
      return -1;
   zip = malloc(zip_size);
   if (!zip) {
      printf("unifrog libretro zip alloc failed path=%s size=%u\n",
         zip_path, (unsigned)zip_size);
      return -1;
   }
   loading_draw("LOADING ZIP", "READING", 15);
   if (fread(zip, 1, zip_size, file) != zip_size) {
      printf("unifrog libretro zip read failed path=%s size=%u\n",
         zip_path, (unsigned)zip_size);
      goto out;
   }

   if (zip_select_rom_entry(zip, zip_size, info->valid_extensions,
       &entry) != 0) {
      printf("unifrog libretro zip no supported entry path=%s exts=%s\n",
         zip_path, info->valid_extensions ? info->valid_extensions : "?");
      goto out;
   }
   printf("unifrog libretro zip entry name=%s method=%u compressed=%u uncompressed=%u flags=0x%04x\n",
      entry.name, entry.method, entry.compressed_size,
      entry.uncompressed_size, entry.flags);
   loading_draw("LOADING ZIP", "EXTRACTING", 35);

   if ((size_t)entry.local_offset + sizeof(local) > zip_size ||
       fseek(file, (long)entry.local_offset, SEEK_SET) != 0 ||
       fread(local, 1, sizeof(local), file) != sizeof(local) ||
       read_le32(local) != 0x04034b50u) {
      printf("unifrog libretro zip local header failed path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }
   if (read_le16(local + 8) != entry.method) {
      printf("unifrog libretro zip method mismatch path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }
   local_name_len = read_le16(local + 26);
   local_extra_len = read_le16(local + 28);
   if ((size_t)entry.local_offset + sizeof(local) + local_name_len +
       local_extra_len + entry.compressed_size > zip_size ||
       fseek(file, (long)(entry.local_offset + sizeof(local) +
       local_name_len + local_extra_len), SEEK_SET) != 0) {
      printf("unifrog libretro zip data offset failed path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }

   compressed = malloc(entry.compressed_size);
   rom = rom_alloc_aligned(entry.uncompressed_size);
   if (!compressed || !rom) {
      printf("unifrog libretro zip data alloc failed path=%s entry=%s compressed=%u uncompressed=%u\n",
         zip_path, entry.name, entry.compressed_size,
         entry.uncompressed_size);
      goto out;
   }
   if (fread(compressed, 1, entry.compressed_size, file) !=
       entry.compressed_size) {
      printf("unifrog libretro zip entry read failed path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }
   if (entry.method == 0) {
      if (entry.compressed_size != entry.uncompressed_size) {
         printf("unifrog libretro zip stored size mismatch path=%s entry=%s\n",
            zip_path, entry.name);
         goto out;
      }
      memcpy(rom, compressed, entry.uncompressed_size);
   } else if (zip_inflate_raw(rom, entry.uncompressed_size, compressed,
       entry.compressed_size) != Z_OK) {
      printf("unifrog libretro zip inflate failed path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }
   if ((uint32_t)crc32(crc32(0L, Z_NULL, 0), rom,
       (uInt)entry.uncompressed_size) != entry.crc32) {
      printf("unifrog libretro zip crc mismatch path=%s entry=%s\n",
         zip_path, entry.name);
      goto out;
   }

   free(compressed);
   free(zip);
   if (out_name && out_name_size)
      unifrog_text_copy(out_name, out_name_size, entry.name);
   *out_data = rom;
   *out_size = entry.uncompressed_size;
   printf("unifrog libretro zip loaded entry=%s size=%u\n",
      entry.name, (unsigned)*out_size);
   loading_draw("LOADING ZIP", "READY", 68);
   return 0;

out:
   rom_free_aligned(rom);
   free(compressed);
   free(zip);
   return ret;
}

int zip_extract_rom_to_cache(FILE *file, const char *zip_path,
   const struct retro_system_info *info, char *cache_path,
   size_t cache_path_size)
{
   struct zip_rom_entry entry;
   char entry_name[256];
   uint8_t *rom = NULL;
   size_t rom_size = 0;
   size_t zip_size = 0;
   FILE *out = NULL;
   int ret = -1;

   if (file_size(file, &zip_size) == 0 &&
       zip_select_rom_entry_stream(file, zip_path, zip_size,
       info->valid_extensions, &entry) == 0 &&
       content_cache_path(zip_path, entry.name, info->valid_extensions,
       cache_path, cache_path_size) == 0) {
      struct stat st;

      if (stat(cache_path, &st) == 0 &&
          st.st_size == (off_t)entry.uncompressed_size) {
         printf("unifrog libretro zip cache hit entry=%s cache=%s size=%u\n",
            entry.name, cache_path, entry.uncompressed_size);
         unifrog_log_sync("zip_cache hit path=%s entry=%s cache=%s size=%u",
            zip_path, entry.name, cache_path, entry.uncompressed_size);
         loading_draw("LOADING ZIP", "CACHE HIT", 58);
         return 0;
      }
      (void)fseek(file, 0, SEEK_SET);
   }

   if (zip_load_rom_data(file, zip_path, info, &rom, &rom_size,
       entry_name, sizeof(entry_name)) != 0)
      return -1;
   if (content_cache_path(zip_path, entry_name, info->valid_extensions,
       cache_path, cache_path_size) != 0) {
      printf("unifrog libretro zip cache path failed path=%s entry=%s\n",
         zip_path, entry_name);
      goto out;
   }
   out = fopen(cache_path, "wb");
   if (!out) {
      printf("unifrog libretro zip cache open failed path=%s cache=%s\n",
         zip_path, cache_path);
      goto out;
   }
   if (write_all(out, rom, rom_size) != 0 || fflush(out) != 0) {
      printf("unifrog libretro zip cache write failed path=%s cache=%s size=%u\n",
         zip_path, cache_path, (unsigned)rom_size);
      goto out;
   }
   printf("unifrog libretro zip extracted entry=%s cache=%s size=%u\n",
      entry_name, cache_path, (unsigned)rom_size);
   loading_draw("LOADING ZIP", "EXTRACTED", 58);
   ret = 0;

out:
   if (out)
      fclose(out);
   if (ret != 0 && cache_path && cache_path[0])
      unlink(cache_path);
   rom_free_aligned(rom);
   return ret;
}
