#ifndef UNIFROG_PRESENTER_H
#define UNIFROG_PRESENTER_H

#include <stdint.h>

#include <unifrog/fb.h>
#include <unifrog/ge.h>

#ifdef __cplusplus
extern "C" {
#endif

enum unifrog_present_flags {
   UNIFROG_PRESENT_VSYNC = 1u << 0,
   UNIFROG_PRESENT_KEEP_ASPECT = 1u << 1,
   UNIFROG_PRESENT_KEEP_SIZE = 1u << 2,
};

struct unifrog_presenter {
   struct unifrog_fb fb;
   struct unifrog_ge ge;
   unsigned flags;
   unsigned active_buffer;
   unsigned buffer_count;
   int last_dst_x;
   int last_dst_y;
   int last_dst_w;
   int last_dst_h;
   unsigned cleared_buffer_mask;
   unsigned present_count;
   uint64_t present_total_count;
   uint64_t present_ge_count;
   uint64_t present_sync_count;
   uint64_t present_vsync_count;
   uint64_t present_pan_count;
   unsigned present_max_count;
   unsigned last_vsync_count;
};

struct unifrog_presenter_stats {
   unsigned frames;
   uint64_t total_count;
   uint64_t ge_count;
   uint64_t sync_count;
   uint64_t vsync_count;
   uint64_t pan_count;
   unsigned max_count;
   int dst_x;
   int dst_y;
   int dst_w;
   int dst_h;
};

int unifrog_presenter_open_with_clock(struct unifrog_presenter *presenter,
   unsigned buffers, unsigned flags, enum unifrog_ge_clock clock);
int unifrog_presenter_open(struct unifrog_presenter *presenter,
   unsigned buffers, unsigned flags);
void unifrog_presenter_close(struct unifrog_presenter *presenter);
int unifrog_presenter_clear(struct unifrog_presenter *presenter, uint32_t argb);
int unifrog_presenter_present_rgb565(struct unifrog_presenter *presenter,
   const void *pixels, unsigned width, unsigned height, unsigned pitch_bytes);
void unifrog_presenter_take_stats(struct unifrog_presenter *presenter,
   struct unifrog_presenter_stats *stats);

#ifdef __cplusplus
}
#endif

#endif
