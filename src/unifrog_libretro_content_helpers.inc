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

static void rom_free_aligned(void *ptr)
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

static void probe_rom_seek_path(const char *path)
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
      close(fd);
      rom_free_aligned(data);
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

static int path_is_zip(const char *path)
{
   return unifrog_text_ends_with_ci(path, ".zip");
}

static int path_is_wrapped_compressed(const char *path)
{
   return path_is_lz4(path) || path_is_zstd(path);
}

static int copy_path_without_last_extension(const char *path, char *out,
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
   if (mkdir("/media/mmcblk0/unifrog", 0777) != 0 && errno != EEXIST)
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

