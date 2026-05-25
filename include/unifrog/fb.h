#ifndef UNIFROG_FB_H
#define UNIFROG_FB_H

#include <stddef.h>
#include <stdint.h>

#include <unifrog/ge.h>
#include <unifrog/gfx.h>

#ifdef __cplusplus
extern "C" {
#endif

enum unifrog_fb_open_flags {
   UNIFROG_FB_OPEN_DEFAULT = 0,
   UNIFROG_FB_OPEN_CACHED = 1u << 0,
   UNIFROG_FB_OPEN_PRESERVE = 1u << 1,
   UNIFROG_FB_OPEN_XRGB8888 = 1u << 2,
};

struct unifrog_fb {
   int fd;
   uint16_t *pixels;
   unsigned width;
   unsigned height;
   unsigned stride_pixels;
   unsigned pitch_bytes;
   unsigned bpp;
   size_t smem_len;
   size_t visible_bytes;
   unsigned max_buffers;
   unsigned buffer_count;
   unsigned current_buffer;
};

int unifrog_fb_open(struct unifrog_fb *fb, unsigned flags);
void unifrog_fb_close(struct unifrog_fb *fb);
struct unifrog_surface unifrog_fb_surface(const struct unifrog_fb *fb);
struct unifrog_surface unifrog_fb_surface_for_buffer(const struct unifrog_fb *fb,
   unsigned buffer_index);
struct unifrog_ge_surface unifrog_fb_ge_surface(const struct unifrog_fb *fb);
struct unifrog_ge_surface unifrog_fb_ge_surface_for_buffer(const struct unifrog_fb *fb,
   unsigned buffer_index);
void unifrog_fb_flush(const struct unifrog_fb *fb);
void unifrog_fb_flush_buffer(const struct unifrog_fb *fb, unsigned buffer_index);
int unifrog_fb_wait_vsync(const struct unifrog_fb *fb);
int unifrog_fb_set_buffer_count(struct unifrog_fb *fb, unsigned buffers);
int unifrog_fb_pan(struct unifrog_fb *fb, unsigned buffer_index);

#ifdef __cplusplus
}
#endif

#endif
