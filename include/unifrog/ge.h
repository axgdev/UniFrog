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
   UNIFROG_GE_CLOCK_SELECTOR_0 = 0,
   UNIFROG_GE_CLOCK_SELECTOR_1 = 1,
   UNIFROG_GE_CLOCK_SELECTOR_2 = 2,
   UNIFROG_GE_CLOCK_SELECTOR_3 = 3,

   /* Legacy SDK spellings.  These are aliases for raw selectors, not
    * guaranteed silicon frequencies. */
   UNIFROG_GE_CLOCK_198MHZ = UNIFROG_GE_CLOCK_SELECTOR_0,
   UNIFROG_GE_CLOCK_148MHZ = UNIFROG_GE_CLOCK_SELECTOR_1,
   UNIFROG_GE_CLOCK_225MHZ = UNIFROG_GE_CLOCK_SELECTOR_2,
   UNIFROG_GE_CLOCK_238MHZ = UNIFROG_GE_CLOCK_SELECTOR_3,
};

/* Clock values are the raw HCGE_SET_CLOCK selectors.  The SDK names are
 * retained for source compatibility, but they are not a reliable description
 * of the HC15xx silicon clock source: the bootloader, Linux, and hcge_hw_reset
 * leave the stable GE source at selector 3.  Normal startup must inherit that
 * setting rather than perform a live clock transition.  Callers requesting a
 * runtime change must do so only while the GE is idle. */
#define UNIFROG_GE_CLOCK_FAST_SELECTOR \
   ((unsigned)UNIFROG_GE_CLOCK_SELECTOR_3)
/* Compatibility alias for callers that explicitly request the tested fast
 * selector; this is a raw selector, not a frequency value. */
#define UNIFROG_GE_CLOCK_FAST \
   ((enum unifrog_ge_clock)UNIFROG_GE_CLOCK_FAST_SELECTOR)

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
   /* hcge_open()/hcge_hw_reset() establish selector 3 before the first
    * command.  Keep a small software mirror so an idempotent request for
    * that inherited selector does not issue a live clock ioctl. */
   enum unifrog_ge_clock clock_selector;
   int clock_selector_valid;
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
