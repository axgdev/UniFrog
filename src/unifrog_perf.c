#include <unifrog/perf.h>

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>

#include <cpu_func.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <kernel/fb.h>
#include <kernel/io.h>
#include <hcuapi/ge.h>
#include <hcge/ge_api.h>

#define MSYSIO_BASE 0xb8800000u
#define HC1512_CHIP_ID 0x1512u
#define MIPS_CACHE_INDEX_BASE 0x80000000u
#define MIPS_CACHE_INDEX_END  0x80004000u
#define MIPS_CACHE_LINE_BYTES 16u

static unsigned scpu_selector_to_mhz(unsigned selector)
{
   switch (selector) {
   case 0:
      return 594;
   case 1:
      return 396;
   case 2:
      return 297;
   case 3:
   case 4:
   case 5:
   case 6:
      return 198;
   default:
      return 0;
   }
}

static unsigned scpu_mctrl2_to_mhz(uint32_t mctrl)
{
   uint32_t n = mctrl & 0xffffu;

   if (n & 0x8000u)
      n &= 0x7fffu;

   return (unsigned)(((n * 27u) + 9u) / 10u);
}

static void query_scpu(struct unifrog_perf_caps *caps)
{
   unsigned selector;

   if (REG32_GET_FIELD2(MSYSIO_BASE + 0x0, 16, 16) != HC1512_CHIP_ID)
      return;

   selector = (REG32_READ(MSYSIO_BASE + 0x74) >> 8) & 0x7u;
   caps->scpu_selector = selector;
   if (selector == 7 && ((REG32_READ(MSYSIO_BASE + 0x7c) >> 7) & 1u))
      caps->scpu_mhz_est = scpu_mctrl2_to_mhz(REG32_READ(MSYSIO_BASE + 0x380) >> 16);
   else
      caps->scpu_mhz_est = scpu_selector_to_mhz(selector);
}

static uint64_t current_count_ticks_per_us(void)
{
   static uint32_t cached_reg074;
   static uint32_t cached_reg07c;
   static uint32_t cached_reg380;
   static uint64_t cached_ticks_per_us;
   uint32_t reg074;
   uint32_t reg07c;
   uint32_t reg380;
   unsigned selector;
   unsigned mhz;

   if (REG32_GET_FIELD2(MSYSIO_BASE + 0x0, 16, 16) != HC1512_CHIP_ID)
      return 405;

   reg074 = REG32_READ(MSYSIO_BASE + 0x74);
   reg07c = REG32_READ(MSYSIO_BASE + 0x7c);
   reg380 = REG32_READ(MSYSIO_BASE + 0x380);
   if (cached_ticks_per_us && reg074 == cached_reg074 &&
       reg07c == cached_reg07c && reg380 == cached_reg380)
      return cached_ticks_per_us;

   selector = (reg074 >> 8) & 0x7u;
   if (selector == 7 && ((reg07c >> 7) & 1u))
      mhz = scpu_mctrl2_to_mhz(reg380 >> 16);
   else
      mhz = scpu_selector_to_mhz(selector);

   cached_ticks_per_us = mhz ? ((uint64_t)mhz + 1ull) / 2ull : 405ull;
   if (!cached_ticks_per_us)
      cached_ticks_per_us = 1;
   cached_reg074 = reg074;
   cached_reg07c = reg07c;
   cached_reg380 = reg380;
   return cached_ticks_per_us;
}

static void query_framebuffer(struct unifrog_perf_caps *caps)
{
   struct fb_fix_screeninfo fix;
   struct fb_var_screeninfo var;
   unsigned screen_bytes;
   int fd = open("/dev/fb0", O_RDWR);

   if (fd < 0)
      return;

   memset(&fix, 0, sizeof(fix));
   memset(&var, 0, sizeof(var));
   if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0 &&
       ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0) {
      caps->caps |= UNIFROG_PERF_CAP_FRAMEBUFFER |
         UNIFROG_PERF_CAP_FRAMEBUFFER_VSYNC;
      if (fix.ypanstep || fix.ywrapstep || fix.smem_len)
         caps->caps |= UNIFROG_PERF_CAP_FRAMEBUFFER_PAN;
      caps->framebuffer_width = var.xres;
      caps->framebuffer_height = var.yres;
      caps->framebuffer_bpp = var.bits_per_pixel;
      caps->framebuffer_stride_bytes = fix.line_length ?
         fix.line_length : (var.xres * var.bits_per_pixel / 8);
      caps->framebuffer_bytes = fix.smem_len;
      screen_bytes = var.xres * var.yres * (var.bits_per_pixel / 8);
      caps->framebuffer_buffers = screen_bytes ? fix.smem_len / screen_bytes : 0;
   }

   close(fd);
}

static void query_ge(struct unifrog_perf_caps *caps)
{
   hcge_context *ctx = NULL;

   if (hcge_open(&ctx) != 0 || !ctx)
      return;

   caps->caps |= UNIFROG_PERF_CAP_GE_FILL |
      UNIFROG_PERF_CAP_GE_BLIT |
      UNIFROG_PERF_CAP_GE_STRETCH |
      UNIFROG_PERF_CAP_GE_CLOCK;

   if (ctx->ge_fd >= 0) {
      cmdq_buf_info_t info;

      memset(&info, 0, sizeof(info));
      if (ioctl(ctx->ge_fd, HCGE_GET_CMDQ_BUFINFO, &info) == 0)
         caps->ge_cmdq_bytes = info.size;
   }

   hcge_close(ctx);
}

static void add_if_open(struct unifrog_perf_caps *caps,
   const char *path, uint32_t cap)
{
   int fd = open(path, O_RDWR);

   if (fd < 0)
      fd = open(path, O_RDONLY);
   if (fd >= 0) {
      caps->caps |= cap;
      close(fd);
   }
}

static void add_if_dir(struct unifrog_perf_caps *caps,
   const char *path, uint32_t cap)
{
   DIR *dir = opendir(path);

   if (dir) {
      caps->caps |= cap;
      closedir(dir);
   }
}

uint32_t unifrog_perf_count(void)
{
   uint32_t count;

   __asm__ volatile("mfc0 %0, $9" : "=r"(count));
   return count;
}

uint32_t unifrog_perf_elapsed(uint32_t start, uint32_t end)
{
   return end - start;
}

uint64_t unifrog_perf_time_us(void)
{
   return ((uint64_t)xTaskGetTickCount() * 1000000ull) /
      (uint64_t)configTICK_RATE_HZ;
}

uint32_t unifrog_perf_time_ms(void)
{
   return (uint32_t)(((uint64_t)xTaskGetTickCount() * 1000ull) /
      (uint64_t)configTICK_RATE_HZ);
}

void unifrog_perf_delay_us(unsigned us)
{
   uint32_t start;
   uint64_t target;

   if (!us)
      return;

   target = (uint64_t)us * current_count_ticks_per_us();
   if (target > UINT32_MAX)
      target = UINT32_MAX;

   start = unifrog_perf_count();
   while ((uint64_t)unifrog_perf_elapsed(start, unifrog_perf_count()) < target)
      ;
}

void unifrog_perf_cache_flush(const void *ptr, size_t len)
{
   if (ptr && len)
      cache_flush((void *)ptr, (uint32_t)len);
}

void unifrog_perf_cache_invalidate(const void *ptr, size_t len)
{
   if (ptr && len)
      cache_invalidate((void *)ptr, (uint32_t)len);
}

void unifrog_perf_cache_flush_invalidate(const void *ptr, size_t len)
{
   if (ptr && len)
      cache_flush_invalidate((void *)ptr, (uint32_t)len);
}

static void mips_cache_sync_all(void)
{
   uintptr_t idx;

   /*
    * HCRTOS cache_flush_all() only covers D-cache on this SDK path. JIT users
    * such as gpSP need emitted code visible to I-cache as well.
    */
   for (idx = MIPS_CACHE_INDEX_BASE; idx < MIPS_CACHE_INDEX_END;
        idx += MIPS_CACHE_LINE_BYTES) {
      __asm__ volatile(
         ".set push\n"
         ".set noreorder\n"
         ".set mips3\n"
         "cache 0x01, 0(%0)\n"
         "cache 0x01, 0(%0)\n"
         ".set pop\n"
         :
         : "r"(idx)
         : "memory");
   }

   __asm__ volatile(
      ".set push\n"
      ".set noreorder\n"
      ".set mips32\n"
      "sync\n"
      ".set pop\n"
      ::: "memory");

   for (idx = MIPS_CACHE_INDEX_BASE; idx < MIPS_CACHE_INDEX_END;
        idx += MIPS_CACHE_LINE_BYTES) {
      __asm__ volatile(
         ".set push\n"
         ".set noreorder\n"
         ".set mips3\n"
         "cache 0x00, 0(%0)\n"
         "cache 0x00, 0(%0)\n"
         ".set pop\n"
         :
         : "r"(idx)
         : "memory");
   }

   __asm__ volatile("nop; nop; nop; nop; nop" ::: "memory");
}

void unifrog_perf_cache_flush_all(void)
{
   mips_cache_sync_all();
}

void _flush_cache(void *ptr, int len, int cache)
{
   (void)cache;

   if (!ptr || len <= 0)
      return;

   mips_cache_sync_all();
}

void *unifrog_perf_cached_addr(const void *ptr)
{
   return UNIFROG_PERF_CACHED_ALIAS(ptr);
}

void *unifrog_perf_uncached_addr(const void *ptr)
{
   return UNIFROG_PERF_UNCACHED_ALIAS(ptr);
}

uintptr_t unifrog_perf_phys_addr(const void *ptr)
{
   return UNIFROG_PERF_PHYS_ALIAS(ptr);
}

int unifrog_perf_query_caps(struct unifrog_perf_caps *caps)
{
   if (!caps)
      return -1;

   memset(caps, 0, sizeof(*caps));
   caps->caps = UNIFROG_PERF_CAP_CP0_COUNT |
      UNIFROG_PERF_CAP_CACHE_CONTROL |
      UNIFROG_PERF_CAP_UNCACHED_ALIAS |
      UNIFROG_PERF_CAP_WIRELESS_GAMEPAD;

   query_scpu(caps);
   query_framebuffer(caps);
   query_ge(caps);
   add_if_open(caps, "/dev/dis", UNIFROG_PERF_CAP_DISPLAY_CONTROLLER);
   add_if_open(caps, "/dev/viddec", UNIFROG_PERF_CAP_HARDWARE_VIDEO);
   add_if_open(caps, "/dev/vidsink", UNIFROG_PERF_CAP_HARDWARE_VIDEO);
   add_if_open(caps, "/dev/sndC0i2so", UNIFROG_PERF_CAP_AUDIO_OUTPUT);
   add_if_open(caps, "/dev/mmcblk0", UNIFROG_PERF_CAP_SD_STORAGE);
   add_if_dir(caps, "/media/mmcblk0", UNIFROG_PERF_CAP_SD_STORAGE);
   add_if_open(caps, "/dev/adc", UNIFROG_PERF_CAP_ADC);
   add_if_open(caps, "/dev/queryadc0", UNIFROG_PERF_CAP_ADC);
   add_if_open(caps, "/dev/backlight", UNIFROG_PERF_CAP_BACKLIGHT_PWM);
   add_if_open(caps, "/dev/pwm2", UNIFROG_PERF_CAP_BACKLIGHT_PWM);
   add_if_open(caps, "/dev/mmz", UNIFROG_PERF_CAP_MMZ);
   add_if_open(caps, "/dev/dsc", UNIFROG_PERF_CAP_DSC);
   return 0;
}
