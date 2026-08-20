#ifndef UNIFROG_GE_INTERNAL_H
#define UNIFROG_GE_INTERNAL_H

#include <stdint.h>

#include <unifrog/ge.h>

#ifdef __cplusplus
extern "C" {
#endif

int unifrog_ge_fill_at(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   const struct unifrog_ge_rect *rect,
   uint32_t argb, uintptr_t phys_addr);
int unifrog_ge_blit_at(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   int dst_x, int dst_y,
   const struct unifrog_ge_surface *src,
   const struct unifrog_ge_rect *src_rect,
   unsigned flags, uintptr_t dst_phys_addr);
int unifrog_ge_stretch_at(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   const struct unifrog_ge_rect *dst_rect,
   const struct unifrog_ge_surface *src,
   const struct unifrog_ge_rect *src_rect,
   unsigned flags, uintptr_t dst_phys_addr);

#ifdef __cplusplus
}
#endif

#endif
