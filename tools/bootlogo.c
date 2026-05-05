#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGO_WIDTH 320u
#define LOGO_HEIGHT 240u
#define FONT_WIDTH 5u
#define FONT_HEIGHT 7u
#define FONT_SCALE 2u
#define GLYPH_STEP ((FONT_WIDTH + 1u) * FONT_SCALE)
#define VERSION_MAX 48u

struct rgb {
   uint8_t r;
   uint8_t g;
   uint8_t b;
};

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
      fprintf(stderr, "usage: %s input.ppm version output.ppm output.inc\n",
         argv[0]);
      return 1;
   }
   pixels = calloc(LOGO_WIDTH * LOGO_HEIGHT, sizeof(*pixels));
   if (!pixels) {
      fprintf(stderr, "alloc logo: %s\n", strerror(errno));
      return 1;
   }
   if (read_ppm(argv[1], pixels) == 0) {
      draw_version(pixels, argv[2]);
      if (write_ppm(argv[3], pixels) == 0 &&
          write_rgb565_inc(argv[4], pixels) == 0)
         ret = 0;
   }
   free(pixels);
   return ret;
}
