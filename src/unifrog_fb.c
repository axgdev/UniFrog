#include <unifrog/fb.h>

#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/unistd.h>

#include <kernel/fb.h>
#include <hcuapi/fb.h>

#include <unifrog/log.h>
#include <unifrog/perf.h>

static void clear_fb(struct unifrog_fb *fb)
{
   if (fb) {
      memset(fb, 0, sizeof(*fb));
      fb->fd = -1;
   }
}

static int get_var(int fd, struct fb_var_screeninfo *var)
{
   memset(var, 0, sizeof(*var));
   return ioctl(fd, FBIOGET_VSCREENINFO, var);
}

static int put_format_var(int fd, struct fb_var_screeninfo *var, unsigned flags)
{
   unsigned xoffset = var->xoffset;
   unsigned yoffset = var->yoffset;

   if (flags & UNIFROG_FB_OPEN_PRESERVE) {
      if (var->xres_virtual < var->xres)
         var->xres_virtual = var->xres;
      if (var->yres_virtual < var->yres)
         var->yres_virtual = var->yres;
      if (xoffset + var->xres <= var->xres_virtual)
         var->xoffset = xoffset;
      else
         var->xoffset = 0;
      if (yoffset + var->yres <= var->yres_virtual)
         var->yoffset = yoffset;
      else
         var->yoffset = 0;
   } else {
      var->xoffset = 0;
      var->yoffset = 0;
      var->xres_virtual = var->xres;
      var->yres_virtual = var->yres;
   }
   if (flags & UNIFROG_FB_OPEN_XRGB8888) {
      var->bits_per_pixel = 32;
      var->red.length = 8;
      var->green.length = 8;
      var->blue.length = 8;
      var->transp.length = 0;
   } else {
      var->bits_per_pixel = 16;
      var->red.length = 5;
      var->green.length = 6;
      var->blue.length = 5;
      var->transp.length = 0;
   }
   return ioctl(fd, FBIOPUT_VSCREENINFO, var);
}

static int fb_var_is_usable_format(const struct fb_var_screeninfo *var,
   unsigned flags)
{
   if (!var)
      return 0;
   if (flags & UNIFROG_FB_OPEN_XRGB8888) {
      if (var->bits_per_pixel != 32 ||
          var->red.length != 8 ||
          var->green.length != 8 ||
          var->blue.length != 8 ||
          var->transp.length != 0)
         return 0;
   } else {
      if (var->bits_per_pixel != 16 ||
          var->red.length != 5 ||
          var->green.length != 6 ||
          var->blue.length != 5)
         return 0;
   }
   if (var->xres_virtual < var->xres || var->yres_virtual < var->yres)
      return 0;
   if (var->xoffset + var->xres > var->xres_virtual ||
       var->yoffset + var->yres > var->yres_virtual)
      return 0;
   return 1;
}

int unifrog_fb_open(struct unifrog_fb *fb, unsigned flags)
{
   struct fb_fix_screeninfo fix;
   struct fb_var_screeninfo var;
   int fd;
   int put_var = 0;
   const char *fail_stage = "open";
   size_t screen_bytes;
   uint32_t start_ms;
   uint32_t open_ms;
   uint32_t get1_ms;
   uint32_t put_ms;
   uint32_t get2_ms;
   uint32_t cache_ms;
   uint32_t mmap_ms;
   uint32_t clear_ms = 0;
   uint32_t pan_ms = 0;
   uint32_t blank_ms = 0;

   if (!fb)
      return -1;
   clear_fb(fb);
   fb->fd = -1;

   start_ms = unifrog_perf_time_ms();
   fd = open("/dev/fb0", O_RDWR);
   if (fd < 0) {
      unifrog_log("unifrog fb open flags=0x%x ret=-1 stage=open total_ms=%lu\n",
         flags, (unsigned long)(unifrog_perf_time_ms() - start_ms));
      return -1;
   }
   open_ms = unifrog_perf_time_ms();

   memset(&fix, 0, sizeof(fix));
   if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
       get_var(fd, &var) != 0) {
      fail_stage = "get1";
      goto fail;
   }
   get1_ms = unifrog_perf_time_ms();

   if (!(flags & UNIFROG_FB_OPEN_PRESERVE) ||
       !fb_var_is_usable_format(&var, flags)) {
      put_var = 1;
      if (put_format_var(fd, &var, flags) != 0) {
         fail_stage = "put_var";
         goto fail;
      }
   }
   put_ms = unifrog_perf_time_ms();

   if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
       get_var(fd, &var) != 0) {
      fail_stage = "get2";
      goto fail;
   }
   get2_ms = unifrog_perf_time_ms();

   ioctl(fd, HCFBIOSET_MMAP_CACHE,
      (flags & UNIFROG_FB_OPEN_CACHED) ? HCFB_MMAP_CACHE : HCFB_MMAP_NO_CACHE);
   cache_ms = unifrog_perf_time_ms();

   fb->pixels = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (fb->pixels == MAP_FAILED) {
      fb->pixels = NULL;
      fail_stage = "mmap";
      goto fail;
   }
   mmap_ms = unifrog_perf_time_ms();

   fb->fd = fd;
   fb->width = var.xres;
   fb->height = var.yres;
   fb->bpp = var.bits_per_pixel;
   fb->pitch_bytes = fix.line_length ?
      fix.line_length : (var.xres * var.bits_per_pixel / 8);
   fb->stride_pixels = fb->pitch_bytes / sizeof(uint16_t);
   fb->smem_len = fix.smem_len;
   fb->visible_bytes = (size_t)fb->pitch_bytes * fb->height;
   screen_bytes = fb->visible_bytes;
   fb->max_buffers = screen_bytes ? fix.smem_len / screen_bytes : 1;
   if (fb->max_buffers == 0)
      fb->max_buffers = 1;
   fb->buffer_count = var.yres ? var.yres_virtual / var.yres : 1;
   if (fb->buffer_count == 0 || fb->buffer_count > fb->max_buffers)
      fb->buffer_count = 1;
   fb->current_buffer = var.yres ? var.yoffset / var.yres : 0;
   if (fb->current_buffer >= fb->buffer_count)
      fb->current_buffer = 0;
   if (!(flags & UNIFROG_FB_OPEN_PRESERVE)) {
      memset(fb->pixels, 0, fb->visible_bytes);
      unifrog_perf_cache_flush(fb->pixels, fb->visible_bytes);
      clear_ms = unifrog_perf_time_ms();
      (void)unifrog_fb_pan(fb, 0);
      pan_ms = unifrog_perf_time_ms();
      ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
      blank_ms = unifrog_perf_time_ms();
   }
   unifrog_log("unifrog fb open flags=0x%x preserve=%u put_var=%d "
      "total_ms=%lu open_ms=%lu get1_ms=%lu put_ms=%lu get2_ms=%lu "
      "cache_ms=%lu mmap_ms=%lu clear_ms=%lu pan_ms=%lu blank_ms=%lu "
      "%ux%u bpp=%u stride=%u current=%u buffers=%u max=%u smem=%lu\n",
      flags, (flags & UNIFROG_FB_OPEN_PRESERVE) ? 1u : 0u, put_var,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      (unsigned long)(open_ms - start_ms),
      (unsigned long)(get1_ms - open_ms),
      (unsigned long)(put_ms - get1_ms),
      (unsigned long)(get2_ms - put_ms),
      (unsigned long)(cache_ms - get2_ms),
      (unsigned long)(mmap_ms - cache_ms),
      clear_ms ? (unsigned long)(clear_ms - mmap_ms) : 0ul,
      pan_ms ? (unsigned long)(pan_ms - clear_ms) : 0ul,
      blank_ms ? (unsigned long)(blank_ms - pan_ms) : 0ul,
      fb->width, fb->height, fb->bpp, fb->stride_pixels,
      fb->current_buffer, fb->buffer_count, fb->max_buffers,
      (unsigned long)fb->smem_len);
   return 0;

fail:
   unifrog_log("unifrog fb open flags=0x%x ret=-1 stage=%s total_ms=%lu\n",
      flags, fail_stage, (unsigned long)(unifrog_perf_time_ms() - start_ms));
   close(fd);
   clear_fb(fb);
   fb->fd = -1;
   return -1;
}

void unifrog_fb_close(struct unifrog_fb *fb)
{
   if (!fb)
      return;
   if (fb->pixels)
      munmap(fb->pixels, fb->smem_len);
   if (fb->fd >= 0)
      close(fb->fd);
   clear_fb(fb);
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
   if (!fb || !fb->pixels || buffer_index >= fb->buffer_count)
      return surface;
   if (fb->bpp != 16)
      return surface;
   surface.pixels = fb->pixels + buffer_index * fb->stride_pixels * fb->height;
   surface.width = fb->width;
   surface.height = fb->height;
   surface.stride = fb->stride_pixels;
   return surface;
}

struct unifrog_ge_surface unifrog_fb_ge_surface(const struct unifrog_fb *fb)
{
   return unifrog_fb_ge_surface_for_buffer(fb, fb ? fb->current_buffer : 0);
}

struct unifrog_ge_surface unifrog_fb_ge_surface_for_buffer(const struct unifrog_fb *fb,
   unsigned buffer_index)
{
   struct unifrog_ge_surface surface;

   memset(&surface, 0, sizeof(surface));
   if (!fb || !fb->pixels || buffer_index >= fb->buffer_count)
      return surface;
   surface.pixels = (uint8_t *)fb->pixels +
      (size_t)buffer_index * fb->pitch_bytes * fb->height;
   surface.width = fb->width;
   surface.height = fb->height;
   surface.pitch_bytes = fb->pitch_bytes;
   surface.format = fb->bpp == 32 ?
      UNIFROG_GE_FORMAT_XRGB8888 : UNIFROG_GE_FORMAT_RGB565;
   return surface;
}

void unifrog_fb_flush(const struct unifrog_fb *fb)
{
   unifrog_fb_flush_buffer(fb, fb ? fb->current_buffer : 0);
}

void unifrog_fb_flush_buffer(const struct unifrog_fb *fb, unsigned buffer_index)
{
   if (fb && fb->pixels) {
      uint16_t *base;

      if (buffer_index >= fb->buffer_count)
         return;
      base = (uint16_t *)((uint8_t *)fb->pixels +
         (size_t)buffer_index * fb->pitch_bytes * fb->height);
      unifrog_perf_cache_flush(base, fb->visible_bytes);
   }
}

int unifrog_fb_wait_vsync(const struct unifrog_fb *fb)
{
   int ret = 0;

   if (!fb || fb->fd < 0)
      return -1;
   return ioctl(fb->fd, FBIO_WAITFORVSYNC, &ret);
}

int unifrog_fb_set_buffer_count(struct unifrog_fb *fb, unsigned buffers)
{
   struct fb_var_screeninfo var;
   unsigned old_buffers;
   uint32_t start_ms;
   uint32_t get_ms;
   uint32_t put_ms;

   if (!fb || fb->fd < 0 || buffers == 0 || buffers > fb->max_buffers)
      return -1;
   old_buffers = fb->buffer_count;
   if (old_buffers == buffers) {
      unifrog_log("unifrog fb buffers old=%u requested=%u ret=0 cached=1 current=%u\n",
         old_buffers, buffers, fb->current_buffer);
      return 0;
   }
   start_ms = unifrog_perf_time_ms();
   if (get_var(fb->fd, &var) != 0)
      return -1;
   get_ms = unifrog_perf_time_ms();
   var.yres_virtual = var.yres * buffers;
   if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &var) != 0)
      return -1;
   put_ms = unifrog_perf_time_ms();
   fb->buffer_count = buffers;
   if (fb->current_buffer >= buffers)
      fb->current_buffer = 0;
   unifrog_log("unifrog fb buffers old=%u requested=%u ret=0 "
      "total_ms=%lu get_ms=%lu put_ms=%lu current=%u yvirt=%u\n",
      old_buffers, buffers, (unsigned long)(put_ms - start_ms),
      (unsigned long)(get_ms - start_ms),
      (unsigned long)(put_ms - get_ms), fb->current_buffer,
      var.yres_virtual);
   return 0;
}

int unifrog_fb_pan(struct unifrog_fb *fb, unsigned buffer_index)
{
   struct fb_var_screeninfo var;

   if (!fb || fb->fd < 0 || buffer_index >= fb->buffer_count)
      return -1;
   if (get_var(fb->fd, &var) != 0)
      return -1;
   var.xoffset = 0;
   var.yoffset = buffer_index * fb->height;
   if (ioctl(fb->fd, FBIOPAN_DISPLAY, &var) != 0)
      return -1;
   fb->current_buffer = buffer_index;
   return 0;
}
