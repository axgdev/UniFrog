#include <unifrog/ge.h>

#include <string.h>
#include <sys/ioctl.h>

#include <hcuapi/ge.h>
#include <hcge/ge_api.h>

#include <unifrog/perf.h>

static int format_to_hcge(enum unifrog_ge_format format,
   HCGESurfacePixelFormat *hcge_format)
{
   switch (format) {
   case UNIFROG_GE_FORMAT_RGB565:
      *hcge_format = HCGE_DSPF_RGB16;
      return 0;
   case UNIFROG_GE_FORMAT_ARGB8888:
      *hcge_format = HCGE_DSPF_ARGB;
      return 0;
   default:
      return -1;
   }
}

static unsigned format_bytes_per_pixel(enum unifrog_ge_format format)
{
   switch (format) {
   case UNIFROG_GE_FORMAT_RGB565:
      return 2;
   case UNIFROG_GE_FORMAT_ARGB8888:
      return 4;
   default:
      return 0;
   }
}

static int surface_is_valid(const struct unifrog_ge_surface *surface)
{
   unsigned bytes_per_pixel;

   if (!surface || !surface->pixels ||
       surface->width == 0 || surface->height == 0)
      return 0;
   bytes_per_pixel = format_bytes_per_pixel(surface->format);
   if (bytes_per_pixel == 0)
      return 0;
   return surface && surface->pixels &&
      surface->pitch_bytes >= surface->width * bytes_per_pixel;
}

static int rect_is_valid(const struct unifrog_ge_rect *rect)
{
   return rect && rect->w > 0 && rect->h > 0;
}

static size_t surface_bytes(const struct unifrog_ge_surface *surface)
{
   return (size_t)surface->pitch_bytes * (size_t)surface->height;
}

static int source_rect_is_valid(const struct unifrog_ge_surface *surface,
   const struct unifrog_ge_rect *rect)
{
   if (!surface_is_valid(surface) || !rect_is_valid(rect))
      return 0;
   if (rect->x < 0 || rect->y < 0)
      return 0;
   if (rect->x + rect->w > (int)surface->width ||
       rect->y + rect->h > (int)surface->height)
      return 0;
   return 1;
}

static void flush_surface(const struct unifrog_ge_surface *surface)
{
   if (surface_is_valid(surface))
      unifrog_perf_cache_flush(surface->pixels, surface_bytes(surface));
}

static int setup_surface(HCGE_CoreSurface *hcge_surface,
   HCGE_CoreSurfaceBuffer *buffer,
   const struct unifrog_ge_surface *surface)
{
   HCGESurfacePixelFormat format;

   if (!surface_is_valid(surface) || format_to_hcge(surface->format, &format) != 0)
      return -1;

   memset(hcge_surface, 0, sizeof(*hcge_surface));
   memset(buffer, 0, sizeof(*buffer));
   hcge_surface->config.size.w = surface->width;
   hcge_surface->config.size.h = surface->height;
   hcge_surface->config.format = format;
   buffer->phys = (unsigned long)unifrog_perf_phys_addr(surface->pixels);
   buffer->pitch = surface->pitch_bytes;
   return 0;
}

static void setup_common_state(hcge_state *state)
{
   memset(state, 0, sizeof(*state));
   state->render_options = HCGE_DSRO_NONE;
   state->drawingflags = HCGE_DSDRAW_NOFX;
   state->blittingflags = HCGE_DSBLIT_NOFX;
   state->src_blend = HCGE_DSBF_SRCALPHA;
   state->dst_blend = HCGE_DSBF_ZERO;
}

static unsigned hcge_blit_flags(unsigned flags)
{
   unsigned blit_flags = HCGE_DSBLIT_NOFX;

   if (flags & UNIFROG_GE_ROTATE_90)
      blit_flags |= HCGE_DSBLIT_ROTATE90;
   if (flags & UNIFROG_GE_ROTATE_180)
      blit_flags |= HCGE_DSBLIT_ROTATE180;
   if (flags & UNIFROG_GE_ROTATE_270)
      blit_flags |= HCGE_DSBLIT_ROTATE270;
   return blit_flags;
}

static void setup_clip(hcge_state *state,
   const struct unifrog_ge_surface *dst)
{
   state->mod_hw = HCGE_SMF_CLIP;
   state->clip.x1 = 0;
   state->clip.y1 = 0;
   state->clip.x2 = (int)dst->width - 1;
   state->clip.y2 = (int)dst->height - 1;
}

static hcge_context *ge_context(struct unifrog_ge *ge)
{
   return ge ? (hcge_context *)ge->context : NULL;
}

int unifrog_ge_open(struct unifrog_ge *ge)
{
   hcge_context *ctx = NULL;

   if (!ge)
      return -1;
   memset(ge, 0, sizeof(*ge));
   ge->fd = -1;

   if (hcge_open(&ctx) != 0 || !ctx)
      return -1;

   ge->context = ctx;
   ge->fd = ctx->ge_fd;
   return 0;
}

void unifrog_ge_close(struct unifrog_ge *ge)
{
   hcge_context *ctx = ge_context(ge);

   if (ctx)
      hcge_close(ctx);
   if (ge) {
      ge->context = NULL;
      ge->fd = -1;
   }
}

int unifrog_ge_set_clock(struct unifrog_ge *ge, enum unifrog_ge_clock clock)
{
   hcge_context *ctx = ge_context(ge);

   if (!ctx || ctx->ge_fd < 0)
      return -1;
   return ioctl(ctx->ge_fd, HCGE_SET_CLOCK, clock);
}

int unifrog_ge_set_fast_clock(struct unifrog_ge *ge)
{
   return unifrog_ge_set_clock(ge, UNIFROG_GE_CLOCK_FAST);
}

int unifrog_ge_sync(struct unifrog_ge *ge)
{
   hcge_context *ctx = ge_context(ge);

   if (!ctx)
      return -1;
   return hcge_engine_sync(ctx);
}

int unifrog_ge_fill(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   const struct unifrog_ge_rect *rect,
   uint32_t argb)
{
   hcge_context *ctx = ge_context(ge);
   hcge_state *state;
   HCGERectangle drect;

   if (!ctx || !surface_is_valid(dst) || !rect_is_valid(rect))
      return -1;

   state = &ctx->state;
   setup_common_state(state);
   if (setup_surface(&state->destination, &state->dst, dst) != 0)
      return -1;
   setup_clip(state, dst);
   state->color.a = (argb >> 24) & 0xff;
   state->color.r = (argb >> 16) & 0xff;
   state->color.g = (argb >> 8) & 0xff;
   state->color.b = argb & 0xff;
   drect.x = rect->x;
   drect.y = rect->y;
   drect.w = rect->w;
   drect.h = rect->h;

   state->accel = HCGE_DFXL_FILLRECTANGLE;
   hcge_set_state(ctx, state, state->accel);
   return hcge_fill_rect(ctx, &drect) ? 0 : -1;
}

int unifrog_ge_blit(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   int dst_x, int dst_y,
   const struct unifrog_ge_surface *src,
   const struct unifrog_ge_rect *src_rect,
   unsigned flags)
{
   hcge_context *ctx = ge_context(ge);
   hcge_state *state;
   HCGERectangle srect;

   if (!ctx || !surface_is_valid(dst) ||
       !source_rect_is_valid(src, src_rect))
      return -1;

   if (flags & UNIFROG_GE_FLUSH_SOURCE)
      flush_surface(src);
   if (flags & UNIFROG_GE_FLUSH_DESTINATION)
      flush_surface(dst);

   state = &ctx->state;
   setup_common_state(state);
   state->blittingflags = hcge_blit_flags(flags);
   if (setup_surface(&state->destination, &state->dst, dst) != 0 ||
       setup_surface(&state->source, &state->src, src) != 0)
      return -1;
   setup_clip(state, dst);
   srect.x = src_rect->x;
   srect.y = src_rect->y;
   srect.w = src_rect->w;
   srect.h = src_rect->h;

   state->accel = HCGE_DFXL_BLIT;
   hcge_set_state(ctx, state, state->accel);
   return hcge_blit(ctx, &srect, dst_x, dst_y) ? 0 : -1;
}

int unifrog_ge_stretch(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   const struct unifrog_ge_rect *dst_rect,
   const struct unifrog_ge_surface *src,
   const struct unifrog_ge_rect *src_rect,
   unsigned flags)
{
   hcge_context *ctx = ge_context(ge);
   hcge_state *state;
   HCGERectangle srect;
   HCGERectangle drect;

   if (!ctx || !surface_is_valid(dst) || !rect_is_valid(dst_rect) ||
       !source_rect_is_valid(src, src_rect))
      return -1;

   if (flags & UNIFROG_GE_FLUSH_SOURCE)
      flush_surface(src);
   if (flags & UNIFROG_GE_FLUSH_DESTINATION)
      flush_surface(dst);

   state = &ctx->state;
   setup_common_state(state);
   state->blittingflags = hcge_blit_flags(flags);
   if (setup_surface(&state->destination, &state->dst, dst) != 0 ||
       setup_surface(&state->source, &state->src, src) != 0)
      return -1;
   setup_clip(state, dst);
   srect.x = src_rect->x;
   srect.y = src_rect->y;
   srect.w = src_rect->w;
   srect.h = src_rect->h;
   drect.x = dst_rect->x;
   drect.y = dst_rect->y;
   drect.w = dst_rect->w;
   drect.h = dst_rect->h;

   state->accel = HCGE_DFXL_STRETCHBLIT;
   hcge_set_state(ctx, state, state->accel);
   return hcge_stretch_blit(ctx, &srect, &drect) ? 0 : -1;
}
