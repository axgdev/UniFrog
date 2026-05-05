#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#define LOGO_WIDTH 320u
#define LOGO_HEIGHT 240u
#define FONT_WIDTH 5u
#define FONT_HEIGHT 7u
#define FONT_SCALE 2u
#define GLYPH_STEP ((FONT_WIDTH + 1u) * FONT_SCALE)
#define VERSION_MAX 48u
#define PNG_MAX_FILE_BYTES (256u * 1024u)
#define PNG_MAX_IDAT_BYTES (256u * 1024u)

struct rgb {
   uint8_t r;
   uint8_t g;
   uint8_t b;
};

static uint32_t be32(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int read_file(const char *path, uint8_t **out, size_t *out_size)
{
   FILE *file;
   long size;
   uint8_t *data;

   file = fopen(path, "rb");
   if (!file) {
      fprintf(stderr, "open %s: %s\n", path, strerror(errno));
      return -1;
   }
   if (fseek(file, 0, SEEK_END) != 0) {
      fclose(file);
      return -1;
   }
   size = ftell(file);
   if (size <= 0 || size > (long)PNG_MAX_FILE_BYTES) {
      fprintf(stderr, "%s: invalid size\n", path);
      fclose(file);
      return -1;
   }
   if (fseek(file, 0, SEEK_SET) != 0) {
      fclose(file);
      return -1;
   }
   data = malloc((size_t)size);
   if (!data) {
      fclose(file);
      return -1;
   }
   if (fread(data, 1, (size_t)size, file) != (size_t)size) {
      fprintf(stderr, "read %s: truncated\n", path);
      free(data);
      fclose(file);
      return -1;
   }
   fclose(file);
   *out = data;
   *out_size = (size_t)size;
   return 0;
}

static int append_bytes(uint8_t **data, size_t *size, const uint8_t *chunk,
   size_t chunk_size)
{
   uint8_t *next;

   if (chunk_size == 0)
      return 0;
   if (chunk_size > PNG_MAX_IDAT_BYTES || *size > PNG_MAX_IDAT_BYTES - chunk_size)
      return -1;
   next = realloc(*data, *size + chunk_size);
   if (!next)
      return -1;
   memcpy(next + *size, chunk, chunk_size);
   *data = next;
   *size += chunk_size;
   return 0;
}

static const uint8_t font_digits[10][7] = {
   {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
   {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
   {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
   {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
   {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
   {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e},
   {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
   {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
   {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
   {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
};

static const uint8_t font_upper[26][7] = {
   {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
   {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
   {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
   {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
   {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
   {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
   {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f},
   {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
   {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
   {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c},
   {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
   {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
   {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
   {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
   {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
   {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
   {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
   {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
   {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
   {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
   {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
   {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
   {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a},
   {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
   {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
   {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
};

static const uint8_t font_dash[7] = {0, 0, 0, 0x1f, 0, 0, 0};
static const uint8_t font_dot[7] = {0, 0, 0, 0, 0, 0x0c, 0x0c};
static const uint8_t font_colon[7] = {0, 0x0c, 0x0c, 0, 0x0c, 0x0c, 0};
static const uint8_t font_plus[7] = {0, 0x04, 0x04, 0x1f, 0x04, 0x04, 0};
static const uint8_t font_slash[7] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
static const uint8_t font_unknown[7] = {0x1f, 0x11, 0x02, 0x04, 0x04, 0, 0x04};

static const uint8_t *glyph_for_char(int ch)
{
   if (ch >= '0' && ch <= '9')
      return font_digits[ch - '0'];
   if (ch >= 'a' && ch <= 'z')
      ch = toupper(ch);
   if (ch >= 'A' && ch <= 'Z')
      return font_upper[ch - 'A'];
   if (ch == '-')
      return font_dash;
   if (ch == '.')
      return font_dot;
   if (ch == ':')
      return font_colon;
   if (ch == '+')
      return font_plus;
   if (ch == '/')
      return font_slash;
   if (ch == '_' || ch == ' ')
      return NULL;
   return font_unknown;
}

static int read_token(FILE *file, char *buf, size_t size)
{
   int ch;
   size_t used = 0;

   do {
      ch = fgetc(file);
      if (ch == '#') {
         do {
            ch = fgetc(file);
         } while (ch != EOF && ch != '\n');
      }
   } while (ch != EOF && isspace(ch));

   if (ch == EOF)
      return -1;
   while (ch != EOF && !isspace(ch)) {
      if (used + 1u >= size)
         return -1;
      buf[used++] = (char)ch;
      ch = fgetc(file);
   }
   buf[used] = '\0';
   return 0;
}

static int read_ppm(const char *path, struct rgb *pixels)
{
   FILE *file;
   char token[32];
   long width;
   long height;
   long maxval;
   size_t count = LOGO_WIDTH * LOGO_HEIGHT;

   file = fopen(path, "rb");
   if (!file) {
      fprintf(stderr, "open %s: %s\n", path, strerror(errno));
      return -1;
   }
   if (read_token(file, token, sizeof(token)) != 0 || strcmp(token, "P6") != 0 ||
       read_token(file, token, sizeof(token)) != 0) {
      fclose(file);
      return -1;
   }
   width = strtol(token, NULL, 10);
   if (read_token(file, token, sizeof(token)) != 0) {
      fclose(file);
      return -1;
   }
   height = strtol(token, NULL, 10);
   if (read_token(file, token, sizeof(token)) != 0) {
      fclose(file);
      return -1;
   }
   maxval = strtol(token, NULL, 10);
   if (width != (long)LOGO_WIDTH || height != (long)LOGO_HEIGHT || maxval != 255) {
      fprintf(stderr, "%s: expected P6 %ux%u maxval 255\n",
         path, LOGO_WIDTH, LOGO_HEIGHT);
      fclose(file);
      return -1;
   }
   if (fread(pixels, sizeof(*pixels), count, file) != count) {
      fprintf(stderr, "read %s: truncated\n", path);
      fclose(file);
      return -1;
   }
   fclose(file);
   return 0;
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c)
{
   int p = (int)a + (int)b - (int)c;
   int pa = abs(p - (int)a);
   int pb = abs(p - (int)b);
   int pc = abs(p - (int)c);

   if (pa <= pb && pa <= pc)
      return a;
   if (pb <= pc)
      return b;
   return c;
}

static int unfilter_png(uint8_t *dst, const uint8_t *src,
   unsigned width, unsigned height, unsigned channels)
{
   size_t row_bytes = (size_t)width * channels;
   const uint8_t *in = src;

   for (unsigned y = 0; y < height; y++) {
      unsigned filter = *in++;
      uint8_t *row = dst + (size_t)y * row_bytes;
      const uint8_t *prev = y ? row - row_bytes : NULL;

      memcpy(row, in, row_bytes);
      in += row_bytes;
      for (size_t x = 0; x < row_bytes; x++) {
         uint8_t left = x >= channels ? row[x - channels] : 0;
         uint8_t up = prev ? prev[x] : 0;
         uint8_t up_left = prev && x >= channels ? prev[x - channels] : 0;

         if (filter == 1)
            row[x] = (uint8_t)(row[x] + left);
         else if (filter == 2)
            row[x] = (uint8_t)(row[x] + up);
         else if (filter == 3)
            row[x] = (uint8_t)(row[x] + (uint8_t)(((unsigned)left + up) >> 1));
         else if (filter == 4)
            row[x] = (uint8_t)(row[x] + paeth(left, up, up_left));
         else if (filter != 0)
            return -1;
      }
   }
   return 0;
}

static int read_png(const char *path, struct rgb *pixels)
{
   static const uint8_t signature[8] = {
      0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
   };
   uint8_t *file_data = NULL;
   uint8_t *idat = NULL;
   uint8_t *inflated = NULL;
   uint8_t *rows = NULL;
   struct rgb palette[256];
   uint8_t alpha[256];
   size_t file_size = 0;
   size_t idat_size = 0;
   size_t pos = 8;
   unsigned width = 0;
   unsigned height = 0;
   unsigned channels = 0;
   unsigned color_type = 0;
   unsigned palette_entries = 0;
   int ret = -1;

   memset(palette, 0, sizeof(palette));
   memset(alpha, 255, sizeof(alpha));
   if (read_file(path, &file_data, &file_size) != 0)
      return -1;
   if (file_size < 33 || memcmp(file_data, signature, sizeof(signature)) != 0)
      goto out;

   while (pos + 12 <= file_size) {
      uint32_t len = be32(file_data + pos);
      const uint8_t *type = file_data + pos + 4;
      const uint8_t *chunk = file_data + pos + 8;

      pos += 8;
      if (len > file_size - pos)
         goto out;
      if (memcmp(type, "IHDR", 4) == 0) {
         unsigned bit_depth;

         if (len != 13)
            goto out;
         width = be32(chunk);
         height = be32(chunk + 4);
         bit_depth = chunk[8];
         color_type = chunk[9];
         if (width != LOGO_WIDTH || height != LOGO_HEIGHT ||
             bit_depth != 8 || chunk[10] != 0 || chunk[11] != 0 ||
             chunk[12] != 0)
            goto out;
         if (color_type == 3)
            channels = 1;
         else if (color_type == 2)
            channels = 3;
         else if (color_type == 6)
            channels = 4;
         else
            goto out;
      } else if (memcmp(type, "PLTE", 4) == 0) {
         if (len % 3 != 0 || len / 3 > 256)
            goto out;
         palette_entries = len / 3;
         for (unsigned i = 0; i < palette_entries; i++) {
            palette[i].r = chunk[i * 3u];
            palette[i].g = chunk[i * 3u + 1u];
            palette[i].b = chunk[i * 3u + 2u];
         }
      } else if (memcmp(type, "tRNS", 4) == 0) {
         if (len > 256)
            goto out;
         memcpy(alpha, chunk, len);
      } else if (memcmp(type, "IDAT", 4) == 0) {
         if (append_bytes(&idat, &idat_size, chunk, len) != 0)
            goto out;
      } else if (memcmp(type, "IEND", 4) == 0) {
         break;
      }
      pos += len + 4;
   }

   if (!width || !height || !channels || !idat_size ||
       (color_type == 3 && palette_entries == 0))
      goto out;

   {
      size_t row_bytes = (size_t)width * channels;
      uLongf inflated_size = (uLongf)((row_bytes + 1u) * height);

      inflated = malloc((size_t)inflated_size);
      rows = malloc(row_bytes * height);
      if (!inflated || !rows)
         goto out;
      if (uncompress(inflated, &inflated_size, idat, (uLong)idat_size) != Z_OK ||
          inflated_size != (uLongf)((row_bytes + 1u) * height))
         goto out;
      if (unfilter_png(rows, inflated, width, height, channels) != 0)
         goto out;
      for (unsigned y = 0; y < height; y++) {
         for (unsigned x = 0; x < width; x++) {
            size_t src = ((size_t)y * width + x) * channels;
            size_t dst = (size_t)y * width + x;

            if (color_type == 3) {
               unsigned idx = rows[src];
               struct rgb px;
               unsigned a;

               if (idx >= palette_entries)
                  goto out;
               px = palette[idx];
               a = alpha[idx];
               pixels[dst].r = (uint8_t)((px.r * a + 127u) / 255u);
               pixels[dst].g = (uint8_t)((px.g * a + 127u) / 255u);
               pixels[dst].b = (uint8_t)((px.b * a + 127u) / 255u);
            } else {
               pixels[dst].r = rows[src];
               pixels[dst].g = rows[src + 1u];
               pixels[dst].b = rows[src + 2u];
            }
         }
      }
      ret = 0;
   }

out:
   if (ret != 0)
      fprintf(stderr, "%s: expected 320x240 8-bit PNG or P6 PPM\n", path);
   free(rows);
   free(inflated);
   free(idat);
   free(file_data);
   return ret;
}

static int read_logo(const char *path, struct rgb *pixels)
{
   size_t len = strlen(path);

   if (len >= 4 && strcmp(path + len - 4, ".png") == 0)
      return read_png(path, pixels);
   return read_ppm(path, pixels);
}

static int write_ppm(const char *path, const struct rgb *pixels)
{
   FILE *file = fopen(path, "wb");

   if (!file) {
      fprintf(stderr, "open %s: %s\n", path, strerror(errno));
      return -1;
   }
   fprintf(file, "P6\n%u %u\n255\n", LOGO_WIDTH, LOGO_HEIGHT);
   if (fwrite(pixels, sizeof(*pixels), LOGO_WIDTH * LOGO_HEIGHT, file) !=
       LOGO_WIDTH * LOGO_HEIGHT) {
      fprintf(stderr, "write %s: %s\n", path, strerror(errno));
      fclose(file);
      return -1;
   }
   if (fclose(file) != 0) {
      fprintf(stderr, "close %s: %s\n", path, strerror(errno));
      return -1;
   }
   return 0;
}

static void set_pixel(struct rgb *pixels, unsigned x, unsigned y,
   struct rgb color)
{
   if (x >= LOGO_WIDTH || y >= LOGO_HEIGHT)
      return;
   pixels[y * LOGO_WIDTH + x] = color;
}

static void fill_rect(struct rgb *pixels, unsigned x, unsigned y,
   unsigned w, unsigned h, struct rgb color)
{
   for (unsigned yy = 0; yy < h; yy++)
      for (unsigned xx = 0; xx < w; xx++)
         set_pixel(pixels, x + xx, y + yy, color);
}

static void draw_char(struct rgb *pixels, unsigned x, unsigned y, int ch,
   struct rgb color)
{
   const uint8_t *glyph = glyph_for_char(ch);

   if (!glyph)
      return;
   for (unsigned row = 0; row < FONT_HEIGHT; row++) {
      for (unsigned col = 0; col < FONT_WIDTH; col++) {
         if ((glyph[row] & (1u << (FONT_WIDTH - 1u - col))) == 0)
            continue;
         fill_rect(pixels, x + col * FONT_SCALE, y + row * FONT_SCALE,
            FONT_SCALE, FONT_SCALE, color);
      }
   }
}

static void draw_version(struct rgb *pixels, const char *version)
{
   struct rgb bg = {0, 0, 0};
   struct rgb fg = {226, 236, 222};
   char text[VERSION_MAX + 1u];
   size_t len;
   unsigned text_w;
   unsigned x;
   unsigned y;

   snprintf(text, sizeof(text), "%s", version && version[0] ? version : "unknown");
   len = strlen(text);
   if (len > VERSION_MAX) {
      memmove(text + 1, text + len - (VERSION_MAX - 1u), VERSION_MAX - 1u);
      text[0] = '~';
      text[VERSION_MAX] = '\0';
      len = VERSION_MAX;
   }
   text_w = len ? (unsigned)(len - 1u) * GLYPH_STEP + FONT_WIDTH * FONT_SCALE : 0;
   x = text_w < LOGO_WIDTH ? (LOGO_WIDTH - text_w) / 2u : 0;
   y = LOGO_HEIGHT - 26u;
   fill_rect(pixels, 0, LOGO_HEIGHT - 31u, LOGO_WIDTH, 31u, bg);
   for (size_t i = 0; i < len; i++)
      draw_char(pixels, x + (unsigned)i * GLYPH_STEP, y, text[i], fg);
}

static uint16_t rgb565(struct rgb px)
{
   return (uint16_t)(((uint16_t)(px.r & 0xf8u) << 8) |
      ((uint16_t)(px.g & 0xfcu) << 3) | ((uint16_t)px.b >> 3));
}

static int write_rgb565_inc(const char *path, const struct rgb *pixels)
{
   FILE *file = fopen(path, "wb");
   unsigned run = 0;
   uint16_t prev = 0;
   unsigned words_on_line = 0;

   if (!file) {
      fprintf(stderr, "open %s: %s\n", path, strerror(errno));
      return -1;
   }
   fprintf(file, "static const uint16_t unifrog_boot_logo_rle[] = {\n");
   for (unsigned i = 0; i < LOGO_WIDTH * LOGO_HEIGHT; i++) {
      uint16_t color = rgb565(pixels[i]);

      if (run != 0 && color == prev && run < 65535u) {
         run++;
         continue;
      }
      if (run != 0) {
         fprintf(file, "0x%04x,%u,", prev, run);
         words_on_line += 2u;
         if (words_on_line >= 8u) {
            fputc('\n', file);
            words_on_line = 0;
         }
      }
      prev = color;
      run = 1;
   }
   if (run != 0)
      fprintf(file, "0x%04x,%u,", prev, run);
   fprintf(file, "\n};\n");
   fprintf(file, "#define UNIFROG_BOOT_LOGO_RLE_WORDS "
      "(sizeof(unifrog_boot_logo_rle) / sizeof(unifrog_boot_logo_rle[0]))\n");
   if (fclose(file) != 0) {
      fprintf(stderr, "close %s: %s\n", path, strerror(errno));
      return -1;
   }
   return 0;
}

int main(int argc, char **argv)
{
   struct rgb *pixels;
   int ret = 1;

   if (argc != 5) {
      fprintf(stderr, "usage: %s input.png|input.ppm version output.ppm output.inc\n",
         argv[0]);
      return 1;
   }
   pixels = calloc(LOGO_WIDTH * LOGO_HEIGHT, sizeof(*pixels));
   if (!pixels) {
      fprintf(stderr, "alloc logo: %s\n", strerror(errno));
      return 1;
   }
   if (read_logo(argv[1], pixels) == 0) {
      draw_version(pixels, argv[2]);
      if (write_ppm(argv[3], pixels) == 0 &&
          write_rgb565_inc(argv[4], pixels) == 0)
         ret = 0;
   }
   free(pixels);
   return ret;
}
