#include <unifrog/boot_logo.h>

#include <stdint.h>
#include <stdio.h>

#include <unifrog/av.h>
#include <unifrog/backlight.h>
#include <unifrog/boot_trace.h>
#include <unifrog/gfx.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>

#define printf unifrog_log

#define BOOT_LOGO_WIDTH 256u
#define BOOT_LOGO_HEIGHT 100u
#define BOOT_LOGO_BACKLIGHT 70u

#include "../assets/boot/unifrog-logo-rgb565.inc"

static struct unifrog_fb early_logo_fb;
static int early_logo_fb_open;
static int boot_logo_active;

static int draw_logo(struct unifrog_fb *fb, const char *tag)
{
   struct unifrog_surface surface;
   unsigned x0;
   unsigned y0;
   unsigned x;
   unsigned y;
   unsigned pos = 0;
   unsigned i;
   uint16_t *dst;
   int ret;

   if (!fb || !fb->pixels ||
       fb->width < BOOT_LOGO_WIDTH ||
       fb->height < BOOT_LOGO_HEIGHT)
      return -1;

   surface = unifrog_fb_surface(fb);
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height, 0);
   x0 = (surface.width - BOOT_LOGO_WIDTH) / 2u;
   y0 = (surface.height - BOOT_LOGO_HEIGHT) / 2u;
   for (i = 0; i + 1 < UNIFROG_BOOT_LOGO_RLE_WORDS; i += 2u) {
      uint16_t color = unifrog_boot_logo_rle[i];
      unsigned count = unifrog_boot_logo_rle[i + 1u];
      while (count-- && pos < BOOT_LOGO_WIDTH * BOOT_LOGO_HEIGHT) {
         x = pos % BOOT_LOGO_WIDTH;
         y = pos / BOOT_LOGO_WIDTH;
         dst = surface.pixels + (y0 + y) * surface.stride + x0;
         dst[x] = color;
         pos++;
      }
   }

   unifrog_fb_flush(fb);
   (void)unifrog_fb_pan(fb, fb->current_buffer);
   (void)unifrog_av_set_mode(0);
   ret = unifrog_backlight_set(BOOT_LOGO_BACKLIGHT);
   boot_logo_active = 1;
   unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_BOOT_LOGO_DONE,
      unifrog_perf_time_ms(), BOOT_LOGO_BACKLIGHT, (uint32_t)ret);
   unifrog_boot_trace_log("boot.logo_done");
   printf("unifrog boot_logo shown tag=%s %ux%u backlight=%u ret=%d\n",
      tag ? tag : "", BOOT_LOGO_WIDTH, BOOT_LOGO_HEIGHT,
      BOOT_LOGO_BACKLIGHT, ret);
   return ret;
}

int unifrog_boot_logo_present(struct unifrog_fb *fb, const char *tag)
{
   return draw_logo(fb, tag);
}

int unifrog_boot_logo_present_early(void)
{
   int ret;

   if (early_logo_fb_open)
      return 0;
   if (unifrog_fb_open(&early_logo_fb, UNIFROG_FB_OPEN_DEFAULT) != 0) {
      printf("unifrog boot_logo early fb_open failed\n");
      return -1;
   }
   if (unifrog_fb_set_buffer_count(&early_logo_fb, 2) != 0)
      (void)unifrog_fb_set_buffer_count(&early_logo_fb, 1);
   early_logo_fb_open = 1;
   ret = draw_logo(&early_logo_fb, "early");
   if (ret != 0) {
      unifrog_fb_close(&early_logo_fb);
      early_logo_fb_open = 0;
      boot_logo_active = 0;
      return ret;
   }
   return 0;
}

int unifrog_boot_logo_is_active(void)
{
   return boot_logo_active;
}

void unifrog_boot_logo_mark_replaced(void)
{
   boot_logo_active = 0;
}

void unifrog_boot_logo_release_early(void)
{
   if (!early_logo_fb_open)
      return;
   unifrog_fb_close(&early_logo_fb);
   early_logo_fb_open = 0;
}
