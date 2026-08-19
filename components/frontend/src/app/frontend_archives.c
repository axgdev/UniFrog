#include "frontend_internal.h"

/* Private theme/update archive extraction helpers. */
static uint16_t read_le16(const uint8_t *p)
{
   return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int file_size_fp(FILE *file, size_t *out)
{
   long end;
   long pos;

   if (!file || !out)
      return -1;
   pos = ftell(file);
   if (pos < 0 || fseek(file, 0, SEEK_END) != 0)
      return -1;
   end = ftell(file);
   if (end < 0 || fseek(file, pos, SEEK_SET) != 0)
      return -1;
   *out = (size_t)end;
   return 0;
}

static int zip_entry_name_is_dir(const char *name)
{
   size_t len = name ? strlen(name) : 0;

   return len > 0 && name[len - 1u] == '/';
}

static int zip_name_safe(const char *name)
{
   if (!name || !name[0] || name[0] == '/')
      return 0;
   if (strstr(name, "..") || strchr(name, '\\') || strchr(name, ':'))
      return 0;
   return 1;
}

static const char *theme_archive_rel(const char *name)
{
   const char *slash;

   if (!name || !name[0])
      return name;
   slash = strchr(name, '/');
   if (!slash)
      return name;
   if (slash > name + 2) {
      const char *x = name;
      int left = 0;
      int right = 0;

      while (*x >= '0' && *x <= '9') {
         left = 1;
         x++;
      }
      if (left && *x == 'x') {
         x++;
         while (*x >= '0' && *x <= '9') {
            right = 1;
            x++;
         }
         if (right && x == slash)
            return slash + 1;
      }
   }
   return name;
}

static int theme_archive_resolution_rank(const char *name)
{
   static const char *const prefixes[] = {
      "320x240/", "640x480/", "720x480/", "720x576/", "720x720/",
      "1024x768/", "1280x720/",
   };

   if (!name)
      return 0;
   for (unsigned i = 0; i < ARRAY_SIZE(prefixes); i++) {
      if (strncmp(name, prefixes[i], strlen(prefixes[i])) == 0)
         return (int)i + 1;
   }
   return 0;
}

static int theme_archive_has_lower_duplicate(const struct frontend_zip_entry *entries,
   unsigned count, const char *name)
{
   const char *rel = theme_archive_rel(name);
   int rank = theme_archive_resolution_rank(name);

   if (!entries || !name || rel == name || rank == 0)
      return 0;
   for (unsigned i = 0; i < count; i++) {
      int other_rank;

      if (!entries[i].name[0] || strcmp(entries[i].name, name) == 0)
         continue;
      if (strcmp(theme_archive_rel(entries[i].name), rel) != 0)
         continue;
      other_rank = theme_archive_resolution_rank(entries[i].name);
      if (other_rank == 0 && theme_archive_rel(entries[i].name) !=
          entries[i].name)
         continue;
      if (other_rank == 0 || other_rank < rank)
         return 1;
   }
   return 0;
}

static int theme_archive_entry_needed(const struct frontend_zip_entry *entries,
   unsigned count, const char *name)
{
   const char *rel;
   int rank;

   if (!name || !name[0])
      return 0;
   if (zip_entry_name_is_dir(name))
      return 0;
   rel = theme_archive_rel(name);
   rank = theme_archive_resolution_rank(name);
   if (theme_archive_has_lower_duplicate(entries, count, name))
      return 0;
   if (strcmp(rel, "version.txt") == 0 ||
       strcmp(rel, "credits.txt") == 0 ||
       strcmp(rel, "preview.png") == 0)
      return 1;
   if (strncmp(rel, "scheme/", 7) == 0)
      return rank == 0 || rank == 1 || rank == 2;
   if (strncmp(rel, "font/", 5) == 0 || strncmp(rel, "glyph/", 6) == 0)
      return 1;
   if (strncmp(rel, "image/", 6) == 0 &&
       unifrog_text_ends_with_ci(rel, ".png"))
      return 1;
   if (rel == name && strchr(rel, '/'))
      return 0;
   return 0;
}

static int theme_archive_stamp(char *stamp, size_t stamp_size,
   const char *archive_path)
{
   struct stat st;
   int wrote;

   if (!stamp || stamp_size == 0 || !archive_path ||
       stat(archive_path, &st) != 0)
      return -1;
   wrote = snprintf(stamp, stamp_size, "extractor=5\nsize=%lu\nmtime=%lu\n",
      (unsigned long)st.st_size, (unsigned long)st.st_mtime);
   return wrote > 0 && (size_t)wrote < stamp_size ? 0 : -1;
}

static int theme_archive_stamp_matches(const char *dest_root,
   const char *archive_path)
{
   char stamp_path[FRONTEND_MAX_PATH];
   char expected[80];
   char actual[80];
   FILE *file;
   size_t got;

   if (!dest_root || !archive_path ||
       frontend_path_join(stamp_path, sizeof(stamp_path), dest_root,
          ".unifrog-muxthm-stamp") != 0 ||
       theme_archive_stamp(expected, sizeof(expected), archive_path) != 0)
      return 0;
   file = fopen(stamp_path, "rb");
   if (!file)
      return 0;
   got = fread(actual, 1, sizeof(actual) - 1u, file);
   fclose(file);
   actual[got] = '\0';
   return strcmp(actual, expected) == 0;
}

static void theme_archive_write_stamp(const char *dest_root,
   const char *archive_path)
{
   char stamp_path[FRONTEND_MAX_PATH];
   char stamp[80];

   if (!dest_root || !archive_path ||
       frontend_path_join(stamp_path, sizeof(stamp_path), dest_root,
          ".unifrog-muxthm-stamp") != 0 ||
       theme_archive_stamp(stamp, sizeof(stamp), archive_path) != 0)
      return;
   (void)frontend_write_text_file(stamp_path, stamp);
}

static int zip_find_eocd(const uint8_t *zip, size_t zip_size,
   size_t *eocd_offset)
{
   size_t min_pos;
   size_t pos;

   if (!zip || !eocd_offset || zip_size < 22u)
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

static int zip_locate_entry_data(FILE *file, size_t zip_size,
   const struct frontend_zip_entry *entry, size_t *data_offset)
{
   uint8_t local[30];
   uint16_t local_name_len;
   uint16_t local_extra_len;
   size_t offset;

   if (!file || !entry || !data_offset ||
       (size_t)entry->local_offset > zip_size ||
       sizeof(local) > zip_size - (size_t)entry->local_offset)
      return -1;
   if (fseek(file, (long)entry->local_offset, SEEK_SET) != 0 ||
       fread(local, 1, sizeof(local), file) != sizeof(local) ||
       read_le32(local) != 0x04034b50u)
      return -1;
   if (read_le16(local + 8) != entry->method)
      return -1;
   local_name_len = read_le16(local + 26);
   local_extra_len = read_le16(local + 28);
   offset = (size_t)entry->local_offset + sizeof(local);
   if ((size_t)local_name_len > zip_size - offset)
      return -1;
   offset += local_name_len;
   if ((size_t)local_extra_len > zip_size - offset)
      return -1;
   offset += local_extra_len;
   if (entry->compressed_size > zip_size - offset)
      return -1;
   *data_offset = offset;
   return 0;
}

static int zip_copy_stored(FILE *in, FILE *out, size_t size)
{
   uint8_t buf[4096];
   size_t remaining = size;

   while (remaining > 0) {
      size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);

      if (fread(buf, 1, chunk, in) != chunk ||
          fwrite(buf, 1, chunk, out) != chunk)
         return -1;
      remaining -= chunk;
   }
   return 0;
}

static int zip_inflate_to_file(FILE *in, FILE *out, size_t compressed_size,
   size_t uncompressed_size)
{
   uint8_t in_buf[4096];
   uint8_t out_buf[4096];
   z_stream stream;
   size_t remaining = compressed_size;
   int zret;
   int ret = -1;

   memset(&stream, 0, sizeof(stream));
   zret = inflateInit2(&stream, -MAX_WBITS);
   if (zret != Z_OK)
      return -1;
   while (remaining > 0) {
      size_t chunk = remaining < sizeof(in_buf) ? remaining : sizeof(in_buf);

      if (fread(in_buf, 1, chunk, in) != chunk)
         goto out;
      remaining -= chunk;
      stream.next_in = in_buf;
      stream.avail_in = (uInt)chunk;
      do {
         stream.next_out = out_buf;
         stream.avail_out = sizeof(out_buf);
         zret = inflate(&stream, Z_NO_FLUSH);
         if (zret != Z_OK && zret != Z_STREAM_END)
            goto out;
         chunk = sizeof(out_buf) - stream.avail_out;
         if (chunk > 0 && fwrite(out_buf, 1, chunk, out) != chunk)
            goto out;
         if (zret == Z_STREAM_END)
            break;
      } while (stream.avail_in > 0 || stream.avail_out == 0);
   }
   while (zret != Z_STREAM_END) {
      size_t chunk;

      stream.next_out = out_buf;
      stream.avail_out = sizeof(out_buf);
      zret = inflate(&stream, Z_FINISH);
      if (zret != Z_OK && zret != Z_STREAM_END)
         goto out;
      chunk = sizeof(out_buf) - stream.avail_out;
      if (chunk > 0 && fwrite(out_buf, 1, chunk, out) != chunk)
         goto out;
      if (chunk == 0 && zret != Z_STREAM_END)
         goto out;
   }
   if (zret == Z_STREAM_END && stream.total_out == uncompressed_size)
      ret = 0;

out:
   (void)inflateEnd(&stream);
   return ret;
}

static int zip_inflate_to_file_memory(FILE *in, FILE *out,
   size_t compressed_size, size_t uncompressed_size)
{
   uint8_t *compressed = NULL;
   uint8_t *uncompressed = NULL;
   z_stream stream;
   int zret;
   int ret = -1;

   if (compressed_size == 0 || uncompressed_size == 0 ||
       compressed_size > 1024u * 1024u || uncompressed_size > 1024u * 1024u)
      return -1;
   compressed = malloc(compressed_size);
   uncompressed = malloc(uncompressed_size);
   if (!compressed || !uncompressed)
      goto out;
   if (fread(compressed, 1, compressed_size, in) != compressed_size)
      goto out;
   memset(&stream, 0, sizeof(stream));
   if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
      goto out;
   stream.next_in = compressed;
   stream.avail_in = (uInt)compressed_size;
   stream.next_out = uncompressed;
   stream.avail_out = (uInt)uncompressed_size;
   zret = inflate(&stream, Z_FINISH);
   if (zret == Z_STREAM_END &&
       stream.total_out == uncompressed_size &&
       fwrite(uncompressed, 1, uncompressed_size, out) == uncompressed_size)
      ret = 0;
   (void)inflateEnd(&stream);

out:
   free(uncompressed);
   free(compressed);
   return ret;
}

int install_theme_archive(const char *archive_path, char *installed_name,
   size_t installed_name_size, frontend_progress_cb progress,
   void *progress_userdata)
{
   FILE *zip_file = NULL;
   uint8_t *tail = NULL;
   size_t zip_size;
   size_t tail_size;
   size_t tail_eocd;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries = 0;
   struct frontend_zip_entry *archive_entries = NULL;
   char theme_name[96] = "";
   char dest_root[FRONTEND_MAX_PATH] = "";
   const char *stage = "start";
   unsigned extracted = 0;
   unsigned skipped = 0;
   unsigned asset_errors = 0;
   int ret = -1;
   uint32_t start_ms = unifrog_perf_time_ms();
   uint32_t open_ms = 0;
   uint32_t tail_ms = 0;
   uint32_t scan_ms = 0;
   uint32_t extract_ms = 0;
   uint32_t t0;

   if (!archive_path || !archive_path[0]) {
      stage = "bad_archive_path";
      goto out;
   }
   unifrog_text_copy(theme_name, sizeof(theme_name), frontend_basename(archive_path));
   frontend_strip_known_suffix(theme_name, ".muxthm");
   frontend_strip_known_suffix(theme_name, ".zip");
   if (!theme_name[0]) {
      stage = "bad_theme_name";
      goto out;
   }
   if (frontend_path_join(dest_root, sizeof(dest_root), FRONTEND_THEME_ROOT, theme_name) != 0) {
      stage = "dest_path";
      goto out;
   }
   unifrog_log("frontend theme archive install begin path=%s name=%s dest=%s\n",
      archive_path, theme_name, dest_root);
   if (progress)
      progress(progress_userdata, "preparing", 0, 100);
   stage = "mkdir_dest";
   frontend_ensure_data_dirs();
   if (frontend_mkdir_p(dest_root) != 0) {
      unifrog_log("frontend theme archive mkdir failed dest=%s errno=%d\n",
         dest_root, errno);
      goto out;
   }
   if (theme_archive_stamp_matches(dest_root, archive_path)) {
      stage = "cached";
      if (installed_name && installed_name_size)
         unifrog_text_copy(installed_name, installed_name_size, theme_name);
      if (progress)
         progress(progress_userdata, "cached", 100, 100);
      ret = 0;
      goto out;
   }

   stage = "open";
   if (progress)
      progress(progress_userdata, "opening", 2, 100);
   t0 = unifrog_perf_time_ms();
   zip_file = fopen(archive_path, "rb");
   if (!zip_file || file_size_fp(zip_file, &zip_size) != 0 || zip_size < 22u)
      goto out;
   open_ms = unifrog_perf_time_ms() - t0;
   stage = "alloc_tail";
   t0 = unifrog_perf_time_ms();
   tail_size = zip_size > (0xffffu + 22u) ? (0xffffu + 22u) : zip_size;
   tail = malloc(tail_size);
   if (!tail)
      goto out;
   stage = "eocd";
   if (fseek(zip_file, (long)(zip_size - tail_size), SEEK_SET) != 0 ||
       fread(tail, 1, tail_size, zip_file) != tail_size ||
       zip_find_eocd(tail, tail_size, &tail_eocd) != 0)
      goto out;
   tail_ms = unifrog_perf_time_ms() - t0;
   entries = read_le16(tail + tail_eocd + 10);
   cd_size = read_le32(tail + tail_eocd + 12);
   cd_offset = read_le32(tail + tail_eocd + 16);
   if ((size_t)cd_offset > zip_size ||
       (size_t)cd_size > zip_size - (size_t)cd_offset ||
       fseek(zip_file, (long)cd_offset, SEEK_SET) != 0)
      goto out;

   stage = "central_directory";
   if (progress)
      progress(progress_userdata, "scanning", 10, 100);
   t0 = unifrog_perf_time_ms();
   archive_entries = calloc(entries ? entries : 1u, sizeof(*archive_entries));
   if (!archive_entries)
      goto out;
   for (uint16_t i = 0; i < entries; i++) {
      uint8_t fixed[46];
      struct frontend_zip_entry entry;
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      size_t copy_len;
      int name_overlong;

      if (fread(fixed, 1, sizeof(fixed), zip_file) != sizeof(fixed) ||
          read_le32(fixed) != 0x02014b50u)
         goto out;
      memset(&entry, 0, sizeof(entry));
      entry.flags = read_le16(fixed + 8);
      entry.method = read_le16(fixed + 10);
      entry.compressed_size = read_le32(fixed + 20);
      entry.uncompressed_size = read_le32(fixed + 24);
      entry.local_offset = read_le32(fixed + 42);
      name_len = read_le16(fixed + 28);
      extra_len = read_le16(fixed + 30);
      comment_len = read_le16(fixed + 32);
      name_overlong = name_len >= sizeof(entry.name);
      copy_len = name_len;
      if (copy_len >= sizeof(entry.name))
         copy_len = sizeof(entry.name) - 1u;
      if (fread(entry.name, 1, copy_len, zip_file) != copy_len)
         goto out;
      entry.name[copy_len] = '\0';
      if ((size_t)name_len > copy_len &&
          fseek(zip_file, (long)((size_t)name_len - copy_len), SEEK_CUR) != 0)
         goto out;
      if (fseek(zip_file, (long)((size_t)extra_len + comment_len), SEEK_CUR) != 0)
         goto out;
      if (name_overlong) {
         asset_errors++;
         continue;
      }
      if ((entry.flags & 1u) || !zip_name_safe(entry.name) ||
          (entry.method != 0 && entry.method != 8)) {
         asset_errors++;
         continue;
      }
      archive_entries[i] = entry;
      if (progress && ((i & 15u) == 0 || i + 1u == entries))
         progress(progress_userdata, "scanning",
            10u + (entries ? ((unsigned)(i + 1u) * 30u / entries) : 30u),
            100);
   }
   if (frontend_remove_tree(dest_root) != 0 && errno != ENOENT)
      unifrog_log("frontend theme archive cleanup warning dest=%s errno=%d\n",
         dest_root, errno);
   if (frontend_mkdir_p(dest_root) != 0)
      goto out;
   for (uint16_t i = 0; i < entries; i++) {
      struct frontend_zip_entry *entry = &archive_entries[i];
      char out_path[FRONTEND_MAX_PATH];
      size_t data_offset;
      FILE *out_file;
      int copy_ret = -1;
      uint32_t extract_t0;

      if (!entry->name[0])
         continue;
      if (progress && ((i & 7u) == 0 || i + 1u == entries))
         progress(progress_userdata, "extracting",
            40u + (entries ? ((unsigned)(i + 1u) * 58u / entries) : 58u),
            100);
      if (!theme_archive_entry_needed(archive_entries, entries, entry->name)) {
         skipped++;
         continue;
      }
      if (frontend_path_join(out_path, sizeof(out_path), dest_root, entry->name) != 0)
         goto out;
      if (zip_entry_name_is_dir(entry->name)) {
         if (frontend_mkdir_p(out_path) != 0)
            goto out;
         continue;
      }
      if (zip_locate_entry_data(zip_file, zip_size, entry, &data_offset) != 0 ||
          frontend_ensure_parent_dir(out_path) != 0 ||
          fseek(zip_file, (long)data_offset, SEEK_SET) != 0) {
         asset_errors++;
         unifrog_log("frontend theme archive entry skipped name=%s reason=locate errno=%d\n",
            entry->name, errno);
         stage = "central_directory";
         continue;
      }
      stage = entry->name;
      out_file = fopen(out_path, "wb");
      extract_t0 = unifrog_perf_time_ms();
      if (out_file) {
         if (entry->method == 0)
            copy_ret = zip_copy_stored(zip_file, out_file,
               entry->uncompressed_size);
         else {
            long data_pos = ftell(zip_file);

            copy_ret = zip_inflate_to_file_memory(zip_file, out_file,
               entry->compressed_size, entry->uncompressed_size);
            if (copy_ret != 0 && data_pos >= 0 &&
                fseek(zip_file, data_pos, SEEK_SET) == 0)
               copy_ret = zip_inflate_to_file(zip_file, out_file,
                  entry->compressed_size, entry->uncompressed_size);
         }
         if (fclose(out_file) != 0)
            copy_ret = -1;
      }
      if (copy_ret != 0) {
         asset_errors++;
         (void)unlink(out_path);
         unifrog_log("frontend theme archive entry skipped name=%s method=%u comp=%u uncomp=%u errno=%d\n",
            entry->name, entry->method, entry->compressed_size,
            entry->uncompressed_size, errno);
      } else {
         extracted++;
         extract_ms += unifrog_perf_time_ms() - extract_t0;
      }
      if (copy_ret != 0) {
         stage = "central_directory";
         continue;
      }
      stage = "central_directory";
   }
   scan_ms = unifrog_perf_time_ms() - t0;
   if (extracted == 0) {
      stage = "no_assets";
      goto out;
   }
   if (installed_name && installed_name_size)
      unifrog_text_copy(installed_name, installed_name_size, theme_name);
   theme_archive_write_stamp(dest_root, archive_path);
   ret = 0;

out:
   if (progress)
      progress(progress_userdata, ret == 0 ? "done" : stage, 100, 100);
   free(archive_entries);
   free(tail);
   if (zip_file)
      fclose(zip_file);
   unifrog_log("frontend theme archive install path=%s name=%s ret=%d stage=%s extracted=%u skipped=%u errors=%u entries=%u ms=%u open_ms=%u tail_ms=%u scan_ms=%u extract_ms=%u\n",
      archive_path, theme_name, ret, stage, extracted, skipped, asset_errors,
      entries, (unsigned)(unifrog_perf_time_ms() - start_ms),
      (unsigned)open_ms, (unsigned)tail_ms, (unsigned)scan_ms,
      (unsigned)extract_ms);
   return ret;
}

static int package_entry_needed(const char *name)
{
   const char *rel = name;

   if (!name || !name[0] || zip_entry_name_is_dir(name))
      return 0;
   if (strncmp(rel, "./", 2) == 0)
      rel += 2;
   return strncmp(rel, "bios/", 5) == 0 ||
      strncmp(rel, "unifrog/", 8) == 0;
}

static const char *package_entry_rel(const char *name)
{
   if (!name)
      return "";
   if (strncmp(name, "./", 2) == 0)
      return name + 2;
   return name;
}

int install_update_archive(const char *archive_path, char *slot_name,
   size_t slot_name_size)
{
   FILE *zip_file = NULL;
   uint8_t *tail = NULL;
   size_t zip_size;
   size_t tail_size;
   size_t tail_eocd;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries = 0;
   struct frontend_zip_entry *archive_entries = NULL;
   char slot[64] = "";
   char dest_root[FRONTEND_MAX_PATH] = "";
   char check_path[FRONTEND_MAX_PATH];
   const char *stage = "start";
   unsigned extracted = 0;
   unsigned skipped = 0;
   unsigned errors = 0;
   int ret = -1;
   uint32_t start_ms = unifrog_perf_time_ms();

   if (!archive_path || !archive_path[0])
      goto out;
   unifrog_text_copy(slot, sizeof(slot), frontend_basename(archive_path));
   frontend_strip_known_suffix(slot, ".zip");
   frontend_sanitize_slot_name(slot);
   if (!slot[0])
      goto out;
   if (frontend_path_join(dest_root, sizeof(dest_root), FRONTEND_VERSION_ROOT, slot) != 0)
      goto out;
   unifrog_log("frontend update install begin archive=%s slot=%s dest=%s\n",
      archive_path, slot, dest_root);
   frontend_ensure_data_dirs();
   if (frontend_mkdir_p(FRONTEND_VERSION_ROOT) != 0)
      goto out;
   if (frontend_remove_tree_under(dest_root, FRONTEND_VERSION_ROOT) != 0 &&
       errno != ENOENT)
      unifrog_log("frontend update cleanup warning dest=%s errno=%d\n",
         dest_root, errno);
   if (frontend_mkdir_p(dest_root) != 0)
      goto out;

   stage = "open";
   zip_file = fopen(archive_path, "rb");
   if (!zip_file || file_size_fp(zip_file, &zip_size) != 0 || zip_size < 22u)
      goto out;
   stage = "eocd";
   tail_size = zip_size > (0xffffu + 22u) ? (0xffffu + 22u) : zip_size;
   tail = malloc(tail_size);
   if (!tail)
      goto out;
   if (fseek(zip_file, (long)(zip_size - tail_size), SEEK_SET) != 0 ||
       fread(tail, 1, tail_size, zip_file) != tail_size ||
       zip_find_eocd(tail, tail_size, &tail_eocd) != 0)
      goto out;
   entries = read_le16(tail + tail_eocd + 10);
   cd_size = read_le32(tail + tail_eocd + 12);
   cd_offset = read_le32(tail + tail_eocd + 16);
   if ((size_t)cd_offset > zip_size ||
       (size_t)cd_size > zip_size - (size_t)cd_offset ||
       fseek(zip_file, (long)cd_offset, SEEK_SET) != 0)
      goto out;

   stage = "central_directory";
   archive_entries = calloc(entries ? entries : 1u, sizeof(*archive_entries));
   if (!archive_entries)
      goto out;
   for (uint16_t i = 0; i < entries; i++) {
      uint8_t fixed[46];
      struct frontend_zip_entry entry;
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      size_t copy_len;
      int name_overlong;

      if (fread(fixed, 1, sizeof(fixed), zip_file) != sizeof(fixed) ||
          read_le32(fixed) != 0x02014b50u)
         goto out;
      memset(&entry, 0, sizeof(entry));
      entry.flags = read_le16(fixed + 8);
      entry.method = read_le16(fixed + 10);
      entry.compressed_size = read_le32(fixed + 20);
      entry.uncompressed_size = read_le32(fixed + 24);
      entry.local_offset = read_le32(fixed + 42);
      name_len = read_le16(fixed + 28);
      extra_len = read_le16(fixed + 30);
      comment_len = read_le16(fixed + 32);
      name_overlong = name_len >= sizeof(entry.name);
      copy_len = name_len;
      if (copy_len >= sizeof(entry.name))
         copy_len = sizeof(entry.name) - 1u;
      if (fread(entry.name, 1, copy_len, zip_file) != copy_len)
         goto out;
      entry.name[copy_len] = '\0';
      if ((size_t)name_len > copy_len &&
          fseek(zip_file, (long)((size_t)name_len - copy_len), SEEK_CUR) != 0)
         goto out;
      if (fseek(zip_file, (long)((size_t)extra_len + comment_len), SEEK_CUR) != 0)
         goto out;
      if (name_overlong) {
         errors++;
         continue;
      }
      if ((entry.flags & 1u) || !zip_name_safe(entry.name) ||
          (entry.method != 0 && entry.method != 8)) {
         errors++;
         continue;
      }
      archive_entries[i] = entry;
   }

   stage = "extract";
   for (uint16_t i = 0; i < entries; i++) {
      struct frontend_zip_entry *entry = &archive_entries[i];
      char out_path[FRONTEND_MAX_PATH];
      size_t data_offset;
      FILE *out_file;
      int copy_ret = -1;

      if (!entry->name[0])
         continue;
      if (!package_entry_needed(entry->name)) {
         skipped++;
         continue;
      }
      if (frontend_path_join(out_path, sizeof(out_path), dest_root,
          package_entry_rel(entry->name)) != 0)
         goto out;
      if (zip_locate_entry_data(zip_file, zip_size, entry, &data_offset) != 0 ||
          frontend_ensure_parent_dir(out_path) != 0 ||
          fseek(zip_file, (long)data_offset, SEEK_SET) != 0) {
         errors++;
         continue;
      }
      out_file = fopen(out_path, "wb");
      if (out_file) {
         if (entry->method == 0)
            copy_ret = zip_copy_stored(zip_file, out_file,
               entry->uncompressed_size);
         else
            copy_ret = zip_inflate_to_file(zip_file, out_file,
               entry->compressed_size, entry->uncompressed_size);
         if (fclose(out_file) != 0)
            copy_ret = -1;
      }
      if (copy_ret == 0)
         extracted++;
      else {
         errors++;
         (void)unlink(out_path);
      }
   }

   if (errors)
      goto out;
   if (frontend_path_join(check_path, sizeof(check_path), dest_root,
       "unifrog/firmware/unifrog.bin") != 0 || !frontend_file_exists(check_path))
      goto out;
   if (frontend_path_join(check_path, sizeof(check_path), dest_root,
       "bios/bisrv.asd") != 0 || !frontend_file_exists(check_path))
      goto out;
   if (frontend_path_join(check_path, sizeof(check_path), dest_root,
       "unifrog/manifest.ini") != 0 || !frontend_file_exists(check_path))
      goto out;
   if (slot_name && slot_name_size)
      unifrog_text_copy(slot_name, slot_name_size, slot);
   ret = 0;

out:
   free(archive_entries);
   free(tail);
   if (zip_file)
      fclose(zip_file);
   unifrog_log("frontend update install archive=%s slot=%s ret=%d stage=%s extracted=%u skipped=%u errors=%u entries=%u ms=%u\n",
      archive_path ? archive_path : "", slot, ret, stage, extracted, skipped,
      errors, entries, (unsigned)(unifrog_perf_time_ms() - start_ms));
   return ret;
}

int validate_update_archive(const char *archive_path, char *summary,
   size_t summary_size)
{
   FILE *zip_file = NULL;
   uint8_t *tail = NULL;
   size_t zip_size;
   size_t tail_size;
   size_t tail_eocd;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries = 0;
   unsigned has_bios = 0;
   unsigned has_fw = 0;
   unsigned has_manifest = 0;
   unsigned dist_entries = 0;
   unsigned user_payload = 0;
   unsigned bad = 0;
   int ret = -1;

   if (summary && summary_size)
      summary[0] = '\0';
   if (!archive_path)
      goto out;
   zip_file = fopen(archive_path, "rb");
   if (!zip_file || file_size_fp(zip_file, &zip_size) != 0 || zip_size < 22u)
      goto out;
   tail_size = zip_size > (0xffffu + 22u) ? (0xffffu + 22u) : zip_size;
   tail = malloc(tail_size);
   if (!tail)
      goto out;
   if (fseek(zip_file, (long)(zip_size - tail_size), SEEK_SET) != 0 ||
       fread(tail, 1, tail_size, zip_file) != tail_size ||
       zip_find_eocd(tail, tail_size, &tail_eocd) != 0)
      goto out;
   entries = read_le16(tail + tail_eocd + 10);
   cd_size = read_le32(tail + tail_eocd + 12);
   cd_offset = read_le32(tail + tail_eocd + 16);
   if ((size_t)cd_offset > zip_size ||
       (size_t)cd_size > zip_size - (size_t)cd_offset ||
       fseek(zip_file, (long)cd_offset, SEEK_SET) != 0)
      goto out;

   for (uint16_t i = 0; i < entries; i++) {
      uint8_t fixed[46];
      char name[FRONTEND_MAX_PATH];
      const char *rel;
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      uint16_t flags;
      uint16_t method;
      size_t copy_len;
      int name_overlong;

      if (fread(fixed, 1, sizeof(fixed), zip_file) != sizeof(fixed) ||
          read_le32(fixed) != 0x02014b50u)
         goto out;
      name_len = read_le16(fixed + 28);
      extra_len = read_le16(fixed + 30);
      comment_len = read_le16(fixed + 32);
      flags = read_le16(fixed + 8);
      method = read_le16(fixed + 10);
      name_overlong = name_len >= sizeof(name);
      copy_len = name_len;
      if (copy_len >= sizeof(name))
         copy_len = sizeof(name) - 1u;
      if (fread(name, 1, copy_len, zip_file) != copy_len)
         goto out;
      name[copy_len] = '\0';
      if ((size_t)name_len > copy_len &&
          fseek(zip_file, (long)((size_t)name_len - copy_len), SEEK_CUR) != 0)
         goto out;
      if (fseek(zip_file, (long)((size_t)extra_len + comment_len), SEEK_CUR) != 0)
         goto out;
      if (name_overlong || !zip_name_safe(name) || (flags & 1u) ||
          (method != 0 && method != 8)) {
         bad++;
         continue;
      }
      rel = package_entry_rel(name);
      if (strcmp(rel, "bios/bisrv.asd") == 0)
         has_bios = 1;
      else if (strcmp(rel, "unifrog/firmware/unifrog.bin") == 0)
         has_fw = 1;
      else if (strcmp(rel, "unifrog/manifest.ini") == 0)
         has_manifest = 1;
      else if (strncmp(rel, "unifrog/", 8) == 0)
         dist_entries++;
      else if (strncmp(rel, "unifrog_data/", 13) == 0 &&
               !zip_entry_name_is_dir(rel))
         user_payload++;
   }
   ret = has_bios && has_fw && has_manifest && bad == 0 &&
      user_payload == 0 ? 0 : -1;

out:
   if (summary && summary_size)
      snprintf(summary, summary_size, "%s entries=%u bios=%u fw=%u manifest=%u dist=%u data=%u bad=%u",
         ret == 0 ? "ok" : "bad", entries, has_bios, has_fw, has_manifest,
         dist_entries, user_payload, bad);
   free(tail);
   if (zip_file)
      fclose(zip_file);
   unifrog_log("frontend package zip_check path=%s ret=%d summary=%s\n",
      archive_path ? archive_path : "", ret, summary ? summary : "");
   return ret;
}

void mark_boot_ok(void)
{
   char text[160];

   snprintf(text, sizeof(text), "commit=%s\npending=%s\n",
      UNIFROG_GIT_COMMIT,
      frontend_file_exists(UNIFROG_PENDING_VERSION_PATH) ? "yes" : "no");
   (void)frontend_write_text_file(UNIFROG_BOOT_OK_PATH, text);
   if (frontend_file_exists(UNIFROG_PENDING_VERSION_PATH))
      unifrog_log("frontend update boot_ok with pending marker present\n");
}

int activate_installed_version(const char *slot)
{
   char root[FRONTEND_MAX_PATH];
   char src[FRONTEND_MAX_PATH];
   char dst[FRONTEND_MAX_PATH];
   char marker[160];
   int ret = -1;

   if (!slot || !slot[0] ||
       frontend_path_join(root, sizeof(root), FRONTEND_VERSION_ROOT, slot) != 0)
      return -1;
   unifrog_log("frontend update activate begin slot=%s root=%s\n", slot, root);
   snprintf(marker, sizeof(marker), "slot=%s\nstage=pending\napplied_by=%s\n",
      slot, UNIFROG_GIT_COMMIT);
   (void)frontend_write_text_file(UNIFROG_PENDING_VERSION_PATH, marker);
   (void)unlink(UNIFROG_BOOT_OK_PATH);
   if (frontend_path_join(src, sizeof(src), root, "unifrog/firmware/unifrog.bin") != 0 ||
       frontend_copy_file_path(src, FRONTEND_DIST_ROOT "/firmware/unifrog.bin") != 0)
      goto out;
   if (frontend_path_join(src, sizeof(src), root, "bios/bisrv.asd") != 0 ||
       frontend_copy_file_path(src, FRONTEND_ROOT "/bios/bisrv.asd") != 0)
      goto out;
   (void)frontend_remove_tree_under(FRONTEND_DIST_ROOT "/cores", FRONTEND_DIST_ROOT);
   (void)frontend_remove_tree_under(FRONTEND_DIST_ROOT "/modules", FRONTEND_DIST_ROOT);
   if (frontend_path_join(src, sizeof(src), root, "unifrog") != 0 ||
       frontend_copy_tree_merge(src, FRONTEND_DIST_ROOT, NULL) != 0)
      goto out;
   snprintf(marker, sizeof(marker), "slot=%s\nstage=active\napplied_by=%s\n",
      slot, UNIFROG_GIT_COMMIT);
   (void)frontend_write_text_file(FRONTEND_ACTIVE_VERSION_PATH, marker);
   (void)unlink(UNIFROG_PENDING_VERSION_PATH);
   if (frontend_path_join(dst, sizeof(dst), root, ".active") == 0)
      (void)frontend_write_text_file(dst, marker);
   ret = 0;

out:
   unifrog_log("frontend update activate slot=%s ret=%d\n", slot, ret);
   return ret;
}
