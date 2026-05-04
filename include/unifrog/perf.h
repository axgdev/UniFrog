#ifndef UNIFROG_PERF_H
#define UNIFROG_PERF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_PERF_CACHED_ALIAS(ptr) \
   ((void *)((((uintptr_t)(ptr)) | 0x80000000u) & 0x9fffffffu))
#define UNIFROG_PERF_UNCACHED_ALIAS(ptr) \
   ((void *)(((uintptr_t)(ptr)) | 0xa0000000u))
#define UNIFROG_PERF_PHYS_ALIAS(ptr) \
   ((uintptr_t)(ptr) & 0x1fffffffu)

enum unifrog_perf_cap {
   UNIFROG_PERF_CAP_CP0_COUNT = 1u << 0,
   UNIFROG_PERF_CAP_CACHE_CONTROL = 1u << 1,
   UNIFROG_PERF_CAP_UNCACHED_ALIAS = 1u << 2,
   UNIFROG_PERF_CAP_FRAMEBUFFER = 1u << 3,
   UNIFROG_PERF_CAP_FRAMEBUFFER_PAN = 1u << 4,
   UNIFROG_PERF_CAP_FRAMEBUFFER_VSYNC = 1u << 5,
   UNIFROG_PERF_CAP_GE_FILL = 1u << 6,
   UNIFROG_PERF_CAP_GE_BLIT = 1u << 7,
   UNIFROG_PERF_CAP_GE_STRETCH = 1u << 8,
   UNIFROG_PERF_CAP_GE_CLOCK = 1u << 9,
   UNIFROG_PERF_CAP_DISPLAY_CONTROLLER = 1u << 10,
   UNIFROG_PERF_CAP_HARDWARE_VIDEO = 1u << 11,
   UNIFROG_PERF_CAP_AUDIO_OUTPUT = 1u << 12,
   UNIFROG_PERF_CAP_SD_STORAGE = 1u << 13,
   UNIFROG_PERF_CAP_ADC = 1u << 14,
   UNIFROG_PERF_CAP_BACKLIGHT_PWM = 1u << 15,
   UNIFROG_PERF_CAP_WIRELESS_GAMEPAD = 1u << 16,
   UNIFROG_PERF_CAP_MMZ = 1u << 17,
   UNIFROG_PERF_CAP_DSC = 1u << 18,
};

struct unifrog_perf_caps {
   uint32_t caps;
   unsigned scpu_selector;
   unsigned scpu_mhz_est;
   unsigned framebuffer_width;
   unsigned framebuffer_height;
   unsigned framebuffer_bpp;
   unsigned framebuffer_stride_bytes;
   unsigned framebuffer_bytes;
   unsigned framebuffer_buffers;
   unsigned ge_cmdq_bytes;
};

uint32_t unifrog_perf_count(void);
uint32_t unifrog_perf_elapsed(uint32_t start, uint32_t end);
uint64_t unifrog_perf_time_us(void);
uint32_t unifrog_perf_time_ms(void);
void unifrog_perf_delay_us(unsigned us);
void unifrog_perf_cache_flush(const void *ptr, size_t len);
void unifrog_perf_cache_invalidate(const void *ptr, size_t len);
void unifrog_perf_cache_flush_invalidate(const void *ptr, size_t len);
void unifrog_perf_cache_flush_all(void);
void *unifrog_perf_cached_addr(const void *ptr);
void *unifrog_perf_uncached_addr(const void *ptr);
uintptr_t unifrog_perf_phys_addr(const void *ptr);
int unifrog_perf_query_caps(struct unifrog_perf_caps *caps);

#ifdef __cplusplus
}
#endif

#endif
