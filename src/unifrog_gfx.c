#include <unifrog/gfx.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <unifrog/text.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

#define TTF_MAX_FONTS 8
#define TTF_GLYPH_CACHE 160
#define TTF_PIXEL_HEIGHT 12.0f
#define TTF_ALPHA_CUTOFF 40u
#define TTF_ALPHA_SOLID 176u
#define TTF_MAX_BYTES (24u * 1024u * 1024u)
#define TTF_PATH_LIST_MAX 1024
#define TTF_PATH_MAX 512

struct font5x7_glyph {
   char c;
   uint8_t cols[5];
};

static const uint8_t font5x7_alnum[36][5] = {
   {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
   {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
   {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
   {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
   {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
   {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
   {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
   {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
   {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
   {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
   {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
   {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
   {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
   {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
   {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
   {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
   {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
   {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
};

static const struct font5x7_glyph font5x7_punctuation[] = {
   {'!', {0x00,0x00,0x5f,0x00,0x00}},
   {'"', {0x00,0x07,0x00,0x07,0x00}},
   {'#', {0x14,0x7f,0x14,0x7f,0x14}},
   {'$', {0x24,0x2a,0x7f,0x2a,0x12}},
   {'%', {0x23,0x13,0x08,0x64,0x62}},
   {'&', {0x36,0x49,0x55,0x22,0x50}},
   {'\'', {0x00,0x05,0x03,0x00,0x00}},
   {'(', {0x00,0x1c,0x22,0x41,0x00}},
   {')', {0x00,0x41,0x22,0x1c,0x00}},
   {'*', {0x14,0x08,0x3e,0x08,0x14}},
   {'+', {0x08,0x08,0x3e,0x08,0x08}},
   {',', {0x00,0x50,0x30,0x00,0x00}},
   {'-', {0x08,0x08,0x08,0x08,0x08}},
   {'.', {0x00,0x60,0x60,0x00,0x00}},
   {'/', {0x20,0x10,0x08,0x04,0x02}},
   {':', {0x00,0x36,0x36,0x00,0x00}},
   {';', {0x00,0x56,0x36,0x00,0x00}},
   {'<', {0x08,0x14,0x22,0x41,0x00}},
   {'=', {0x14,0x14,0x14,0x14,0x14}},
   {'>', {0x00,0x41,0x22,0x14,0x08}},
   {'?', {0x02,0x01,0x51,0x09,0x06}},
   {'@', {0x32,0x49,0x79,0x41,0x3e}},
   {'[', {0x00,0x7f,0x41,0x41,0x00}},
   {'\\', {0x02,0x04,0x08,0x10,0x20}},
   {']', {0x00,0x41,0x41,0x7f,0x00}},
   {'^', {0x04,0x02,0x01,0x02,0x04}},
   {'_', {0x40,0x40,0x40,0x40,0x40}},
   {'`', {0x00,0x01,0x02,0x04,0x00}},
   {'{', {0x00,0x08,0x36,0x41,0x00}},
   {'|', {0x00,0x00,0x7f,0x00,0x00}},
   {'}', {0x00,0x41,0x36,0x08,0x00}},
   {'~', {0x08,0x04,0x08,0x10,0x08}},
};

static uint8_t font5x7_custom[95][5];
static uint8_t font5x7_custom_valid[95];
struct ttf_loaded_font {
   uint8_t *data;
   size_t size;
   stbtt_fontinfo info;
   float scale;
   int ascent;
   int descent;
   int line_gap;
};

struct ttf_cached_glyph {
   uint32_t codepoint;
   unsigned font_index;
   int glyph_index;
   int width;
   int height;
   int xoff;
   int yoff;
   int advance;
   int lsb;
   uint8_t *bitmap;
   uint32_t age;
};

static struct ttf_loaded_font ttf_fonts[TTF_MAX_FONTS];
static unsigned ttf_font_count;
static struct ttf_cached_glyph ttf_cache[TTF_GLYPH_CACHE];
static uint32_t ttf_cache_age;
static float ttf_pixel_height = TTF_PIXEL_HEIGHT;
static int ttf_active;
static int ttf_baseline_offset;

static int font_space(char c)
{
   return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static const uint8_t *glyph_for(char c)
{
   size_t i;
   unsigned char uc = (unsigned char)c;

   if (uc >= 32 && uc <= 126 &&
       font5x7_custom_valid[uc - 32])
      return font5x7_custom[uc - 32];

   if (c >= '0' && c <= '9')
      return font5x7_alnum[c - '0'];
   if (c >= 'A' && c <= 'Z')
      return font5x7_alnum[10 + (c - 'A')];
   if (c >= 'a' && c <= 'z')
      return font5x7_alnum[10 + (c - 'a')];

   for (i = 0; i < sizeof(font5x7_punctuation) / sizeof(font5x7_punctuation[0]); i++) {
      if (font5x7_punctuation[i].c == c)
         return font5x7_punctuation[i].cols;
   }

   return NULL;
}

static int surface_is_valid(const struct unifrog_surface *surface)
{
   return surface && surface->pixels &&
      surface->width > 0 && surface->height > 0 &&
      surface->stride >= surface->width;
}

static void ttf_clear(void)
{
   for (unsigned i = 0; i < ttf_font_count; i++) {
      free(ttf_fonts[i].data);
      ttf_fonts[i].data = NULL;
   }
   for (unsigned i = 0; i < TTF_GLYPH_CACHE; i++) {
      stbtt_FreeBitmap(ttf_cache[i].bitmap, NULL);
      ttf_cache[i].bitmap = NULL;
      ttf_cache[i].codepoint = 0;
      ttf_cache[i].glyph_index = 0;
   }
   memset(ttf_fonts, 0, sizeof(ttf_fonts));
   memset(ttf_cache, 0, sizeof(ttf_cache));
   ttf_font_count = 0;
   ttf_cache_age = 0;
   ttf_active = 0;
   ttf_baseline_offset = 0;
}

static uint16_t blend_rgb565(uint16_t dst, uint16_t src, unsigned alpha)
{
   unsigned inv = 255u - alpha;
   unsigned sr = (src >> 11) & 0x1fu;
   unsigned sg = (src >> 5) & 0x3fu;
   unsigned sb = src & 0x1fu;
   unsigned dr = (dst >> 11) & 0x1fu;
   unsigned dg = (dst >> 5) & 0x3fu;
   unsigned db = dst & 0x1fu;
   unsigned r;
   unsigned g;
   unsigned b;

   if (alpha >= 255u)
      return src;
   if (alpha == 0)
      return dst;
   r = (sr * alpha + dr * inv + 127u) / 255u;
   g = (sg * alpha + dg * inv + 127u) / 255u;
   b = (sb * alpha + db * inv + 127u) / 255u;
   return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint32_t utf8_next_codepoint(const char **text)
{
   const unsigned char *p = (const unsigned char *)*text;
   uint32_t cp;

   if (!p || !*p)
      return 0;
   if (p[0] < 0x80) {
      *text += 1;
      return p[0];
   }
   if ((p[0] & 0xe0u) == 0xc0u && (p[1] & 0xc0u) == 0x80u) {
      cp = ((uint32_t)(p[0] & 0x1fu) << 6) |
         (uint32_t)(p[1] & 0x3fu);
      *text += 2;
      return cp >= 0x80u ? cp : '?';
   }
   if ((p[0] & 0xf0u) == 0xe0u && (p[1] & 0xc0u) == 0x80u &&
       (p[2] & 0xc0u) == 0x80u) {
      cp = ((uint32_t)(p[0] & 0x0fu) << 12) |
         ((uint32_t)(p[1] & 0x3fu) << 6) |
         (uint32_t)(p[2] & 0x3fu);
      *text += 3;
      return cp >= 0x800u ? cp : '?';
   }
   if ((p[0] & 0xf8u) == 0xf0u && (p[1] & 0xc0u) == 0x80u &&
       (p[2] & 0xc0u) == 0x80u && (p[3] & 0xc0u) == 0x80u) {
      cp = ((uint32_t)(p[0] & 0x07u) << 18) |
         ((uint32_t)(p[1] & 0x3fu) << 12) |
         ((uint32_t)(p[2] & 0x3fu) << 6) |
         (uint32_t)(p[3] & 0x3fu);
      *text += 4;
      return cp >= 0x10000u && cp <= 0x10ffffu ? cp : '?';
   }
   *text += 1;
   return '?';
}

static struct ttf_cached_glyph *ttf_find_cache(uint32_t codepoint,
   uint32_t next_codepoint, int *out_kern)
{
   struct ttf_cached_glyph *slot = NULL;
   uint32_t oldest_age = UINT32_MAX;
   unsigned oldest = 0;

   if (out_kern)
      *out_kern = 0;
   for (unsigned i = 0; i < TTF_GLYPH_CACHE; i++) {
      if (ttf_cache[i].glyph_index &&
          ttf_cache[i].codepoint == codepoint) {
         ttf_cache[i].age = ++ttf_cache_age;
         if (out_kern && next_codepoint) {
            struct ttf_loaded_font *font =
               &ttf_fonts[ttf_cache[i].font_index];
            int next_glyph = stbtt_FindGlyphIndex(&font->info,
               (int)next_codepoint);

            if (next_glyph)
               *out_kern = (int)(font->scale *
                  (float)stbtt_GetGlyphKernAdvance(&font->info,
                     ttf_cache[i].glyph_index, next_glyph));
         }
         return &ttf_cache[i];
      }
      if (!ttf_cache[i].glyph_index)
         slot = &ttf_cache[i];
      if (ttf_cache[i].age < oldest_age) {
         oldest_age = ttf_cache[i].age;
         oldest = i;
      }
   }
   if (!slot)
      slot = &ttf_cache[oldest];
   stbtt_FreeBitmap(slot->bitmap, NULL);
   memset(slot, 0, sizeof(*slot));
   return slot;
}

static struct ttf_cached_glyph *ttf_load_glyph(uint32_t codepoint,
   uint32_t next_codepoint, int *out_kern)
{
   struct ttf_cached_glyph *slot = ttf_find_cache(codepoint, next_codepoint,
      out_kern);

   if (!slot || slot->glyph_index)
      return slot;
   for (unsigned i = 0; i < ttf_font_count; i++) {
      struct ttf_loaded_font *font = &ttf_fonts[i];
      int glyph = stbtt_FindGlyphIndex(&font->info, (int)codepoint);
      int advance = 0;
      int lsb = 0;

      if (!glyph)
         continue;
      slot->bitmap = stbtt_GetGlyphBitmap(&font->info, font->scale,
         font->scale, glyph, &slot->width, &slot->height, &slot->xoff,
         &slot->yoff);
      stbtt_GetGlyphHMetrics(&font->info, glyph, &advance, &lsb);
      slot->advance = (int)((float)advance * font->scale + 0.5f);
      slot->lsb = (int)((float)lsb * font->scale + 0.5f);
      slot->codepoint = codepoint;
      slot->font_index = i;
      slot->glyph_index = glyph;
      slot->age = ++ttf_cache_age;
      if (out_kern && next_codepoint) {
         int next_glyph = stbtt_FindGlyphIndex(&font->info,
            (int)next_codepoint);

         if (next_glyph)
            *out_kern = (int)(font->scale *
               (float)stbtt_GetGlyphKernAdvance(&font->info, glyph,
                  next_glyph));
      }
      return slot;
   }
   return NULL;
}

static void ttf_blit_glyph(const struct unifrog_surface *surface,
   int x, int y, const struct ttf_cached_glyph *glyph, uint16_t color)
{
   if (!glyph || !glyph->bitmap)
      return;
   for (int yy = 0; yy < glyph->height; yy++) {
      int dy = y + glyph->yoff + yy;
      const uint8_t *src_row;
      uint16_t *dst_row;

      if (dy < 0 || dy >= (int)surface->height)
         continue;
      src_row = glyph->bitmap + yy * glyph->width;
      dst_row = surface->pixels + (unsigned)dy * surface->stride;
      for (int xx = 0; xx < glyph->width; xx++) {
         int dx = x + glyph->xoff + xx;
         unsigned alpha;

         if (dx < 0 || dx >= (int)surface->width)
            continue;
         alpha = src_row[xx];
         if (alpha >= TTF_ALPHA_CUTOFF) {
            if (alpha >= TTF_ALPHA_SOLID)
               alpha = 255u;
            else
               alpha = ((alpha - TTF_ALPHA_CUTOFF) * 255u) /
                  (TTF_ALPHA_SOLID - TTF_ALPHA_CUTOFF);
            dst_row[dx] = blend_rgb565(dst_row[dx], color, alpha);
         }
      }
   }
}

static uint32_t utf8_peek_codepoint(const char *text)
{
   return utf8_next_codepoint(&text);
}

static void ttf_draw_text(const struct unifrog_surface *surface,
   int x, int y, const char *text, uint16_t color)
{
   int xpos = x;
   int baseline = y + ttf_baseline_offset;

   if (!surface_is_valid(surface) || !text)
      return;
   while (*text) {
      const char *next = text;
      uint32_t cp = utf8_next_codepoint(&next);
      uint32_t next_cp = utf8_peek_codepoint(next);
      int kern = 0;
      struct ttf_cached_glyph *glyph;

      if (cp == '\n') {
         xpos = x;
         baseline += (int)(ttf_pixel_height + 3.0f);
         text = next;
         continue;
      }
      glyph = ttf_load_glyph(cp, next_cp, &kern);
      if (!glyph && cp != '?')
         glyph = ttf_load_glyph('?', next_cp, &kern);
      if (glyph) {
         ttf_blit_glyph(surface, xpos, baseline, glyph, color);
         xpos += glyph->advance + kern;
      } else {
         xpos += 6;
      }
      text = next;
   }
}

void unifrog_gfx_put_pixel(const struct unifrog_surface *surface,
   int x, int y, uint16_t color)
{
   if (!surface_is_valid(surface))
      return;
   if (x < 0 || y < 0 || x >= (int)surface->width || y >= (int)surface->height)
      return;

   surface->pixels[(unsigned)y * surface->stride + (unsigned)x] = color;
}

void unifrog_gfx_fill_rect(const struct unifrog_surface *surface,
   int x, int y, int w, int h, uint16_t color)
{
   int x2;
   int y2;

   if (!surface_is_valid(surface) || w <= 0 || h <= 0)
      return;

   x2 = x + w;
   y2 = y + h;
   if (x < 0)
      x = 0;
   if (y < 0)
      y = 0;
   if (x2 > (int)surface->width)
      x2 = (int)surface->width;
   if (y2 > (int)surface->height)
      y2 = (int)surface->height;
   if (x >= x2 || y >= y2)
      return;

   for (int yy = y; yy < y2; yy++) {
      uint16_t *row = surface->pixels + (unsigned)yy * surface->stride;
      for (int xx = x; xx < x2; xx++)
         row[xx] = color;
   }
}

void unifrog_gfx_draw_hline(const struct unifrog_surface *surface,
   int x, int y, int w, uint16_t color)
{
   unifrog_gfx_fill_rect(surface, x, y, w, 1, color);
}

void unifrog_gfx_draw_vline(const struct unifrog_surface *surface,
   int x, int y, int h, uint16_t color)
{
   unifrog_gfx_fill_rect(surface, x, y, 1, h, color);
}

void unifrog_gfx_draw_char(const struct unifrog_surface *surface,
   int x, int y, char c, uint16_t color, int scale)
{
   const uint8_t *glyph = glyph_for(c);

   if (scale <= 0 || c == ' ')
      return;
   if (!glyph) {
      unifrog_gfx_fill_rect(surface, x + 1 * scale, y + 6 * scale,
         3 * scale, scale, color);
      return;
   }

   for (int col = 0; col < 5; col++) {
      uint8_t bits = glyph[col];
      for (int row = 0; row < 7; row++) {
         if (bits & (1 << row)) {
            unifrog_gfx_fill_rect(surface, x + col * scale, y + row * scale,
               scale, scale, color);
         }
      }
   }
}

void unifrog_gfx_draw_text(const struct unifrog_surface *surface,
   int x, int y, const char *text, uint16_t color, int scale)
{
   if (!text || scale <= 0)
      return;
   if (ttf_active && scale == 1) {
      ttf_draw_text(surface, x, y, text, color);
      return;
   }

   while (*text) {
      unifrog_gfx_draw_char(surface, x, y, *text, color, scale);
      x += 6 * scale;
      text++;
   }
}

static int parse_font_line(const char *line, unsigned char *out_code,
   uint8_t cols[5])
{
   char *end;
   unsigned long code;

   while (font_space(*line))
      line++;
   if (!*line || *line == '#')
      return 0;

   if ((*line >= '0' && *line <= '9') &&
       (line[1] == 'x' || line[1] == 'X' ||
        (line[1] >= '0' && line[1] <= '9'))) {
      code = strtoul(line, &end, 0);
      line = end;
   } else {
      code = (unsigned char)*line++;
   }

   while (font_space(*line))
      line++;
   if (*line != '=' || code < 32 || code > 126)
      return -1;
   line++;

   for (unsigned i = 0; i < 5; i++) {
      unsigned long col;

      while (font_space(*line) || *line == ',')
         line++;
      if (!*line)
         return -1;
      col = strtoul(line, &end, 16);
      if (end == line || col > 0x7f)
         return -1;
      cols[i] = (uint8_t)col;
      line = end;
   }

   *out_code = (unsigned char)code;
   return 1;
}

static int load_file_bytes(const char *path, uint8_t **out_data,
   size_t *out_size)
{
   FILE *file;
   uint8_t *data;
   size_t size;

   if (!path || !out_data || !out_size)
      return -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   if (fseek(file, 0, SEEK_END) != 0) {
      fclose(file);
      return -1;
   }
   size = (size_t)ftell(file);
   if (size == 0 || size > TTF_MAX_BYTES ||
       fseek(file, 0, SEEK_SET) != 0) {
      fclose(file);
      return -1;
   }
   data = malloc(size);
   if (!data) {
      fclose(file);
      return -1;
   }
   if (fread(data, 1, size, file) != size) {
      free(data);
      fclose(file);
      return -1;
   }
   fclose(file);
   *out_data = data;
   *out_size = size;
   return 0;
}

static int ttf_add_font_file(const char *path)
{
   struct ttf_loaded_font *font;
   uint8_t *data = NULL;
   size_t size = 0;
   int offset;

   if (ttf_font_count >= TTF_MAX_FONTS)
      return -1;

   if (load_file_bytes(path, &data, &size) != 0)
      return -1;
   offset = stbtt_GetFontOffsetForIndex(data, 0);
   if (offset < 0)
      offset = 0;
   font = &ttf_fonts[ttf_font_count];
   memset(font, 0, sizeof(*font));
   if (!stbtt_InitFont(&font->info, data, offset)) {
      free(data);
      return -1;
   }
   font->data = data;
   font->size = size;
   font->scale = stbtt_ScaleForPixelHeight(&font->info, ttf_pixel_height);
   stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent,
      &font->line_gap);
   if (ttf_font_count == 0) {
      ttf_baseline_offset = (int)((float)font->ascent * font->scale + 0.5f);
      if (ttf_baseline_offset < 1)
         ttf_baseline_offset = (int)ttf_pixel_height;
   }
   ttf_font_count++;
   return 0;
}

static int load_ttf_file(const char *path)
{
   char paths[TTF_PATH_LIST_MAX];
   char part[TTF_PATH_MAX];
   unsigned loaded = 0;
   size_t start = 0;
   size_t len;

   unifrog_text_copy(paths, sizeof(paths), path);
   ttf_clear();
   ttf_pixel_height = TTF_PIXEL_HEIGHT;
   len = strlen(paths);
   for (size_t i = 0; i <= len; i++) {
      if (paths[i] == ';' || paths[i] == '|' || paths[i] == '\0') {
         size_t end = i;
         size_t count;

         while (start < end && (paths[start] == ' ' || paths[start] == '\t'))
            start++;
         while (end > start && (paths[end - 1u] == ' ' ||
             paths[end - 1u] == '\t'))
            end--;
         count = end - start;
         if (count > 5 && memcmp(paths + start, "size=", 5) == 0) {
            int size = atoi(paths + start + 5);

            if (size >= 8 && size <= 20)
               ttf_pixel_height = (float)size;
         } else if (count > 0 && count < sizeof(part)) {
            memcpy(part, paths + start, count);
            part[count] = '\0';
            if (ttf_add_font_file(part) == 0)
               loaded++;
         }
         start = i + 1u;
      }
   }
   if (!loaded) {
      ttf_clear();
      return -1;
   }
   ttf_active = 1;
   return (int)loaded;
}

static int font_path_list_has_ttf(const char *path)
{
   const char *start = path;

   if (!path)
      return 0;
   for (const char *p = path;; p++) {
      if (*p == ';' || *p == '|' || *p == '\0') {
         size_t len = (size_t)(p - start);

         if ((len > 4 &&
              strncasecmp(start + len - 4u, ".ttf", 4) == 0) ||
             (len > 4 &&
              strncasecmp(start + len - 4u, ".otf", 4) == 0))
            return 1;
         if (*p == '\0')
            break;
         start = p + 1u;
      }
   }
   return 0;
}

int unifrog_gfx_load_font5x7_file(const char *path)
{
   FILE *file;
   char line[128];
   unsigned loaded = 0;

   if (!path || !path[0])
      return -1;
   if (font_path_list_has_ttf(path))
      return load_ttf_file(path);
   file = fopen(path, "rb");
   if (!file)
      return -1;

   memset(font5x7_custom_valid, 0, sizeof(font5x7_custom_valid));
   while (fgets(line, sizeof(line), file)) {
      unsigned char code = 0;
      uint8_t cols[5];
      int ret = parse_font_line(line, &code, cols);

      if (ret <= 0)
         continue;
      memcpy(font5x7_custom[code - 32], cols, sizeof(cols));
      font5x7_custom_valid[code - 32] = 1;
      loaded++;
   }
   fclose(file);
   if (loaded)
      ttf_clear();
   return (int)loaded;
}
