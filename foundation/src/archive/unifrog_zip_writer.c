#include <unifrog/zip.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <unifrog/zlib_port.h>

#define ZIP_LOCAL_SIGNATURE 0x04034b50u
#define ZIP_CENTRAL_SIGNATURE 0x02014b50u
#define ZIP_END_SIGNATURE 0x06054b50u
#define ZIP_VERSION 20u
#define ZIP_IO_BYTES 4096u
#define ZIP_NAME_MAX 255u
#define ZIP_ENTRY_MAX 256u

struct zip_writer_entry {
   char *name;
   uint32_t crc32;
   uint32_t size;
   uint32_t local_offset;
   uint16_t dos_date;
   uint16_t dos_time;
};

struct zip_writer_impl {
   FILE *file;
   char *path;
   struct zip_writer_entry *entries;
   unsigned count;
   unsigned capacity;
   uint16_t pending_dos_date;
   uint16_t pending_dos_time;
};

static void writer_dos_time(uint16_t *date, uint16_t *clock)
{
   time_t now = time(NULL);
   struct tm *value = localtime(&now);
   unsigned year;

   if (!value || value->tm_year < 80) {
      *date = 0x0021u;
      *clock = 0;
      return;
   }
   year = (unsigned)(value->tm_year + 1900);
   if (year > 2107u)
      year = 2107u;
   *date = (uint16_t)(((year - 1980u) << 9) |
      ((unsigned)(value->tm_mon + 1) << 5) | (unsigned)value->tm_mday);
   *clock = (uint16_t)(((unsigned)value->tm_hour << 11) |
      ((unsigned)value->tm_min << 5) | ((unsigned)value->tm_sec >> 1));
}

static void put16(uint8_t *p, uint16_t value)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
   p[2] = (uint8_t)(value >> 16);
   p[3] = (uint8_t)(value >> 24);
}

static int write_exact(FILE *file, const void *data, size_t size)
{
   return size == 0 || fwrite(data, 1, size, file) == size ? 0 : -1;
}

static int writer_name_valid(const char *name)
{
   size_t segment = 0;
   size_t len;

   if (!name || !name[0] || name[0] == '/' || name[0] == '\\')
      return 0;
   len = strlen(name);
   if (len > ZIP_NAME_MAX)
      return 0;
   for (size_t i = 0; i < len; i++) {
      char c = name[i];

      if (c == '\\' || c == ':' || c == '\r' || c == '\n' || c == '\0')
         return 0;
      if (c == '/') {
         if (!segment || (segment == 1 && name[i - 1] == '.') ||
             (segment == 2 && name[i - 2] == '.' && name[i - 1] == '.'))
            return 0;
         segment = 0;
      } else {
         segment++;
      }
   }
   return segment > 0 && !(segment == 1 && name[len - 1] == '.') &&
      !(segment == 2 && name[len - 2] == '.' && name[len - 1] == '.');
}

static int writer_reserve(struct zip_writer_impl *impl)
{
   struct zip_writer_entry *next;
   unsigned capacity;

   if (impl->count < impl->capacity)
      return 0;
   if (impl->capacity >= ZIP_ENTRY_MAX)
      return -1;
   capacity = impl->capacity ? impl->capacity * 2u : 16u;
   if (capacity > ZIP_ENTRY_MAX)
      capacity = ZIP_ENTRY_MAX;
   next = realloc(impl->entries, capacity * sizeof(*next));
   if (!next)
      return -1;
   impl->entries = next;
   impl->capacity = capacity;
   return 0;
}

static int writer_begin_entry(struct zip_writer_impl *impl, const char *name,
   long *header_offset)
{
   uint8_t header[30];
   uint16_t dos_date;
   uint16_t dos_time;
   size_t name_len = strlen(name);
   long offset;

   if (!writer_name_valid(name) || writer_reserve(impl) != 0)
      return -1;
   offset = ftell(impl->file);
   if (offset < 0 || (unsigned long)offset > UINT32_MAX)
      return -1;
   memset(header, 0, sizeof(header));
   put32(header, ZIP_LOCAL_SIGNATURE);
   put16(header + 4, ZIP_VERSION);
   writer_dos_time(&dos_date, &dos_time);
   impl->pending_dos_date = dos_date;
   impl->pending_dos_time = dos_time;
   put16(header + 10, dos_time);
   put16(header + 12, dos_date);
   put16(header + 26, (uint16_t)name_len);
   if (write_exact(impl->file, header, sizeof(header)) != 0 ||
       write_exact(impl->file, name, name_len) != 0)
      return -1;
   *header_offset = offset;
   return 0;
}

static int writer_finish_entry(struct zip_writer_impl *impl, const char *name,
   long header_offset, uint32_t crc, uint32_t size)
{
   struct zip_writer_entry *entry;
   uint8_t fields[12];
   long end = ftell(impl->file);
   char *copy;

   if (end < 0 || fseek(impl->file, header_offset + 14, SEEK_SET) != 0)
      return -1;
   put32(fields, crc);
   put32(fields + 4, size);
   put32(fields + 8, size);
   if (write_exact(impl->file, fields, sizeof(fields)) != 0 ||
       fseek(impl->file, end, SEEK_SET) != 0)
      return -1;
   copy = malloc(strlen(name) + 1u);
   if (!copy)
      return -1;
   strcpy(copy, name);
   entry = &impl->entries[impl->count++];
   entry->name = copy;
   entry->crc32 = crc;
   entry->size = size;
   entry->local_offset = (uint32_t)header_offset;
   entry->dos_date = impl->pending_dos_date;
   entry->dos_time = impl->pending_dos_time;
   return 0;
}

int unifrog_zip_writer_open_path(const char *path,
   struct unifrog_zip_writer *writer)
{
   struct zip_writer_impl *impl;

   if (!path || !path[0] || !writer)
      return -1;
   memset(writer, 0, sizeof(*writer));
   impl = calloc(1, sizeof(*impl));
   if (!impl)
      return -1;
   impl->path = malloc(strlen(path) + 1u);
   if (!impl->path) {
      free(impl);
      return -1;
   }
   strcpy(impl->path, path);
   impl->file = fopen(path, "w+b");
   if (!impl->file) {
      free(impl->path);
      free(impl);
      return -1;
   }
   writer->impl = impl;
   return 0;
}

int unifrog_zip_writer_add_data(struct unifrog_zip_writer *writer,
   const char *name, const void *data, size_t size)
{
   struct zip_writer_impl *impl = writer ? writer->impl : NULL;
   uint32_t crc;
   long offset;

   if (!impl || (!data && size) || size > UINT32_MAX ||
       writer_begin_entry(impl, name, &offset) != 0)
      return -1;
   crc = (uint32_t)crc32(crc32(0L, Z_NULL, 0), data, (uInt)size);
   if (write_exact(impl->file, data, size) != 0)
      return -1;
   return writer_finish_entry(impl, name, offset, crc, (uint32_t)size);
}

int unifrog_zip_writer_add_path(struct unifrog_zip_writer *writer,
   const char *name, const char *path, size_t maximum_size)
{
   struct zip_writer_impl *impl = writer ? writer->impl : NULL;
   uint8_t buffer[ZIP_IO_BYTES];
   struct stat st;
   FILE *input;
   uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
   uint32_t total = 0;
   long offset;
   int ret = -1;

   if (!impl || !path || stat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
       st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX ||
       (maximum_size && (uint64_t)st.st_size > maximum_size) ||
       writer_begin_entry(impl, name, &offset) != 0)
      return -1;
   input = fopen(path, "rb");
   if (!input)
      return -1;
   for (;;) {
      size_t got = fread(buffer, 1, sizeof(buffer), input);

      if (got) {
         crc = (uint32_t)crc32(crc, buffer, (uInt)got);
         if (write_exact(impl->file, buffer, got) != 0)
            goto out;
         total += (uint32_t)got;
      }
      if (got < sizeof(buffer)) {
         if (ferror(input))
            goto out;
         break;
      }
   }
   ret = writer_finish_entry(impl, name, offset, crc, total);
out:
   fclose(input);
   return ret;
}

static void writer_free(struct zip_writer_impl *impl)
{
   if (!impl)
      return;
   for (unsigned i = 0; i < impl->count; i++)
      free(impl->entries[i].name);
   free(impl->entries);
   free(impl->path);
   free(impl);
}

int unifrog_zip_writer_close(struct unifrog_zip_writer *writer)
{
   struct zip_writer_impl *impl = writer ? writer->impl : NULL;
   uint8_t central[46];
   uint8_t end[22];
   long central_offset;
   long final_offset;
   int ret = -1;

   if (!impl)
      return -1;
   central_offset = ftell(impl->file);
   if (central_offset < 0 || (unsigned long)central_offset > UINT32_MAX)
      goto out;
   for (unsigned i = 0; i < impl->count; i++) {
      struct zip_writer_entry *entry = &impl->entries[i];
      size_t name_len = strlen(entry->name);

      memset(central, 0, sizeof(central));
      put32(central, ZIP_CENTRAL_SIGNATURE);
      put16(central + 4, ZIP_VERSION);
      put16(central + 6, ZIP_VERSION);
      put16(central + 12, entry->dos_time);
      put16(central + 14, entry->dos_date);
      put32(central + 16, entry->crc32);
      put32(central + 20, entry->size);
      put32(central + 24, entry->size);
      put16(central + 28, (uint16_t)name_len);
      put32(central + 42, entry->local_offset);
      if (write_exact(impl->file, central, sizeof(central)) != 0 ||
          write_exact(impl->file, entry->name, name_len) != 0)
         goto out;
   }
   final_offset = ftell(impl->file);
   if (final_offset < central_offset || (unsigned long)final_offset > UINT32_MAX)
      goto out;
   memset(end, 0, sizeof(end));
   put32(end, ZIP_END_SIGNATURE);
   put16(end + 8, (uint16_t)impl->count);
   put16(end + 10, (uint16_t)impl->count);
   put32(end + 12, (uint32_t)(final_offset - central_offset));
   put32(end + 16, (uint32_t)central_offset);
   if (write_exact(impl->file, end, sizeof(end)) != 0 ||
       fflush(impl->file) != 0 || fclose(impl->file) != 0) {
      impl->file = NULL;
      goto out;
   }
   impl->file = NULL;
   ret = 0;
out:
   if (impl->file)
      fclose(impl->file);
   if (ret != 0)
      remove(impl->path);
   writer_free(impl);
   writer->impl = NULL;
   return ret;
}

void unifrog_zip_writer_abort(struct unifrog_zip_writer *writer)
{
   struct zip_writer_impl *impl = writer ? writer->impl : NULL;

   if (!impl)
      return;
   if (impl->file)
      fclose(impl->file);
   remove(impl->path);
   writer_free(impl);
   writer->impl = NULL;
}
