#include <unifrog/presenter.h>

#include <string.h>

#include <unifrog/ge_internal.h>
#include <unifrog/perf.h>

static void clear_presenter(struct unifrog_presenter *presenter)
{
   if (presenter)
      memset(presenter, 0, sizeof(*presenter));
}

static struct unifrog_ge_rect full_rect(unsigned width, unsigned height)
{
   struct unifrog_ge_rect rect;

   rect.x = 0;
   rect.y = 0;
   rect.w = width;
   rect.h = height;
   return rect;
}

static struct unifrog_ge_rect scaled_rect(unsigned src_w, unsigned src_h,
   unsigned dst_w, unsigned dst_h, unsigned flags)
{
   struct unifrog_ge_rect rect;
   unsigned w = dst_w;
   unsigned h = dst_h;

   if ((flags & UNIFROG_PRESENT_KEEP_SIZE) && src_w && src_h) {
      w = src_w <= dst_w ? src_w : dst_w;
      h = src_h <= dst_h ? src_h : dst_h;
   } else if ((flags & UNIFROG_PRESENT_KEEP_ASPECT) && src_w && src_h) {
      if ((uint64_t)dst_w * src_h > (uint64_t)dst_h * src_w)
         w = (unsigned)(((uint64_t)dst_h * src_w) / src_h);
      else
         h = (unsigned)(((uint64_t)dst_w * src_h) / src_w);
      if (w == 0)
         w = 1;
      if (h == 0)
         h = 1;
   }

   rect.x = (int)((dst_w - w) / 2);
   rect.y = (int)((dst_h - h) / 2);
   rect.w = (int)w;
   rect.h = (int)h;
   return rect;
}

static int unifrog_presenter_open_internal(
   struct unifrog_presenter *presenter, unsigned buffers, unsigned flags,
   int apply_clock, enum unifrog_ge_clock clock)
{
   if (!presenter)
      return -1;

   clear_presenter(presenter);
   if (buffers == 0)
      buffers = 2;

   if (unifrog_fb_open(&presenter->fb, UNIFROG_FB_OPEN_DEFAULT) != 0)
      goto fail;
   if (buffers > presenter->fb.max_buffers)
      buffers = presenter->fb.max_buffers;
   if (buffers == 0)
      buffers = 1;
   if (unifrog_fb_set_buffer_count(&presenter->fb, buffers) != 0) {
      if (buffers != 1 || unifrog_fb_set_buffer_count(&presenter->fb, 1) != 0)
         goto fail;
      buffers = 1;
   }

   if (unifrog_ge_open(&presenter->ge) != 0)
      goto fail;
   /* A clock value is an explicit live hardware transition.  The default
    * presenter path does not apply one and inherits the bootloader/kernel
    * setting. */
   if (apply_clock && unifrog_ge_set_clock(&presenter->ge, clock) != 0)
      goto fail;

   presenter->flags = flags;
   presenter->buffer_count = buffers;
   presenter->active_buffer = 0;
   presenter->last_dst_w = -1;
   presenter->last_dst_h = -1;
   presenter->cleared_buffer_mask = 0;
   return 0;

fail:
   unifrog_presenter_close(presenter);
   return -1;
}

int unifrog_presenter_open_with_clock(struct unifrog_presenter *presenter,
   unsigned buffers, unsigned flags, enum unifrog_ge_clock clock)
{
   return unifrog_presenter_open_internal(presenter, buffers, flags, 1,
      clock);
}

int unifrog_presenter_open(struct unifrog_presenter *presenter,
   unsigned buffers, unsigned flags)
{
   /* Keep the clock inherited from the bootloader/kernel.  The setter is a
    * live GE MMIO transition and belongs only to an explicit runtime change. */
   return unifrog_presenter_open_internal(presenter, buffers, flags, 0,
      UNIFROG_GE_CLOCK_SELECTOR_3);
}

void unifrog_presenter_close(struct unifrog_presenter *presenter)
{
   if (!presenter)
      return;
   unifrog_ge_close(&presenter->ge);
   unifrog_fb_close(&presenter->fb);
   clear_presenter(presenter);
}

int unifrog_presenter_clear(struct unifrog_presenter *presenter, uint32_t argb)
{
   struct unifrog_ge_surface dst;
   struct unifrog_ge_rect rect;
   int ret;

   if (!presenter)
      return -1;

   dst = unifrog_fb_ge_surface_for_buffer(&presenter->fb, presenter->active_buffer);
   rect = full_rect(presenter->fb.width, presenter->fb.height);
   ret = unifrog_ge_fill_at(&presenter->ge, &dst, &rect, argb,
      presenter->fb.phys_start +
         (uintptr_t)presenter->active_buffer * presenter->fb.pitch_bytes *
         presenter->fb.height);
   if (ret == 0)
      ret = unifrog_ge_sync(&presenter->ge);
   return ret;
}

static int unifrog_presenter_present_ge(struct unifrog_presenter *presenter,
   const void *pixels, unsigned width, unsigned height, unsigned pitch_bytes,
   enum unifrog_ge_format format)
{
   struct unifrog_ge_surface src;
   struct unifrog_ge_surface dst;
   struct unifrog_ge_rect src_rect;
   struct unifrog_ge_rect dst_rect;
   unsigned next_buffer;
   uint32_t total_start;
   uint32_t ge_start;
   uint32_t sync_start;
   uint32_t vsync_start = 0;
   uint32_t pan_start;
   unsigned total_count;
   unsigned vsync_count = 0;
   int ret;

   if (!presenter || !pixels || width == 0 || height == 0 || pitch_bytes == 0)
      return -1;

   presenter->last_vsync_count = 0;
   total_start = unifrog_perf_count();
   next_buffer = presenter->active_buffer;
   if (presenter->buffer_count > 1)
      next_buffer = (presenter->active_buffer + 1) % presenter->buffer_count;

   memset(&src, 0, sizeof(src));
   src.pixels = (void *)pixels;
   src.width = width;
   src.height = height;
   src.pitch_bytes = pitch_bytes;
   src.format = format;
   dst = unifrog_fb_ge_surface_for_buffer(&presenter->fb, next_buffer);
   src_rect = full_rect(width, height);
   dst_rect = scaled_rect(width, height,
      presenter->fb.width, presenter->fb.height, presenter->flags);

   if (dst_rect.x != presenter->last_dst_x ||
       dst_rect.y != presenter->last_dst_y ||
       dst_rect.w != presenter->last_dst_w ||
       dst_rect.h != presenter->last_dst_h) {
      presenter->last_dst_x = dst_rect.x;
      presenter->last_dst_y = dst_rect.y;
      presenter->last_dst_w = dst_rect.w;
      presenter->last_dst_h = dst_rect.h;
      presenter->cleared_buffer_mask = 0;
   }

   if ((dst_rect.w != (int)presenter->fb.width ||
       dst_rect.h != (int)presenter->fb.height) &&
       !(presenter->cleared_buffer_mask & (1u << next_buffer))) {
      struct unifrog_ge_rect clear = full_rect(presenter->fb.width, presenter->fb.height);
      ret = unifrog_ge_fill_at(&presenter->ge, &dst, &clear, 0xff000000u,
         presenter->fb.phys_start +
            (uintptr_t)next_buffer * presenter->fb.pitch_bytes *
            presenter->fb.height);
      if (ret != 0)
         return ret;
      presenter->cleared_buffer_mask |= 1u << next_buffer;
   }

   ge_start = unifrog_perf_count();
   if (src_rect.w == dst_rect.w && src_rect.h == dst_rect.h) {
      ret = unifrog_ge_blit_at(&presenter->ge, &dst, dst_rect.x, dst_rect.y,
         &src, &src_rect, UNIFROG_GE_FLUSH_SOURCE,
         presenter->fb.phys_start +
            (uintptr_t)next_buffer * presenter->fb.pitch_bytes *
            presenter->fb.height);
      if (ret == 0)
         presenter->blit_count++;
   } else {
      ret = unifrog_ge_stretch_at(&presenter->ge, &dst, &dst_rect,
         &src, &src_rect, UNIFROG_GE_FLUSH_SOURCE,
         presenter->fb.phys_start +
            (uintptr_t)next_buffer * presenter->fb.pitch_bytes *
            presenter->fb.height);
      if (ret == 0)
         presenter->stretch_count++;
   }
   presenter->present_ge_count +=
      (uint64_t)unifrog_perf_elapsed(ge_start, unifrog_perf_count());
   if (ret != 0)
      return ret;
   if (presenter->flags & UNIFROG_PRESENT_VSYNC) {
      vsync_start = unifrog_perf_count();
   }
   sync_start = unifrog_perf_count();
   ret = unifrog_ge_sync(&presenter->ge);
   presenter->present_sync_count +=
      (uint64_t)unifrog_perf_elapsed(sync_start, unifrog_perf_count());
   if (ret != 0)
      return ret;
   if (presenter->flags & UNIFROG_PRESENT_VSYNC) {
      (void)unifrog_fb_wait_vsync(&presenter->fb);
      vsync_count = unifrog_perf_elapsed(vsync_start, unifrog_perf_count());
      presenter->present_vsync_count += (uint64_t)vsync_count;
   }
   pan_start = unifrog_perf_count();
   ret = unifrog_fb_pan(&presenter->fb, next_buffer);
   presenter->present_pan_count +=
      (uint64_t)unifrog_perf_elapsed(pan_start, unifrog_perf_count());
   if (ret == 0)
      presenter->active_buffer = next_buffer;
   total_count = unifrog_perf_elapsed(total_start, unifrog_perf_count());
   presenter->present_total_count += total_count;
   presenter->last_vsync_count = vsync_count;
   if (total_count > presenter->present_max_count)
      presenter->present_max_count = total_count;
   presenter->present_count++;
   return ret;
}

int unifrog_presenter_present_rgb565(struct unifrog_presenter *presenter,
   const void *pixels, unsigned width, unsigned height, unsigned pitch_bytes)
{
   return unifrog_presenter_present_ge(presenter, pixels, width, height,
      pitch_bytes, UNIFROG_GE_FORMAT_RGB565);
}

int unifrog_presenter_present_xrgb8888(struct unifrog_presenter *presenter,
   const void *pixels, unsigned width, unsigned height, unsigned pitch_bytes)
{
   return unifrog_presenter_present_ge(presenter, pixels, width, height,
      pitch_bytes, UNIFROG_GE_FORMAT_XRGB8888);
}

void unifrog_presenter_take_stats(struct unifrog_presenter *presenter,
   struct unifrog_presenter_stats *stats)
{
   if (!stats)
      return;
   memset(stats, 0, sizeof(*stats));
   if (!presenter)
      return;

   stats->frames = presenter->present_count;
   stats->total_count = presenter->present_total_count;
   stats->ge_count = presenter->present_ge_count;
   stats->sync_count = presenter->present_sync_count;
   stats->vsync_count = presenter->present_vsync_count;
   stats->pan_count = presenter->present_pan_count;
   stats->blits = presenter->blit_count;
   stats->stretches = presenter->stretch_count;
   stats->max_count = presenter->present_max_count;
   stats->dst_x = presenter->last_dst_x;
   stats->dst_y = presenter->last_dst_y;
   stats->dst_w = presenter->last_dst_w;
   stats->dst_h = presenter->last_dst_h;

   presenter->present_count = 0;
   presenter->present_total_count = 0;
   presenter->present_ge_count = 0;
   presenter->present_sync_count = 0;
   presenter->present_vsync_count = 0;
   presenter->present_pan_count = 0;
   presenter->blit_count = 0;
   presenter->stretch_count = 0;
   presenter->present_max_count = 0;
}
