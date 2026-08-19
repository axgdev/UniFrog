#include <unifrog/zip.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <unifrog/zlib_port.h>

#define UNIFROG_ZIP_EOCD_SIG 0x06054b50u
#define UNIFROG_ZIP_CDH_SIG 0x02014b50u
#define UNIFROG_ZIP_LH_SIG 0x04034b50u
#define UNIFROG_ZIP_EOCD_MIN 22u
#define UNIFROG_ZIP_EOCD_MAX_COMMENT 0xffffu
#define UNIFROG_ZIP_EOCD_SCAN_MAX (UNIFROG_ZIP_EOCD_MIN + \
   UNIFROG_ZIP_EOCD_MAX_COMMENT)
#define UNIFROG_ZIP_CDH_FIXED 46u
#define UNIFROG_ZIP_LH_FIXED 30u
#define UNIFROG_ZIP_IO_CHUNK 4096u

struct unifrog_zip_impl {
   FILE *file;
   size_t file_size;
   size_t entry_count;
   size_t *data_offsets;
   struct unifrog_zip_entry *entries;
   char *name_storage;
   unsigned owns_file;
};

struct unifrog_zip_scan_entry {
   const uint8_t *name;
   size_t name_len;
   size_t data_offset;
   uint16_t flags;
   uint16_t method;
   uint32_t crc32;
   uint32_t compressed_size;
   uint32_t uncompressed_size;
   uint32_t local_offset;
   int keep;
};

static uint16_t zip_read_le16(const uint8_t *p)
{
   return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t zip_read_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int zip_file_size(FILE *file, size_t *out_size)
{
   long start;
   long end;

   if (!file || !out_size)
      return -1;
   start = ftell(file);
   if (start < 0)
      return -1;
   if (fseek(file, 0, SEEK_END) != 0)
      return -1;
   end = ftell(file);
   if (end < 0 || fseek(file, start, SEEK_SET) != 0)
      return -1;
   *out_size = (size_t)end;
   return 0;
}

static int zip_read_exact(FILE *file, void *buf, size_t size)
{
   return fread(buf, 1, size, file) == size ? 0 : -1;
}

static int zip_seek(FILE *file, size_t offset)
{
   if (offset > (size_t)LONG_MAX)
      return -1;
   return fseek(file, (long)offset, SEEK_SET);
}

static int zip_skip(FILE *file, size_t count)
{
   if (count > (size_t)LONG_MAX)
      return -1;
   return fseek(file, (long)count, SEEK_CUR);
}

static int zip_find_eocd(const uint8_t *zip, size_t zip_size,
   size_t *eocd_offset)
{
   size_t min_pos;
   size_t pos;

   if (!zip || !eocd_offset || zip_size < UNIFROG_ZIP_EOCD_MIN)
      return -1;
   min_pos = zip_size > UNIFROG_ZIP_EOCD_SCAN_MAX ?
      zip_size - UNIFROG_ZIP_EOCD_SCAN_MAX : 0u;
   pos = zip_size - UNIFROG_ZIP_EOCD_MIN;
   for (;;) {
      if (zip_read_le32(zip + pos) == UNIFROG_ZIP_EOCD_SIG) {
         uint16_t comment_len = zip_read_le16(zip + pos + 20);

         if (pos + UNIFROG_ZIP_EOCD_MIN + (size_t)comment_len == zip_size) {
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

/* ZIP names must stay relative: reject absolute paths, drive prefixes, empty
 * segments, and explicit parent traversal.
 */
static int zip_name_is_safe(const uint8_t *name, size_t name_len)
{
   size_t segment_len = 0;

   if (!name || name_len == 0)
      return 0;
   if (name[0] == '/' || name[0] == '\\' || name[0] == ':')
      return 0;
   if (memchr(name, '\0', name_len))
      return 0;
   for (size_t i = 0; i < name_len; i++) {
      unsigned char c = name[i];

      if (c == '/' || c == '\\' || c == ':') {
         if (segment_len == 0)
            return 0;
         if (segment_len == 1 && name[i - 1u] == '.')
            return 0;
         if (segment_len == 2 && name[i - 2u] == '.' && name[i - 1u] == '.')
            return 0;
         segment_len = 0;
         continue;
      }
      segment_len++;
   }
   if (segment_len == 0)
      return name[name_len - 1u] == '/';
   if (segment_len == 1 && name[name_len - 1u] == '.')
      return 0;
   if (segment_len == 2 && name[name_len - 2u] == '.' &&
       name[name_len - 1u] == '.')
      return 0;
   return 1;
}

static int zip_name_is_dir(const uint8_t *name, size_t name_len)
{
   return name_len > 0 && name[name_len - 1u] == '/';
}

static int zip_range_within_file(size_t offset, size_t length, size_t file_size)
{
   return offset <= file_size && length <= file_size - offset;
}

static int zip_compare_file_bytes(FILE *file, const uint8_t *expected,
   size_t expected_len)
{
   uint8_t buf[256];
   size_t remaining = expected_len;
   size_t offset = 0;

   while (remaining > 0) {
      size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);

      if (zip_read_exact(file, buf, chunk) != 0)
         return -1;
      if (memcmp(buf, expected + offset, chunk) != 0)
         return -1;
      offset += chunk;
      remaining -= chunk;
   }
   return 0;
}

static int zip_scan_entry(FILE *file, const uint8_t *cd, size_t cd_size,
   size_t file_size, size_t cursor, struct unifrog_zip_scan_entry *scan,
   size_t *next_cursor)
{
   const uint8_t *fixed;
   uint16_t name_len;
   uint16_t extra_len;
   uint16_t comment_len;
   uint32_t local_offset;
   uint32_t compressed_size;
   uint32_t uncompressed_size;
   size_t record_size;
   size_t local_pos;
   size_t data_offset;
   uint8_t local[UNIFROG_ZIP_LH_FIXED];
   uint16_t local_flags;
   uint16_t local_method;
   uint16_t local_name_len;
   uint16_t local_extra_len;

   if (!file || !cd || !scan || !next_cursor || cursor > cd_size ||
       cd_size - cursor < UNIFROG_ZIP_CDH_FIXED)
      return -1;
   fixed = cd + cursor;
   if (zip_read_le32(fixed) != UNIFROG_ZIP_CDH_SIG)
      return -1;
   scan->flags = zip_read_le16(fixed + 8);
   scan->method = zip_read_le16(fixed + 10);
   scan->crc32 = zip_read_le32(fixed + 16);
   compressed_size = zip_read_le32(fixed + 20);
   uncompressed_size = zip_read_le32(fixed + 24);
   name_len = zip_read_le16(fixed + 28);
   extra_len = zip_read_le16(fixed + 30);
   comment_len = zip_read_le16(fixed + 32);
   local_offset = zip_read_le32(fixed + 42);
   scan->compressed_size = compressed_size;
   scan->uncompressed_size = uncompressed_size;
   scan->local_offset = local_offset;
   if (compressed_size == 0xffffffffu || uncompressed_size == 0xffffffffu ||
       local_offset == 0xffffffffu)
      return -1;
   record_size = UNIFROG_ZIP_CDH_FIXED + (size_t)name_len +
      (size_t)extra_len + (size_t)comment_len;
   if (record_size > cd_size - cursor)
      return -1;
   scan->name = fixed + UNIFROG_ZIP_CDH_FIXED;
   scan->name_len = name_len;
   if (scan->flags & 1u)
      return -1;
   if (!zip_name_is_safe(scan->name, scan->name_len))
      return -1;
   scan->keep = !zip_name_is_dir(scan->name, scan->name_len) &&
      (scan->method == 0u || scan->method == 8u);
   if (scan->method == 0u && scan->compressed_size != scan->uncompressed_size)
      return -1;

   if (!zip_range_within_file((size_t)local_offset, UNIFROG_ZIP_LH_FIXED,
         file_size))
      return -1;
   if (zip_seek(file, (size_t)local_offset) != 0)
      return -1;
   if (zip_read_exact(file, local, sizeof(local)) != 0)
      return -1;
   if (zip_read_le32(local) != UNIFROG_ZIP_LH_SIG)
      return -1;
   local_flags = zip_read_le16(local + 6);
   local_method = zip_read_le16(local + 8);
   local_name_len = zip_read_le16(local + 26);
   local_extra_len = zip_read_le16(local + 28);
   if (local_flags & 1u)
      return -1;
   if (local_method != scan->method)
      return -1;
   if ((size_t)local_name_len != scan->name_len)
      return -1;
   local_pos = (size_t)local_offset + UNIFROG_ZIP_LH_FIXED;
   if (!zip_range_within_file(local_pos, scan->name_len, file_size))
      return -1;
   if (zip_compare_file_bytes(file, scan->name, scan->name_len) != 0)
      return -1;
   if (!zip_range_within_file(local_pos + scan->name_len, local_extra_len,
         file_size))
      return -1;
   if (zip_skip(file, local_extra_len) != 0)
      return -1;
   data_offset = local_pos + scan->name_len + local_extra_len;
   if (!zip_range_within_file(data_offset, scan->compressed_size, file_size))
      return -1;
   scan->data_offset = data_offset;
   *next_cursor = cursor + record_size;
   return 0;
}

static int zip_copy_stored(FILE *in, FILE *out, uint32_t size,
   uint32_t *crc_out)
{
   uint8_t buf[UNIFROG_ZIP_IO_CHUNK];
   size_t remaining = size;
   uint32_t crc = crc32(0L, Z_NULL, 0);

   while (remaining > 0) {
      size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);

      if (zip_read_exact(in, buf, chunk) != 0)
         return -1;
      if (fwrite(buf, 1, chunk, out) != chunk)
         return -1;
      crc = (uint32_t)crc32(crc, buf, (uInt)chunk);
      remaining -= chunk;
   }
   if (crc_out)
      *crc_out = crc;
   return 0;
}

static int zip_inflate_raw(FILE *in, FILE *out, uint32_t compressed_size,
   uint32_t uncompressed_size, uint32_t *crc_out)
{
   uint8_t in_buf[UNIFROG_ZIP_IO_CHUNK];
   uint8_t out_buf[UNIFROG_ZIP_IO_CHUNK];
   z_stream stream;
   size_t remaining = compressed_size;
   uint32_t crc = crc32(0L, Z_NULL, 0);
   int zret = Z_OK;
   int ret = -1;

   memset(&stream, 0, sizeof(stream));
   if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
      return -1;
   while (remaining > 0) {
      size_t chunk = remaining < sizeof(in_buf) ? remaining : sizeof(in_buf);

      if (zip_read_exact(in, in_buf, chunk) != 0)
         goto out;
      remaining -= chunk;
      stream.next_in = in_buf;
      stream.avail_in = (uInt)chunk;
      while (stream.avail_in > 0) {
         size_t produced;

         stream.next_out = out_buf;
         stream.avail_out = sizeof(out_buf);
         zret = inflate(&stream, Z_NO_FLUSH);
         if (zret != Z_OK && zret != Z_STREAM_END)
            goto out;
         produced = sizeof(out_buf) - stream.avail_out;
         if (produced > 0) {
            if (fwrite(out_buf, 1, produced, out) != produced)
               goto out;
            crc = (uint32_t)crc32(crc, out_buf, (uInt)produced);
         }
         if (stream.total_out > uncompressed_size)
            goto out;
         if (zret == Z_STREAM_END) {
            if (stream.avail_in != 0 || remaining != 0)
               goto out;
            break;
         }
      }
      if (zret == Z_STREAM_END)
         break;
   }
   while (zret != Z_STREAM_END) {
      size_t produced;

      stream.next_out = out_buf;
      stream.avail_out = sizeof(out_buf);
      zret = inflate(&stream, Z_FINISH);
      if (zret != Z_OK && zret != Z_STREAM_END)
         goto out;
      produced = sizeof(out_buf) - stream.avail_out;
      if (produced > 0) {
         if (fwrite(out_buf, 1, produced, out) != produced)
            goto out;
         crc = (uint32_t)crc32(crc, out_buf, (uInt)produced);
      }
      if (stream.total_out > uncompressed_size)
         goto out;
      if (produced == 0 && zret != Z_STREAM_END)
         goto out;
   }
   if (zret == Z_STREAM_END && stream.total_out == uncompressed_size) {
      if (crc_out)
         *crc_out = crc;
      ret = 0;
   }

out:
   (void)inflateEnd(&stream);
   return ret;
}

static void zip_free_impl(struct unifrog_zip_impl *impl)
{
   if (!impl)
      return;
   if (impl->owns_file && impl->file)
      fclose(impl->file);
   free(impl->data_offsets);
   free(impl->entries);
   free(impl->name_storage);
   free(impl);
}

static int zip_open_from_file(FILE *file, int own_file,
   struct unifrog_zip_archive *zip)
{
   struct unifrog_zip_impl *impl = NULL;
   uint8_t *tail = NULL;
   uint8_t *cd = NULL;
   size_t file_size = 0;
   size_t tail_size = 0;
   size_t eocd_offset = 0;
   size_t cd_offset = 0;
   size_t cd_size = 0;
   size_t cursor = 0;
   size_t supported_count = 0;
   size_t name_storage = 0;
   long restore_pos = -1;
   uint16_t entries_total = 0;
   int ret = -1;

   if (!file || !zip)
      return -1;
   memset(zip, 0, sizeof(*zip));
   restore_pos = ftell(file);
   if (restore_pos < 0)
      restore_pos = -1;
   if (zip_file_size(file, &file_size) != 0 || file_size < UNIFROG_ZIP_EOCD_MIN)
      goto out;
   tail_size = file_size > UNIFROG_ZIP_EOCD_SCAN_MAX ?
      UNIFROG_ZIP_EOCD_SCAN_MAX : file_size;
   tail = malloc(tail_size);
   if (!tail)
      goto out;
   if (zip_seek(file, file_size - tail_size) != 0 ||
       zip_read_exact(file, tail, tail_size) != 0 ||
       zip_find_eocd(tail, tail_size, &eocd_offset) != 0)
      goto out;
   if (zip_read_le16(tail + eocd_offset + 4) != 0 ||
       zip_read_le16(tail + eocd_offset + 6) != 0)
      goto out;
   entries_total = zip_read_le16(tail + eocd_offset + 10);
   cd_size = zip_read_le32(tail + eocd_offset + 12);
   cd_offset = zip_read_le32(tail + eocd_offset + 16);
   if (entries_total == 0xffffu || cd_size == 0xffffffffu ||
       cd_offset == 0xffffffffu)
      goto out;
   if (cd_size > file_size || cd_offset > file_size - cd_size)
      goto out;
   cd = malloc(cd_size ? cd_size : 1u);
   if (!cd)
      goto out;
   if (zip_seek(file, cd_offset) != 0 || zip_read_exact(file, cd, cd_size) != 0)
      goto out;

   cursor = 0;
   for (uint16_t i = 0; i < entries_total; i++) {
      struct unifrog_zip_scan_entry scan;
      size_t next_cursor;

      memset(&scan, 0, sizeof(scan));
      if (zip_scan_entry(file, cd, cd_size, file_size, cursor, &scan,
            &next_cursor) != 0)
         goto out;
      if (scan.keep) {
         if (supported_count == SIZE_MAX ||
             name_storage > SIZE_MAX - scan.name_len - 1u)
            goto out;
         supported_count++;
         name_storage += scan.name_len + 1u;
      }
      cursor = next_cursor;
   }
   if (cursor != cd_size)
      goto out;

   impl = calloc(1, sizeof(*impl));
   if (!impl)
      goto out;
   impl->file = file;
   impl->file_size = file_size;
   impl->owns_file = (unsigned)own_file;
   impl->entry_count = supported_count;
   if (supported_count > 0) {
      impl->entries = calloc(supported_count, sizeof(*impl->entries));
      impl->data_offsets = calloc(supported_count, sizeof(*impl->data_offsets));
      impl->name_storage = malloc(name_storage);
      if (!impl->entries || !impl->data_offsets || !impl->name_storage)
         goto out;
   }

   cursor = 0;
   {
      size_t entry_index = 0;
      size_t name_cursor = 0;

      for (uint16_t i = 0; i < entries_total; i++) {
         struct unifrog_zip_scan_entry scan;
         struct unifrog_zip_entry *entry;
         size_t next_cursor;

         memset(&scan, 0, sizeof(scan));
         if (zip_scan_entry(file, cd, cd_size, file_size, cursor, &scan,
               &next_cursor) != 0)
            goto out;
         if (scan.keep) {
            entry = &impl->entries[entry_index];
            entry->name = impl->name_storage + name_cursor;
            memcpy(impl->name_storage + name_cursor, scan.name, scan.name_len);
            impl->name_storage[name_cursor + scan.name_len] = '\0';
            entry->flags = scan.flags;
            entry->method = scan.method;
            entry->crc32 = scan.crc32;
            entry->compressed_size = scan.compressed_size;
            entry->uncompressed_size = scan.uncompressed_size;
            entry->local_offset = scan.local_offset;
            impl->data_offsets[entry_index] = scan.data_offset;
            entry_index++;
            name_cursor += scan.name_len + 1u;
         }
         cursor = next_cursor;
      }
   }

   free(cd);
   free(tail);
   if (restore_pos >= 0)
      (void)fseek(file, restore_pos, SEEK_SET);
   zip->impl = impl;
   return 0;

out:
   if (restore_pos >= 0)
      (void)fseek(file, restore_pos, SEEK_SET);
   free(cd);
   free(tail);
   zip_free_impl(impl);
   if (own_file && file)
      fclose(file);
   return ret;
}

int unifrog_zip_open_path(const char *path, struct unifrog_zip_archive *zip)
{
   FILE *file;

   if (!path || !zip)
      return -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   return zip_open_from_file(file, 1, zip);
}

int unifrog_zip_open_file(FILE *file, struct unifrog_zip_archive *zip)
{
   return zip_open_from_file(file, 0, zip);
}

void unifrog_zip_close(struct unifrog_zip_archive *zip)
{
   struct unifrog_zip_impl *impl;

   if (!zip)
      return;
   impl = (struct unifrog_zip_impl *)zip->impl;
   zip->impl = NULL;
   zip_free_impl(impl);
}

size_t unifrog_zip_entry_count(const struct unifrog_zip_archive *zip)
{
   const struct unifrog_zip_impl *impl = zip ? (const struct unifrog_zip_impl *)
      zip->impl : NULL;

   return impl ? impl->entry_count : 0u;
}

const struct unifrog_zip_entry *unifrog_zip_entries(
   const struct unifrog_zip_archive *zip)
{
   const struct unifrog_zip_impl *impl = zip ? (const struct unifrog_zip_impl *)
      zip->impl : NULL;

   return impl ? impl->entries : NULL;
}

const struct unifrog_zip_entry *unifrog_zip_find(
   const struct unifrog_zip_archive *zip, const char *name)
{
   const struct unifrog_zip_impl *impl = zip ? (const struct unifrog_zip_impl *)
      zip->impl : NULL;

   if (!impl || !name || !name[0])
      return NULL;
   for (size_t i = 0; i < impl->entry_count; i++) {
      if (strcmp(impl->entries[i].name, name) == 0)
         return &impl->entries[i];
   }
   return NULL;
}

static int zip_extract_entry_index(const struct unifrog_zip_impl *impl,
   size_t index, FILE *out)
{
   const struct unifrog_zip_entry *entry;
   long restore_pos;
   uint32_t crc = 0;
   int ret = -1;

   if (!impl || !out || index >= impl->entry_count)
      return -1;
   entry = &impl->entries[index];
   restore_pos = ftell(impl->file);
   if (restore_pos < 0)
      restore_pos = -1;
   if (zip_seek(impl->file, impl->data_offsets[index]) != 0)
      goto out;
   if (entry->method == 0u) {
      if (zip_copy_stored(impl->file, out, entry->compressed_size, &crc) != 0)
         goto out;
   } else if (entry->method == 8u) {
      if (zip_inflate_raw(impl->file, out, entry->compressed_size,
            entry->uncompressed_size, &crc) != 0)
         goto out;
   } else {
      goto out;
   }
   if (crc != entry->crc32)
      goto out;
   ret = 0;

out:
   if (restore_pos >= 0)
      (void)fseek(impl->file, restore_pos, SEEK_SET);
   return ret;
}

int unifrog_zip_extract_entry_to_file(const struct unifrog_zip_archive *zip,
   const struct unifrog_zip_entry *entry, FILE *out)
{
   const struct unifrog_zip_impl *impl = zip ? (const struct unifrog_zip_impl *)
      zip->impl : NULL;

   if (!impl || !entry || !out)
      return -1;
   for (size_t i = 0; i < impl->entry_count; i++) {
      if (&impl->entries[i] == entry)
         return zip_extract_entry_index(impl, i, out);
   }
   return -1;
}

int unifrog_zip_extract_name_to_file(const struct unifrog_zip_archive *zip,
   const char *name, FILE *out)
{
   const struct unifrog_zip_entry *entry;

   entry = unifrog_zip_find(zip, name);
   if (!entry)
      return -1;
   return unifrog_zip_extract_entry_to_file(zip, entry, out);
}

int unifrog_zip_extract_name_to_path(const struct unifrog_zip_archive *zip,
   const char *name, const char *out_path)
{
   FILE *out;
   int ret;

   if (!zip || !name || !out_path || !out_path[0])
      return -1;
   out = fopen(out_path, "wb");
   if (!out)
      return -1;
   ret = unifrog_zip_extract_name_to_file(zip, name, out);
   fclose(out);
   return ret;
}
