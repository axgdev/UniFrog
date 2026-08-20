#ifndef UNIFROG_GFX_H
#define UNIFROG_GFX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_RGB565(r, g, b) \
   ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

struct unifrog_surface {
   uint16_t *pixels;
   unsigned width;
   unsigned height;
   unsigned stride;
};

void unifrog_gfx_put_pixel(const struct unifrog_surface *surface,
   int x, int y, uint16_t color);
void unifrog_gfx_fill_rect(const struct unifrog_surface *surface,
   int x, int y, int w, int h, uint16_t color);
void unifrog_gfx_draw_hline(const struct unifrog_surface *surface,
   int x, int y, int w, uint16_t color);
void unifrog_gfx_draw_vline(const struct unifrog_surface *surface,
   int x, int y, int h, uint16_t color);
void unifrog_gfx_draw_char(const struct unifrog_surface *surface,
   int x, int y, char c, uint16_t color, int scale);
void unifrog_gfx_draw_text(const struct unifrog_surface *surface,
   int x, int y, const char *text, uint16_t color, int scale);
void unifrog_gfx_draw_text_bitmap(const struct unifrog_surface *surface,
   int x, int y, const char *text, uint16_t color, int scale);
int unifrog_gfx_font_height(void);
int unifrog_gfx_font_advance(void);
int unifrog_gfx_load_font5x7_file(const char *path);
void unifrog_gfx_reset_font5x7(void);

#ifdef __cplusplus
}
#endif

#endif
