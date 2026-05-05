#include <unifrog/fb.h>

#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/unistd.h>

#include <kernel/fb.h>
#include <hcuapi/fb.h>

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

static int put_rgb565_var(int fd, struct fb_var_screeninfo *var)
{
   var->xoffset = 0;
   var->yoffset = 0;
   var->xres_virtual = var->xres;
   var->yres_virtual = var->yres;
   var->bits_per_pixel = 16;
   var->red.length = 5;
   var->green.length = 6;
   var->blue.length = 5;
   return ioctl(fd, FBIOPUT_VSCREENINFO, var);
}

int unifrog_fb_open(struct unifrog_fb *fb, unsigned flags)
{
   struct fb_fix_screeninfo fix;
   struct fb_var_screeninfo var;
   int fd;
   size_t screen_bytes;

   if (!fb)
      return -1;
   clear_fb(fb);
   fb->fd = -1;

   fd = open("/dev/fb0", O_RDWR);
   if (fd < 0)
      return -1;

   memset(&fix, 0, sizeof(fix));
   if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
       get_var(fd, &var) != 0 ||
       put_rgb565_var(fd, &var) != 0 ||
       ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
       get_var(fd, &var) != 0)
      goto fail;

   ioctl(fd, HCFBIOSET_MMAP_CACHE,
      (flags & UNIFROG_FB_OPEN_CACHED) ? HCFB_MMAP_CACHE : HCFB_MMAP_NO_CACHE);

   fb->pixels = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (fb->pixels == MAP_FAILED) {
      fb->pixels = NULL;
      goto fail;
   }

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
   fb->current_buffer = 0;
   memset(fb->pixels, 0, fb->visible_bytes);
   unifrog_perf_cache_flush(fb->pixels, fb->visible_bytes);
   (void)unifrog_fb_pan(fb, 0);
   ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
   return 0;

fail:
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
   if (fb->fd > 0)
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
   surface.pixels = fb->pixels + buffer_index * fb->stride_pixels * fb->height;
   surface.width = fb->width;
   surface.height = fb->height;
   surface.pitch_bytes = fb->pitch_bytes;
   surface.format = UNIFROG_GE_FORMAT_RGB565;
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
      base = fb->pixels + buffer_index * fb->stride_pixels * fb->height;
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

   if (!fb || fb->fd < 0 || buffers == 0 || buffers > fb->max_buffers)
      return -1;
   if (get_var(fb->fd, &var) != 0)
      return -1;
   var.yres_virtual = var.yres * buffers;
   if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &var) != 0)
      return -1;
   fb->buffer_count = buffers;
   if (fb->current_buffer >= buffers)
      fb->current_buffer = 0;
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
