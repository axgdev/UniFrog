#ifndef UNIFROG_GE_H
#define UNIFROG_GE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum unifrog_ge_format {
   UNIFROG_GE_FORMAT_RGB565 = 0,
   UNIFROG_GE_FORMAT_ARGB8888 = 1,
   UNIFROG_GE_FORMAT_XRGB8888 = 2,
};

enum unifrog_ge_clock {
   UNIFROG_GE_CLOCK_198MHZ = 0,
   UNIFROG_GE_CLOCK_148MHZ = 1,
   UNIFROG_GE_CLOCK_225MHZ = 2,
   UNIFROG_GE_CLOCK_238MHZ = 3,
};

#define UNIFROG_GE_CLOCK_FAST UNIFROG_GE_CLOCK_198MHZ

enum unifrog_ge_flags {
   UNIFROG_GE_FLUSH_SOURCE = 1u << 0,
   UNIFROG_GE_FLUSH_DESTINATION = 1u << 1,
   UNIFROG_GE_ROTATE_90 = 1u << 2,
   UNIFROG_GE_ROTATE_180 = 1u << 3,
   UNIFROG_GE_ROTATE_270 = 1u << 4,
};

struct unifrog_ge {
   void *context;
   int fd;
};

struct unifrog_ge_surface {
   void *pixels;
   unsigned width;
   unsigned height;
   unsigned pitch_bytes;
   enum unifrog_ge_format format;
};

struct unifrog_ge_rect {
   int x;
   int y;
   int w;
   int h;
};

int unifrog_ge_open(struct unifrog_ge *ge);
void unifrog_ge_close(struct unifrog_ge *ge);
int unifrog_ge_set_clock(struct unifrog_ge *ge, enum unifrog_ge_clock clock);
int unifrog_ge_set_fast_clock(struct unifrog_ge *ge);
int unifrog_ge_sync(struct unifrog_ge *ge);
int unifrog_ge_fill(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   const struct unifrog_ge_rect *rect,
   uint32_t argb);
int unifrog_ge_blit(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   int dst_x, int dst_y,
   const struct unifrog_ge_surface *src,
   const struct unifrog_ge_rect *src_rect,
   unsigned flags);
int unifrog_ge_stretch(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   const struct unifrog_ge_rect *dst_rect,
   const struct unifrog_ge_surface *src,
   const struct unifrog_ge_rect *src_rect,
   unsigned flags);

#ifdef __cplusplus
}
#endif

#endif
