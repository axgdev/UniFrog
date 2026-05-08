#include <unifrog/hw_probe.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unifrog/build_info.h>
#include <unifrog/fb.h>
#include <unifrog/gfx.h>
#include <unifrog/log.h>
#include <unifrog/perf.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct hw_probe_reg {
   const char *name;
   uint32_t addr;
};

struct hw_probe_range {
   const char *name;
   uint32_t start;
   uint32_t end;
   uint32_t step;
};

static const struct hw_probe_reg probe_regs[] = {
   { "CHIP_ID", 0xb8800000u },
   { "IRQ_STATUS1", 0xb8800030u },
   { "IRQ_STATUS2", 0xb8800034u },
   { "IRQ_MASK1", 0xb8800038u },
   { "IRQ_MASK2", 0xb880003cu },
   { "GPIOLCTRL", 0xb8800044u },
   { "CLK_GATE0", 0xb8800060u },
   { "CLK_GATE1", 0xb8800064u },
   { "STRAP_INFO", 0xb8800070u },
   { "STRAP_CTRL", 0xb8800074u },
   { "SYS_CLK_CTR", 0xb8800078u },
   { "VE_GE_CLK", 0xb880007cu },
   { "RESET_CTR0", 0xb8800080u },
   { "RESET_CTR1", 0xb8800084u },
   { "SD_EJTAG", 0xb8800094u },
   { "BOOT_REG_A4", 0xb88000a4u },
   { "GPIOBCTRL", 0xb88000c4u },
   { "GPIORCTRL", 0xb88000e4u },
   { "I2S_DIV", 0xb8800148u },
   { "SPO_DIV", 0xb880014cu },
   { "F128", 0xb8800154u },
   { "MAPPING", 0xb8800220u },
   { "GPIOTCTRL", 0xb8800344u },
   { "CPU_CLK_PLL", 0xb8800380u },
   { "IP_CLK_CTR", 0xb88003c8u },
   { "VID_SRC_CTRL0", 0xb8800444u },
   { "VID_SRC_CTRL1", 0xb8800448u },
   { "VID_SRC_CTRL2", 0xb880044cu },
   { "FS_PLL0", 0xb8800470u },
   { "FS_PLL1", 0xb8800478u },
   { "PINMUXL", 0xb88004a0u },
   { "PINMUXB", 0xb88004c0u },
   { "PINMUXR", 0xb88004e0u },
   { "PINMUXT", 0xb8800500u },
   { "BOOT_OBS_2E090", 0xb882e090u },
   { "BOOT_OBS_2E098", 0xb882e098u },
   { "SDIO_CMD_CTRL", 0xb884c000u },
   { "SDIO_CMD_ARG", 0xb884c004u },
   { "SDIO_BLOCK_SIZE", 0xb884c008u },
   { "SDIO_STATUS", 0xb884c00bu },
   { "SDIO_RESP0", 0xb884c010u },
   { "SDIO_DMA_ADDR", 0xb884c020u },
   { "SDIO_DMA_LEN", 0xb884c028u },
   { "SDIO_XFER_CTRL", 0xb884c030u },
   { "TIMER0_STATUS", 0xb8818a08u },
   { "TIMER1_STATUS", 0xb8818a18u },
   { "TIMER3_STATUS", 0xb8818a38u },
   { "TIMER4_STATUS", 0xb8818a48u },
};

static const struct hw_probe_range probe_ranges[] = {
   { "sysio_head", 0xb8800000u, 0xb88000a4u, 4u },
   { "clock_misc", 0xb8800140u, 0xb8800160u, 4u },
   { "mapping", 0xb8800200u, 0xb8800230u, 4u },
   { "clock_pll", 0xb8800340u, 0xb88003d0u, 4u },
   { "video_src", 0xb8800440u, 0xb8800450u, 4u },
   { "audio_pll", 0xb8800470u, 0xb8800480u, 4u },
   { "pinmux_l", 0xb88004a0u, 0xb88004bcu, 4u },
   { "pinmux_b", 0xb88004c0u, 0xb88004dcu, 4u },
   { "pinmux_r", 0xb88004e0u, 0xb88004fcu, 4u },
   { "pinmux_t", 0xb8800500u, 0xb880051cu, 4u },
   { "boot_obs", 0xb882e080u, 0xb882e0a0u, 4u },
   { "timer_irq", 0xb8818a00u, 0xb8818a5cu, 4u },
   { "sdio", 0xb884c000u, 0xb884c04cu, 4u },
};

static uint32_t hw_read32(uint32_t addr)
{
   return *(volatile const uint32_t *)addr;
}

static uint16_t hw_read16(uint32_t addr)
{
   return *(volatile const uint16_t *)addr;
}

static uint8_t hw_read8(uint32_t addr)
{
   return *(volatile const uint8_t *)addr;
}

static void draw_status(struct unifrog_fb *fb, const char *line1,
   const char *line2, const char *line3)
{
   struct unifrog_surface surface;

   if (!fb || !fb->pixels)
      return;

   surface = unifrog_fb_surface(fb);
   unifrog_gfx_fill_rect(&surface, 0, 0, (int)surface.width,
      (int)surface.height, UNIFROG_RGB565(4, 8, 14));
   unifrog_gfx_draw_text(&surface, 10, 14, "UniFrog HW Probe",
      UNIFROG_RGB565(210, 232, 255), 2);
   unifrog_gfx_draw_text(&surface, 10, 48, line1 ? line1 : "",
      UNIFROG_RGB565(160, 210, 180), 1);
   unifrog_gfx_draw_text(&surface, 10, 64, line2 ? line2 : "",
      UNIFROG_RGB565(245, 245, 220), 1);
   unifrog_gfx_draw_text(&surface, 10, 80, line3 ? line3 : "",
      UNIFROG_RGB565(210, 210, 210), 1);
   unifrog_gfx_draw_text(&surface, 10, 218, "Logs: latest_log/logunifrog*.txt",
      UNIFROG_RGB565(140, 160, 180), 1);
   unifrog_fb_flush(fb);
}

static void log_reg32(const char *kind, const char *name, uint32_t addr)
{
   uint32_t value = hw_read32(addr);

   unifrog_log("hwprobe %s name=%s addr=0x%08lx value32=0x%08lx t_ms=%lu\n",
      kind, name, (unsigned long)addr, (unsigned long)value,
      (unsigned long)unifrog_perf_time_ms());
}

static void log_width_probe(void)
{
   uint32_t addr = 0xb8800000u;

   unifrog_log("hwprobe width name=CHIP_ID addr=0x%08lx value8_0=0x%02lx value8_1=0x%02lx value8_2=0x%02lx value8_3=0x%02lx value16_0=0x%04lx value16_2=0x%04lx value32=0x%08lx\n",
      (unsigned long)addr,
      (unsigned long)hw_read8(addr + 0u),
      (unsigned long)hw_read8(addr + 1u),
      (unsigned long)hw_read8(addr + 2u),
      (unsigned long)hw_read8(addr + 3u),
      (unsigned long)hw_read16(addr + 0u),
      (unsigned long)hw_read16(addr + 2u),
      (unsigned long)hw_read32(addr));
}

static void log_stability(const char *name, uint32_t addr)
{
   unsigned i;
   unsigned changes = 0;
   uint32_t first = hw_read32(addr);
   uint32_t prev = first;
   uint32_t last = first;
   uint32_t min = first;
   uint32_t max = first;

   for (i = 1; i < 8; i++) {
      unifrog_perf_delay_us(1000);
      last = hw_read32(addr);
      if (last != prev)
         changes++;
      if (last < min)
         min = last;
      if (last > max)
         max = last;
      prev = last;
   }

   unifrog_log("hwprobe stability name=%s addr=0x%08lx first=0x%08lx last=0x%08lx min=0x%08lx max=0x%08lx changes=%u\n",
      name, (unsigned long)addr, (unsigned long)first,
      (unsigned long)last, (unsigned long)min, (unsigned long)max,
      changes);
}

static void log_counter_probe(void)
{
   uint32_t c0;
   uint32_t c1;
   uint32_t ms0;
   uint32_t ms1;

   ms0 = unifrog_perf_time_ms();
   c0 = unifrog_perf_count();
   unifrog_perf_delay_us(100000);
   c1 = unifrog_perf_count();
   ms1 = unifrog_perf_time_ms();
   unifrog_log("hwprobe counter ms_delta=%lu count_delta=%lu count_per_ms=%lu c0=0x%08lx c1=0x%08lx\n",
      (unsigned long)(ms1 - ms0),
      (unsigned long)unifrog_perf_elapsed(c0, c1),
      (unsigned long)((ms1 != ms0) ? unifrog_perf_elapsed(c0, c1) / (ms1 - ms0) : 0u),
      (unsigned long)c0, (unsigned long)c1);
}

static void log_caps(void)
{
   struct unifrog_perf_caps caps;

   memset(&caps, 0, sizeof(caps));
   if (unifrog_perf_query_caps(&caps) != 0) {
      unifrog_log("hwprobe caps ret=-1\n");
      return;
   }

   unifrog_log("hwprobe caps mask=0x%08lx scpu_selector=%u scpu_mhz_est=%u fb=%ux%u bpp=%u stride=%u bytes=%u buffers=%u ge_cmdq=%u\n",
      (unsigned long)caps.caps, caps.scpu_selector, caps.scpu_mhz_est,
      caps.framebuffer_width, caps.framebuffer_height, caps.framebuffer_bpp,
      caps.framebuffer_stride_bytes, caps.framebuffer_bytes,
      caps.framebuffer_buffers, caps.ge_cmdq_bytes);
}

int unifrog_hw_probe_run(void)
{
   struct unifrog_fb fb;
   int fb_ret;
   unsigned i;
   char line[64];

   memset(&fb, 0, sizeof(fb));
   unifrog_log_set_auto_flush_bytes(512);
   unifrog_log("hwprobe begin version=1 commit=%s dirty=%d sdk=%s media=%s t_ms=%lu\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY, UNIFROG_SDK_GIT_COMMIT,
      UNIFROG_HCRTOS_MEDIA, (unsigned long)unifrog_perf_time_ms());
   (void)unifrog_log_flush();

   fb_ret = unifrog_fb_open(&fb, UNIFROG_FB_OPEN_DEFAULT);
   unifrog_log("hwprobe framebuffer open_ret=%d width=%u height=%u stride=%u bpp=%u bytes=%lu buffers=%u\n",
      fb_ret, fb.width, fb.height, fb.stride_pixels, fb.bpp,
      (unsigned long)fb.visible_bytes, fb.buffer_count);
   draw_status(fb_ret == 0 ? &fb : NULL, "framebuffer ready",
      "capturing read-only registers", "phase 1/5");
   (void)unifrog_log_flush();

   log_caps();
   log_counter_probe();
   log_width_probe();
   (void)unifrog_log_flush();

   draw_status(fb_ret == 0 ? &fb : NULL, "named register snapshot",
      "capturing known boot/sysio state", "phase 2/5");
   for (i = 0; i < ARRAY_SIZE(probe_regs); i++) {
      snprintf(line, sizeof(line), "%s 0x%08lx", probe_regs[i].name,
         (unsigned long)probe_regs[i].addr);
      draw_status(fb_ret == 0 ? &fb : NULL, "named register snapshot",
         line, "phase 2/5");
      log_reg32("reg", probe_regs[i].name, probe_regs[i].addr);
      if ((i & 7u) == 7u)
         (void)unifrog_log_flush();
   }
   (void)unifrog_log_flush();

   draw_status(fb_ret == 0 ? &fb : NULL, "stability sampling",
      "watching volatile registers", "phase 3/5");
   log_stability("IRQ_STATUS1", 0xb8800030u);
   log_stability("IRQ_STATUS2", 0xb8800034u);
   log_stability("SYS_CLK_CTR", 0xb8800078u);
   log_stability("CPU_CLK_PLL", 0xb8800380u);
   log_stability("TIMER0_STATUS", 0xb8818a08u);
   log_stability("TIMER1_STATUS", 0xb8818a18u);
   log_stability("TIMER3_STATUS", 0xb8818a38u);
   log_stability("TIMER4_STATUS", 0xb8818a48u);
   log_stability("SDIO_STATUS", 0xb884c00bu);
   log_stability("SDIO_XFER_CTRL", 0xb884c030u);
   (void)unifrog_log_flush();

   for (i = 0; i < ARRAY_SIZE(probe_ranges); i++) {
      uint32_t addr;

      snprintf(line, sizeof(line), "%s 0x%08lx", probe_ranges[i].name,
         (unsigned long)probe_ranges[i].start);
      draw_status(fb_ret == 0 ? &fb : NULL, "range snapshot",
         line, "phase 4/5");
      unifrog_log("hwprobe range begin name=%s start=0x%08lx end=0x%08lx step=%lu\n",
         probe_ranges[i].name, (unsigned long)probe_ranges[i].start,
         (unsigned long)probe_ranges[i].end,
         (unsigned long)probe_ranges[i].step);
      for (addr = probe_ranges[i].start; addr <= probe_ranges[i].end;
           addr += probe_ranges[i].step) {
         log_reg32("range", probe_ranges[i].name, addr);
      }
      unifrog_log("hwprobe range end name=%s\n", probe_ranges[i].name);
      (void)unifrog_log_flush();
   }

   draw_status(fb_ret == 0 ? &fb : NULL, "probe complete",
      "power off after logs flush", "phase 5/5");
   unifrog_log("hwprobe end t_ms=%lu pending=%lu last_result=%d path=%s\n",
      (unsigned long)unifrog_perf_time_ms(),
      (unsigned long)unifrog_log_pending(), unifrog_log_last_result(),
      unifrog_log_last_path() ? unifrog_log_last_path() : "?");
   (void)unifrog_log_flush_force();

   if (fb_ret == 0)
      unifrog_fb_close(&fb);
   return 0;
}
