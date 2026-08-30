#include <unifrog/gfx.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unifrog/text.h>

#ifndef UNIFROG_GFX_NO_LVGL
#include <lvgl.h>
#include <font/lv_font_loader.h>
#include <misc/lv_fs.h>
#include <extra/libs/fsdrv/lv_fsdrv.h>
#endif

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#define TTF_FIRST_CHAR 32
#define TTF_CHAR_COUNT 95
#define TTF_BITMAP_W 512
#define TTF_BITMAP_H 256
#define TTF_PIXEL_HEIGHT 13.0f
#define TTF_MAX_BYTES (4u * 1024u * 1024u)
#define TTF_UNICODE_CACHE 128u

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

static const uint8_t font5x7_lower[26][5] = {
   {0x20,0x54,0x54,0x54,0x78}, {0x7f,0x48,0x44,0x44,0x38},
   {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7f},
   {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7e,0x09,0x01,0x02},
   {0x0c,0x52,0x52,0x52,0x3e}, {0x7f,0x08,0x04,0x04,0x78},
   {0x00,0x44,0x7d,0x40,0x00}, {0x20,0x40,0x44,0x3d,0x00},
   {0x7f,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7f,0x40,0x00},
   {0x7c,0x04,0x18,0x04,0x78}, {0x7c,0x08,0x04,0x04,0x78},
   {0x38,0x44,0x44,0x44,0x38}, {0x7c,0x14,0x14,0x14,0x08},
   {0x08,0x14,0x14,0x18,0x7c}, {0x7c,0x08,0x04,0x04,0x08},
   {0x48,0x54,0x54,0x54,0x20}, {0x04,0x3f,0x44,0x40,0x20},
   {0x3c,0x40,0x40,0x20,0x7c}, {0x1c,0x20,0x40,0x20,0x1c},
   {0x3c,0x40,0x30,0x40,0x3c}, {0x44,0x28,0x10,0x28,0x44},
   {0x0c,0x50,0x50,0x50,0x3c}, {0x44,0x64,0x54,0x4c,0x44},
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
static uint8_t *ttf_data;
static uint8_t *ttf_bitmap;
static stbtt_bakedchar ttf_chars[TTF_CHAR_COUNT];
static int ttf_active;
static int ttf_baseline_offset;
static stbtt_fontinfo ttf_info;
static float ttf_scale;
struct ttf_unicode_glyph {
   uint32_t codepoint;
   uint8_t *bitmap;
   int width;
   int height;
   int x_offset;
   int y_offset;
   int advance;
   uint32_t used;
};
static struct ttf_unicode_glyph ttf_unicode[TTF_UNICODE_CACHE];
static uint32_t ttf_unicode_clock;
#ifndef UNIFROG_GFX_NO_LVGL
static lv_font_t *lvgl_font;
static int lvgl_font_div = 1;
static int lvgl_fs_ready;
#endif

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
      return font5x7_lower[c - 'a'];

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

static uint32_t utf8_next(const char **text)
{
   const unsigned char *p = (const unsigned char *)*text;
   uint32_t codepoint;
   unsigned count;

   if (!p || !*p)
      return 0;
   if (p[0] < 0x80u) {
      *text += 1;
      return p[0];
   }
   if ((p[0] & 0xe0u) == 0xc0u) {
      codepoint = p[0] & 0x1fu;
      count = 1;
   } else if ((p[0] & 0xf0u) == 0xe0u) {
      codepoint = p[0] & 0x0fu;
      count = 2;
   } else if ((p[0] & 0xf8u) == 0xf0u) {
      codepoint = p[0] & 0x07u;
      count = 3;
   } else {
      *text += 1;
      return '?';
   }
   for (unsigned i = 1; i <= count; i++) {
      if ((p[i] & 0xc0u) != 0x80u) {
         *text += 1;
         return '?';
      }
      codepoint = (codepoint << 6) | (p[i] & 0x3fu);
   }
   *text += count + 1u;
   return codepoint;
}

static void ttf_clear(void)
{
   for (unsigned i = 0; i < TTF_UNICODE_CACHE; i++) {
      free(ttf_unicode[i].bitmap);
      memset(&ttf_unicode[i], 0, sizeof(ttf_unicode[i]));
   }
   free(ttf_data);
   free(ttf_bitmap);
   ttf_data = NULL;
   ttf_bitmap = NULL;
   ttf_active = 0;
   ttf_baseline_offset = 0;
   ttf_scale = 0.0f;
   ttf_unicode_clock = 0;
}

static void lvgl_font_clear(void)
{
#ifndef UNIFROG_GFX_NO_LVGL
   if (lvgl_font)
      lv_font_free(lvgl_font);
   lvgl_font = NULL;
   lvgl_font_div = 1;
#endif
}

/* Boost midtone coverage so small TTF glyphs read as crisp strokes. */
static unsigned ttf_sharpen(unsigned alpha)
{
   alpha = alpha + (alpha >> 1);
   return alpha > 255u ? 255u : alpha;
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

static struct ttf_unicode_glyph *ttf_unicode_glyph(uint32_t codepoint)
{
   struct ttf_unicode_glyph *slot = NULL;
   int advance;
   int bearing;

   for (unsigned i = 0; i < TTF_UNICODE_CACHE; i++) {
      if (ttf_unicode[i].codepoint == codepoint && ttf_unicode[i].bitmap) {
         ttf_unicode[i].used = ++ttf_unicode_clock;
         return &ttf_unicode[i];
      }
      if (!slot || !ttf_unicode[i].bitmap ||
          ttf_unicode[i].used < slot->used)
         slot = &ttf_unicode[i];
   }
   if (!slot || stbtt_FindGlyphIndex(&ttf_info, (int)codepoint) == 0)
      return NULL;
   free(slot->bitmap);
   memset(slot, 0, sizeof(*slot));
   slot->bitmap = stbtt_GetCodepointBitmap(&ttf_info, ttf_scale, ttf_scale,
      (int)codepoint, &slot->width, &slot->height, &slot->x_offset,
      &slot->y_offset);
   if (!slot->bitmap)
      return NULL;
   stbtt_GetCodepointHMetrics(&ttf_info, (int)codepoint, &advance, &bearing);
   (void)bearing;
   slot->codepoint = codepoint;
   slot->advance = (int)((float)advance * ttf_scale + 0.5f);
   if (slot->advance < 1)
      slot->advance = slot->width;
   slot->used = ++ttf_unicode_clock;
   return slot;
}

static void ttf_draw_text(const struct unifrog_surface *surface,
   int x, int y, const char *text, uint16_t color)
{
   float xpos = (float)x;
   float ypos = (float)(y + ttf_baseline_offset);

   if (!surface_is_valid(surface) || !text)
      return;
   while (*text) {
      uint32_t ch = utf8_next(&text);
      stbtt_aligned_quad quad;
      const stbtt_bakedchar *glyph;
      int gx0;
      int gy0;
      int gw;
      int gh;

      if (ch >= TTF_FIRST_CHAR && ch < TTF_FIRST_CHAR + TTF_CHAR_COUNT) {
      glyph = &ttf_chars[ch - TTF_FIRST_CHAR];
      stbtt_GetBakedQuad(ttf_chars, TTF_BITMAP_W, TTF_BITMAP_H,
         ch - TTF_FIRST_CHAR, &xpos, &ypos, &quad, 1);
      gx0 = (int)quad.x0;
      gy0 = (int)quad.y0;
      gw = (int)(glyph->x1 - glyph->x0);
      gh = (int)(glyph->y1 - glyph->y0);

      for (int yy = 0; yy < gh; yy++) {
         int dy = gy0 + yy;
         const uint8_t *src_row;
         uint16_t *dst_row;

         if (dy < 0 || dy >= (int)surface->height)
            continue;
         src_row = ttf_bitmap + ((unsigned)glyph->y0 + (unsigned)yy) *
            TTF_BITMAP_W + glyph->x0;
         dst_row = surface->pixels + (unsigned)dy * surface->stride;
         for (int xx = 0; xx < gw; xx++) {
            int dx = gx0 + xx;
            unsigned alpha;

            if (dx < 0 || dx >= (int)surface->width)
               continue;
            alpha = ttf_sharpen(src_row[xx]);
            if (alpha)
               dst_row[dx] = blend_rgb565(dst_row[dx], color, alpha);
         }
      }
      } else {
         struct ttf_unicode_glyph *unicode = ttf_unicode_glyph(ch);

         if (!unicode)
            unicode = ttf_unicode_glyph('?');
         if (!unicode)
            continue;
         gx0 = (int)xpos + unicode->x_offset;
         gy0 = (int)ypos + unicode->y_offset;
         for (int yy = 0; yy < unicode->height; yy++) {
            int dy = gy0 + yy;

            if (dy < 0 || dy >= (int)surface->height)
               continue;
            for (int xx = 0; xx < unicode->width; xx++) {
               int dx = gx0 + xx;
               unsigned alpha;

               if (dx < 0 || dx >= (int)surface->width)
                  continue;
               alpha = ttf_sharpen(unicode->bitmap[yy * unicode->width + xx]);
               if (alpha) {
                  uint16_t *dst = &surface->pixels[(unsigned)dy *
                     surface->stride + (unsigned)dx];

                  *dst = blend_rgb565(*dst, color, alpha);
               }
            }
         }
         xpos += (float)unicode->advance;
      }
   }
}

#ifndef UNIFROG_GFX_NO_LVGL
static unsigned lvgl_glyph_alpha(const uint8_t *bitmap, unsigned index,
   unsigned bpp)
{
   static const unsigned alpha2[4] = { 0, 85, 170, 255 };

   if (!bitmap)
      return 0;
   if (bpp == 1)
      return (bitmap[index >> 3] & (0x80u >> (index & 7u))) ? 255u : 0u;
   if (bpp == 2)
      return alpha2[(bitmap[index >> 2] >> (6u - ((index & 3u) * 2u))) & 3u];
   if (bpp == 4)
      return ((bitmap[index >> 1] >> ((index & 1u) ? 0u : 4u)) & 0x0fu) * 17u;
   return bitmap[index];
}

static void lvgl_font_draw_text(const struct unifrog_surface *surface,
   int x, int y, const char *text, uint16_t color)
{
   int pen_x = x;
   int div = lvgl_font_div > 0 ? lvgl_font_div : 1;
   int baseline;

   if (!surface_is_valid(surface) || !text || !lvgl_font)
      return;
   /*
    * LVGL's base_line is the distance from the baseline to the bottom of the
    * line box. Convert the caller's top y to a baseline position.
    */
   baseline = y + ((int)lvgl_font->line_height -
      (int)lvgl_font->base_line + div - 1) / div;
   while (*text) {
      uint32_t ch = utf8_next(&text);
      const char *next_text = text;
      uint32_t next = utf8_next(&next_text);
      lv_font_glyph_dsc_t dsc;
      const uint8_t *bitmap;

      memset(&dsc, 0, sizeof(dsc));
      if (!lvgl_font->get_glyph_dsc(lvgl_font, &dsc, ch,
          next)) {
         ch = '?';
         if (!lvgl_font->get_glyph_dsc(lvgl_font, &dsc, ch, next))
            continue;
      }
      bitmap = lvgl_font->get_glyph_bitmap(lvgl_font, ch);
      if (bitmap && dsc.box_w && dsc.box_h) {
         int box_w = ((int)dsc.box_w + div - 1) / div;
         int box_h = ((int)dsc.box_h + div - 1) / div;
         int gx = pen_x + dsc.ofs_x / div;
         int gy = baseline - dsc.ofs_y / div - box_h;

         for (int yy = 0; yy < box_h; yy++) {
            int dy = gy + yy;

            if (dy < 0 || dy >= (int)surface->height)
               continue;
            for (int xx = 0; xx < box_w; xx++) {
               int dx = gx + xx;
               unsigned alpha = 0;

               if (dx < 0 || dx >= (int)surface->width)
                  continue;
               for (int sy = yy * div; sy < (yy + 1) * div &&
                    sy < (int)dsc.box_h; sy++) {
                  for (int sx = xx * div; sx < (xx + 1) * div &&
                       sx < (int)dsc.box_w; sx++) {
                     unsigned a = lvgl_glyph_alpha(bitmap,
                        (unsigned)sy * dsc.box_w + (unsigned)sx,
                        dsc.bpp ? dsc.bpp : 4u);

                     if (a > alpha)
                        alpha = a;
                  }
               }
               if (alpha) {
                  uint16_t *dst = &surface->pixels[(unsigned)dy *
                     surface->stride + (unsigned)dx];
                  unsigned draw_alpha = alpha + ((255u - alpha) / 2u);

                  *dst = blend_rgb565(*dst, color, draw_alpha);
                  if (div > 1 && draw_alpha > 192u &&
                      dx + 1 < (int)surface->width) {
                     uint16_t *next = dst + 1;

                     *next = blend_rgb565(*next, color, draw_alpha / 4u);
                  }
               }
            }
         }
      }
      pen_x += ((int)dsc.adv_w + div - 1) / div;
   }
}
#endif

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
#ifndef UNIFROG_GFX_NO_LVGL
   if (lvgl_font && scale == 1) {
      lvgl_font_draw_text(surface, x, y, text, color);
      return;
   }
#endif
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

void unifrog_gfx_draw_text_bitmap(const struct unifrog_surface *surface,
   int x, int y, const char *text, uint16_t color, int scale)
{
   if (!text || scale <= 0)
      return;
   while (*text) {
      unifrog_gfx_draw_char(surface, x, y, *text, color, scale);
      x += 6 * scale;
      text++;
   }
}

int unifrog_gfx_text_width_limit(const char *text, int max_px)
{
   int width = 0;
   int chars = 0;

   if (!text || max_px <= 0)
      return 0;
   while (*text) {
      uint32_t ch = utf8_next(&text);
      int adv = 0;

#ifndef UNIFROG_GFX_NO_LVGL
      if (lvgl_font) {
         lv_font_glyph_dsc_t dsc;
         const char *peek = text;

         memset(&dsc, 0, sizeof(dsc));
         if (!lvgl_font->get_glyph_dsc(lvgl_font, &dsc, ch,
             utf8_next(&peek)))
            continue;
         adv = ((int)dsc.adv_w + lvgl_font_div - 1) /
            (lvgl_font_div > 0 ? lvgl_font_div : 1);
      } else
#endif
      if (ttf_active) {
         if (ch >= TTF_FIRST_CHAR && ch < TTF_FIRST_CHAR + TTF_CHAR_COUNT)
            adv = (int)(ttf_chars[ch - TTF_FIRST_CHAR].xadvance + 0.5f);
         else {
            struct ttf_unicode_glyph *glyph = ttf_unicode_glyph(ch);

            if (!glyph)
               glyph = ttf_unicode_glyph('?');
            if (!glyph)
               continue;
            adv = glyph->advance;
         }
      } else {
         adv = 6;
      }
      if (width + adv > max_px)
         break;
      width += adv;
      chars++;
   }
   return chars;
}

int unifrog_gfx_font_height(void)
{
#ifndef UNIFROG_GFX_NO_LVGL
   if (lvgl_font) {
      int div = lvgl_font_div > 0 ? lvgl_font_div : 1;
      return ((int)lvgl_font->line_height + div - 1) / div;
   }
#endif
   if (ttf_active)
      return (int)(TTF_PIXEL_HEIGHT + 0.5f);
   return 8;
}

int unifrog_gfx_font_advance(void)
{
#ifndef UNIFROG_GFX_NO_LVGL
   if (lvgl_font) {
      int div = lvgl_font_div > 0 ? lvgl_font_div : 1;
      int adv = ((int)lvgl_font->line_height + div - 1) / div;

      if (adv > 8)
         return 8;
      if (adv < 5)
         return 5;
   }
#endif
   if (ttf_active)
      return 10;
   return 6;
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

static int load_ttf_file(const char *path)
{
   uint8_t *data = NULL;
   uint8_t *bitmap = NULL;
   size_t size = 0;
   int offset;
   int bake_ret;
   int ascent;
   int descent;
   int line_gap;
   float scale;

   if (load_file_bytes(path, &data, &size) != 0)
      return -1;
   (void)size;
   offset = stbtt_GetFontOffsetForIndex(data, 0);
   if (offset < 0)
      offset = 0;
   if (!stbtt_InitFont(&ttf_info, data, offset)) {
      free(data);
      return -1;
   }
   bitmap = calloc(1, TTF_BITMAP_W * TTF_BITMAP_H);
   if (!bitmap) {
      free(data);
      return -1;
   }
   bake_ret = stbtt_BakeFontBitmap(data, offset, TTF_PIXEL_HEIGHT,
      bitmap, TTF_BITMAP_W, TTF_BITMAP_H, TTF_FIRST_CHAR,
      TTF_CHAR_COUNT, ttf_chars);
   if (bake_ret <= 0) {
      free(bitmap);
      free(data);
      return -1;
   }
   scale = stbtt_ScaleForPixelHeight(&ttf_info, TTF_PIXEL_HEIGHT);
   stbtt_GetFontVMetrics(&ttf_info, &ascent, &descent, &line_gap);
   (void)descent;
   (void)line_gap;

   lvgl_font_clear();
   ttf_clear();
   ttf_data = data;
   ttf_bitmap = bitmap;
   ttf_baseline_offset = (int)((float)ascent * scale + 0.5f);
   ttf_scale = scale;
   if (ttf_baseline_offset < 1)
      ttf_baseline_offset = (int)TTF_PIXEL_HEIGHT;
   ttf_active = 1;
   return TTF_CHAR_COUNT;
}

#ifndef UNIFROG_GFX_NO_LVGL
static int load_lvgl_bin_font(const char *path)
{
   char lv_path[320];
   lv_font_t *font;

   if (!lvgl_fs_ready) {
      _lv_fs_init();
      lv_fs_stdio_init();
      lvgl_fs_ready = 1;
   }
   snprintf(lv_path, sizeof(lv_path), "S:%s", path);
   font = lv_font_load(lv_path);
   if (!font)
      return -1;
   if (!font->get_glyph_dsc || !font->get_glyph_bitmap ||
       font->line_height <= 0) {
      lv_font_free(font);
      return -1;
   }
   lvgl_font_clear();
   ttf_clear();
   lvgl_font = font;
   if (font->line_height > 12)
      lvgl_font_div = 2;
   return 95;
}
#endif

int unifrog_gfx_load_font5x7_file(const char *path)
{
   FILE *file;
   char line[128];
   unsigned loaded = 0;

   if (!path || !path[0])
      return -1;
   if (unifrog_text_ends_with_ci(path, ".ttf") ||
       unifrog_text_ends_with_ci(path, ".otf"))
      return load_ttf_file(path);
   if (unifrog_text_ends_with_ci(path, ".bin"))
#ifdef UNIFROG_GFX_NO_LVGL
      return -1;
#else
      return load_lvgl_bin_font(path);
#endif
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
   if (loaded) {
      lvgl_font_clear();
      ttf_clear();
   }
   return (int)loaded;
}

void unifrog_gfx_reset_font5x7(void)
{
   memset(font5x7_custom_valid, 0, sizeof(font5x7_custom_valid));
   lvgl_font_clear();
   ttf_clear();
}
