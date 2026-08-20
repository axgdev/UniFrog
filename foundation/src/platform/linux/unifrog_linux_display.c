#include <unifrog/fb.h>
#include <unifrog/linux_host.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t linux_display_lock = PTHREAD_MUTEX_INITIALIZER;
static struct unifrog_fb *linux_active_fb;

static unsigned fb_requested_buffers(unsigned flags)
{
   if (flags & UNIFROG_FB_OPEN_BUFFERS_3)
      return 3;
   if (flags & UNIFROG_FB_OPEN_BUFFERS_2)
      return 2;
   return 1;
}

static unsigned ge_bpp(enum unifrog_ge_format format)
{
   return format == UNIFROG_GE_FORMAT_RGB565 ? 2u : 4u;
}

static uint16_t argb_to_rgb565(uint32_t argb)
{
   return UNIFROG_RGB565((argb >> 16) & 0xff, (argb >> 8) & 0xff,
      argb & 0xff);
}

int unifrog_fb_open(struct unifrog_fb *fb, unsigned flags)
{
   unsigned buffers;
   unsigned bpp;
   size_t bytes;

   if (!fb)
      return -EINVAL;
   memset(fb, 0, sizeof(*fb));
   fb->fd = -1;
   fb->width = 320;
   fb->height = 240;
   fb->bpp = (flags & UNIFROG_FB_OPEN_XRGB8888) ? 32u : 16u;
   bpp = fb->bpp / 8u;
   fb->pitch_bytes = fb->width * bpp;
   fb->stride_pixels = fb->pitch_bytes / sizeof(uint16_t);
   fb->visible_bytes = (size_t)fb->pitch_bytes * fb->height;
   buffers = fb_requested_buffers(flags);
   fb->max_buffers = buffers;
   fb->buffer_count = buffers;
   fb->smem_len = fb->visible_bytes * buffers;
   bytes = fb->smem_len ? fb->smem_len : fb->visible_bytes;
   fb->pixels = calloc(1, bytes);
   if (fb->pixels) {
      pthread_mutex_lock(&linux_display_lock);
      linux_active_fb = fb;
      pthread_mutex_unlock(&linux_display_lock);
   }
   return fb->pixels ? 0 : -ENOMEM;
}

void unifrog_fb_close(struct unifrog_fb *fb)
{
   if (!fb)
      return;
   pthread_mutex_lock(&linux_display_lock);
   if (linux_active_fb == fb)
      linux_active_fb = NULL;
   free(fb->pixels);
   memset(fb, 0, sizeof(*fb));
   fb->fd = -1;
   pthread_mutex_unlock(&linux_display_lock);
}

struct unifrog_surface unifrog_fb_surface(const struct unifrog_fb *fb)
{
   return unifrog_fb_surface_for_buffer(fb, fb ? fb->current_buffer : 0);
}

struct unifrog_surface unifrog_fb_surface_for_buffer(const struct unifrog_fb *fb,
   unsigned buffer_index)
{
   struct unifrog_surface surface;

   memset(&surface, 0, sizeof(surface));
   if (!fb || !fb->pixels || buffer_index >= fb->buffer_count ||
       fb->bpp != 16)
      return surface;
   surface.pixels = fb->pixels +
      (size_t)buffer_index * fb->height * fb->stride_pixels;
   surface.width = fb->width;
   surface.height = fb->height;
   surface.stride = fb->stride_pixels;
   return surface;
}

struct unifrog_ge_surface unifrog_fb_ge_surface(const struct unifrog_fb *fb)
{
   return unifrog_fb_ge_surface_for_buffer(fb, fb ? fb->current_buffer : 0);
}

struct unifrog_ge_surface unifrog_fb_ge_surface_for_buffer(
   const struct unifrog_fb *fb, unsigned buffer_index)
{
   struct unifrog_ge_surface surface;

   memset(&surface, 0, sizeof(surface));
   if (!fb || !fb->pixels || buffer_index >= fb->buffer_count)
      return surface;
   surface.pixels = (unsigned char *)fb->pixels +
      (size_t)buffer_index * fb->visible_bytes;
   surface.width = fb->width;
   surface.height = fb->height;
   surface.pitch_bytes = fb->pitch_bytes;
   surface.format = fb->bpp == 32 ? UNIFROG_GE_FORMAT_XRGB8888 :
      UNIFROG_GE_FORMAT_RGB565;
   return surface;
}

void unifrog_fb_flush(const struct unifrog_fb *fb)
{
   (void)fb;
}

void unifrog_fb_flush_buffer(const struct unifrog_fb *fb, unsigned buffer_index)
{
   (void)fb;
   (void)buffer_index;
}

int unifrog_fb_wait_vsync(const struct unifrog_fb *fb)
{
   (void)fb;
   return 0;
}

int unifrog_fb_set_buffer_count(struct unifrog_fb *fb, unsigned buffers)
{
   if (!fb || buffers == 0 || buffers > fb->max_buffers)
      return -EINVAL;
   fb->buffer_count = buffers;
   if (fb->current_buffer >= fb->buffer_count)
      fb->current_buffer = 0;
   return 0;
}

int unifrog_fb_pan(struct unifrog_fb *fb, unsigned buffer_index)
{
   if (!fb || buffer_index >= fb->buffer_count)
      return -EINVAL;
   pthread_mutex_lock(&linux_display_lock);
   fb->current_buffer = buffer_index;
   linux_active_fb = fb;
   pthread_mutex_unlock(&linux_display_lock);
   return 0;
}

int unifrog_linux_display_copy_rgb565(uint16_t *pixels, unsigned width,
   unsigned height)
{
   struct unifrog_fb *fb;

   if (!pixels)
      return -EINVAL;
   pthread_mutex_lock(&linux_display_lock);
   fb = linux_active_fb;
   if (!fb || !fb->pixels || fb->width != width || fb->height != height) {
      pthread_mutex_unlock(&linux_display_lock);
      return -ENOENT;
   }
   if (fb->bpp == 16) {
      const uint16_t *src = fb->pixels +
         (size_t)fb->current_buffer * fb->height * fb->stride_pixels;

      for (unsigned y = 0; y < height; y++)
         memcpy(pixels + (size_t)y * width,
            src + (size_t)y * fb->stride_pixels,
            (size_t)width * sizeof(*pixels));
   } else {
      const unsigned char *base = (const unsigned char *)fb->pixels +
         (size_t)fb->current_buffer * fb->visible_bytes;

      for (unsigned y = 0; y < height; y++) {
         const uint32_t *src =
            (const uint32_t *)(base + (size_t)y * fb->pitch_bytes);
         uint16_t *dst = pixels + (size_t)y * width;

         for (unsigned x = 0; x < width; x++)
            dst[x] = argb_to_rgb565(src[x]);
      }
   }
   pthread_mutex_unlock(&linux_display_lock);
   return 0;
}

int unifrog_ge_open(struct unifrog_ge *ge)
{
   if (!ge)
      return -EINVAL;
   memset(ge, 0, sizeof(*ge));
   ge->fd = -1;
   ge->context = ge;
   return 0;
}

void unifrog_ge_close(struct unifrog_ge *ge)
{
   if (ge) {
      ge->context = NULL;
      ge->fd = -1;
   }
}

int unifrog_ge_set_clock(struct unifrog_ge *ge, enum unifrog_ge_clock clock)
{
   (void)ge;
   (void)clock;
   return 0;
}

int unifrog_ge_set_fast_clock(struct unifrog_ge *ge)
{
   return unifrog_ge_set_clock(ge, UNIFROG_GE_CLOCK_FAST);
}

int unifrog_ge_sync(struct unifrog_ge *ge)
{
   (void)ge;
   return 0;
}

int unifrog_ge_fill(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   const struct unifrog_ge_rect *rect,
   uint32_t argb)
{
   (void)ge;
   if (!dst || !dst->pixels || !rect || rect->w <= 0 || rect->h <= 0)
      return -EINVAL;
   if (dst->format == UNIFROG_GE_FORMAT_RGB565) {
      uint16_t color = argb_to_rgb565(argb);

      for (int y = 0; y < rect->h; y++) {
         int dy = rect->y + y;
         uint16_t *row;

         if (dy < 0 || dy >= (int)dst->height)
            continue;
         row = (uint16_t *)((unsigned char *)dst->pixels +
            (size_t)dy * dst->pitch_bytes);
         for (int x = 0; x < rect->w; x++) {
            int dx = rect->x + x;

            if (dx >= 0 && dx < (int)dst->width)
               row[dx] = color;
         }
      }
      return 0;
   }
   for (int y = 0; y < rect->h; y++) {
      int dy = rect->y + y;
      uint32_t *row;

      if (dy < 0 || dy >= (int)dst->height)
         continue;
      row = (uint32_t *)((unsigned char *)dst->pixels +
         (size_t)dy * dst->pitch_bytes);
      for (int x = 0; x < rect->w; x++) {
         int dx = rect->x + x;

         if (dx >= 0 && dx < (int)dst->width)
            row[dx] = argb;
      }
   }
   return 0;
}

int unifrog_ge_blit(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   int dst_x, int dst_y,
   const struct unifrog_ge_surface *src,
   const struct unifrog_ge_rect *src_rect,
   unsigned flags)
{
   struct unifrog_ge_rect dst_rect;

   (void)flags;
   if (!src_rect)
      return -EINVAL;
   dst_rect.x = dst_x;
   dst_rect.y = dst_y;
   dst_rect.w = src_rect->w;
   dst_rect.h = src_rect->h;
   return unifrog_ge_stretch(ge, dst, &dst_rect, src, src_rect, 0);
}

int unifrog_ge_stretch(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst,
   const struct unifrog_ge_rect *dst_rect,
   const struct unifrog_ge_surface *src,
   const struct unifrog_ge_rect *src_rect,
   unsigned flags)
{
   unsigned src_bpp;
   unsigned dst_bpp;

   (void)ge;
   (void)flags;
   if (!dst || !src || !dst_rect || !src_rect || !dst->pixels ||
       !src->pixels || dst_rect->w <= 0 || dst_rect->h <= 0 ||
       src_rect->w <= 0 || src_rect->h <= 0)
      return -EINVAL;
   src_bpp = ge_bpp(src->format);
   dst_bpp = ge_bpp(dst->format);
   for (int y = 0; y < dst_rect->h; y++) {
      int dy = dst_rect->y + y;
      int sy = src_rect->y + (y * src_rect->h) / dst_rect->h;

      if (dy < 0 || dy >= (int)dst->height ||
          sy < 0 || sy >= (int)src->height)
         continue;
      for (int x = 0; x < dst_rect->w; x++) {
         int dx = dst_rect->x + x;
         int sx = src_rect->x + (x * src_rect->w) / dst_rect->w;
         const unsigned char *sp;
         unsigned char *dp;

         if (dx < 0 || dx >= (int)dst->width ||
             sx < 0 || sx >= (int)src->width)
            continue;
         sp = (const unsigned char *)src->pixels +
            (size_t)sy * src->pitch_bytes + (size_t)sx * src_bpp;
         dp = (unsigned char *)dst->pixels +
            (size_t)dy * dst->pitch_bytes + (size_t)dx * dst_bpp;
         if (src->format == dst->format) {
            memcpy(dp, sp, dst_bpp);
         } else if (dst->format == UNIFROG_GE_FORMAT_RGB565) {
            uint32_t c = *(const uint32_t *)sp;
            *(uint16_t *)dp = argb_to_rgb565(c);
         } else {
            uint16_t c = *(const uint16_t *)sp;
            uint32_t r = ((c >> 11) & 0x1fu) * 255u / 31u;
            uint32_t g = ((c >> 5) & 0x3fu) * 255u / 63u;
            uint32_t b = (c & 0x1fu) * 255u / 31u;
            *(uint32_t *)dp = 0xff000000u | (r << 16) | (g << 8) | b;
         }
      }
   }
   return 0;
}
