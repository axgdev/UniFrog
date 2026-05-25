#ifndef UNIFROG_PNG_H
#define UNIFROG_PNG_H

#include <stdint.h>

#include <unifrog/ge.h>
#include <unifrog/gfx.h>

#ifdef __cplusplus
extern "C" {
#endif

struct unifrog_png_image {
   unsigned width;
   unsigned height;
   uint16_t *pixels;
   uint8_t *alpha;
};

int unifrog_png_load_file(const char *path, struct unifrog_png_image *image);
void unifrog_png_free(struct unifrog_png_image *image);
void unifrog_png_draw(const struct unifrog_surface *surface,
   const struct unifrog_png_image *image, int x, int y, int w, int h);
int unifrog_png_is_opaque(const struct unifrog_png_image *image);
unsigned unifrog_png_alpha_coverage(const struct unifrog_png_image *image);
int unifrog_png_draw_ge(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst, const struct unifrog_png_image *image,
   int x, int y, int w, int h);
int unifrog_png_draw_ge_async(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst, const struct unifrog_png_image *image,
   int x, int y, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
