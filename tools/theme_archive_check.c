#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint16_t rd16(const uint8_t *p)
{
   return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int ends_with(const char *s, const char *suffix)
{
   size_t sl = strlen(s);
   size_t tl = strlen(suffix);

   return sl >= tl && strcmp(s + sl - tl, suffix) == 0;
}

static int safe_name(const char *name)
{
   return name && name[0] && name[0] != '/' && !strstr(name, "..") &&
      !strchr(name, '\\') && !strchr(name, ':');
}

static const char *archive_rel(const char *name)
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

static int preferred_resolution(const char *name)
{
   if (!name)
      return 0;
   return strncmp(name, "320x240/", 8) == 0 ||
      strncmp(name, "640x480/", 8) == 0;
}

static int needed_entry(const char *name)
{
   const char *rel;
   const char *launch_rel;

   if (!name || !name[0] || ends_with(name, "/"))
      return 0;
   rel = archive_rel(name);
   if (rel == name && strchr(rel, '/'))
      return 0;
   if (rel != name && !preferred_resolution(name) &&
       strncmp(rel, "scheme/", 7) != 0)
      return 0;
   if (strcmp(rel, "version.txt") == 0 || strcmp(rel, "credits.txt") == 0)
      return 1;
   if (strncmp(rel, "scheme/", 7) == 0 || strncmp(rel, "font/", 5) == 0 ||
       strncmp(rel, "glyph/", 6) == 0)
      return 1;
   if (strcmp(rel, "image/wall/default.png") == 0 ||
       strcmp(rel, "image/wall/muxlaunch.png") == 0)
      return 1;
   launch_rel = "image/wall/muxlaunch/";
   if (strncmp(rel, launch_rel, strlen(launch_rel)) == 0 &&
       ends_with(rel, ".png"))
      return 1;
   launch_rel = "image/static/muxlaunch/";
   if (strncmp(rel, launch_rel, strlen(launch_rel)) == 0 &&
       ends_with(rel, ".png"))
      return 1;
   return 0;
}

static int find_eocd(const uint8_t *data, size_t size, size_t *out)
{
   size_t min;
   size_t pos;

   if (!data || size < 22 || !out)
      return -1;
   min = size > 0x10015u ? size - 0x10015u : 0;
   pos = size - 22u;
   for (;;) {
      if (rd32(data + pos) == 0x06054b50u) {
         uint16_t comment = rd16(data + pos + 20);

         if (pos + 22u + comment == size) {
            *out = pos;
            return 0;
         }
      }
      if (pos == min)
         break;
      pos--;
   }
   return -1;
}

static int check_entry_data(const uint8_t *zip, size_t zip_size,
   uint32_t local_offset, uint16_t method, uint32_t compressed_size,
   uint32_t uncompressed_size)
{
   uint16_t name_len;
   uint16_t extra_len;
   size_t data_offset;

   if ((size_t)local_offset + 30u > zip_size ||
       rd32(zip + local_offset) != 0x04034b50u)
      return -1;
   name_len = rd16(zip + local_offset + 26);
   extra_len = rd16(zip + local_offset + 28);
   data_offset = (size_t)local_offset + 30u + name_len + extra_len;
   if (data_offset > zip_size || compressed_size > zip_size - data_offset)
      return -1;
   if (method == 0)
      return compressed_size == uncompressed_size ? 0 : -1;
   if (method == 8) {
      z_stream stream;
      uint8_t out[4096];
      size_t produced = 0;
      int zret;

      memset(&stream, 0, sizeof(stream));
      if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
         return -1;
      stream.next_in = (Bytef *)(zip + data_offset);
      stream.avail_in = compressed_size;
      do {
         stream.next_out = out;
         stream.avail_out = sizeof(out);
         zret = inflate(&stream, Z_NO_FLUSH);
         if (zret != Z_OK && zret != Z_STREAM_END) {
            (void)inflateEnd(&stream);
            return -1;
         }
         produced += sizeof(out) - stream.avail_out;
      } while (zret != Z_STREAM_END &&
         (stream.avail_in > 0 || stream.avail_out == 0));
      while (zret != Z_STREAM_END) {
         size_t chunk;

         stream.next_out = out;
         stream.avail_out = sizeof(out);
         zret = inflate(&stream, Z_FINISH);
         if (zret != Z_OK && zret != Z_STREAM_END) {
            (void)inflateEnd(&stream);
            return -1;
         }
         chunk = sizeof(out) - stream.avail_out;
         produced += chunk;
         if (chunk == 0 && zret != Z_STREAM_END) {
            (void)inflateEnd(&stream);
            return -1;
         }
      }
      (void)inflateEnd(&stream);
      return zret == Z_STREAM_END && produced == uncompressed_size ? 0 : -1;
   }
   return -1;
}

int main(int argc, char **argv)
{
   FILE *file;
   uint8_t *data;
   long len;
   size_t eocd;
   size_t pos;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries;
   unsigned needed = 0;
   unsigned scheme = 0;
   unsigned wall = 0;
   unsigned bad = 0;

   if (argc != 2) {
      fprintf(stderr, "usage: %s THEME.muxthm\n", argv[0]);
      return 2;
   }
   file = fopen(argv[1], "rb");
   if (!file)
      return 1;
   if (fseek(file, 0, SEEK_END) != 0 || (len = ftell(file)) < 22 ||
       fseek(file, 0, SEEK_SET) != 0) {
      fclose(file);
      return 1;
   }
   data = malloc((size_t)len);
   if (!data) {
      fclose(file);
      return 1;
   }
   if (fread(data, 1, (size_t)len, file) != (size_t)len) {
      free(data);
      fclose(file);
      return 1;
   }
   fclose(file);
   if (find_eocd(data, (size_t)len, &eocd) != 0) {
      fprintf(stderr, "missing ZIP central directory\n");
      free(data);
      return 1;
   }
   entries = rd16(data + eocd + 10);
   cd_size = rd32(data + eocd + 12);
   cd_offset = rd32(data + eocd + 16);
   if ((size_t)cd_offset + cd_size > (size_t)len) {
      fprintf(stderr, "bad ZIP central directory bounds\n");
      free(data);
      return 1;
   }
   pos = cd_offset;
   for (uint16_t i = 0; i < entries; i++) {
      char name[256];
      uint16_t flags;
      uint16_t method;
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      uint32_t compressed_size;
      uint32_t uncompressed_size;
      uint32_t local_offset;
      size_t copy;

      if (pos + 46u > (size_t)len || rd32(data + pos) != 0x02014b50u) {
         fprintf(stderr, "bad central entry %u\n", (unsigned)i);
         free(data);
         return 1;
      }
      flags = rd16(data + pos + 8);
      method = rd16(data + pos + 10);
      compressed_size = rd32(data + pos + 20);
      uncompressed_size = rd32(data + pos + 24);
      name_len = rd16(data + pos + 28);
      extra_len = rd16(data + pos + 30);
      comment_len = rd16(data + pos + 32);
      local_offset = rd32(data + pos + 42);
      pos += 46u;
      if (pos + name_len + extra_len + comment_len > (size_t)len) {
         fprintf(stderr, "bad central entry length %u\n", (unsigned)i);
         free(data);
         return 1;
      }
      copy = name_len < sizeof(name) - 1u ? name_len : sizeof(name) - 1u;
      memcpy(name, data + pos, copy);
      name[copy] = '\0';
      if (!safe_name(name) || (flags & 1u) || (method != 0 && method != 8))
         bad++;
      if (needed_entry(name)) {
         needed++;
         if (check_entry_data(data, (size_t)len, local_offset, method,
             compressed_size, uncompressed_size) != 0)
            bad++;
         if (strstr(name, "/scheme/") ||
             strncmp(name, "scheme/", 7) == 0)
            scheme++;
         if (strstr(name, "/image/wall/") ||
             strncmp(name, "image/wall/", 11) == 0 ||
             strstr(name, "/image/static/muxlaunch/") ||
             strncmp(name, "image/static/muxlaunch/", 23) == 0)
            wall++;
      }
      pos += name_len + extra_len + comment_len;
   }
   printf("muxthm entries=%u needed=%u scheme=%u wall=%u bad=%u\n",
      entries, needed, scheme, wall, bad);
   free(data);
   return needed > 0 && (scheme > 0 || wall > 0) && bad == 0 ? 0 : 1;
}
