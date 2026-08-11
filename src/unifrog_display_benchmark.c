#include <unifrog/display_benchmark.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <kernel/delay.h>

#include <unifrog/fb.h>
#include <unifrog/ge.h>
#include <unifrog/log.h>
#include <unifrog/paths.h>
#include <unifrog/perf.h>

#define DISPLAY_BENCH_FRAMES 60u
#define DISPLAY_COLOR_REPORT UNIFROG_REPORT_ROOT "/display-color-test.txt"

struct display_bench_case {
   const char *name;
   unsigned fb_flags;
   enum unifrog_ge_format src_format;
   unsigned src_bpp;
};

static void write_summary(char *summary, size_t summary_size,
   const char *text)
{
   if (!summary || summary_size == 0)
      return;
   snprintf(summary, summary_size, "%s", text ? text : "");
}

static void fill_rgb565(uint16_t *pixels, unsigned width, unsigned height)
{
   for (unsigned y = 0; y < height; y++) {
      for (unsigned x = 0; x < width; x++) {
         unsigned r = (x * 255u) / (width ? width : 1u);
         unsigned g = (y * 255u) / (height ? height : 1u);
         unsigned b = ((x ^ y) * 255u) / ((width + height) ? (width + height) : 1u);

         pixels[y * width + x] = (uint16_t)(((r & 0xf8u) << 8) |
            ((g & 0xfcu) << 3) | (b >> 3));
      }
   }
}

static void fill_xrgb8888(uint32_t *pixels, unsigned width, unsigned height)
{
   for (unsigned y = 0; y < height; y++) {
      for (unsigned x = 0; x < width; x++) {
         unsigned r = (x * 255u) / (width ? width : 1u);
         unsigned g = (y * 255u) / (height ? height : 1u);
         unsigned b = ((x ^ y) * 255u) / ((width + height) ? (width + height) : 1u);

         pixels[y * width + x] = 0xff000000u | (r << 16) | (g << 8) | b;
      }
   }
}

static int write_fb_ppm(const char *path, const struct unifrog_fb *fb)
{
   FILE *file;

   if (!path || !fb || !fb->pixels || !fb->width || !fb->height)
      return -1;
   file = fopen(path, "wb");
   if (!file)
      return -1;
   fprintf(file, "P6\n%u %u\n255\n", fb->width, fb->height);
   for (unsigned y = 0; y < fb->height; y++) {
      const uint8_t *row = (const uint8_t *)fb->pixels +
         (size_t)y * fb->pitch_bytes;

      for (unsigned x = 0; x < fb->width; x++) {
         uint8_t rgb[3];

         if (fb->bpp == 32) {
            uint32_t c = ((const uint32_t *)row)[x];

            rgb[0] = (uint8_t)(c >> 16);
            rgb[1] = (uint8_t)(c >> 8);
            rgb[2] = (uint8_t)c;
         } else {
            uint16_t c = ((const uint16_t *)row)[x];

            rgb[0] = (uint8_t)((c >> 8) & 0xf8u);
            rgb[0] |= rgb[0] >> 5;
            rgb[1] = (uint8_t)((c >> 3) & 0xfcu);
            rgb[1] |= rgb[1] >> 6;
            rgb[2] = (uint8_t)((c << 3) & 0xf8u);
            rgb[2] |= rgb[2] >> 5;
         }
         fwrite(rgb, 1, sizeof(rgb), file);
      }
   }
   return fclose(file) == 0 ? 0 : -1;
}

static int run_color_case(FILE *report, const char *name, unsigned fb_flags,
   enum unifrog_ge_format format, unsigned src_bpp, const char *ppm_path)
{
   struct unifrog_fb fb;
   struct unifrog_ge ge;
   struct unifrog_ge_surface src;
   struct unifrog_ge_surface dst;
   struct unifrog_ge_rect rect;
   void *source = NULL;
   size_t source_bytes;
   int ret = -1;

   memset(&fb, 0, sizeof(fb));
   memset(&ge, 0, sizeof(ge));
   memset(&src, 0, sizeof(src));
   memset(&dst, 0, sizeof(dst));
   if (unifrog_fb_open(&fb, fb_flags) != 0)
      goto out;
   if (unifrog_ge_open(&ge) != 0)
      goto out;
   source_bytes = (size_t)fb.width * fb.height * src_bpp;
   source = malloc(source_bytes);
   if (!source)
      goto out;
   if (format == UNIFROG_GE_FORMAT_RGB565)
      fill_rgb565(source, fb.width, fb.height);
   else
      fill_xrgb8888(source, fb.width, fb.height);
   unifrog_perf_cache_flush(source, source_bytes);

   memset(&src, 0, sizeof(src));
   src.pixels = source;
   src.width = fb.width;
   src.height = fb.height;
   src.pitch_bytes = fb.width * src_bpp;
   src.format = format;
   dst = unifrog_fb_ge_surface(&fb);
   rect.x = 0;
   rect.y = 0;
   rect.w = fb.width;
   rect.h = fb.height;
   if (unifrog_ge_blit(&ge, &dst, 0, 0, &src, &rect,
       UNIFROG_GE_FLUSH_SOURCE) != 0)
      goto out;
   if (unifrog_ge_sync(&ge) != 0)
      goto out;
   unifrog_fb_flush(&fb);
   msleep(250);
   ret = write_fb_ppm(ppm_path, &fb);

out:
   fprintf(report, "case|%s|%s|fb_bpp=%u src_bpp=%u path=%s errno=%d\n",
      ret == 0 ? "OK" : "FAIL", name ? name : "?", fb.bpp, src_bpp * 8u,
      ppm_path ? ppm_path : "", errno);
   printf("unifrog display_color case=%s ret=%d fb_bpp=%u src_bpp=%u path=%s errno=%d\n",
      name ? name : "?", ret, fb.bpp, src_bpp * 8u,
      ppm_path ? ppm_path : "", errno);
   free(source);
   unifrog_ge_close(&ge);
   unifrog_fb_close(&fb);
   return ret;
}

static int run_case(FILE *report, const struct display_bench_case *bench,
   unsigned *best_total_count, const char **best_name)
{
   struct unifrog_fb fb;
   struct unifrog_ge ge;
   struct unifrog_ge_surface src;
   struct unifrog_ge_rect rect;
   void *source = NULL;
   size_t source_bytes;
   uint64_t blit_us = 0;
   uint64_t sync_us = 0;
   uint64_t pan_us = 0;
   uint64_t max_us = 0;
   uint64_t total_start;
   unsigned frames = 0;
   unsigned buffers;
   int ret = -1;

   memset(&fb, 0, sizeof(fb));
   memset(&ge, 0, sizeof(ge));
   memset(&src, 0, sizeof(src));
   if (!bench)
      return -1;
   if (unifrog_fb_open(&fb, bench->fb_flags) != 0)
      goto out;
   buffers = fb.max_buffers >= 2 ? 2 : 1;
   if (unifrog_fb_set_buffer_count(&fb, buffers) != 0)
      goto out;
   if (unifrog_ge_open(&ge) != 0)
      goto out;

   source_bytes = (size_t)fb.width * fb.height * bench->src_bpp;
   source = malloc(source_bytes);
   if (!source)
      goto out;
   if (bench->src_format == UNIFROG_GE_FORMAT_RGB565)
      fill_rgb565(source, fb.width, fb.height);
   else
      fill_xrgb8888(source, fb.width, fb.height);
   unifrog_perf_cache_flush(source, source_bytes);

   memset(&src, 0, sizeof(src));
   src.pixels = source;
   src.width = fb.width;
   src.height = fb.height;
   src.pitch_bytes = fb.width * bench->src_bpp;
   src.format = bench->src_format;
   rect.x = 0;
   rect.y = 0;
   rect.w = fb.width;
   rect.h = fb.height;

   total_start = unifrog_perf_time_us();
   for (frames = 0; frames < DISPLAY_BENCH_FRAMES; frames++) {
      unsigned next = buffers > 1 ? ((fb.current_buffer + 1u) % buffers) : 0;
      struct unifrog_ge_surface dst =
         unifrog_fb_ge_surface_for_buffer(&fb, next);
      uint64_t t0 = unifrog_perf_time_us();
      uint64_t t1;
      uint64_t t2;
      uint64_t t3;
      uint64_t frame_us;

      if (unifrog_ge_blit(&ge, &dst, 0, 0, &src, &rect, 0) != 0)
         break;
      t1 = unifrog_perf_time_us();
      if (unifrog_ge_sync(&ge) != 0)
         break;
      t2 = unifrog_perf_time_us();
      if (unifrog_fb_pan(&fb, next) != 0)
         break;
      t3 = unifrog_perf_time_us();
      blit_us += t1 - t0;
      sync_us += t2 - t1;
      pan_us += t3 - t2;
      frame_us = t3 - t0;
      if (frame_us > max_us)
         max_us = frame_us;
   }

   if (frames == DISPLAY_BENCH_FRAMES) {
      uint64_t total_us = unifrog_perf_time_us() - total_start;
      unsigned avg_us = (unsigned)(total_us / frames);
      unsigned fps_x100 = total_us ?
         (unsigned)(((uint64_t)frames * 100000000ull) / total_us) : 0;
      unsigned mib_x100 = total_us ?
         (unsigned)(((uint64_t)source_bytes * frames * 100000000ull) /
            (total_us * 1024ull * 1024ull)) : 0;

      fprintf(report,
         "case|OK|%s|fb_bpp=%u src_bpp=%u frames=%u avg_us=%u max_us=%u fps_x100=%u mib_s_x100=%u blit_us=%llu sync_us=%llu pan_us=%llu\n",
         bench->name, fb.bpp, bench->src_bpp * 8u, frames, avg_us,
         (unsigned)max_us, fps_x100, mib_x100,
         (unsigned long long)(blit_us / frames),
         (unsigned long long)(sync_us / frames),
         (unsigned long long)(pan_us / frames));
      printf("unifrog display_benchmark case=%s fb_bpp=%u src_bpp=%u frames=%u avg_us=%u max_us=%u fps_x100=%u mib_s_x100=%u blit_us=%llu sync_us=%llu pan_us=%llu\n",
         bench->name, fb.bpp, bench->src_bpp * 8u, frames, avg_us,
         (unsigned)max_us, fps_x100, mib_x100,
         (unsigned long long)(blit_us / frames),
         (unsigned long long)(sync_us / frames),
         (unsigned long long)(pan_us / frames));
      if (best_total_count && best_name &&
          (*best_total_count == 0 || total_us < *best_total_count)) {
         *best_total_count = (unsigned)total_us;
         *best_name = bench->name;
      }
      ret = 0;
   } else {
      fprintf(report, "case|FAIL|%s|frames=%u errno=%d\n",
         bench->name, frames, errno);
      printf("unifrog display_benchmark case=%s fail frames=%u errno=%d\n",
         bench->name, frames, errno);
   }

out:
   free(source);
   unifrog_ge_close(&ge);
   unifrog_fb_close(&fb);
   if (bench && (bench->fb_flags & UNIFROG_FB_OPEN_XRGB8888)) {
      struct unifrog_fb restore;

      memset(&restore, 0, sizeof(restore));
      if (unifrog_fb_open(&restore, UNIFROG_FB_OPEN_DEFAULT) == 0)
         unifrog_fb_close(&restore);
   }
   return ret;
}

int unifrog_display_benchmark_run(char *summary, size_t summary_size)
{
   static const struct display_bench_case cases[] = {
      { "rgb565_scanout", UNIFROG_FB_OPEN_DEFAULT,
         UNIFROG_GE_FORMAT_RGB565, 2 },
      { "xrgb8888_to_rgb565", UNIFROG_FB_OPEN_DEFAULT,
         UNIFROG_GE_FORMAT_XRGB8888, 4 },
      { "xrgb8888_scanout", UNIFROG_FB_OPEN_XRGB8888,
         UNIFROG_GE_FORMAT_XRGB8888, 4 },
   };
   FILE *report;
   unsigned best_total_count = 0;
   const char *best_name = "none";
   int failures = 0;

   write_summary(summary, summary_size, "running");
   report = fopen(UNIFROG_DISPLAY_BENCHMARK_REPORT, "wb");
   if (!report) {
      write_summary(summary, summary_size, "report open failed");
      return -1;
   }

   fprintf(report, "show=1\n");
   fprintf(report, "title=DISPLAY BENCHMARK\n");
   fprintf(report, "detail=GE blit/sync/pan raw throughput, no vsync wait\n");
   printf("unifrog display_benchmark begin frames=%u report=%s\n",
      DISPLAY_BENCH_FRAMES, UNIFROG_DISPLAY_BENCHMARK_REPORT);
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      if (run_case(report, &cases[i], &best_total_count, &best_name) != 0)
         failures++;
      fflush(report);
      msleep(100);
   }
   fprintf(report, "best=%s\n", best_name);
   fprintf(report, "failures=%d\n", failures);
   fclose(report);

   /* Return the frontend to its normal low-bandwidth scanout mode. */
   {
      struct unifrog_fb fb;

      memset(&fb, 0, sizeof(fb));
      if (unifrog_fb_open(&fb, UNIFROG_FB_OPEN_DEFAULT) == 0)
         unifrog_fb_close(&fb);
   }
   printf("unifrog display_benchmark done failures=%d best=%s\n",
      failures, best_name);
   write_summary(summary, summary_size, best_name);
   (void)unifrog_log_flush();
   return failures ? -1 : 0;
}

int unifrog_display_color_test_run(char *summary, size_t summary_size)
{
   FILE *report;
   int failures = 0;

   write_summary(summary, summary_size, "running");
   (void)mkdir(UNIFROG_SCREENSHOT_ROOT, 0777);
   report = fopen(DISPLAY_COLOR_REPORT, "wb");
   if (!report) {
      write_summary(summary, summary_size, "report open failed");
      return -1;
   }
   fprintf(report, "show=1\n");
   fprintf(report, "title=DISPLAY COLOR TEST\n");
   fprintf(report, "detail=Compare PPM captures for 565 conversion and native 8888 scanout\n");

   if (run_color_case(report, "rgb565_scanout",
       UNIFROG_FB_OPEN_DEFAULT, UNIFROG_GE_FORMAT_RGB565, 2,
       UNIFROG_SCREENSHOT_ROOT "/display-color-rgb565.ppm") != 0)
      failures++;
   if (run_color_case(report, "xrgb8888_to_rgb565",
       UNIFROG_FB_OPEN_DEFAULT, UNIFROG_GE_FORMAT_XRGB8888, 4,
       UNIFROG_SCREENSHOT_ROOT "/display-color-8888-to-565.ppm") != 0)
      failures++;
   if (run_color_case(report, "xrgb8888_scanout",
       UNIFROG_FB_OPEN_XRGB8888, UNIFROG_GE_FORMAT_XRGB8888, 4,
       UNIFROG_SCREENSHOT_ROOT "/display-color-xrgb8888.ppm") != 0)
      failures++;

   fprintf(report, "failures=%d\n", failures);
   fclose(report);
   {
      struct unifrog_fb fb;

      memset(&fb, 0, sizeof(fb));
      if (unifrog_fb_open(&fb, UNIFROG_FB_OPEN_DEFAULT) == 0)
         unifrog_fb_close(&fb);
   }
   write_summary(summary, summary_size, failures ? "fail" : "ok");
   (void)unifrog_log_flush();
   return failures ? -1 : 0;
}
