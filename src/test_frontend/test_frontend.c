#include <fcntl.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <cpu_func.h>
#include <kernel/fb.h>
#include <kernel/io.h>
#include <kernel/delay.h>
#include <hcuapi/fb.h>
#include <hcuapi/avsync.h>
#include <hcuapi/gpio.h>
#include <hcuapi/pinpad.h>
#include <hcuapi/pinmux.h>
#include <hcuapi/pwm.h>
#include <hcuapi/dis.h>
#include <ffplayer.h>

#include <js2300/js2300.h>

#include <unifrog/abi.h>
#include <unifrog/audio.h>
#include <unifrog/battery.h>
#include <unifrog/ge.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/libretro_host.h>
#include <unifrog/log.h>
#include <unifrog/path.h>
#include <unifrog/perf.h>
#include <unifrog/presenter.h>
#include <unifrog/runtime.h>
#include <unifrog/text.h>

#define RGB565(r, g, b) UNIFROG_RGB565((r), (g), (b))

#define MAX_BROWSER_ITEMS 96
#define MAX_PATH_LEN 256
#define VIDEO_OUT_W 640
#define VIDEO_OUT_H 480
#define PANEL_OUT_W 320
#define PANEL_OUT_H 240
#define WORKING_VIDEO_MODE 0
#define VIDEO_STREAM_CACHE_BYTES UNIFROG_APP_STREAM_BUFFER_DEFAULT_BYTES
#define BUILD_SCPU_CLOCK 7
#define BUILD_SCPU_DIG_PLL_MHZ 810
#define MSYSIO_BASE 0xb8800000u
#define HC1512_CHIP_ID 0x1512u
#define STATUS_LED_PIN PINPAD_L25
#define STATUS_LED_NAME "L25"
#define WIRELESS_DIAG_STOCK_HOLD_SECONDS 10u
#define WIRELESS_DIAG_FINAL_POLL_SECONDS 10u
#define WIRELESS_DIAG_FIXED_WINDOW_MS 600u
#define STANDBY_SCPU_SELECTOR 3u
#define STANDBY_SCPU_MHZ 198u
#define STANDBY_BUTTON_POLL_US 25000
#define BATTERY_POLL_FRAMES 60u
#define GAMBATTE_TEST_ROM "/media/mmcblk0/ROMS/gbb/Pokemon - Crystal Version (USA).gbc"
#define AUDIO_DIAG_CHUNK_FRAMES 256u
#define AUDIO_DIAG_DURATION_MS 2500u

#ifndef UNIFROG_GIT_COMMIT
#define UNIFROG_GIT_COMMIT "unknown"
#endif

#ifndef UNIFROG_GIT_DIRTY
#define UNIFROG_GIT_DIRTY 1
#endif

extern int sf2000_lcd_panel_brightness_probe(unsigned char brightness, unsigned char cabc);
#define printf unifrog_log

#define BTN_R UNIFROG_BUTTON_R
#define BTN_Y UNIFROG_BUTTON_Y
#define BTN_X UNIFROG_BUTTON_X
#define BTN_L UNIFROG_BUTTON_L
#define BTN_A UNIFROG_BUTTON_A
#define BTN_B UNIFROG_BUTTON_B
#define BTN_SELECT UNIFROG_BUTTON_SELECT
#define BTN_START UNIFROG_BUTTON_START
#define BTN_UP UNIFROG_BUTTON_UP
#define BTN_DOWN UNIFROG_BUTTON_DOWN
#define BTN_LEFT UNIFROG_BUTTON_LEFT
#define BTN_RIGHT UNIFROG_BUTTON_RIGHT
#define BTN_LAST UNIFROG_BUTTON_COUNT

struct unifrog_input_event {
   uint16_t type;
   uint16_t code;
   int32_t value;
};

static int fbdev = -1;
static struct fb_fix_screeninfo fix;
static struct fb_var_screeninfo var;
static uint16_t *fb_base;
static uint16_t *fb;
static uint32_t line_pixels;
static unsigned fb_buffers = 1;
static unsigned fb_active_buffer;
static unsigned js_draw_buffer;
static int js_frame_open;
static char js_requested_action[32];
static int js_native_action_active;
static int js_relaunch_requested;
static size_t fb_mapped_len;
static void clear_button_latches(void);
static void draw_screen(void);
static int selected;
static uint32_t frames;
static int dirty = 1;

enum view_mode {
   VIEW_MENU,
   VIEW_FRAMEBUFFER,
   VIEW_INPUT,
   VIEW_STORAGE,
   VIEW_BROWSER,
   VIEW_VIDEO,
   VIEW_REPORT,
   VIEW_PERF_LAB,
   VIEW_AUDIO_DIAG,
   VIEW_DISCOVERY,
   VIEW_BRIGHTNESS,
   VIEW_PLAYOPTS,
   VIEW_PLAYER,
};

enum menu_item {
   MENU_FRAMEBUFFER,
   MENU_INPUT,
   MENU_STORAGE,
   MENU_BROWSER,
   MENU_JS_FRONTEND,
   MENU_GAMBATTE_TEST,
   MENU_VIDEO,
   MENU_REPORT,
   MENU_PERF_LAB,
   MENU_MEMORY_ABI,
   MENU_AUDIO_DIAG,
   MENU_BRIGHTNESS,
   MENU_WIRELESS,
   MENU_STANDBY,
   MENU_REBOOT,
   MENU_COUNT,
};

enum report_kind {
   REPORT_KIND_PERFORMANCE,
   REPORT_KIND_MEMORY_ABI,
};

struct browser_item {
   char name[64];
   int is_dir;
};

static enum view_mode view = VIEW_MENU;
static int menu_selected;
static int perf_selected;
static int perf_scroll;
static int audio_diag_selected;
static int audio_diag_scroll;
static int browser_selected;
static int browser_scroll;
static char browser_path[MAX_PATH_LEN] = "/media/mmcblk0";
static struct browser_item browser_items[MAX_BROWSER_ITEMS];
static int browser_count;
static char status_line[96] = "READY";
static char player_path[MAX_PATH_LEN];
static void *player_handle;
static int player_started;
static int dis_fd = -1;
static uint32_t player_watch_frame;
static int64_t player_last_pos = -1;
static int player_stall_count;
static int player_video_w;
static int player_video_h;
static int player_display_mode;
static uint32_t player_display_mode_frame;
static int play_preset;
static int play_quick_mode;
static int play_audio_enabled = 1;
static int play_buffering_enabled;
static int report_done;
static enum report_kind report_kind;
static int fb_brightness = 50;
static int fb_brightness_supported = -1;
static int backlight_supported = -1;
static int backlight_pwm_supported = -1;
static int backlight_pwm_level = 50;
static int backlight_pwm_profile;
static int lcd_power_candidate_on = 1;
static int panel_brightness_index;
static int led_candidate_on;
static int led_pattern_enabled;
static int led_pattern_mode;
static uint32_t led_pattern_last_frame;
static struct unifrog_battery_status battery_status;
static int flush_memlog(void);

struct stream_video {
   int fd;
   off_t size;
   off_t pos;
   off_t cache_start;
   size_t cache_size;
   uint8_t *cache;
   int active;
};

static const int panel_brightness_levels[] = { 255, 128, 64, 16, 1, 0 };
struct backlight_pwm_profile {
   const char *name;
   int freq;
   int polarity;
   int inverse;
};

static const struct backlight_pwm_profile backlight_pwm_profiles[] = {
   {"F10000 P1", 10000, 1, 0},
   {"F1000 P0", 1000, 0, 1},
   {"F20000 P1", 20000, 1, 0},
   {"F5000 P1", 5000, 1, 0},
   {"F500 P0", 500, 0, 1},
   {"F10000 P0", 10000, 0, 1},
};

static struct stream_video player_stream;

struct scpu_clock_snapshot {
   int valid;
   uint32_t reg074;
   uint32_t reg07c;
   uint32_t reg380;
   unsigned selector;
   unsigned pll_enabled;
   unsigned mhz;
};

struct playback_preset {
   const char *name;
   HCPlayerSyncType sync_type;
   int qm_drop_thresh;
   int audio_flush_thres;
};

enum perf_lab_item {
   PERF_LAB_FULL_SWEEP,
   PERF_LAB_CAPS,
   PERF_LAB_MEMORY,
   PERF_LAB_FRAMEBUFFER,
   PERF_LAB_GE,
   PERF_LAB_PRESENTER,
   PERF_LAB_DISPLAY_VIDEO,
   PERF_LAB_AUDIO,
   PERF_LAB_STORAGE,
   PERF_LAB_HARDWARE_LEADS,
   PERF_LAB_SCPU_INFO,
   PERF_LAB_SCPU_RUNTIME,
   PERF_LAB_COUNT,
};

static const struct playback_preset playback_presets[] = {
   {"audio loose", HCPLAYER_AUDIO_MASTER, 3, 0},
   {"stc sync", HCPLAYER_SYNC_STC, 1, 0},
   {"freerun", HCPLAYER_FREERUN, 1, 0},
   {"audio quick", HCPLAYER_AUDIO_MASTER, 1, 0},
   {"video master", HCPLAYER_VIDEO_MASTER, 1, 0},
};

static const char *perf_lab_items[] = {
   "FULL EXPERIMENT SWEEP",
   "CAPABILITY SNAPSHOT",
   "MEMORY AND CACHE",
   "FRAMEBUFFER PATH",
   "GE ACCELERATOR",
   "PRESENTER PATH",
   "DISPLAY AND VIDEO",
   "AUDIO DMA OUTPUT",
   "STORAGE IO",
   "HARDWARE LEADS",
   "SCPU CLOCK INFO",
   "SCPU RUNTIME PROBE",
};

static void log_abi_region(const char *name,
                           const struct unifrog_abi_memory_region *region)
{
   printf("MEMABI region=%s size=%u cached=0x%08lx phys=0x%08lx bytes=%lu flags=0x%08lx\n",
      name, (unsigned)region->size,
      (unsigned long)region->cached_base,
      (unsigned long)region->physical_base,
      (unsigned long)region->bytes,
      (unsigned long)region->flags);
}

static unsigned memory_pattern_errors(volatile uint32_t *base, size_t words,
                                      uint32_t seed)
{
   unsigned errors = 0;

   for (size_t i = 0; i < words; i++)
      base[i] = seed ^ (uint32_t)(i * 0x01010101u);
   unifrog_perf_cache_flush_invalidate((const void *)base, words * sizeof(uint32_t));

   for (size_t i = 0; i < words; i++) {
      uint32_t expected = seed ^ (uint32_t)(i * 0x01010101u);
      if (base[i] != expected)
         errors++;
   }

   return errors;
}

static void run_memory_abi_probe(void)
{
   struct unifrog_abi_memory_layout layout;
   struct unifrog_abi_memory_slot app_slot;
   const struct unifrog_abi *abi = unifrog_abi_get();
   unsigned errors = 0;
   unsigned heap_ok = 0;
   unsigned heap_fail = 0;
   const size_t probe_bytes = 4096;
   void *heap_blocks[6];
   const size_t heap_sizes[] = {
      4u * 1024u * 1024u,
      2u * 1024u * 1024u,
      1u * 1024u * 1024u,
      512u * 1024u,
      256u * 1024u,
      128u * 1024u,
   };

   memset(heap_blocks, 0, sizeof(heap_blocks));
   printf("MEMABI start abi_magic=0x%08lx abi_size=%lu abi_version=%u.%u.%u compatible=%d\n",
      (unsigned long)abi->magic, (unsigned long)abi->size,
      (unsigned)UNIFROG_ABI_VERSION_GET_MAJOR(abi->version),
      (unsigned)UNIFROG_ABI_VERSION_GET_MINOR(abi->version),
      (unsigned)UNIFROG_ABI_VERSION_GET_PATCH(abi->version),
      unifrog_abi_compatible(abi->version));

   if (unifrog_abi_memory_layout(&layout) != 0) {
      printf("MEMABI layout=fail\n");
      unifrog_text_copy(status_line, sizeof(status_line), "MEM ABI FAIL");
      report_done = 1;
      flush_memlog();
      return;
   }

   printf("MEMABI layout size=%u version=0x%08lx regions=%u flags=0x%08lx\n",
      (unsigned)layout.size, (unsigned long)layout.version,
      (unsigned)layout.region_count, (unsigned long)layout.flags);
   log_abi_region("runtime", &layout.runtime);
   log_abi_region("external", &layout.external);
   log_abi_region("media", &layout.media);

   if (unifrog_abi_application_memory_slot(&app_slot) != 0) {
      printf("MEMABI app_slot=fail\n");
      unifrog_text_copy(status_line, sizeof(status_line), "MEM SLOT FAIL");
      report_done = 1;
      flush_memlog();
      return;
   }

   printf("MEMABI app_slot base=0x%08lx bytes=%lu flags=0x%08lx\n",
      (unsigned long)app_slot.base, (unsigned long)app_slot.bytes,
      (unsigned long)app_slot.flags);

   if (app_slot.bytes >= probe_bytes * 3) {
      uintptr_t first = app_slot.base;
      uintptr_t middle = app_slot.base + (app_slot.bytes / 2);
      uintptr_t last = app_slot.base + app_slot.bytes - probe_bytes;

      middle &= ~(uintptr_t)(probe_bytes - 1);
      errors += memory_pattern_errors((volatile uint32_t *)first,
                                      probe_bytes / sizeof(uint32_t),
                                      0xa1000000u);
      errors += memory_pattern_errors((volatile uint32_t *)middle,
                                      probe_bytes / sizeof(uint32_t),
                                      0xb2000000u);
      errors += memory_pattern_errors((volatile uint32_t *)last,
                                      probe_bytes / sizeof(uint32_t),
                                      0xc3000000u);
      printf("MEMABI arena_probe first=0x%08lx middle=0x%08lx last=0x%08lx errors=%u\n",
         (unsigned long)first, (unsigned long)middle,
         (unsigned long)last, errors);
   } else {
      printf("MEMABI arena_probe skipped bytes=%lu\n", (unsigned long)app_slot.bytes);
      errors++;
   }

   for (unsigned i = 0; i < ARRAY_SIZE(heap_sizes); i++) {
      heap_blocks[i] = malloc(heap_sizes[i]);
      if (heap_blocks[i]) {
         memset(heap_blocks[i], 0x5a, heap_sizes[i] < 4096 ? heap_sizes[i] : 4096);
         heap_ok++;
         printf("MEMABI heap_alloc index=%u bytes=%lu ptr=0x%08lx\n",
            i, (unsigned long)heap_sizes[i], (unsigned long)heap_blocks[i]);
      } else {
         heap_fail++;
         printf("MEMABI heap_alloc index=%u bytes=%lu ptr=null\n",
            i, (unsigned long)heap_sizes[i]);
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(heap_blocks); i++) {
      if (heap_blocks[i])
         free(heap_blocks[i]);
   }

   printf("MEMABI done errors=%u heap_ok=%u heap_fail=%u\n",
      errors, heap_ok, heap_fail);
   snprintf(status_line, sizeof(status_line), "MEM ERR %u HEAP %u/%u",
      errors, heap_ok, (unsigned)ARRAY_SIZE(heap_blocks));
   report_done = 1;
   flush_memlog();
}

enum audio_diag_route {
   AUDIO_DIAG_LEFT_ONLY,
   AUDIO_DIAG_RIGHT_ONLY,
   AUDIO_DIAG_DUAL_MONO,
   AUDIO_DIAG_DIFFERENTIAL,
};

struct audio_diag_test {
   const char *name;
   unsigned rate;
   unsigned start_hz;
   unsigned end_hz;
   enum audio_diag_route route;
   unsigned volume;
   int backend;
   int gate_only;
   unsigned gain;
   int auto_gate;
};

static const struct audio_diag_test audio_diag_tests[] = {
   {"AUTO GATE TONE SILENCE", 32000, 440, 440, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_AUDSINK, 0, 8, 1},
   {"GATE ONLY NO PCM", 32000, 0, 0, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_AUTO, 1, 1, 0},
   {"AUDSINK SILENCE LEFT", 32000, 0, 0, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_AUDSINK, 0, 1, 0},
   {"SND SILENCE LEFT", 32000, 0, 0, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_SND, 0, 1, 0},
   {"AUDSINK TONE GAIN1", 32000, 440, 440, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_AUDSINK, 0, 1, 0},
   {"AUDSINK TONE GAIN4", 32000, 440, 440, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_AUDSINK, 0, 4, 0},
   {"AUDSINK TONE GAIN8", 32000, 440, 440, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_AUDSINK, 0, 8, 0},
   {"AUDSINK TONE GAIN12", 32000, 440, 440, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_AUDSINK, 0, 12, 0},
   {"SND TONE GAIN8", 32000, 440, 440, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_SND, 0, 8, 0},
   {"AUDSINK TONE RIGHT", 32000, 440, 440, AUDIO_DIAG_RIGHT_ONLY, 75, UNIFROG_AUDIO_BACKEND_AUDSINK, 0, 8, 0},
   {"AUDSINK TONE DUAL", 32000, 440, 440, AUDIO_DIAG_DUAL_MONO, 75, UNIFROG_AUDIO_BACKEND_AUDSINK, 0, 8, 0},
   {"AUDSINK SWEEP GAIN8", 32000, 200, 3000, AUDIO_DIAG_LEFT_ONLY, 75, UNIFROG_AUDIO_BACKEND_AUDSINK, 0, 8, 0},
};

static const int16_t sine_table[32] = {
   0, 6393, 12539, 18204, 23170, 27246, 30274, 32138,
   32767, 32138, 30274, 27246, 23170, 18204, 12539, 6393,
   0, -6393, -12539, -18204, -23170, -27246, -30274, -32138,
   -32767, -32138, -30274, -27246, -23170, -18204, -12539, -6393,
};

static const char *audio_diag_running_name;
static int16_t audio_diag_buffer[AUDIO_DIAG_CHUNK_FRAMES * 2];

static const char *audio_diag_route_name(enum audio_diag_route route)
{
   switch (route) {
   case AUDIO_DIAG_LEFT_ONLY:
      return "left";
   case AUDIO_DIAG_RIGHT_ONLY:
      return "right";
   case AUDIO_DIAG_DUAL_MONO:
      return "dual";
   case AUDIO_DIAG_DIFFERENTIAL:
      return "diff";
   default:
      return "?";
   }
}

static const char *audio_diag_backend_name(int backend)
{
   switch (backend) {
   case UNIFROG_AUDIO_BACKEND_SND:
      return "snd";
   case UNIFROG_AUDIO_BACKEND_AUDSINK:
      return "audsink";
   default:
      return "auto";
   }
}

static int flush_memlog(void)
{
   size_t pending = unifrog_log_pending();
   int ret = unifrog_log_flush();
   const char *path = unifrog_log_last_path();

   if (ret == UNIFROG_LOG_ERR_OPEN)
      unifrog_text_copy(status_line, sizeof(status_line), "LOG OPEN FAILED");
   else if (ret == UNIFROG_LOG_ERR_WRITE)
      unifrog_text_copy(status_line, sizeof(status_line), "LOG WRITE FAILED");
   else
      unifrog_text_copy(status_line, sizeof(status_line), "LOG FLUSHED");
   printf("unifrog log flush ret=%d pending=%lu path=%s\n",
      ret, (unsigned long)pending, path ? path : "?");
   return ret;
}

static int apply_fb_brightness(void)
{
   hcfb_enhance_t enhance;

   if (fbdev < 0)
      return -1;

   memset(&enhance, 0, sizeof(enhance));
   if (ioctl(fbdev, HCFBIOGET_ENHANCE, &enhance) != 0) {
      fb_brightness_supported = 0;
      printf("unifrog brightness fb enhance get failed\n");
      return -1;
   }

   enhance.brightness = fb_brightness;
   if (ioctl(fbdev, HCFBIOSET_ENHANCE, &enhance) != 0) {
      fb_brightness_supported = 0;
      printf("unifrog brightness fb enhance set failed value=%d\n", fb_brightness);
      return -1;
   }

   fb_brightness_supported = 1;
   printf("unifrog brightness fb enhance value=%d\n", fb_brightness);
   return 0;
}

static int apply_backlight_level(void)
{
   int fd;
   int value;

   if (fb_brightness < 0)
      fb_brightness = 0;
   if (fb_brightness > 100)
      fb_brightness = 100;

   fd = open("/dev/backlight", O_RDWR);
   if (fd < 0) {
      backlight_supported = 0;
      printf("unifrog brightness /dev/backlight unavailable\n");
      return -1;
   }

   value = fb_brightness;
   if (write(fd, &value, sizeof(value)) != (ssize_t)sizeof(value)) {
      close(fd);
      backlight_supported = 0;
      printf("unifrog brightness /dev/backlight write failed value=%d\n", value);
      return -1;
   }

   close(fd);
   backlight_supported = 1;
   printf("unifrog brightness /dev/backlight value=%d\n", value);
   return 0;
}

static void update_battery_status(int force_log)
{
   if (unifrog_battery_update(&battery_status, force_log) > 0)
      dirty = 1;
}

static void stop_backlight_pwm_probe(void)
{
   int fd = open("/dev/pwm2", O_RDWR);

   if (fd >= 0) {
      ioctl(fd, PWMIOC_STOP, 0);
      close(fd);
   }

   pinmux_configure(PINPAD_R05, PINMUX_R05_GPIO);
   gpio_configure(PINPAD_R05, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_R05, false);
   lcd_power_candidate_on = 1;
   printf("unifrog brightness pwm2 stop restore pin=PINPAD_R05 gpio=0\n");
}

static int apply_backlight_pwm_probe(void)
{
   int fd;
   int duty_level;
   uint32_t period_ns;
   struct pwm_info_s info;
   int ret_set;
   int ret_start;
   const struct backlight_pwm_profile *profile;

   if (backlight_pwm_profile < 0)
      backlight_pwm_profile = 0;
   if (backlight_pwm_profile >= (int)ARRAY_SIZE(backlight_pwm_profiles))
      backlight_pwm_profile = 0;
   if (backlight_pwm_level < 0)
      backlight_pwm_level = 0;
   if (backlight_pwm_level > 100)
      backlight_pwm_level = 100;

   profile = &backlight_pwm_profiles[backlight_pwm_profile];
   duty_level = profile->inverse ? 100 - backlight_pwm_level : backlight_pwm_level;
   period_ns = 1000000000u / (uint32_t)profile->freq;

   memset(&info, 0, sizeof(info));
   info.period_ns = period_ns;
   info.duty_ns = (period_ns * (uint32_t)duty_level) / 100u;
   info.polarity = profile->polarity ? true : false;

   pinmux_configure(PINPAD_R05, PINMUX_R05_PWM_2);
   fd = open("/dev/pwm2", O_RDWR);
   if (fd < 0) {
      gpio_configure(PINPAD_R05, GPIO_DIR_OUTPUT);
      gpio_set_output(PINPAD_R05, false);
      backlight_pwm_supported = 0;
      printf("unifrog brightness pwm2 unavailable visible_level=%d profile=%s\n",
         backlight_pwm_level, profile->name);
      snprintf(status_line, sizeof(status_line), "NO /DEV/PWM2");
      dirty = 1;
      return -1;
   }

   ret_set = ioctl(fd, PWMIOC_SETCHARACTERISTICS, &info);
   ret_start = ioctl(fd, PWMIOC_START, 0);
   close(fd);

   backlight_pwm_supported = (ret_set == 0 && ret_start == 0) ? 1 : 0;
   printf("unifrog brightness pwm2 visible_level=%d duty_level=%d profile=%s freq=%d period_ns=%u duty_ns=%u polarity=%d inverse=%d ret_set=%d ret_start=%d\n",
      backlight_pwm_level, duty_level, profile->name, profile->freq,
      (unsigned)info.period_ns, (unsigned)info.duty_ns, profile->polarity,
      profile->inverse, ret_set, ret_start);
   snprintf(status_line, sizeof(status_line), "PWM2 %s L%d", profile->name, backlight_pwm_level);
   dirty = 1;
   return backlight_pwm_supported ? 0 : -1;
}

static void change_backlight_pwm_level(int delta)
{
   backlight_pwm_level += delta;
   if (backlight_pwm_level < 0)
      backlight_pwm_level = 0;
   if (backlight_pwm_level > 100)
      backlight_pwm_level = 100;
   apply_backlight_pwm_probe();
}

static void next_backlight_pwm_profile(void)
{
   backlight_pwm_profile++;
   if (backlight_pwm_profile >= (int)ARRAY_SIZE(backlight_pwm_profiles))
      backlight_pwm_profile = 0;
   apply_backlight_pwm_probe();
}

static void next_panel_brightness_level(void)
{
   int level;
   int cabc;
   int ret;

   panel_brightness_index++;
   if (panel_brightness_index >= (int)ARRAY_SIZE(panel_brightness_levels) * 4)
      panel_brightness_index = 0;

   level = panel_brightness_levels[panel_brightness_index % ARRAY_SIZE(panel_brightness_levels)];
   cabc = panel_brightness_index / ARRAY_SIZE(panel_brightness_levels);
   ret = sf2000_lcd_panel_brightness_probe((unsigned char)level, (unsigned char)cabc);
   printf("unifrog brightness panel level=%d cabc=%d ret=%d\n", level, cabc, ret);
   snprintf(status_line, sizeof(status_line), "PANEL %d CABC %d", level, cabc);
   dirty = 1;
}

static void change_brightness(int delta)
{
   fb_brightness += delta;
   if (fb_brightness < 0)
      fb_brightness = 0;
   if (fb_brightness > 100)
      fb_brightness = 100;

   if (apply_fb_brightness() == 0)
      snprintf(status_line, sizeof(status_line), "FB BRIGHTNESS %d", fb_brightness);
   else
      snprintf(status_line, sizeof(status_line), "FB BRIGHTNESS UNSUPPORTED");
   dirty = 1;
}

static void set_status_led_green(int green, const char *reason)
{
   led_pattern_enabled = 0;
   led_candidate_on = green ? 1 : 0;
   gpio_configure(STATUS_LED_PIN, GPIO_DIR_OUTPUT);
   gpio_set_output(STATUS_LED_PIN, green ? false : true);
   printf("unifrog led status pin=PINPAD_%s color=%s gpio=%d reason=%s\n",
      STATUS_LED_NAME, green ? "green" : "red", green ? 0 : 1, reason ? reason : "manual");
   snprintf(status_line, sizeof(status_line), "L25 %s", green ? "GREEN" : "RED");
   dirty = 1;
}

static void toggle_led_pattern(void)
{
   led_pattern_enabled = !led_pattern_enabled;
   led_pattern_mode = 0;
   led_pattern_last_frame = 0;
   printf("unifrog led pattern %s pin=PINPAD_%s\n",
      led_pattern_enabled ? "start" : "stop", STATUS_LED_NAME);
   if (!led_pattern_enabled)
      set_status_led_green(1, "pattern_stop");
   else
      unifrog_text_copy(status_line, sizeof(status_line), "L25 PATTERN START");
   dirty = 1;
}

static void update_led_pattern(void)
{
   uint32_t local_frame;
   int mode;
   int green;

   if (!led_pattern_enabled || view != VIEW_BRIGHTNESS)
      return;

   local_frame = frames % 420;
   mode = local_frame / 70;
   switch (mode) {
   case 0:
      green = 0;
      break;
   case 1:
      green = 1;
      break;
   case 2:
      green = ((local_frame / 15) & 1) ? 1 : 0;
      break;
   case 3:
      green = (local_frame & 1) ? 1 : 0;
      break;
   case 4:
      green = ((local_frame % 35) < 5 || ((local_frame % 35) >= 10 && (local_frame % 35) < 15)) ? 1 : 0;
      break;
   default:
      green = ((local_frame % 35) < 5 || ((local_frame % 35) >= 10 && (local_frame % 35) < 15)) ? 0 : 1;
      break;
   }

   if (mode != led_pattern_mode || green != led_candidate_on || frames - led_pattern_last_frame >= 30) {
      led_pattern_mode = mode;
      led_pattern_last_frame = frames;
      gpio_configure(STATUS_LED_PIN, GPIO_DIR_OUTPUT);
      gpio_set_output(STATUS_LED_PIN, green ? false : true);
      led_candidate_on = green ? 1 : 0;
      printf("unifrog led pattern mode=%d frame=%u color=%s gpio=%d\n",
         mode, (unsigned)frames, green ? "green" : "red", green ? 0 : 1);
      snprintf(status_line, sizeof(status_line), "L25 PATTERN %d %s", mode, green ? "GREEN" : "RED");
      dirty = 1;
   }
}

static void set_lcd_power_candidate(int on)
{
   if (on)
      stop_backlight_pwm_probe();

   lcd_power_candidate_on = on ? 1 : 0;
   pinmux_configure(PINPAD_R05, PINMUX_R05_GPIO);
   gpio_configure(PINPAD_R05, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_R05, lcd_power_candidate_on ? false : true);
   printf("unifrog brightness lcd_power_candidate pin=PINPAD_R05 on=%d gpio=%d\n",
      lcd_power_candidate_on, lcd_power_candidate_on ? 0 : 1);
   snprintf(status_line, sizeof(status_line), "R05 LCD %s", lcd_power_candidate_on ? "ON" : "OFF");
   dirty = 1;
}

static struct unifrog_surface framebuffer_surface(void)
{
   struct unifrog_surface surface;

   surface.pixels = fb;
   surface.width = var.xres;
   surface.height = var.yres;
   surface.stride = line_pixels;
   return surface;
}

static struct unifrog_ge_surface framebuffer_ge_surface(void)
{
   struct unifrog_ge_surface surface;

   surface.pixels = fb;
   surface.width = var.xres;
   surface.height = var.yres;
   surface.pitch_bytes = line_pixels * sizeof(uint16_t);
   surface.format = UNIFROG_GE_FORMAT_RGB565;
   return surface;
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
   struct unifrog_surface surface = framebuffer_surface();

   unifrog_gfx_fill_rect(&surface, x, y, w, h, color);
}

static void draw_hline(int x, int y, int w, uint16_t color)
{
   struct unifrog_surface surface = framebuffer_surface();

   unifrog_gfx_draw_hline(&surface, x, y, w, color);
}

static void draw_text(int x, int y, const char *text, uint16_t color, int scale)
{
   struct unifrog_surface surface = framebuffer_surface();

   unifrog_gfx_draw_text(&surface, x, y, text, color, scale);
}

static int button_pressed(int button)
{
   return unifrog_input_pressed((enum unifrog_button)button);
}

static void run_wireless_diagnostics(void)
{
   static const uint8_t channels[] = { 0x04, 0x1d, 0x31, 0x4f };
   printf("\nWIRELESS_DIAG start note=run_from_menu stock_hold_first\n");
   unifrog_text_copy(status_line, sizeof(status_line), "WIRELESS DIAG RUNNING");
   draw_screen();

   unifrog_input_wireless_reset();
   unifrog_input_wireless_init();
   printf("WIRELESS_DIAG init_done initialized=%d bus_ok=%d channel=%u\n",
      unifrog_input_wireless_initialized(),
      unifrog_input_wireless_bus_ok(),
      unifrog_input_wireless_channel_index());
   unifrog_input_log_wireless_sdio_state("diag_after_init");

   if (unifrog_input_wireless_available()) {
      uint32_t start_ms;
      unsigned polls = 0;
      int got_packet = 0;

      unifrog_text_copy(status_line, sizeof(status_line), "WIRELESS: PRESS BUTTONS");
      draw_screen();
      start_ms = unifrog_perf_time_ms();
      printf("WIRELESS_DIAG stock_hold start duration_s=%u note=press wireless buttons now\n",
         WIRELESS_DIAG_STOCK_HOLD_SECONDS);
      while ((uint32_t)(unifrog_perf_time_ms() - start_ms) <=
         WIRELESS_DIAG_STOCK_HOLD_SECONDS * 1000u) {
         unifrog_input_wireless_poll_once();
         polls++;
         if (unifrog_input_wireless_all_buttons())
            got_packet = 1;
         usleep(2000);
      }
      printf("WIRELESS_DIAG stock_hold done polls=%u got_packet=%d p1_raw=0x%04lx p1_state=0x%08lx p2_raw=0x%04lx p2_state=0x%08lx final_status=0x%02x\n",
         polls, got_packet,
         (unsigned long)unifrog_input_wireless_raw(0),
         (unsigned long)unifrog_input_wireless_buttons(0),
         (unsigned long)unifrog_input_wireless_raw(1),
         (unsigned long)unifrog_input_wireless_buttons(1),
         (unsigned)unifrog_input_wireless_status());
      flush_memlog();

      for (unsigned i = 0; i < ARRAY_SIZE(channels); i++) {
         char tag[24];
         snprintf(tag, sizeof(tag), "fixed_%02x", channels[i]);
         got_packet |= unifrog_input_wireless_receive_window(tag, channels[i],
            WIRELESS_DIAG_FIXED_WINDOW_MS, 2000);
      }
      printf("WIRELESS_DIAG fixed_windows done got_packet=%d\n", got_packet);

      start_ms = unifrog_perf_time_ms();
      unifrog_input_wireless_prepare_poll();
      printf("WIRELESS_DIAG final_poll start note=press wireless buttons now duration_s=%u\n",
         WIRELESS_DIAG_FINAL_POLL_SECONDS);
      while ((uint32_t)(unifrog_perf_time_ms() - start_ms) <=
         WIRELESS_DIAG_FINAL_POLL_SECONDS * 1000u) {
         unifrog_input_wireless_poll_once();
         polls++;
         if (unifrog_input_wireless_all_buttons())
            got_packet = 1;
         usleep(2000);
      }
      printf("WIRELESS_DIAG poll_done polls=%u got_packet=%d p1_raw=0x%04lx p1_state=0x%08lx p2_raw=0x%04lx p2_state=0x%08lx final_status=0x%02x\n",
         polls,
         got_packet,
         (unsigned long)unifrog_input_wireless_raw(0),
         (unsigned long)unifrog_input_wireless_buttons(0),
         (unsigned long)unifrog_input_wireless_raw(1),
         (unsigned long)unifrog_input_wireless_buttons(1),
         (unsigned)unifrog_input_wireless_status());
   }

   unifrog_input_restore_local_bus();
   printf("WIRELESS_DIAG end physical_button_bus_restored\n");
   flush_memlog();
   unifrog_text_copy(status_line, sizeof(status_line), "WIRELESS DIAG DONE");
   report_done = 1;
   dirty = 1;
}

static int is_video_file(const char *name)
{
   static const char *suffixes[] = {
      ".mp4", ".mov", ".mkv", ".avi", ".ts",
      ".m2ts", ".mpg", ".mpeg", ".h264", ".264",
   };

   for (unsigned i = 0; i < ARRAY_SIZE(suffixes); i++) {
      if (unifrog_text_ends_with_ci(name, suffixes[i]))
         return 1;
   }
   return 0;
}

static int is_libretro_game_file(const char *name)
{
   static const char *const suffixes[] = {
      ".gb", ".gbc", ".gba",
   };

   for (unsigned i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
      if (unifrog_text_ends_with_ci(name, suffixes[i]))
         return 1;
   }
   return 0;
}

static void close_stream_video(void)
{
   if (player_stream.fd >= 0)
      close(player_stream.fd);
   if (player_stream.cache)
      free(player_stream.cache);
   memset(&player_stream, 0, sizeof(player_stream));
   player_stream.fd = -1;
}

static int open_stream_video(const char *path)
{
   struct stat st;

   close_stream_video();
   player_stream.fd = open(path, O_RDONLY);
   if (player_stream.fd < 0) {
      printf("unifrog stream open failed path=%s\n", path);
      return -1;
   }

   if (fstat(player_stream.fd, &st) != 0 || st.st_size <= 0) {
      printf("unifrog stream stat failed path=%s\n", path);
      close_stream_video();
      return -1;
   }

   player_stream.cache = malloc(VIDEO_STREAM_CACHE_BYTES);
   if (!player_stream.cache) {
      printf("unifrog stream cache malloc failed bytes=%u\n",
         (unsigned)VIDEO_STREAM_CACHE_BYTES);
      close_stream_video();
      return -1;
   }

   player_stream.size = st.st_size;
   player_stream.pos = 0;
   player_stream.cache_start = -1;
   player_stream.cache_size = 0;
   player_stream.active = 1;
   printf("unifrog stream open ok size=%lld cache=%u path=%s\n",
      (long long)player_stream.size, (unsigned)VIDEO_STREAM_CACHE_BYTES, path);
   return 0;
}

static int stream_video_refill(struct stream_video *stream)
{
   ssize_t got;
   size_t wanted;

   if (lseek(stream->fd, stream->pos, SEEK_SET) < 0)
      return -1;

   wanted = VIDEO_STREAM_CACHE_BYTES;
   if ((off_t)wanted > stream->size - stream->pos)
      wanted = (size_t)(stream->size - stream->pos);

   got = read(stream->fd, stream->cache, wanted);
   if (got <= 0)
      return -1;

   stream->cache_start = stream->pos;
   stream->cache_size = (size_t)got;
   printf("unifrog stream refill pos=%lld bytes=%u\n",
      (long long)stream->cache_start, (unsigned)stream->cache_size);
   return 0;
}

static int stream_video_read(void *opaque, uint8_t *buf, int bufsize)
{
   struct stream_video *stream = (struct stream_video *)opaque;
   size_t total = 0;

   if (!stream || !stream->active || !buf || bufsize <= 0)
      return 0;

   while (total < (size_t)bufsize && stream->pos < stream->size) {
      off_t cache_end = stream->cache_start + (off_t)stream->cache_size;
      size_t offset;
      size_t available;
      size_t copy_size;

      if (stream->cache_start < 0 || stream->pos < stream->cache_start ||
          stream->pos >= cache_end) {
         if (stream_video_refill(stream) != 0)
            break;
         cache_end = stream->cache_start + (off_t)stream->cache_size;
      }

      offset = (size_t)(stream->pos - stream->cache_start);
      available = stream->cache_size - offset;
      copy_size = (size_t)bufsize - total;
      if (copy_size > available)
         copy_size = available;

      memcpy(buf + total, stream->cache + offset, copy_size);
      stream->pos += (off_t)copy_size;
      total += copy_size;
   }

   return (int)total;
}

static int64_t stream_video_seek(void *opaque, int64_t offset, int whence)
{
   struct stream_video *stream = (struct stream_video *)opaque;
   int64_t base;
   int64_t next;

   if (!stream || !stream->active)
      return -1;

   if (whence == SEEK_SET)
      base = 0;
   else if (whence == SEEK_CUR)
      base = (int64_t)stream->pos;
   else if (whence == SEEK_END)
      base = (int64_t)stream->size;
   else
      return -1;

   next = base + offset;
   if (next < 0 || next > (int64_t)stream->size)
      return -1;

   stream->pos = (off_t)next;
   return next;
}

static void save_button_prev(void)
{
   unifrog_input_save_previous();
}

static void clear_button_latches(void)
{
   unifrog_input_clear();
}

static int fb_init(void)
{
   size_t screen_bytes;

   fbdev = open("/dev/fb0", O_RDWR);
   if (fbdev < 0) {
      printf("unifrog fb open failed\n");
      return -1;
   }

   ioctl(fbdev, FBIOGET_FSCREENINFO, &fix);
   ioctl(fbdev, FBIOGET_VSCREENINFO, &var);
   ioctl(fbdev, FBIOBLANK, FB_BLANK_UNBLANK);

   var.xoffset = 0;
   var.yoffset = 0;
   var.xres_virtual = var.xres;
   var.yres_virtual = var.yres;
   var.bits_per_pixel = 16;
   var.red.length = 5;
   var.green.length = 6;
   var.blue.length = 5;
   ioctl(fbdev, FBIOPUT_VSCREENINFO, &var);
   ioctl(fbdev, FBIOGET_FSCREENINFO, &fix);
   ioctl(fbdev, FBIOGET_VSCREENINFO, &var);

   line_pixels = fix.line_length ?
      (uint32_t)(fix.line_length / sizeof(uint16_t)) :
      (uint32_t)(var.xres * var.bits_per_pixel / 16);
   screen_bytes = (size_t)line_pixels * var.yres * sizeof(uint16_t);
   if (screen_bytes && fix.smem_len >= screen_bytes * 2) {
      struct fb_var_screeninfo multivar = var;

      multivar.xoffset = 0;
      multivar.yoffset = 0;
      multivar.yres_virtual = multivar.yres * 2;
      if (ioctl(fbdev, FBIOPUT_VSCREENINFO, &multivar) == 0) {
         ioctl(fbdev, FBIOGET_FSCREENINFO, &fix);
         ioctl(fbdev, FBIOGET_VSCREENINFO, &var);
         fb_buffers = 2;
      }
   }

   fb_base = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbdev, 0);
   if (fb_base == MAP_FAILED) {
      printf("unifrog fb mmap failed\n");
      fb_base = NULL;
      fb = NULL;
      return -1;
   }

   fb = fb_base;
   fb_active_buffer = 0;
   fb_mapped_len = fix.smem_len;
   printf("unifrog fb ready %ux%u line=%u bytes=%u buffers=%u yvirt=%u\n",
      (unsigned)var.xres, (unsigned)var.yres, (unsigned)line_pixels,
      (unsigned)fix.smem_len, fb_buffers, (unsigned)var.yres_virtual);
   return 0;
}

static void fb_close_frontend(void)
{
   if (fb_base && fb_mapped_len)
      munmap(fb_base, fb_mapped_len);
   if (fbdev >= 0)
      close(fbdev);
   fbdev = -1;
   fb_base = NULL;
   fb = NULL;
   fb_mapped_len = 0;
   fb_buffers = 1;
   fb_active_buffer = 0;
   memset(&fix, 0, sizeof(fix));
   memset(&var, 0, sizeof(var));
   line_pixels = 0;
}

static int fb_reopen_frontend(const char *tag)
{
   fb_close_frontend();
   if (fb_init() != 0) {
      printf("unifrog fb reopen tag=%s ret=-1\n", tag ? tag : "none");
      return -1;
   }
   printf("unifrog fb reopen tag=%s ret=0\n", tag ? tag : "none");
   return 0;
}

static void open_dis_device(void)
{
   if (dis_fd >= 0)
      return;
   dis_fd = open("/dev/dis", O_RDWR);
   printf("unifrog dis open %s\n", dis_fd >= 0 ? "ok" : "failed");
}

static void set_video_layer_visible(int visible, int src_w, int src_h,
   int dst_w, int dst_h)
{
   struct dis_layer_blend_order order;
   struct dis_zoom zoom;

   open_dis_device();
   if (dis_fd < 0)
      return;

   memset(&order, 0, sizeof(order));
   order.distype = DIS_TYPE_HD;
   if (visible) {
      order.main_layer = 3;
      order.auxp_layer = 2;
      order.gmas_layer = 1;
      order.gmaf_layer = 0;
   } else {
      order.main_layer = 0;
      order.auxp_layer = 1;
      order.gmas_layer = 2;
      order.gmaf_layer = 3;
   }
   printf("unifrog dis layer video=%d main=%d aux=%d gmas=%d gmaf=%d\n",
      visible, order.main_layer, order.auxp_layer, order.gmas_layer, order.gmaf_layer);
   ioctl(dis_fd, DIS_SET_LAYER_ORDER, &order);

   memset(&zoom, 0, sizeof(zoom));
   zoom.distype = DIS_TYPE_HD;
   zoom.layer = DIS_LAYER_MAIN;
   zoom.src_area.x = 0;
   zoom.src_area.y = 0;
   zoom.src_area.w = src_w > 0 ? src_w : 1920;
   zoom.src_area.h = src_h > 0 ? src_h : 1080;
   zoom.dst_area.x = 0;
   zoom.dst_area.y = 0;
   zoom.dst_area.w = dst_w > 0 ? dst_w : VIDEO_OUT_W;
   zoom.dst_area.h = dst_h > 0 ? dst_h : VIDEO_OUT_H;
   printf("unifrog dis zoom src=%ux%u dst=%ux%u\n",
      zoom.src_area.w, zoom.src_area.h, zoom.dst_area.w, zoom.dst_area.h);
   ioctl(dis_fd, DIS_SET_ZOOM, &zoom);
}

struct video_display_mode {
   const char *name;
   int src_w;
   int src_h;
   int dst_w;
   int dst_h;
};

static const struct video_display_mode video_modes[] = {
   {"1080_to_1920x1080", 1920, 1080, 1920, 1080},
};

static void apply_video_display_mode(int mode)
{
   struct vdec_dis_rect rect;
   const struct video_display_mode *cfg;
   int src_w;
   int src_h;

   if (!player_handle)
      return;

   (void)mode;
   mode = 0;
   cfg = &video_modes[mode];
   src_w = cfg->src_w > 0 ? cfg->src_w : player_video_w;
   src_h = cfg->src_h > 0 ? cfg->src_h : player_video_h;
   if (src_w <= 0)
      src_w = PANEL_OUT_W;
   if (src_h <= 0)
      src_h = PANEL_OUT_H;

   memset(&rect, 0, sizeof(rect));
   rect.src_rect.x = 0;
   rect.src_rect.y = 0;
   rect.src_rect.w = (uint16_t)src_w;
   rect.src_rect.h = (uint16_t)src_h;
   rect.dst_rect.x = 0;
   rect.dst_rect.y = 0;
   rect.dst_rect.w = (uint16_t)cfg->dst_w;
   rect.dst_rect.h = (uint16_t)cfg->dst_h;

   set_video_layer_visible(1, src_w, src_h, cfg->dst_w, cfg->dst_h);
   hcplayer_set_display_rect(player_handle, &rect);
   player_display_mode = mode;
   player_display_mode_frame = frames;
   printf("unifrog player mode %d/%u %s src=%dx%d dst=%dx%d\n",
      mode + 1, (unsigned)ARRAY_SIZE(video_modes), cfg->name,
      src_w, src_h, cfg->dst_w, cfg->dst_h);
}

static size_t visible_fb_bytes(void)
{
   return (size_t)line_pixels * (size_t)var.yres * sizeof(uint16_t);
}

static void restore_frontend_fb_mode(const char *tag)
{
   struct fb_var_screeninfo restore;
   int ret = -1;

   if (fbdev < 0 || !fb_base)
      return;

   restore = var;
   restore.xoffset = 0;
   restore.yoffset = fb_active_buffer * restore.yres;
   restore.xres_virtual = restore.xres;
   restore.yres_virtual = restore.yres * fb_buffers;
   restore.bits_per_pixel = 16;
   restore.red.length = 5;
   restore.green.length = 6;
   restore.blue.length = 5;
   ret = ioctl(fbdev, FBIOPUT_VSCREENINFO, &restore);
   if (ret == 0)
      ret = ioctl(fbdev, FBIOPAN_DISPLAY, &restore);
   ioctl(fbdev, FBIOGET_VSCREENINFO, &var);
   ioctl(fbdev, FBIOGET_FSCREENINFO, &fix);
   line_pixels = fix.line_length ?
      (uint32_t)(fix.line_length / sizeof(uint16_t)) :
      (uint32_t)(var.xres * var.bits_per_pixel / 16);
   fb = fb_base + fb_active_buffer * line_pixels * var.yres;
   printf("unifrog fb restore tag=%s ret=%d buffers=%u active=%u yvirt=%u yoff=%u line=%u\n",
      tag ? tag : "none", ret, fb_buffers, fb_active_buffer,
      (unsigned)var.yres_virtual, (unsigned)var.yoffset,
      (unsigned)line_pixels);
}

static void log_fb_state(const char *tag)
{
   printf("unifrog fb state tag=%s fbdev=%d fb=%p base=%p xres=%u yres=%u xvirt=%u yvirt=%u xoff=%u yoff=%u bpp=%u line=%lu visible_bytes=%lu smem_len=%lu buffers=%u active=%u\n",
      tag ? tag : "none", fbdev, fb, fb_base,
      (unsigned)var.xres, (unsigned)var.yres,
      (unsigned)var.xres_virtual, (unsigned)var.yres_virtual,
      (unsigned)var.xoffset, (unsigned)var.yoffset,
      (unsigned)var.bits_per_pixel, (unsigned long)line_pixels,
      (unsigned long)visible_fb_bytes(), (unsigned long)fix.smem_len,
      fb_buffers, fb_active_buffer);
}

static uint32_t scan_local_buttons(int update_debounce)
{
   (void)update_debounce;
   return unifrog_input_poll_local_raw();
}

static void poll_local_buttons_only(void)
{
   (void)unifrog_input_poll_local_raw();
}

static void poll_buttons(void)
{
   unifrog_input_poll();
}

static void browser_load(void)
{
   DIR *dir;
   struct dirent *entry;

   browser_count = 0;
   browser_selected = 0;
   browser_scroll = 0;

   dir = opendir(browser_path);
   if (!dir) {
      unifrog_text_copy(status_line, sizeof(status_line), "OPEN FAILED");
      printf("unifrog browser open failed path=%s\n", browser_path);
      return;
   }

   if (strcmp(browser_path, "/") != 0 && browser_count < MAX_BROWSER_ITEMS) {
      strcpy(browser_items[browser_count].name, "..");
      browser_items[browser_count].is_dir = 1;
      browser_count++;
   }

   while ((entry = readdir(dir)) && browser_count < MAX_BROWSER_ITEMS) {
      char full[MAX_PATH_LEN];
      struct stat st;

      if (entry->d_name[0] == '.')
         continue;

      unifrog_path_join(full, sizeof(full), browser_path, entry->d_name);
      if (stat(full, &st) != 0)
         continue;

      unifrog_text_copy(browser_items[browser_count].name,
         sizeof(browser_items[browser_count].name), entry->d_name);
      browser_items[browser_count].is_dir = S_ISDIR(st.st_mode);
      browser_count++;
   }

   closedir(dir);
   snprintf(status_line, sizeof(status_line), "BROWSER %d ITEMS", browser_count);
   printf("unifrog browser path=%s items=%d\n", browser_path, browser_count);
}

static void draw_screen(void);
static void start_native_video(const char *path);
static void start_cached_video(void);
static void video_probe(void);

static void browser_enter_selected(void)
{
   struct browser_item *item;
   char next[MAX_PATH_LEN];
   char *slash;

   if (browser_selected < 0 || browser_selected >= browser_count)
      return;

   item = &browser_items[browser_selected];
   if (!item->is_dir) {
      unifrog_path_join(next, sizeof(next), browser_path, item->name);
      if (is_libretro_game_file(item->name)) {
         printf("unifrog browser libretro path=%s\n", next);
         unifrog_text_copy(status_line, sizeof(status_line), "RUNNING CORE");
         draw_screen();
         (void)unifrog_libretro_run_path(next);
         fb_reopen_frontend("libretro_return");
         if (js_native_action_active) {
            js_relaunch_requested = 1;
            view = VIEW_MENU;
            unifrog_text_copy(status_line, sizeof(status_line), "RETURNING TO JS");
         } else {
            view = VIEW_BROWSER;
            browser_load();
         }
         dirty = 1;
         return;
      }
      if (is_video_file(item->name)) {
         start_native_video(next);
         return;
      }
      snprintf(status_line, sizeof(status_line), "FILE %s", item->name);
      printf("unifrog browser file path=%s\n", next);
      return;
   }

   if (strcmp(item->name, "..") == 0) {
      slash = strrchr(browser_path, '/');
      if (slash && slash != browser_path)
         *slash = 0;
      else
         strcpy(browser_path, "/");
   } else {
      if (strcmp(browser_path, "/") == 0)
         unifrog_path_join(next, sizeof(next), "/", item->name);
      else
         unifrog_path_join(next, sizeof(next), browser_path, item->name);
      unifrog_text_copy(browser_path, sizeof(browser_path), next);
   }

   browser_load();
}

static void launch_gambatte_test_rom(void)
{
   struct stat st;

   if (stat(GAMBATTE_TEST_ROM, &st) != 0) {
      unifrog_text_copy(status_line, sizeof(status_line), "TEST ROM MISSING");
      printf("unifrog gambatte test missing path=%s\n", GAMBATTE_TEST_ROM);
      if (js_native_action_active)
         js_relaunch_requested = 1;
      dirty = 1;
      return;
   }

   printf("unifrog gambatte test path=%s\n", GAMBATTE_TEST_ROM);
   if (!js_native_action_active) {
      unifrog_text_copy(status_line, sizeof(status_line), "RUNNING GAMBATTE TEST");
      draw_screen();
   }
   (void)unifrog_libretro_run_gambatte(GAMBATTE_TEST_ROM);
   fb_reopen_frontend("gambatte_test_return");
   if (js_native_action_active) {
      js_relaunch_requested = 1;
      view = VIEW_MENU;
      unifrog_text_copy(status_line, sizeof(status_line), "RETURNING TO JS");
   } else {
      view = VIEW_MENU;
      unifrog_text_copy(status_line, sizeof(status_line), "GAMBATTE TEST DONE");
   }
   dirty = 1;
}

static uint32_t test_frontend_millis(void)
{
   return unifrog_perf_time_ms();
}

static void wait_for_button_release(unsigned timeout_ms)
{
   uint32_t start = test_frontend_millis();

   while (test_frontend_millis() - start < timeout_ms) {
      unifrog_input_save_previous();
      unifrog_input_poll_with_wireless_divisor(1);
      if (unifrog_input_buttons() == 0)
         break;
      usleep(16000);
   }
   clear_button_latches();
}

static void js_host_log(void *opaque, const char *message)
{
   (void)opaque;
   printf("js2300 %s\n", message ? message : "");
}

static int js_host_flush_log(void *opaque)
{
   (void)opaque;
   return flush_memlog();
}

static uint32_t js_host_millis(void *opaque)
{
   (void)opaque;
   return test_frontend_millis();
}

static void js_host_sleep_ms(void *opaque, uint32_t ms)
{
   (void)opaque;
   if (ms > 1000)
      ms = 1000;
   if (ms)
      msleep(ms);
}

static void js_host_begin_frame(void)
{
   if (js_frame_open)
      return;

   js_draw_buffer = fb_active_buffer;
   if (fb_base && fb_buffers > 1)
      js_draw_buffer = (fb_active_buffer + 1) % fb_buffers;
   if (fb_base)
      fb = fb_base + js_draw_buffer * line_pixels * var.yres;
   js_frame_open = 1;
}

static void js_host_video_clear(void *opaque, uint16_t color)
{
   (void)opaque;
   js_host_begin_frame();
   fill_rect(0, 0, var.xres, var.yres, color);
}

static void js_host_video_rects(void *opaque, const struct js2300_rect *rects,
   size_t count)
{
   (void)opaque;
   js_host_begin_frame();
   for (size_t i = 0; i < count; i++)
      fill_rect(rects[i].x, rects[i].y, rects[i].w, rects[i].h, rects[i].color);
}

static void js_host_video_text(void *opaque, int x, int y, const char *text,
   uint16_t color)
{
   (void)opaque;
   js_host_begin_frame();
   draw_text(x, y, text, color, 1);
}

static void js_host_video_present(void *opaque)
{
   int ret = 0;
   (void)opaque;

   if (!js_frame_open) {
      ioctl(fbdev, FBIO_WAITFORVSYNC, &ret);
      return;
   }

   unifrog_perf_cache_flush(fb, visible_fb_bytes());
   ioctl(fbdev, FBIO_WAITFORVSYNC, &ret);
   if (fb_buffers > 1) {
      var.xoffset = 0;
      var.yoffset = js_draw_buffer * var.yres;
   }
   ioctl(fbdev, FBIOPAN_DISPLAY, &var);
   fb_active_buffer = js_draw_buffer;
   js_frame_open = 0;
}

static uint32_t js_host_input_poll(void *opaque)
{
   uint32_t buttons;
   uint32_t out = 0;
   static uint32_t last_out;
   static unsigned transition_logs;
   (void)opaque;

   unifrog_input_save_previous();
   unifrog_input_poll_with_wireless_divisor(1);
   buttons = unifrog_input_buttons();

   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP))
      out |= 1u << 0;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN))
      out |= 1u << 1;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT))
      out |= 1u << 2;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT))
      out |= 1u << 3;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A))
      out |= 1u << 4;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B))
      out |= 1u << 5;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_X))
      out |= 1u << 6;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_Y))
      out |= 1u << 7;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_L))
      out |= 1u << 8;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_R))
      out |= 1u << 9;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT))
      out |= 1u << 10;
   if (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START))
      out |= 1u << 11;

   if (out != last_out && transition_logs < 16) {
      printf("js2300 input buttons=0x%08lx js=0x%08lx prev=0x%08lx\n",
         (unsigned long)buttons, (unsigned long)out, (unsigned long)last_out);
      transition_logs++;
   }
   last_out = out;

   return out;
}

static void js_host_battery(void *opaque, struct js2300_battery_status *status)
{
   (void)opaque;
   if (!status)
      return;
   unifrog_battery_update(&battery_status, 0);
   status->percent = battery_status.available ? (int)(battery_status.bars * 25u) : -1;
   status->charging = 0;
   status->low = battery_status.low;
}

static int js_host_action(void *opaque, const char *id)
{
   (void)opaque;
   if (!id || !*id)
      return -1;
   if (js_requested_action[0])
      return 0;

   unifrog_text_copy(js_requested_action, sizeof(js_requested_action), id);
   printf("js2300 action request id=%s\n", js_requested_action);
   return 0;
}

static void js_host_exit(void *opaque, const char *reason)
{
   (void)opaque;
   printf("js2300 exit reason=%s\n", reason ? reason : "");
}

static void run_js_requested_action(void)
{
   js_native_action_active = 1;
   if (strcmp(js_requested_action, "continue") == 0 ||
       strcmp(js_requested_action, "library") == 0) {
      launch_gambatte_test_rom();
   } else if (strcmp(js_requested_action, "files") == 0) {
      unifrog_text_copy(browser_path, sizeof(browser_path), "/media/mmcblk0");
      view = VIEW_BROWSER;
      browser_load();
      dirty = 1;
   } else if (strcmp(js_requested_action, "media") == 0) {
      view = VIEW_VIDEO;
      video_probe();
      dirty = 1;
   } else if (strcmp(js_requested_action, "firmware") == 0) {
      unifrog_text_copy(browser_path, sizeof(browser_path), "/media/mmcblk0/firmware");
      view = VIEW_BROWSER;
      browser_load();
      dirty = 1;
   } else if (strcmp(js_requested_action, "settings") == 0) {
      view = VIEW_INPUT;
      dirty = 1;
   } else {
      snprintf(status_line, sizeof(status_line), "JS ACTION %s", js_requested_action);
      js_relaunch_requested = 1;
      dirty = 1;
   }
   js_requested_action[0] = 0;
}

static void launch_js_frontend(void)
{
   struct js2300_config config;
   struct js2300_host host;
   struct js2300_runtime *runtime = NULL;
   size_t old_auto_flush;
   int ret;
   int keep_running;

   js2300_config_init(&config);
   config.app_root = "/media/mmcblk0/unifrog";
   config.entry_script = "main.js";
   config.heap_bytes = 8u * 1024u * 1024u;

   memset(&host, 0, sizeof(host));
   host.size = sizeof(host);
   host.log = js_host_log;
   host.flush_log = js_host_flush_log;
   host.millis = js_host_millis;
   host.sleep_ms = js_host_sleep_ms;
   host.video_clear = js_host_video_clear;
   host.video_rects = js_host_video_rects;
   host.video_text = js_host_video_text;
   host.video_present = js_host_video_present;
   host.input_poll = js_host_input_poll;
   host.battery = js_host_battery;
   host.action = js_host_action;
   host.exit = js_host_exit;

   keep_running = 1;
   while (keep_running) {
      keep_running = 0;
      js_native_action_active = 0;
      js_relaunch_requested = 0;
      js_requested_action[0] = 0;
      printf("js2300 launch root=%s script=%s\n", config.app_root, config.entry_script);
      unifrog_text_copy(status_line, sizeof(status_line), "RUNNING JS2300");
      js_frame_open = 0;
      js_draw_buffer = fb_active_buffer;
      old_auto_flush = unifrog_log_auto_flush_bytes();
      unifrog_log_set_auto_flush_bytes(64 * 1024);
      flush_memlog();

      ret = js2300_runtime_create(&config, &host, &runtime);
      if (ret == 0)
         ret = js2300_runtime_run(runtime);
      js2300_runtime_destroy(runtime);
      runtime = NULL;

      unifrog_log_set_auto_flush_bytes(old_auto_flush);
      printf("js2300 launch done ret=%d\n", ret);
      flush_memlog();
      if (ret == 0 && js_requested_action[0]) {
         wait_for_button_release(1200);
         run_js_requested_action();
         if (js_relaunch_requested) {
            wait_for_button_release(1200);
            keep_running = 1;
         }
      } else {
         view = VIEW_MENU;
         unifrog_text_copy(status_line, sizeof(status_line),
            ret == 0 ? "JS2300 DONE" : "JS2300 FAILED");
         dirty = 1;
         wait_for_button_release(1200);
      }
   }
}

static void audio_diag_fill_chunk(const struct audio_diag_test *test,
   uint32_t *phase, unsigned done_frames, unsigned total_frames,
   unsigned frames)
{
   unsigned hz = test->start_hz;

   if (test->start_hz != test->end_hz && total_frames > 1) {
      hz = test->start_hz +
         ((test->end_hz - test->start_hz) * done_frames) / total_frames;
   }

   for (unsigned i = 0; i < frames; i++) {
      int sample = 0;
      int16_t left = 0;
      int16_t right = 0;

      if (hz > 0) {
         uint32_t step = (uint32_t)(((uint64_t)hz << 32) / test->rate);
         sample = (sine_table[*phase >> 27] / 8) * (int)test->gain;
         if (sample > 32767)
            sample = 32767;
         else if (sample < -32768)
            sample = -32768;
         *phase += step;
      }

      switch (test->route) {
      case AUDIO_DIAG_LEFT_ONLY:
         left = (int16_t)sample;
         break;
      case AUDIO_DIAG_RIGHT_ONLY:
         right = (int16_t)sample;
         break;
      case AUDIO_DIAG_DUAL_MONO:
         left = (int16_t)sample;
         right = (int16_t)sample;
         break;
      case AUDIO_DIAG_DIFFERENTIAL:
         left = (int16_t)sample;
         right = (int16_t)-sample;
         break;
      }

      audio_diag_buffer[i * 2] = left;
      audio_diag_buffer[i * 2 + 1] = right;
   }
}

static void audio_diag_write_silence(struct unifrog_audio *audio,
   unsigned rate, unsigned milliseconds, int *failures)
{
   unsigned total_frames = (rate * milliseconds) / 1000u;
   unsigned done_frames = 0;

   memset(audio_diag_buffer, 0, sizeof(audio_diag_buffer));
   while (done_frames < total_frames) {
      unsigned chunk = total_frames - done_frames;

      if (chunk > AUDIO_DIAG_CHUNK_FRAMES)
         chunk = AUDIO_DIAG_CHUNK_FRAMES;
      if (unifrog_audio_write(audio, audio_diag_buffer, chunk) != 0 &&
          failures)
         (*failures)++;
      done_frames += chunk;
   }
}

static void run_audio_diag_test(unsigned index)
{
   const struct audio_diag_test *test;
   struct unifrog_audio audio;
   unsigned silence_frames;
   unsigned total_frames;
   unsigned done_frames = 0;
   uint32_t phase = 0;
   int open_ret;
   int volume_ret = -1;
   int mute_ret = -1;
   int start_ret = -1;
   int unmute_ret = -1;
   int output_ret = -1;
   int failures = 0;
   int actual_backend = 0;

   if (index >= ARRAY_SIZE(audio_diag_tests))
      return;

   test = &audio_diag_tests[index];
   audio_diag_running_name = test->name;
   snprintf(status_line, sizeof(status_line), "AUDIO %s", test->name);
   draw_screen();
   printf("AUDIO_DIAG start index=%u name=\"%s\" rate=%u route=%s start_hz=%u end_hz=%u volume=%u gain=%u\n",
      index, test->name, test->rate, audio_diag_route_name(test->route),
      test->start_hz, test->end_hz, test->volume, test->gain);
   (void)unifrog_log_flush();

   if (test->gate_only) {
      unifrog_audio_set_system_output_enabled(1);
      unifrog_audio_debug_dump(NULL, test->name);
      usleep((AUDIO_DIAG_DURATION_MS * 1000u));
      unifrog_audio_set_system_output_enabled(0);
      printf("AUDIO_DIAG end index=%u name=\"%s\" gate_only=1\n",
         index, test->name);
      (void)unifrog_log_flush();
      audio_diag_running_name = NULL;
      snprintf(status_line, sizeof(status_line), "DONE %s", test->name);
      dirty = 1;
      return;
   }

   open_ret = unifrog_audio_open_backend(&audio, test->rate, 2, 1536, 4,
      test->backend);
   if (open_ret != 0) {
      printf("AUDIO_DIAG open_fail index=%u name=\"%s\" ret=%d\n",
         index, test->name, open_ret);
      unifrog_text_copy(status_line, sizeof(status_line), "AUDIO OPEN FAILED");
      audio_diag_running_name = NULL;
      dirty = 1;
      return;
   }

   volume_ret = unifrog_audio_set_volume(&audio, test->volume);
   mute_ret = unifrog_audio_set_mute(&audio, 1);
   memset(audio_diag_buffer, 0, sizeof(audio_diag_buffer));
   silence_frames = audio.frame_bytes ? audio.period_bytes / audio.frame_bytes : 0;
   if (silence_frames > AUDIO_DIAG_CHUNK_FRAMES)
      silence_frames = AUDIO_DIAG_CHUNK_FRAMES;
   for (unsigned i = 0; i < 3 && silence_frames > 0; i++) {
      if (unifrog_audio_write(&audio, audio_diag_buffer, silence_frames) != 0)
         failures++;
   }
   start_ret = unifrog_audio_start(&audio);
   unmute_ret = unifrog_audio_set_mute(&audio, 0);
   output_ret = unifrog_audio_set_output_enabled(&audio, 1);
   unifrog_audio_debug_dump(&audio, test->name);
   actual_backend = audio.backend;

   if (test->auto_gate) {
      output_ret = unifrog_audio_set_output_enabled(&audio, 0);
      printf("AUDIO_DIAG auto_gate phase=silence_closed ms=1200 output_ret=%d\n",
         output_ret);
      (void)unifrog_log_flush();
      audio_diag_write_silence(&audio, test->rate, 1200, &failures);

      output_ret = unifrog_audio_set_output_enabled(&audio, 1);
      printf("AUDIO_DIAG auto_gate phase=tone_open ms=1200 output_ret=%d\n",
         output_ret);
      (void)unifrog_log_flush();
      total_frames = (test->rate * 1200u) / 1000u;
      while (done_frames < total_frames) {
         unsigned chunk = total_frames - done_frames;

         if (chunk > AUDIO_DIAG_CHUNK_FRAMES)
            chunk = AUDIO_DIAG_CHUNK_FRAMES;
         audio_diag_fill_chunk(test, &phase, done_frames, total_frames, chunk);
         if (unifrog_audio_write(&audio, audio_diag_buffer, chunk) != 0)
            failures++;
         done_frames += chunk;
      }

      output_ret = unifrog_audio_set_output_enabled(&audio, 0);
      printf("AUDIO_DIAG auto_gate phase=silence_closed_after ms=1200 output_ret=%d\n",
         output_ret);
      (void)unifrog_log_flush();
      audio_diag_write_silence(&audio, test->rate, 1200, &failures);
      goto close_audio;
   }

   total_frames = (test->rate * AUDIO_DIAG_DURATION_MS) / 1000u;
   while (done_frames < total_frames) {
      unsigned chunk = total_frames - done_frames;

      if (chunk > AUDIO_DIAG_CHUNK_FRAMES)
         chunk = AUDIO_DIAG_CHUNK_FRAMES;
      audio_diag_fill_chunk(test, &phase, done_frames, total_frames, chunk);
      if (unifrog_audio_write(&audio, audio_diag_buffer, chunk) != 0)
         failures++;
      done_frames += chunk;
   }

close_audio:
   unifrog_audio_close(&audio);
   printf("AUDIO_DIAG end index=%u name=\"%s\" backend=%s actual=%d frames=%u failures=%d open_ret=%d volume_ret=%d mute_ret=%d start_ret=%d unmute_ret=%d output_ret=%d\n",
      index, test->name, audio_diag_backend_name(test->backend),
      actual_backend, done_frames, failures, open_ret, volume_ret, mute_ret,
      start_ret, unmute_ret, output_ret);
   (void)unifrog_log_flush();
   audio_diag_running_name = NULL;
   snprintf(status_line, sizeof(status_line), "DONE %s", test->name);
   dirty = 1;
}

static void run_audio_diag_all(void)
{
   for (unsigned i = 0; i < ARRAY_SIZE(audio_diag_tests); i++) {
      run_audio_diag_test(i);
      usleep(350000);
   }
}

static void write_storage_probe(void)
{
   const char *path = "/media/mmcblk0/unifrog-storage-test.txt";
   const char *payload = "sf2000 unifrog storage ok\n";
   char readback[64] = {0};
   int fd;

   fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0666);
   if (fd < 0) {
      snprintf(status_line, sizeof(status_line), "STORAGE OPEN FAILED");
      printf("unifrog storage open failed path=%s\n", path);
      return;
   }

   write(fd, payload, strlen(payload));
   lseek(fd, 0, SEEK_SET);
   read(fd, readback, sizeof(readback) - 1);
   close(fd);

   snprintf(status_line, sizeof(status_line), "STORAGE OK");
   printf("unifrog storage path=%s readback=%s", path, readback);
}

static void video_probe(void)
{
   const char *paths[] = {
      "/dev/dis", "/dev/ge", "/dev/fb0", "/dev/viddec", "/dev/vidsink", "/dev/video0"
   };

   printf("unifrog video/display probe start\n");
   for (unsigned i = 0; i < ARRAY_SIZE(paths); i++) {
      int fd = open(paths[i], O_RDWR);
      printf("unifrog video dev %s %s\n", paths[i], fd >= 0 ? "open" : "missing");
      if (fd >= 0)
         close(fd);
   }

   snprintf(status_line, sizeof(status_line), "VIDEO PROBE LOGGED");
}

static int probe_open_path(const char *path)
{
   int fd = open(path, O_RDWR);

   if (fd < 0)
      fd = open(path, O_RDONLY);
   if (fd >= 0) {
      close(fd);
      return 0;
   }
   return -1;
}

static void report_device_probe(void)
{
   const char *paths[] = {
      "/dev/dis", "/dev/ge", "/dev/fb0", "/dev/viddec", "/dev/vidsink",
      "/dev/sndC0i2so", "/dev/sndC0i2si", "/dev/video0",
      "/dev/backlight", "/dev/pwm0", "/dev/pwm1", "/dev/pwm2", "/dev/pwm3",
      "/dev/input/event0", "/dev/input/event1", "/dev/input/event2", "/dev/input/event3",
      "/dev/adc", "/dev/queryadc0",
      "/dev/standby", "/dev/lcddev",
      "/dev/i2c0", "/dev/i2c1", "/dev/i2c2", "/dev/i2c3",
      "/dev/usb0", "/dev/usb1", "/dev/spidev0", "/dev/xpt2046",
      "/dev/efuse", "/dev/persistentmem"
   };

   printf("REPORT device_probe start\n");
   for (unsigned i = 0; i < ARRAY_SIZE(paths); i++) {
      int ret = probe_open_path(paths[i]);
      printf("REPORT dev path=%s result=%s\n", paths[i], ret == 0 ? "open" : "missing");
   }
}

static void report_hardware_leads_probe(void)
{
   static const struct {
      const char *group;
      const char *path;
      const char *note;
   } leads[] = {
      {"mmz", "/dev/mmz", "media-memory allocator lead"},
      {"mmz", "/dev/mmz0", "alternate media-memory node"},
      {"dma", "/dev/dma", "dma copy engine lead"},
      {"dsc", "/dev/dsc", "aes/des/sha engine lead"},
      {"jpeg", "/dev/jpeg", "jpeg encoder lead"},
      {"jpeg", "/dev/jpegenc", "alternate jpeg encoder node"},
      {"video", "/dev/viddec", "hardware decoder"},
      {"video", "/dev/vidsink", "hardware video sink"},
      {"video", "/dev/video0", "v4l/video input lead"},
      {"hdmi", "/dev/hdmi_rx", "hdmi receiver lead"},
      {"usb", "/dev/usb0", "usb host/peripheral lead"},
      {"usb", "/dev/usb1", "second usb controller lead"},
      {"i2c", "/dev/i2c0", "board peripheral bus"},
      {"i2c", "/dev/i2c1", "board peripheral bus"},
      {"i2c", "/dev/i2c2", "board peripheral bus"},
      {"i2c", "/dev/i2c3", "board peripheral bus"},
      {"spi", "/dev/spidev0", "spi peripheral bus"},
      {"uart", "/dev/uart0", "serial lead"},
      {"uart", "/dev/uart1", "serial lead"},
      {"ir", "/dev/input/event0", "ir/input event lead"},
      {"audio", "/dev/sndC0i2so", "pcm output dma path"},
      {"audio", "/dev/sndC0i2si", "pcm input lead"},
      {"storage", "/dev/mmcblk0", "raw sd/mmc block lead"},
      {"storage", "/media/mmcblk0", "mounted sd card"},
   };

   printf("PERFLAB hardware_leads start note=open-only-no-pinmux-changes\n");
   for (unsigned i = 0; i < ARRAY_SIZE(leads); i++) {
      struct stat st;
      int ret = -1;

      if (stat(leads[i].path, &st) == 0 && S_ISDIR(st.st_mode))
         ret = 0;
      else
         ret = probe_open_path(leads[i].path);
      printf("PERFLAB lead group=%s path=%s result=%s note=%s\n",
         leads[i].group, leads[i].path, ret == 0 ? "present" : "missing",
         leads[i].note);
   }
   printf("PERFLAB hardware_leads end\n");
}

static void report_backlight_probe(void)
{
   int fd = open("/dev/backlight", O_RDONLY);
   unsigned char value = 0;
   int ret;

   if (fd < 0) {
      printf("REPORT backlight open=fail\n");
      return;
   }

   ret = read(fd, &value, sizeof(value));
   printf("REPORT backlight read_ret=%d value=%u\n", ret, (unsigned)value);
   close(fd);
}

static void report_adc_probe(void)
{
   for (unsigned i = 0; i < 8; i++) {
      unsigned char raw = 0;
      const char *source = "?";
      int ret = unifrog_battery_read_raw(&raw, &source, i == 0) == 0 ? 1 : -1;
      unsigned battery_mv_est;
      unsigned adc_mv_est;

      battery_mv_est = (unsigned)raw * 20u;
      adc_mv_est = ((unsigned)raw * 2000u) / 255u;
      printf("REPORT adc sample=%u source=%s ret=%d raw=%u adc_mv_est=%u battery_mv_est=%u battery_bars=%u\n",
         i, source, ret, (unsigned)raw, adc_mv_est, battery_mv_est,
         unifrog_battery_bars_for_raw(raw));
      usleep(10000);
   }
}

static void report_i2c_probe(void)
{
   const char *paths[] = {"/dev/i2c0", "/dev/i2c1", "/dev/i2c2", "/dev/i2c3"};

   printf("REPORT i2c_probe note=open_only_scan_disabled\n");
   for (unsigned i = 0; i < ARRAY_SIZE(paths); i++) {
      int fd = open(paths[i], O_RDWR);
      printf("REPORT i2c path=%s result=%s\n", paths[i], fd >= 0 ? "open" : "missing");
      if (fd >= 0)
         close(fd);
   }
}

static void report_input_probe(void)
{
   const char *paths[] = {
      "/dev/input/event0", "/dev/input/event1", "/dev/input/event2", "/dev/input/event3"
   };
   struct pollfd pfds[ARRAY_SIZE(paths)];
   int fds[ARRAY_SIZE(paths)];
   unsigned events[ARRAY_SIZE(paths)];
   uint32_t start;

   unifrog_text_copy(status_line, sizeof(status_line), "PRESS WIRELESS/IR KEYS");
   draw_screen();
   printf("REPORT input_probe start duration_ms=4000 note=press wireless/IR buttons now\n");

   memset(pfds, 0, sizeof(pfds));
   memset(events, 0, sizeof(events));
   for (unsigned i = 0; i < ARRAY_SIZE(paths); i++) {
      fds[i] = open(paths[i], O_RDONLY);

      if (fds[i] < 0) {
         printf("REPORT input path=%s result=missing\n", paths[i]);
         pfds[i].fd = -1;
         continue;
      }

      fcntl(fds[i], F_SETFL, O_NONBLOCK);
      pfds[i].fd = fds[i];
      pfds[i].events = POLLIN | POLLRDNORM;
      printf("REPORT input path=%s result=open\n", paths[i]);
   }

   start = unifrog_perf_count();
   while (unifrog_perf_elapsed(start, unifrog_perf_count()) <= 3240000000u) {
      int pret = poll(pfds, ARRAY_SIZE(pfds), 20);

      if (pret > 0) {
         for (unsigned i = 0; i < ARRAY_SIZE(paths); i++) {
            struct unifrog_input_event ev;
            if (pfds[i].fd < 0 || !(pfds[i].revents & (POLLIN | POLLRDNORM)))
               continue;
            while (read(pfds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
               printf("REPORT input path=%s event=%u type=%u code=%u value=%d\n",
                  paths[i], events[i], (unsigned)ev.type, (unsigned)ev.code, (int)ev.value);
               events[i]++;
               if (events[i] >= 32) {
                  pfds[i].fd = -1;
                  break;
               }
            }
         }
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(paths); i++) {
      printf("REPORT input path=%s events=%u\n", paths[i], events[i]);
      if (fds[i] >= 0)
         close(fds[i]);
   }
}

static void report_rf_register_probe(void)
{
   const uint32_t offsets[] = {
      0x40, 0x44, 0x48, 0x4c, 0x50, 0x54, 0x58, 0x5c, 0x60
   };
   const char *phases[] = {
      "idle_no_buttons",
      "p1_switch_press_buttons",
      "p2_switch_press_buttons"
   };
   uint32_t save_054 = REG32_READ(MSYSIO_BASE + 0x54);
   const uint32_t rf_enable_mask = 0xfeffffffu;

   printf("REPORT rf_reg_probe start note=stock RF_IC table references 0xb8800054 0xb8800050 0xfeffffff\n");
   printf("REPORT rf_reg baseline r50=0x%08lx r54=0x%08lx r58=0x%08lx\n",
      (unsigned long)REG32_READ(MSYSIO_BASE + 0x50),
      (unsigned long)save_054,
      (unsigned long)REG32_READ(MSYSIO_BASE + 0x58));

   for (unsigned enable = 0; enable < 2; enable++) {
      if (enable) {
         uint32_t before = REG32_READ(MSYSIO_BASE + 0x54);
         REG32_WRITE(MSYSIO_BASE + 0x54, before & rf_enable_mask);
         usleep(10000);
         printf("REPORT rf_reg rf_enable_attempt before54=0x%08lx after54=0x%08lx mask=0x%08lx\n",
            (unsigned long)before,
            (unsigned long)REG32_READ(MSYSIO_BASE + 0x54),
            (unsigned long)rf_enable_mask);
      }

      for (unsigned p = 0; p < ARRAY_SIZE(phases); p++) {
         uint32_t first[ARRAY_SIZE(offsets)];
         uint32_t prev[ARRAY_SIZE(offsets)];
         uint32_t changed[ARRAY_SIZE(offsets)];
         unsigned changes[ARRAY_SIZE(offsets)];
         uint32_t start;
         unsigned samples = 0;
         unsigned total_changes = 0;
         char prompt[96];

         snprintf(prompt, sizeof(prompt), "RF %s %s",
            enable ? "EN" : "BASE", phases[p]);
         unifrog_text_copy(status_line, sizeof(status_line), prompt);
         draw_screen();

         for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
            first[i] = REG32_READ(MSYSIO_BASE + offsets[i]);
            prev[i] = first[i];
            changed[i] = 0;
            changes[i] = 0;
         }

         printf("REPORT rf_phase start mode=%s phase=%s duration_ms=2500\n",
            enable ? "enabled" : "baseline", phases[p]);
         start = unifrog_perf_count();
         while (unifrog_perf_elapsed(start, unifrog_perf_count()) <= 2025000000u) {
            for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
               uint32_t value = REG32_READ(MSYSIO_BASE + offsets[i]);
               uint32_t diff = value ^ prev[i];

               if (diff) {
                  changed[i] |= diff;
                  changes[i]++;
                  total_changes++;
                  prev[i] = value;
               }
            }
            samples++;
            usleep(1000);
         }

         printf("REPORT rf_phase summary mode=%s phase=%s samples=%u total_changes=%u\n",
            enable ? "enabled" : "baseline", phases[p], samples, total_changes);
         for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
            if (changed[i] || first[i] != prev[i] ||
                offsets[i] == 0x50 || offsets[i] == 0x54 || offsets[i] == 0x58) {
               printf("REPORT rf_phase reg mode=%s phase=%s off=0x%02x first=0x%08lx final=0x%08lx changed=0x%08lx changes=%u\n",
                  enable ? "enabled" : "baseline", phases[p], (unsigned)offsets[i],
                  (unsigned long)first[i], (unsigned long)prev[i],
                  (unsigned long)changed[i], changes[i]);
            }
         }
      }
   }

   REG32_WRITE(MSYSIO_BASE + 0x54, save_054);
   usleep(10000);
   printf("REPORT rf_reg restored r54=0x%08lx saved=0x%08lx\n",
      (unsigned long)REG32_READ(MSYSIO_BASE + 0x54),
      (unsigned long)save_054);
}

static void report_dis_probe(void)
{
   int fd;
   const enum DIS_TYPE distypes[] = {DIS_TYPE_HD, DIS_TYPE_SD};
   const enum DIS_PIC_LAYER layers[] = {
      DIS_PIC_LAYER_MAIN, DIS_PIC_LAYER_CURRENT, DIS_PIC_LAYER_AUX
   };

   fd = open("/dev/dis", O_RDWR);
   if (fd < 0) {
      printf("REPORT dis open=fail\n");
      return;
   }

   for (unsigned d = 0; d < ARRAY_SIZE(distypes); d++) {
      for (unsigned l = 0; l < ARRAY_SIZE(layers); l++) {
         struct dis_display_info info;

         memset(&info, 0, sizeof(info));
         info.distype = distypes[d];
         info.info.layer = layers[l];
         if (ioctl(fd, DIS_GET_DISPLAY_INFO, (uint32_t)&info) == 0) {
            printf("REPORT dis type=%u layer=%u pic=%ux%u area=%u,%u %ux%u status=%u fmt=%u rotate=%u\n",
               distypes[d], layers[l], info.info.pic_width, info.info.pic_height,
               info.info.pic_dis_area.x, info.info.pic_dis_area.y,
               info.info.pic_dis_area.w, info.info.pic_dis_area.h,
               info.info.status, info.info.sample_format, info.info.rotate_mode);
         } else {
            printf("REPORT dis type=%u layer=%u info=fail\n", distypes[d], layers[l]);
         }
      }
   }

   close(fd);
}

static void report_memory_bench(void)
{
   enum { WORDS = 64 * 1024 };
   uint32_t *buf = NULL;
   volatile uint32_t *cached;
   volatile uint32_t *uncached;
   uint32_t start;
   uint32_t count;
   uint32_t acc = 0;

   buf = malloc(WORDS * sizeof(uint32_t));
   if (!buf) {
      printf("REPORT memory malloc=fail\n");
      return;
   }

   cached = buf;
   uncached = UNIFROG_PERF_UNCACHED_ALIAS(buf);
   start = unifrog_perf_count();
   for (unsigned i = 0; i < WORDS; i++)
      cached[i] = i ^ 0x5a5aa5a5u;
   for (unsigned i = 0; i < WORDS; i++)
      acc ^= cached[i] + (acc << 3);
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("REPORT memory cached words=%u count=%lu count/k=%lu out=0x%08lx\n",
      WORDS, (unsigned long)count, (unsigned long)(count / (WORDS / 1000)),
      (unsigned long)acc);

   acc = 0;
   start = unifrog_perf_count();
   for (unsigned i = 0; i < WORDS / 8; i++)
      uncached[i] = i ^ 0xa55a5aa5u;
   for (unsigned i = 0; i < WORDS / 8; i++)
      acc ^= uncached[i] + (acc << 3);
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("REPORT memory uncached words=%u count=%lu count/k=%lu out=0x%08lx\n",
      WORDS / 8, (unsigned long)count, (unsigned long)(count / ((WORDS / 8) / 1000)),
      (unsigned long)acc);

   start = unifrog_perf_count();
   unifrog_perf_cache_flush(buf, WORDS * sizeof(uint32_t));
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("REPORT cache flush bytes=%u count=%lu\n",
      (unsigned)(WORDS * sizeof(uint32_t)), (unsigned long)count);

   start = unifrog_perf_count();
   unifrog_perf_cache_flush_all();
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("REPORT cache flush_all count=%lu\n", (unsigned long)count);
   free(buf);
}

static void report_copy_experiments(void)
{
   static const size_t sizes[] = {
      1024, 4096, 16 * 1024, 64 * 1024, 160 * 144 * 2,
      256 * 224 * 2, 320 * 240 * 2, 512 * 1024
   };
   uint8_t *base = NULL;
   uint8_t *src;
   uint8_t *dst;
   uint8_t *uncached_dst;
   volatile uint32_t checksum = 0;
   size_t max_size = 512 * 1024;

   base = malloc(max_size * 2 + 128);
   if (!base) {
      printf("PERFEXP copy malloc=fail\n");
      return;
   }

   src = (uint8_t *)(((uintptr_t)base + 31u) & ~31u);
   dst = src + max_size + 64;
   uncached_dst = UNIFROG_PERF_UNCACHED_ALIAS(dst);

   for (size_t i = 0; i < max_size; i++)
      src[i] = (uint8_t)(i * 37u + 11u);

   for (unsigned s = 0; s < ARRAY_SIZE(sizes); s++) {
      size_t bytes = sizes[s];
      unsigned loops = bytes <= 4096 ? 2048 : bytes <= 65536 ? 512 : 96;
      uint32_t start;
      uint32_t count;

      start = unifrog_perf_count();
      for (unsigned i = 0; i < loops; i++) {
         memcpy(dst, src, bytes);
         checksum ^= dst[(i * 97u) % bytes];
      }
      count = unifrog_perf_elapsed(start, unifrog_perf_count());
      printf("PERFEXP copy kind=memcpy_cached bytes=%lu loops=%u count=%lu count/loop=%lu checksum=0x%08lx\n",
         (unsigned long)bytes, loops, (unsigned long)count,
         (unsigned long)(count / loops), (unsigned long)checksum);

      start = unifrog_perf_count();
      for (unsigned i = 0; i < loops; i++) {
         memcpy(dst + 1, src + 1, bytes - 2);
         checksum ^= dst[1 + ((i * 131u) % (bytes - 2))];
      }
      count = unifrog_perf_elapsed(start, unifrog_perf_count());
      printf("PERFEXP copy kind=memcpy_unaligned bytes=%lu loops=%u count=%lu count/loop=%lu checksum=0x%08lx\n",
         (unsigned long)(bytes - 2), loops, (unsigned long)count,
         (unsigned long)(count / loops), (unsigned long)checksum);

      start = unifrog_perf_count();
      for (unsigned i = 0; i < loops; i++) {
         memset(dst, (int)i, bytes);
         checksum ^= dst[(i * 53u) % bytes];
      }
      count = unifrog_perf_elapsed(start, unifrog_perf_count());
      printf("PERFEXP copy kind=memset_cached bytes=%lu loops=%u count=%lu count/loop=%lu checksum=0x%08lx\n",
         (unsigned long)bytes, loops, (unsigned long)count,
         (unsigned long)(count / loops), (unsigned long)checksum);

      if (bytes <= 320 * 240 * 2) {
         start = unifrog_perf_count();
         for (unsigned i = 0; i < loops; i++) {
            memcpy(uncached_dst, src, bytes);
            checksum ^= uncached_dst[(i * 29u) % bytes];
         }
         count = unifrog_perf_elapsed(start, unifrog_perf_count());
         printf("PERFEXP copy kind=memcpy_uncached_dst bytes=%lu loops=%u count=%lu count/loop=%lu checksum=0x%08lx\n",
            (unsigned long)bytes, loops, (unsigned long)count,
            (unsigned long)(count / loops), (unsigned long)checksum);
      }
   }

   free(base);
}

static void report_cache_experiments(void)
{
   static const size_t sizes[] = {
      64, 256, 1024, 4096, 16 * 1024, 64 * 1024, 320 * 240 * 2
   };
   uint8_t *buf = malloc(320 * 240 * 2 + 64);

   if (!buf) {
      printf("PERFEXP cache malloc=fail\n");
      return;
   }

   for (unsigned i = 0; i < ARRAY_SIZE(sizes); i++) {
      unsigned loops = sizes[i] <= 4096 ? 512 : 64;
      uint32_t start = unifrog_perf_count();
      uint32_t count;

      for (unsigned l = 0; l < loops; l++)
         unifrog_perf_cache_flush(buf, sizes[i]);
      count = unifrog_perf_elapsed(start, unifrog_perf_count());
      printf("PERFEXP cache op=flush bytes=%lu loops=%u count=%lu count/loop=%lu\n",
         (unsigned long)sizes[i], loops, (unsigned long)count,
         (unsigned long)(count / loops));
   }

   free(buf);
}

static uint32_t cpu_small_bench(void)
{
   uint32_t start = unifrog_perf_count();
   volatile uint32_t acc = 0x12345678;

   for (unsigned i = 0; i < 300000; i++) {
      acc = (acc * 1664525u) + 1013904223u;
      acc ^= acc >> 13;
      acc += i;
   }
   return unifrog_perf_elapsed(start, unifrog_perf_count()) ^ (acc & 1u);
}

static uint32_t scpu_mhz_to_mctrl2(unsigned mhz)
{
   return ((((mhz * 10u) - 27u) / 27u) | 0x8000u);
}

static unsigned scpu_mctrl2_to_mhz(uint32_t mctrl)
{
   uint32_t n = mctrl & 0xffffu;

   if (n & 0x8000u)
      n &= 0x7fffu;

   return (unsigned)(((n * 27u) + 9u) / 10u);
}

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

static unsigned scpu_current_selector(void)
{
   return (REG32_READ(MSYSIO_BASE + 0x74) >> 8) & 0x7u;
}

static unsigned scpu_pll_enabled(void)
{
   return (REG32_READ(MSYSIO_BASE + 0x7c) >> 7) & 0x1u;
}

static unsigned scpu_current_mhz(void)
{
   unsigned selector = scpu_current_selector();

   if (selector == 7 && scpu_pll_enabled())
      return scpu_mctrl2_to_mhz(REG32_READ(MSYSIO_BASE + 0x380) >> 16);

   return scpu_selector_to_mhz(selector);
}

static void scpu_apply_hc1512(unsigned selector, unsigned pll_mhz)
{
   if (selector == 7) {
      REG32_SET_FIELD2(MSYSIO_BASE + 0x380, 16, 16, scpu_mhz_to_mctrl2(pll_mhz));
      usleep(1000);
      REG32_SET_FIELD2(MSYSIO_BASE + 0x74, 8, 3, 7);
      REG32_SET_FIELD2(MSYSIO_BASE + 0x7c, 7, 1, 1);
   } else {
      REG32_SET_FIELD2(MSYSIO_BASE + 0x7c, 7, 1, 0);
      REG32_SET_FIELD2(MSYSIO_BASE + 0x74, 8, 3, selector);
   }
   REG32_SET_FIELD2(MSYSIO_BASE + 0x74, 22, 1, 1);
   usleep(5000);
}

static void scpu_capture_clock(struct scpu_clock_snapshot *clock)
{
   memset(clock, 0, sizeof(*clock));
   if (REG32_GET_FIELD2(MSYSIO_BASE + 0x0, 16, 16) != HC1512_CHIP_ID)
      return;

   clock->reg074 = REG32_READ(MSYSIO_BASE + 0x74);
   clock->reg07c = REG32_READ(MSYSIO_BASE + 0x7c);
   clock->reg380 = REG32_READ(MSYSIO_BASE + 0x380);
   clock->selector = scpu_current_selector();
   clock->pll_enabled = scpu_pll_enabled();
   clock->mhz = scpu_current_mhz();
   clock->valid = 1;
}

static void report_scpu_info(void)
{
   struct scpu_clock_snapshot clock;
   uint32_t chip = REG32_GET_FIELD2(MSYSIO_BASE + 0x0, 16, 16);

   scpu_capture_clock(&clock);
   printf("PERFLAB scpu_info chip=0x%04lx valid=%d selector=%u pll_enabled=%u mhz_est=%u reg074=0x%08lx reg07c=0x%08lx reg380=0x%08lx\n",
      (unsigned long)chip, clock.valid, clock.selector, clock.pll_enabled,
      clock.mhz, (unsigned long)clock.reg074, (unsigned long)clock.reg07c,
      (unsigned long)clock.reg380);
   printf("PERFLAB scpu_info safe_note=no_clock_change boot_profiles=594/810/918 runtime_probe_is_separate\n");
}

static void scpu_enter_standby_clock(struct scpu_clock_snapshot *clock, const char *reason)
{
   scpu_capture_clock(clock);
   if (!clock->valid) {
      printf("unifrog standby scpu skip unsupported_chip reason=%s\n",
         reason ? reason : "none");
      return;
   }

   printf("unifrog standby scpu slow reason=%s from_selector=%u from_pll=%u from_mhz=%u to_selector=%u to_mhz=%u reg074=0x%08lx reg07c=0x%08lx reg380=0x%08lx\n",
      reason ? reason : "none", clock->selector, clock->pll_enabled,
      clock->mhz, STANDBY_SCPU_SELECTOR, STANDBY_SCPU_MHZ,
      (unsigned long)clock->reg074, (unsigned long)clock->reg07c,
      (unsigned long)clock->reg380);
   scpu_apply_hc1512(STANDBY_SCPU_SELECTOR, STANDBY_SCPU_MHZ);
}

static void scpu_restore_clock(const struct scpu_clock_snapshot *clock, const char *reason)
{
   if (!clock->valid)
      return;

   REG32_WRITE(MSYSIO_BASE + 0x380, clock->reg380);
   REG32_WRITE(MSYSIO_BASE + 0x7c, clock->reg07c);
   REG32_WRITE(MSYSIO_BASE + 0x74, clock->reg074);
   usleep(5000);
   printf("unifrog standby scpu restore reason=%s selector=%u pll=%u mhz=%u reg074=0x%08lx reg07c=0x%08lx reg380=0x%08lx\n",
      reason ? reason : "none", scpu_current_selector(), scpu_pll_enabled(),
      scpu_current_mhz(), (unsigned long)REG32_READ(MSYSIO_BASE + 0x74),
      (unsigned long)REG32_READ(MSYSIO_BASE + 0x7c),
      (unsigned long)REG32_READ(MSYSIO_BASE + 0x380));
}

static void report_scpu_runtime_probe(void)
{
   struct scpu_case {
      const char *name;
      unsigned selector;
      unsigned pll_mhz;
      unsigned restore_after;
   };
   const struct scpu_case cases[] = {
      {"sel3_198", 3, 198, 0},
      {"sel2_297", 2, 297, 0},
      {"sel1_396", 1, 396, 0},
      {"sel0_594", 0, 594, 0},
      {"pll702", 7, 702, 0},
      {"pll756", 7, 756, 0},
      {"pll810", 7, 810, 0},
      {"pll864", 7, 864, 0},
      {"pll918", 7, 918, 0},
      {"under_sel3_198", 3, 198, 1},
   };
   uint32_t chip = REG32_GET_FIELD2(MSYSIO_BASE + 0x0, 16, 16);
   uint32_t save_074 = REG32_READ(MSYSIO_BASE + 0x74);
   uint32_t save_07c = REG32_READ(MSYSIO_BASE + 0x7c);
   uint32_t save_380 = REG32_READ(MSYSIO_BASE + 0x380);
   uint32_t before = cpu_small_bench();

   printf("REPORT scpu_runtime chip=0x%04lx selector=%u pll_enabled=%u mhz_est=%u reg074=0x%08lx reg07c=0x%08lx reg380=0x%08lx before_count=%lu\n",
      (unsigned long)chip, scpu_current_selector(), scpu_pll_enabled(),
      scpu_current_mhz(), (unsigned long)save_074, (unsigned long)save_07c,
      (unsigned long)save_380, (unsigned long)before);

   if (chip != HC1512_CHIP_ID) {
      printf("REPORT scpu_runtime skip unsupported_chip\n");
      return;
   }

   for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
      uint32_t count;

      printf("REPORT scpu_runtime attempt case=%s selector=%u pll=%u\n",
         cases[i].name, cases[i].selector, cases[i].pll_mhz);
      flush_memlog();
      scpu_apply_hc1512(cases[i].selector, cases[i].pll_mhz);
      count = cpu_small_bench();
      printf("REPORT scpu_runtime case=%s selector=%u pll=%u mhz_est=%u count=%lu count_per_mhz=%lu reg074=0x%08lx reg07c=0x%08lx reg380=0x%08lx\n",
         cases[i].name, cases[i].selector, cases[i].pll_mhz,
         scpu_current_mhz(), (unsigned long)count,
         scpu_current_mhz() ? (unsigned long)(count / scpu_current_mhz()) : 0,
         (unsigned long)REG32_READ(MSYSIO_BASE + 0x74),
         (unsigned long)REG32_READ(MSYSIO_BASE + 0x7c),
         (unsigned long)REG32_READ(MSYSIO_BASE + 0x380));
      if (cases[i].restore_after) {
         REG32_WRITE(MSYSIO_BASE + 0x380, save_380);
         REG32_WRITE(MSYSIO_BASE + 0x7c, save_07c);
         REG32_WRITE(MSYSIO_BASE + 0x74, save_074);
         usleep(5000);
         printf("REPORT scpu_runtime post_under_restore case=%s selector=%u pll_enabled=%u mhz_est=%u\n",
            cases[i].name, scpu_current_selector(), scpu_pll_enabled(),
            scpu_current_mhz());
      }
      flush_memlog();
   }

   REG32_WRITE(MSYSIO_BASE + 0x380, save_380);
   REG32_WRITE(MSYSIO_BASE + 0x7c, save_07c);
   REG32_WRITE(MSYSIO_BASE + 0x74, save_074);
   usleep(5000);
   printf("REPORT scpu_runtime restored selector=%u pll_enabled=%u mhz_est=%u reg074=0x%08lx reg07c=0x%08lx reg380=0x%08lx after_count=%lu\n",
      scpu_current_selector(), scpu_pll_enabled(), scpu_current_mhz(),
      (unsigned long)REG32_READ(MSYSIO_BASE + 0x74),
      (unsigned long)REG32_READ(MSYSIO_BASE + 0x7c),
      (unsigned long)REG32_READ(MSYSIO_BASE + 0x380),
      (unsigned long)cpu_small_bench());
}

static void report_fb_bench(void)
{
   uint32_t start;
   uint32_t count;
   size_t words;
   int ret;
   struct fb_var_screeninfo saved = var;
   unsigned screen_bytes;
   unsigned buffers;

   if (!fb)
      return;

   words = visible_fb_bytes() / sizeof(uint16_t);
   start = unifrog_perf_count();
   for (unsigned r = 0; r < 64; r++) {
      uint16_t color = RGB565((r * 17) & 255, (r * 23) & 255, (r * 31) & 255);
      for (size_t i = 0; i < words; i++)
         fb[i] = color;
   }
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("REPORT fb cpu_fill frames=64 pixels=%lu count=%lu count/frame=%lu\n",
      (unsigned long)words, (unsigned long)count, (unsigned long)(count / 64));

   start = unifrog_perf_count();
   unifrog_perf_cache_flush(fb, visible_fb_bytes());
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("REPORT fb visible_flush bytes=%lu count=%lu\n",
      (unsigned long)visible_fb_bytes(), (unsigned long)count);

   start = unifrog_perf_count();
   ret = ioctl(fbdev, FBIOPAN_DISPLAY, &var);
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("REPORT fb pan ret=%d count=%lu\n", ret, (unsigned long)count);

   start = unifrog_perf_count();
   ret = 0;
   ioctl(fbdev, FBIO_WAITFORVSYNC, &ret);
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("REPORT fb vsync ret=%d count=%lu\n", ret, (unsigned long)count);

   screen_bytes = var.xres * var.yres * (var.bits_per_pixel / 8);
   buffers = screen_bytes ? fix.smem_len / screen_bytes : 0;
   if (buffers > 6)
      buffers = 6;
   printf("REPORT fb buffers possible=%u screen_bytes=%u\n", buffers, screen_bytes);
   if (buffers >= 2) {
      unsigned saved_buffers = fb_buffers;

      var.yres_virtual = var.yres * buffers;
      ret = ioctl(fbdev, FBIOPUT_VSCREENINFO, &var);
      printf("REPORT fb multibuffer setup buffers=%u ret=%d yvirt=%u\n",
         buffers, ret, (unsigned)var.yres_virtual);
      if (ret == 0) {
         start = unifrog_perf_count();
         for (unsigned i = 0; i < buffers; i++) {
            var.yoffset = i * var.yres;
            ioctl(fbdev, FBIOPAN_DISPLAY, &var);
         }
         count = unifrog_perf_elapsed(start, unifrog_perf_count());
         printf("REPORT fb multibuffer pan buffers=%u count=%lu count/pan=%lu\n",
            buffers, (unsigned long)count, (unsigned long)(count / buffers));
      }
      var = saved;
      fb_buffers = saved_buffers;
      ioctl(fbdev, FBIOPUT_VSCREENINFO, &var);
      ioctl(fbdev, FBIOPAN_DISPLAY, &var);
      restore_frontend_fb_mode("fb_bench");
   }
}

static void report_fb_present_experiments(void)
{
   uint16_t *src;
   uint16_t *uncached_fb;
   size_t pixels;
   uint32_t start;
   uint32_t count;
   volatile uint32_t checksum = 0;
   unsigned frames = 96;

   if (!fb)
      return;

   pixels = visible_fb_bytes() / sizeof(uint16_t);
   src = malloc(visible_fb_bytes());
   if (!src) {
      printf("PERFEXP fb_present malloc=fail\n");
      return;
   }
   uncached_fb = UNIFROG_PERF_UNCACHED_ALIAS(fb);

   for (size_t i = 0; i < pixels; i++)
      src[i] = RGB565(i & 255u, (i >> 4) & 255u, (i >> 8) & 255u);

   start = unifrog_perf_count();
   for (unsigned f = 0; f < frames; f++) {
      memcpy(fb, src, visible_fb_bytes());
      unifrog_perf_cache_flush(fb, visible_fb_bytes());
      checksum ^= fb[(f * 251u) % pixels];
   }
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("PERFEXP fb_present kind=memcpy_cached_flush bytes=%lu frames=%u count=%lu count/frame=%lu checksum=0x%08lx\n",
      (unsigned long)visible_fb_bytes(), frames, (unsigned long)count,
      (unsigned long)(count / frames), (unsigned long)checksum);

   start = unifrog_perf_count();
   for (unsigned f = 0; f < frames; f++) {
      memcpy(uncached_fb, src, visible_fb_bytes());
      checksum ^= uncached_fb[(f * 197u) % pixels];
   }
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("PERFEXP fb_present kind=memcpy_uncached_no_flush bytes=%lu frames=%u count=%lu count/frame=%lu checksum=0x%08lx\n",
      (unsigned long)visible_fb_bytes(), frames, (unsigned long)count,
      (unsigned long)(count / frames), (unsigned long)checksum);

   start = unifrog_perf_count();
   for (unsigned f = 0; f < frames; f++) {
      uint16_t color = RGB565(f * 3u, f * 5u, f * 7u);
      for (size_t i = 0; i < pixels; i++)
         fb[i] = color;
      unifrog_perf_cache_flush(fb, visible_fb_bytes());
      checksum ^= fb[(f * 149u) % pixels];
   }
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("PERFEXP fb_present kind=cpu_fill_cached_flush bytes=%lu frames=%u count=%lu count/frame=%lu checksum=0x%08lx\n",
      (unsigned long)visible_fb_bytes(), frames, (unsigned long)count,
      (unsigned long)(count / frames), (unsigned long)checksum);

   start = unifrog_perf_count();
   for (unsigned f = 0; f < 32; f++) {
      int ret = 0;
      ioctl(fbdev, FBIO_WAITFORVSYNC, &ret);
   }
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("PERFEXP fb_present kind=vsync_wait frames=32 count=%lu count/frame=%lu\n",
      (unsigned long)count, (unsigned long)(count / 32));

   free(src);
}

struct ge_size_case {
   unsigned w;
   unsigned h;
};

static uint32_t report_ge_fill_once(struct unifrog_ge *ge)
{
   struct unifrog_ge_surface dst = framebuffer_ge_surface();
   struct unifrog_ge_rect rect;
   uint32_t start;

   rect.x = 0;
   rect.y = 0;
   rect.w = var.xres;
   rect.h = var.yres;

   start = unifrog_perf_count();
   for (unsigned i = 0; i < 64; i++) {
      uint32_t color = 0xff000000u | (((i * 31) & 255) << 16) |
         (((i * 17) & 255) << 8) | ((i * 11) & 255);
      unifrog_ge_fill(ge, &dst, &rect, color);
   }
   unifrog_ge_sync(ge);
   return unifrog_perf_elapsed(start, unifrog_perf_count());
}

static uint32_t report_ge_stretch_once(struct unifrog_ge *ge, unsigned src_w, unsigned src_h,
   int flush_source)
{
   struct unifrog_ge_surface dst = framebuffer_ge_surface();
   struct unifrog_ge_surface src_surface;
   struct unifrog_ge_rect srect;
   struct unifrog_ge_rect drect;
   uint16_t *src = NULL;
   uint32_t start;
   uint32_t count;

   src = malloc(src_w * src_h * sizeof(uint16_t));
   if (!src)
      return 0;

   for (unsigned y = 0; y < src_h; y++) {
      for (unsigned x = 0; x < src_w; x++)
         src[y * src_w + x] = RGB565(x * 255 / src_w, y * 255 / src_h, (x + y) & 255);
   }
   if (flush_source)
      unifrog_perf_cache_flush(src, src_w * src_h * sizeof(uint16_t));

   src_surface.pixels = src;
   src_surface.width = src_w;
   src_surface.height = src_h;
   src_surface.pitch_bytes = src_w * sizeof(uint16_t);
   src_surface.format = UNIFROG_GE_FORMAT_RGB565;
   srect.x = 0;
   srect.y = 0;
   srect.w = src_w;
   srect.h = src_h;
   drect.x = 0;
   drect.y = 0;
   drect.w = var.xres;
   drect.h = var.yres;

   start = unifrog_perf_count();
   for (unsigned i = 0; i < 64; i++)
      unifrog_ge_stretch(ge, &dst, &drect, &src_surface, &srect, 0);
   unifrog_ge_sync(ge);
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   free(src);
   return count;
}

static void report_ge_blit_experiments(struct unifrog_ge *ge)
{
   struct unifrog_ge_surface dst = framebuffer_ge_surface();
   struct unifrog_ge_surface src_surface;
   struct unifrog_ge_rect rect;
   uint16_t *src;
   unsigned frames = 128;

   src = malloc(visible_fb_bytes());
   if (!src) {
      printf("PERFEXP ge_blit malloc=fail\n");
      return;
   }

   for (unsigned y = 0; y < var.yres; y++) {
      for (unsigned x = 0; x < var.xres; x++)
         src[y * var.xres + x] = RGB565(x * 255 / var.xres,
            y * 255 / var.yres, (x ^ y) & 255);
   }

   src_surface.pixels = src;
   src_surface.width = var.xres;
   src_surface.height = var.yres;
   src_surface.pitch_bytes = var.xres * sizeof(uint16_t);
   src_surface.format = UNIFROG_GE_FORMAT_RGB565;
   rect.x = 0;
   rect.y = 0;
   rect.w = var.xres;
   rect.h = var.yres;

   for (unsigned mode = 0; mode < 4; mode++) {
      const char *name = "normal";
      unsigned flags = 0;
      uint32_t start;
      uint32_t count;

      if (mode == 1) {
         name = "flush_source";
         flags = UNIFROG_GE_FLUSH_SOURCE;
      } else if (mode == 2) {
         name = "rot180";
         flags = UNIFROG_GE_ROTATE_180 | UNIFROG_GE_FLUSH_SOURCE;
      } else if (mode == 3) {
         name = "rot270";
         flags = UNIFROG_GE_ROTATE_270 | UNIFROG_GE_FLUSH_SOURCE;
      }

      start = unifrog_perf_count();
      for (unsigned i = 0; i < frames; i++)
         unifrog_ge_blit(ge, &dst, 0, 0, &src_surface, &rect, flags);
      unifrog_ge_sync(ge);
      count = unifrog_perf_elapsed(start, unifrog_perf_count());
      printf("PERFEXP ge_blit mode=%s src=%ux%u frames=%u count=%lu count/frame=%lu flags=0x%x\n",
         name, (unsigned)var.xres, (unsigned)var.yres, frames,
         (unsigned long)count, (unsigned long)(count / frames), flags);
   }

   free(src);
}

static void report_ge_bench(void)
{
   const struct ge_size_case sizes[] = {
      {160, 144}, {240, 160}, {256, 224}, {320, 240}, {160, 120}
   };
   const struct {
      const char *name;
      enum unifrog_ge_clock clock;
   } clocks[] = {
      {"198", UNIFROG_GE_CLOCK_198MHZ},
      {"148", UNIFROG_GE_CLOCK_148MHZ},
      {"225", UNIFROG_GE_CLOCK_225MHZ},
      {"238", UNIFROG_GE_CLOCK_238MHZ},
   };
   struct unifrog_ge ge;

   if (unifrog_ge_open(&ge) != 0) {
      printf("REPORT ge open=fail\n");
      return;
   }

   printf("REPORT ge open=ok fd=%d note=HCGE_SET_CLOCK controls GE accelerator clock, not SCPU\n",
      ge.fd);
   for (unsigned c = 0; c < ARRAY_SIZE(clocks); c++) {
      int ret = unifrog_ge_set_clock(&ge, clocks[c].clock);
      uint32_t fill = report_ge_fill_once(&ge);
      printf("REPORT ge clock=%s set_ret=%d fill frames=64 count=%lu count/frame=%lu\n",
         clocks[c].name, ret, (unsigned long)fill, (unsigned long)(fill / 64));
      if (c == 0)
         report_ge_blit_experiments(&ge);
      for (unsigned i = 0; i < ARRAY_SIZE(sizes); i++) {
         uint32_t stretch = report_ge_stretch_once(&ge, sizes[i].w, sizes[i].h, 1);
         uint32_t noflush = report_ge_stretch_once(&ge, sizes[i].w, sizes[i].h, 0);
         printf("REPORT ge clock=%s stretch src=%ux%u dst=%ux%u flush_count=%lu noflush_count=%lu flush/frame=%lu noflush/frame=%lu\n",
            clocks[c].name, sizes[i].w, sizes[i].h, (unsigned)var.xres, (unsigned)var.yres,
            (unsigned long)stretch, (unsigned long)noflush,
            (unsigned long)(stretch / 64), (unsigned long)(noflush / 64));
      }
   }

   unifrog_ge_close(&ge);
}

static void fill_presenter_source(uint16_t *pixels, unsigned pitch_pixels,
   unsigned width, unsigned height, unsigned frame)
{
   for (unsigned y = 0; y < height; y++) {
      for (unsigned x = 0; x < width; x++) {
         unsigned r = (x * 255 / width) ^ (frame * 5);
         unsigned g = (y * 255 / height) ^ (frame * 3);
         unsigned b = ((x + y + frame * 7) & 255);
         pixels[y * pitch_pixels + x] = RGB565(r, g, b);
      }
   }
}

static uint32_t report_presenter_size_once(struct unifrog_presenter *presenter,
   uint16_t *src, unsigned pitch_pixels, unsigned width, unsigned height,
   unsigned frames, unsigned *failures)
{
   uint32_t start;

   if (failures)
      *failures = 0;
   fill_presenter_source(src, pitch_pixels, width, height, 0);

   start = unifrog_perf_count();
   for (unsigned i = 0; i < frames; i++) {
      src[(i % height) * pitch_pixels + (i % width)] ^= 0xffffu;
      if (unifrog_presenter_present_rgb565(presenter, src, width, height,
          pitch_pixels * sizeof(uint16_t)) != 0 && failures)
         (*failures)++;
   }

   return unifrog_perf_elapsed(start, unifrog_perf_count());
}

static void report_presenter_bench(void)
{
   const struct ge_size_case sizes[] = {
      {160, 144}, {240, 160}, {256, 224}, {320, 240}
   };
   struct fb_var_screeninfo saved_var;
   struct unifrog_presenter presenter;
   uint16_t *src;
   unsigned pitch_pixels = 320;
   unsigned frames = 32;
   int have_saved_var = 0;

   src = malloc(320 * 240 * sizeof(uint16_t));
   if (!src) {
      printf("REPORT presenter malloc=fail\n");
      return;
   }

   if (fbdev >= 0 && ioctl(fbdev, FBIOGET_VSCREENINFO, &saved_var) == 0)
      have_saved_var = 1;

   if (unifrog_presenter_open(&presenter, 2, UNIFROG_PRESENT_KEEP_ASPECT) != 0) {
      printf("REPORT presenter open=fail\n");
      free(src);
      return;
   }

   printf("REPORT presenter open=ok buffers=%u max=%u flags=keep_aspect ge_fd=%d\n",
      presenter.buffer_count, presenter.fb.max_buffers, presenter.ge.fd);
   unifrog_presenter_clear(&presenter, 0xff000000u);

   for (unsigned i = 0; i < ARRAY_SIZE(sizes); i++) {
      unsigned failures;
      uint32_t count = report_presenter_size_once(&presenter, src, pitch_pixels,
         sizes[i].w, sizes[i].h, frames, &failures);
      printf("REPORT presenter src=%ux%u dst=%ux%u frames=%u failures=%u count=%lu count/frame=%lu active_buffer=%u\n",
         sizes[i].w, sizes[i].h, presenter.fb.width, presenter.fb.height,
         frames, failures, (unsigned long)count,
         (unsigned long)(count / frames), presenter.active_buffer);
   }

   unifrog_presenter_close(&presenter);

   if (have_saved_var && fbdev >= 0) {
      var = saved_var;
      ioctl(fbdev, FBIOPUT_VSCREENINFO, &var);
      ioctl(fbdev, FBIOPAN_DISPLAY, &var);
      ioctl(fbdev, FBIOGET_VSCREENINFO, &var);
      ioctl(fbdev, FBIOGET_FSCREENINFO, &fix);
      restore_frontend_fb_mode("presenter_bench");
   }
   free(src);
}

static void report_storage_bench(void)
{
   const char *path = "/media/mmcblk0/unifrog-report-io.bin";
   enum { BYTES = 128 * 1024 };
   uint8_t *buf = malloc(BYTES);
   uint32_t start;
   uint32_t count;
   uint32_t sum = 0;
   int fd;
   ssize_t got;

   if (!buf) {
      printf("REPORT storage malloc=fail\n");
      return;
   }

   for (unsigned i = 0; i < BYTES; i++)
      buf[i] = (uint8_t)(i * 13 + 7);

   fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0666);
   if (fd < 0) {
      printf("REPORT storage open=fail path=%s\n", path);
      free(buf);
      return;
   }

   start = unifrog_perf_count();
   got = write(fd, buf, BYTES);
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("REPORT storage write bytes=%d got=%ld count=%lu\n",
      BYTES, (long)got, (unsigned long)count);

   lseek(fd, 0, SEEK_SET);
   memset(buf, 0, BYTES);
   start = unifrog_perf_count();
   got = read(fd, buf, BYTES);
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   for (unsigned i = 0; i < BYTES; i += 257)
      sum = (sum << 5) ^ (sum >> 2) ^ buf[i];
   printf("REPORT storage read bytes=%d got=%ld count=%lu sum=0x%08lx\n",
      BYTES, (long)got, (unsigned long)count, (unsigned long)sum);

   close(fd);
   free(buf);
}

static void report_storage_read_candidate(const char *path)
{
   enum { CHUNK = 32 * 1024 };
   uint8_t *buf;
   uint32_t start;
   uint32_t count;
   uint32_t sum = 0;
   unsigned total = 0;
   unsigned reads = 0;
   int fd;

   fd = open(path, O_RDONLY);
   if (fd < 0) {
      printf("PERFEXP storage_read path=%s open=fail\n", path);
      return;
   }

   buf = malloc(CHUNK);
   if (!buf) {
      printf("PERFEXP storage_read path=%s malloc=fail\n", path);
      close(fd);
      return;
   }

   start = unifrog_perf_count();
   for (;;) {
      ssize_t got = read(fd, buf, CHUNK);

      if (got <= 0)
         break;
      for (ssize_t i = 0; i < got; i += 521)
         sum = (sum << 5) ^ (sum >> 2) ^ buf[i];
      total += (unsigned)got;
      reads++;
      if (total >= 2 * 1024 * 1024)
         break;
   }
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("PERFEXP storage_read path=%s bytes=%u reads=%u count=%lu count/kib=%lu sum=0x%08lx\n",
      path, total, reads, (unsigned long)count,
      total ? (unsigned long)(count / (total / 1024 ? total / 1024 : 1)) : 0,
      (unsigned long)sum);

   close(fd);
   free(buf);
}

static void report_storage_read_experiments(void)
{
   static const char *const paths[] = {
      "/media/mmcblk0/firmware/unifrog.bin",
      "/media/mmcblk0/FIRMWARE/unifrog.bin",
      "/media/mmcblk0/bisrv.asd",
      "/media/mmcblk0/firmware/GB300.ASD",
      "/media/mmcblk0/firmware/SF2000.ASD",
      "/media/mmcblk0/unifrog-report-io.bin",
   };

   for (unsigned i = 0; i < ARRAY_SIZE(paths); i++)
      report_storage_read_candidate(paths[i]);
}

static void report_input_poll_experiments(void)
{
   uint32_t start;
   uint32_t count;
   uint32_t raw = 0;
   uint32_t buttons_sum = 0;
   unsigned loops = 600;

   start = unifrog_perf_count();
   for (unsigned i = 0; i < loops; i++)
      raw ^= unifrog_input_poll_local_raw();
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("PERFEXP input kind=local_poll loops=%u count=%lu count/poll=%lu raw_xor=0x%08lx\n",
      loops, (unsigned long)count, (unsigned long)(count / loops),
      (unsigned long)raw);

   loops = 240;
   start = unifrog_perf_count();
   for (unsigned i = 0; i < loops; i++) {
      unifrog_input_save_previous();
      unifrog_input_poll();
      buttons_sum ^= unifrog_input_buttons();
   }
   count = unifrog_perf_elapsed(start, unifrog_perf_count());
   printf("PERFEXP input kind=full_poll loops=%u wireless=%d count=%lu count/poll=%lu buttons_xor=0x%08lx\n",
      loops, unifrog_input_wireless_available(), (unsigned long)count,
      (unsigned long)(count / loops), (unsigned long)buttons_sum);
}

static void report_audio_bench(void)
{
   const unsigned rates[] = {22050, 44100, 48000};
   const unsigned period_sizes[] = {512, 1024, 1536, 4096};
   int16_t *buf = malloc(4096 * 2 * sizeof(int16_t));

   if (!buf) {
      printf("REPORT audio malloc=fail\n");
      return;
   }

   for (unsigned i = 0; i < 4096 * 2; i++)
      buf[i] = (int16_t)((i * 97) & 0x7fff);

   for (unsigned r = 0; r < ARRAY_SIZE(rates); r++) {
      for (unsigned p = 0; p < ARRAY_SIZE(period_sizes); p++) {
         struct unifrog_audio audio;
         struct pollfd pollfd;
         uint32_t start;
         uint32_t count;
         int ret;
         unsigned writes = 16;
         unsigned failures = 0;
         unsigned frames = period_sizes[p] / (2 * sizeof(int16_t));

         if (unifrog_audio_open(&audio, rates[r], 2, period_sizes[p], 4) != 0) {
            printf("REPORT audio open=fail\n");
            free(buf);
            return;
         }

         ret = unifrog_audio_start(&audio);
         if (ret < 0) {
            printf("REPORT audio rate=%u period=%u start=fail ret=%d\n",
               rates[r], period_sizes[p], ret);
            unifrog_audio_close(&audio);
            continue;
         }

         pollfd.fd = audio.fd;
         pollfd.events = POLLOUT | POLLWRNORM;
         start = unifrog_perf_count();
         for (unsigned i = 0; i < writes; i++) {
            ret = unifrog_audio_write(&audio, buf, frames);
            if (ret < 0) {
               failures++;
               poll(&pollfd, 1, 20);
            }
         }
         count = unifrog_perf_elapsed(start, unifrog_perf_count());
         printf("REPORT audio rate=%u period=%u writes=%u failures=%u count=%lu count/write=%lu\n",
            rates[r], period_sizes[p], writes, failures,
            (unsigned long)count, (unsigned long)(count / writes));
         unifrog_audio_close(&audio);
      }
   }
   free(buf);
}

static void report_firmware_notes(void)
{
   printf("REPORT clocks dts_scpu clock=%u scpu-dig-pll-clk=%uMHz video-engine clock=4 mmc clock=198MHz bus-width=1\n",
      BUILD_SCPU_CLOCK, BUILD_SCPU_DIG_PLL_MHZ);
   printf("REPORT feature_notes ge=fill/blit/stretchblit dis=layers/zoom vidsink=video-plane snd=i2so-dma fb=pan/vsync cache=manual_flush\n");
}

static void report_perf_caps(void)
{
   struct unifrog_perf_caps caps;

   if (unifrog_perf_query_caps(&caps) != 0) {
      printf("REPORT perf_caps query=fail\n");
      return;
   }

   printf("REPORT perf_caps mask=0x%08lx scpu_selector=%u scpu_mhz_est=%u fb=%ux%u bpp=%u stride=%u bytes=%u buffers=%u ge_cmdq=%u\n",
      (unsigned long)caps.caps, caps.scpu_selector, caps.scpu_mhz_est,
      caps.framebuffer_width, caps.framebuffer_height, caps.framebuffer_bpp,
      caps.framebuffer_stride_bytes, caps.framebuffer_bytes,
      caps.framebuffer_buffers, caps.ge_cmdq_bytes);
}

static void flush_report_stage(const char *tag, const char *stage)
{
   int ret = flush_memlog();

   printf("%s flush stage=%s ret=%d\n", tag, stage, ret);
   (void)flush_memlog();
}

static void report_progress_screen(const char *stage)
{
   snprintf(status_line, sizeof(status_line), "REPORT %s", stage);
   draw_screen();
}

static void recover_frontend_after_report(const char *tag)
{
   const char *label = tag ? tag : "report";

   if (fb_reopen_frontend(label) == 0 && fb_base) {
      memset(fb_base, 0, fb_mapped_len);
      unifrog_perf_cache_flush(fb_base, fb_mapped_len);
   } else {
      restore_frontend_fb_mode(label);
   }
}

static void run_performance_report(int allow_risky)
{
   printf("\nREPORT start commit=%s dirty=%d cp0_count fb=%ux%u line=%u bytes=%u memlog=%u\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY,
      (unsigned)var.xres, (unsigned)var.yres, (unsigned)line_pixels,
      (unsigned)fix.smem_len, (unsigned)unifrog_log_capacity());
   report_progress_screen("CAPS");
   report_firmware_notes();
   report_perf_caps();
   report_device_probe();
   report_backlight_probe();
   report_adc_probe();
   flush_report_stage("REPORT", "device_analog");
   report_dis_probe();
   report_progress_screen("MEMORY");
   report_memory_bench();
   report_copy_experiments();
   report_cache_experiments();
   report_input_poll_experiments();
   if (allow_risky)
      report_scpu_runtime_probe();
   else
      report_scpu_info();
   report_progress_screen("FRAMEBUFFER");
   report_fb_bench();
   report_fb_present_experiments();
   report_progress_screen("GE");
   report_ge_bench();
   report_progress_screen("PRESENTER");
   report_presenter_bench();
   report_progress_screen("AUDIO");
   report_audio_bench();
   report_progress_screen("STORAGE");
   report_storage_bench();
   report_storage_read_experiments();
   recover_frontend_after_report("report_end");
   printf("REPORT end\n");
   flush_report_stage("REPORT", "end");

   report_done = 1;
   unifrog_text_copy(status_line, sizeof(status_line), "REPORT DONE - Y LOG");
}

static void run_perf_lab_selected(int allow_risky)
{
   const char *name = perf_lab_items[perf_selected];

   if (perf_selected == PERF_LAB_SCPU_RUNTIME && !allow_risky) {
      unifrog_text_copy(status_line, sizeof(status_line), "PRESS X FOR RISKY CLOCK");
      printf("PERFLAB skip item=scpu_runtime reason=requires_x_confirm\n");
      flush_memlog();
      dirty = 1;
      return;
   }

   snprintf(status_line, sizeof(status_line), "RUN %s", name);
   draw_screen();
   printf("\nPERFLAB start item=%u name=\"%s\" allow_risky=%d\n",
      (unsigned)perf_selected, name, allow_risky);

   switch (perf_selected) {
   case PERF_LAB_FULL_SWEEP:
      run_performance_report(allow_risky);
      break;
   case PERF_LAB_CAPS:
      report_firmware_notes();
      report_perf_caps();
      break;
   case PERF_LAB_MEMORY:
      report_memory_bench();
      report_copy_experiments();
      report_cache_experiments();
      report_input_poll_experiments();
      break;
   case PERF_LAB_FRAMEBUFFER:
      report_fb_bench();
      report_fb_present_experiments();
      break;
   case PERF_LAB_GE:
      report_ge_bench();
      break;
   case PERF_LAB_PRESENTER:
      report_presenter_bench();
      break;
   case PERF_LAB_DISPLAY_VIDEO:
      report_device_probe();
      report_dis_probe();
      video_probe();
      break;
   case PERF_LAB_AUDIO:
      report_audio_bench();
      break;
   case PERF_LAB_STORAGE:
      write_storage_probe();
      report_storage_bench();
      report_storage_read_experiments();
      break;
   case PERF_LAB_HARDWARE_LEADS:
      report_hardware_leads_probe();
      break;
   case PERF_LAB_SCPU_INFO:
      report_scpu_info();
      break;
   case PERF_LAB_SCPU_RUNTIME:
      report_scpu_runtime_probe();
      break;
   default:
      printf("PERFLAB unknown item=%u\n", (unsigned)perf_selected);
      break;
   }

   printf("PERFLAB end item=%u name=\"%s\"\n",
      (unsigned)perf_selected, name);
   flush_report_stage("PERFLAB", name);
   snprintf(status_line, sizeof(status_line), "DONE %s", name);
   report_done = 1;
   dirty = 1;
   draw_screen();
   dirty = 0;
}

static void run_discovery_report(void)
{
   printf("\nDISCOVERY start fb=%ux%u line=%u bytes=%u memlog=%u\n",
      (unsigned)var.xres, (unsigned)var.yres, (unsigned)line_pixels,
      (unsigned)fix.smem_len, (unsigned)unifrog_log_capacity());
   report_firmware_notes();
   report_device_probe();
   flush_report_stage("DISCOVERY", "device");
   report_input_probe();
   flush_report_stage("DISCOVERY", "input");
   report_rf_register_probe();
   flush_report_stage("DISCOVERY", "rf_reg");
   report_backlight_probe();
   report_adc_probe();
   flush_report_stage("DISCOVERY", "analog");
   report_i2c_probe();
   flush_report_stage("DISCOVERY", "i2c");
   printf("DISCOVERY end\n");

   report_done = 1;
   unifrog_text_copy(status_line, sizeof(status_line), "DISCOVERY DONE - Y LOG");
}

static void stop_native_video(void)
{
   unifrog_audio_debug_dump(NULL, "player_before_stop");
   unifrog_audio_set_system_output_enabled(0);

   if (!player_handle)
      goto out;

   printf("unifrog player stop path=%s\n", player_path);
   hcplayer_stop2(player_handle, true, false);
   player_handle = NULL;
   player_started = 0;
   player_watch_frame = 0;
   player_last_pos = -1;
   player_stall_count = 0;
   player_video_w = 0;
   player_video_h = 0;
   player_display_mode = 0;
   player_display_mode_frame = 0;
   ioctl(fbdev, FBIOBLANK, FB_BLANK_UNBLANK);
   set_video_layer_visible(0, 0, 0, 0, 0);
out:
   close_stream_video();
   unifrog_text_copy(status_line, sizeof(status_line), "PLAYER STOPPED");
}

static void recover_from_video(const char *reason)
{
   printf("unifrog player recover reason=%s path=%s\n", reason, player_path);
   stop_native_video();
   if (js_native_action_active) {
      js_relaunch_requested = 1;
      view = VIEW_MENU;
   } else {
      view = VIEW_BROWSER;
   }
   dirty = 1;
   unifrog_text_copy(status_line, sizeof(status_line), reason);
}

static void monitor_native_video(void)
{
   int64_t pos;
   int64_t dur;

   if (!player_handle)
      return;

   if ((frames - player_watch_frame) < 30)
      return;
   player_watch_frame = frames;

   pos = hcplayer_get_position(player_handle);
   dur = hcplayer_get_duration(player_handle);
   printf("unifrog player monitor pos=%lld dur=%lld stall=%d\n",
      pos, dur, player_stall_count);

   if (pos == player_last_pos)
      player_stall_count++;
   else
      player_stall_count = 0;

   player_last_pos = pos;
   if (player_stall_count >= 5)
      recover_from_video("VIDEO STALLED");

}

static void start_cached_video(void)
{
   HCPlayerInitArgs init_args;
   HCPlayerVideoInfo video_info;
   const struct playback_preset *preset;
   int video_w = 0;
   int video_h = 0;

   memset(&init_args, 0, sizeof(init_args));
   memset(&video_info, 0, sizeof(video_info));

   printf("unifrog player init path=%s\n", player_path);
   printf("unifrog player step hcplayer_init\n");
   hcplayer_init(LOG_INFO);
   preset = &playback_presets[play_preset];

   if (!player_stream.active && open_stream_video(player_path) != 0) {
      unifrog_text_copy(status_line, sizeof(status_line), "STREAM OPEN FAILED");
      return;
   }

   init_args.readdata_callback = stream_video_read;
   init_args.readdata_opaque = &player_stream;
   init_args.seekdata_callback = stream_video_seek;
   printf("unifrog player source=stream size=%lld cache=%u\n",
      (long long)player_stream.size, (unsigned)VIDEO_STREAM_CACHE_BYTES);
   init_args.sync_type = preset->sync_type;
   init_args.quick_mode = play_quick_mode != 0;
   init_args.qm_drop_thresh = preset->qm_drop_thresh;
   init_args.audio_flush_thres = preset->audio_flush_thres;
   init_args.buffering_enable = play_buffering_enabled != 0;
   init_args.buffering_start = 200;
   init_args.buffering_end = 1000;
   init_args.disable_audio = play_audio_enabled ? false : true;
   init_args.disable_video = false;
   init_args.msg_id = 0;
   init_args.preview_enable = true;
   init_args.src_area.x = 0;
   init_args.src_area.y = 0;
   init_args.src_area.w = 1920;
   init_args.src_area.h = 1080;
   init_args.dst_area.x = 0;
   init_args.dst_area.y = 0;
   init_args.dst_area.w = 1920;
   init_args.dst_area.h = 1080;

   printf("unifrog player opts preset=%d/%u name=%s sync=%d quick=%d drop=%d audio_flush=%d buffering=%d audio=%d\n",
      play_preset + 1, (unsigned)ARRAY_SIZE(playback_presets), preset->name,
      init_args.sync_type, init_args.quick_mode, init_args.qm_drop_thresh,
      init_args.audio_flush_thres, init_args.buffering_enable,
      !init_args.disable_audio);
   printf("unifrog player step create\n");
   unifrog_audio_debug_dump(NULL, "player_before_create");
   player_handle = hcplayer_create(&init_args);
   if (!player_handle) {
      ioctl(fbdev, FBIOBLANK, FB_BLANK_UNBLANK);
      set_video_layer_visible(0, 0, 0, 0, 0);
      unifrog_text_copy(status_line, sizeof(status_line), "PLAYER CREATE FAILED");
      printf("unifrog player create failed path=%s\n", player_path);
      return;
   }
   printf("unifrog player step created handle=0x%lx\n",
      (unsigned long)player_handle);

   printf("unifrog player step stream info\n");
   if (hcplayer_get_nth_video_stream_info(player_handle, 0, &video_info) == 0) {
      video_w = video_info.width;
      video_h = video_info.height;
      player_video_w = video_w;
      player_video_h = video_h;
      printf("unifrog player stream video codec=0x%x %dx%d fps=%d\n",
         video_info.codec_id, video_w, video_h, (int)video_info.frame_rate);
   } else {
      printf("unifrog player stream info unavailable\n");
   }

   printf("unifrog player step video layer\n");
   apply_video_display_mode(WORKING_VIDEO_MODE);

   printf("unifrog player step blank fb\n");
   ioctl(fbdev, FBIOBLANK, FB_BLANK_NORMAL);
   printf("unifrog player step play\n");
   if (play_audio_enabled) {
      unifrog_audio_set_system_output_enabled(1);
      printf("unifrog player audio gate enabled path=%s\n", player_path);
   }
   unifrog_audio_debug_dump(NULL, "player_before_play");
   hcplayer_play(player_handle);
   unifrog_audio_debug_dump(NULL, "player_after_play");
   player_started = 1;
   player_watch_frame = frames;
   player_last_pos = -1;
   player_stall_count = 0;
   view = VIEW_PLAYER;
   unifrog_text_copy(status_line, sizeof(status_line), "PLAYING VIDEO");
   printf("unifrog player started path=%s\n", player_path);
   dirty = 0;
}

static void start_native_video(const char *path)
{
   printf("unifrog player selected path=%s\n", path);
   stop_native_video();
   unifrog_text_copy(player_path, sizeof(player_path), path);
   if (open_stream_video(path) != 0) {
      unifrog_text_copy(status_line, sizeof(status_line), "STREAM OPEN FAILED");
      dirty = 1;
      return;
   }
   view = VIEW_PLAYOPTS;
   snprintf(status_line, sizeof(status_line), "CHOOSE PLAYBACK");
   dirty = 1;
}

static void reboot_device(void)
{
   unifrog_text_copy(status_line, sizeof(status_line), "REBOOTING");
   draw_screen();
   printf("unifrog power action=reboot\n");
   flush_memlog();
   reset();
   while (1)
      usleep(1000000);
}

static void write_backlight_for_standby(int value, const char *reason)
{
   int fd;
   int ret = -1;

   fd = open("/dev/backlight", O_RDWR);
   if (fd >= 0) {
      ret = write(fd, &value, sizeof(value)) == (ssize_t)sizeof(value) ? 0 : -1;
      close(fd);
   }

   printf("unifrog standby backlight value=%d ret=%d reason=%s\n",
      value, ret, reason ? reason : "none");
}

static void stop_backlight_pwm_for_standby(const char *reason)
{
   int fd;
   int ret = -1;

   fd = open("/dev/pwm2", O_RDWR);
   if (fd >= 0) {
      ret = ioctl(fd, PWMIOC_STOP, 0);
      close(fd);
   }

   printf("unifrog standby pwm2 stop ret=%d reason=%s\n",
      ret, reason ? reason : "none");
}

static void set_lcd_backlight_gpio_for_standby(int on, const char *reason)
{
   lcd_power_candidate_on = on ? 1 : 0;
   pinmux_configure(PINPAD_R05, PINMUX_R05_GPIO);
   gpio_configure(PINPAD_R05, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_R05, lcd_power_candidate_on ? false : true);
   printf("unifrog standby backlight_gpio pin=PINPAD_R05 on=%d gpio=%d reason=%s\n",
      lcd_power_candidate_on, lcd_power_candidate_on ? 0 : 1,
      reason ? reason : "none");
}

static void prepare_display_for_standby(const char *reason)
{
   int ret = -1;

   log_fb_state("standby_before_blank");
   if (fbdev >= 0)
      ret = ioctl(fbdev, FBIOBLANK, FB_BLANK_NORMAL);
   printf("unifrog standby fb blank ret=%d reason=%s\n", ret, reason ? reason : "none");

   write_backlight_for_standby(0, reason);
   stop_backlight_pwm_for_standby(reason);
   set_lcd_backlight_gpio_for_standby(0, reason);
   usleep(100000);
}

static void restore_display_after_standby_return(const char *reason)
{
   int ret = -1;

   log_fb_state("standby_before_unblank");
   if (fbdev >= 0)
      ret = ioctl(fbdev, FBIOBLANK, FB_BLANK_UNBLANK);
   printf("unifrog standby fb unblank ret=%d reason=%s\n", ret, reason ? reason : "none");

   set_lcd_backlight_gpio_for_standby(1, reason);
   apply_backlight_level();
}

static void set_speaker_amp_for_power_state(int enabled, const char *reason)
{
   pinmux_configure(PINPAD_R07, PINMUX_R07_GPIO);
   gpio_configure(PINPAD_R07, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_R07, enabled ? false : true);
   printf("unifrog power speaker_amp enabled=%d pin=PINPAD_R07 gpio=%d reason=%s\n",
      enabled ? 1 : 0, enabled ? 0 : 1, reason ? reason : "none");
}

static void standby_wait_idle(void)
{
#ifdef SF2000_HAVE_MIPS_WAIT
   __asm__ volatile("wait");
#endif
   usleep(STANDBY_BUTTON_POLL_US);
}

static void standby_wait_for_local_release(const char *reason)
{
   unsigned clear_samples = 0;

   printf("unifrog standby wait_release reason=%s\n", reason ? reason : "none");
   while (clear_samples < 3) {
      uint32_t raw = scan_local_buttons(1);

      if (raw == 0)
         clear_samples++;
      else
         clear_samples = 0;

      standby_wait_idle();
   }
   clear_button_latches();
   printf("unifrog standby released reason=%s\n", reason ? reason : "none");
}

static uint32_t standby_wait_for_local_wake(const char *reason)
{
   uint32_t raw;

   printf("unifrog standby wait_wake source=local_buttons reason=%s\n",
      reason ? reason : "none");
   do {
      standby_wait_idle();
      raw = scan_local_buttons(1);
   } while (raw == 0);

   printf("unifrog standby wake source=local_buttons raw=0x%08lx reason=%s\n",
      (unsigned long)raw, reason ? reason : "none");
   return raw;
}

static void enter_app_standby(void)
{
   struct scpu_clock_snapshot clock;

   unifrog_text_copy(status_line, sizeof(status_line), "ENTERING STANDBY");
   draw_screen();
   printf("unifrog standby action=app_standby wake=local_buttons mode=display_off_speaker_off_slow_clock\n");
   flush_memlog();

   standby_wait_for_local_release("entry");
   unifrog_input_wireless_clear();
   prepare_display_for_standby("app_standby");
   set_speaker_amp_for_power_state(0, "app_standby");
   hw_watchdog_disable();
   scpu_enter_standby_clock(&clock, "app_standby");
   flush_memlog();

   (void)standby_wait_for_local_wake("app_standby");

   scpu_restore_clock(&clock, "app_standby");
   set_speaker_amp_for_power_state(1, "app_standby_resume");
   update_battery_status(1);
   unifrog_text_copy(status_line, sizeof(status_line), "STANDBY RESUMED");
   dirty = 1;
   draw_screen();
   dirty = 0;
   printf("unifrog standby redraw complete before_unblank\n");
   log_fb_state("standby_after_redraw");
   restore_display_after_standby_return("app_standby_resume");
   standby_wait_for_local_release("wake_button");
   unifrog_input_wireless_clear();
   printf("unifrog standby returned action=app_standby\n");
   flush_memlog();
}

static void standby_device(void)
{
   enter_app_standby();
}

static void start_performance_report_view(void)
{
   view = VIEW_REPORT;
   report_kind = REPORT_KIND_PERFORMANCE;
   report_done = 0;
   unifrog_text_copy(status_line, sizeof(status_line), "RUNNING REPORT");
   draw_screen();
   run_performance_report(0);
   dirty = 1;
}

static void start_memory_abi_report_view(void)
{
   view = VIEW_REPORT;
   report_kind = REPORT_KIND_MEMORY_ABI;
   report_done = 0;
   unifrog_text_copy(status_line, sizeof(status_line), "RUNNING MEM ABI");
   draw_screen();
   run_memory_abi_probe();
   dirty = 1;
}

static void rerun_report_view(void)
{
   if (report_kind == REPORT_KIND_MEMORY_ABI)
      start_memory_abi_report_view();
   else
      start_performance_report_view();
}

static void update_state(void)
{
   int max_menu = MENU_COUNT - 1;

   if (js_relaunch_requested && view == VIEW_MENU) {
      js_relaunch_requested = 0;
      js_native_action_active = 0;
      wait_for_button_release(1200);
      launch_js_frontend();
      return;
   }

   if (button_pressed(BTN_Y)) {
      flush_memlog();
      dirty = 1;
   }

   if (button_pressed(BTN_B)) {
      if (view == VIEW_PLAYER) {
         recover_from_video("PLAYER STOPPED");
      } else if (view == VIEW_PLAYOPTS) {
         close_stream_video();
         if (js_native_action_active) {
            js_relaunch_requested = 1;
            view = VIEW_MENU;
         } else {
            view = VIEW_BROWSER;
         }
         dirty = 1;
      } else if (view == VIEW_BROWSER || view == VIEW_FRAMEBUFFER || view == VIEW_INPUT ||
          view == VIEW_STORAGE || view == VIEW_VIDEO || view == VIEW_REPORT ||
          view == VIEW_PERF_LAB || view == VIEW_AUDIO_DIAG ||
          view == VIEW_DISCOVERY || view == VIEW_BRIGHTNESS) {
         if (js_native_action_active)
            js_relaunch_requested = 1;
         view = VIEW_MENU;
         dirty = 1;
      }
   }

   if (view == VIEW_MENU) {
      if (button_pressed(BTN_UP) && menu_selected > 0) {
         menu_selected--;
         dirty = 1;
      }
      if (button_pressed(BTN_DOWN) && menu_selected < max_menu) {
         menu_selected++;
         dirty = 1;
      }
      if (button_pressed(BTN_A)) {
         if (menu_selected == MENU_FRAMEBUFFER)
            view = VIEW_FRAMEBUFFER;
         else if (menu_selected == MENU_INPUT)
            view = VIEW_INPUT;
         else if (menu_selected == MENU_STORAGE) {
            view = VIEW_STORAGE;
            write_storage_probe();
         } else if (menu_selected == MENU_BROWSER) {
            view = VIEW_BROWSER;
            browser_load();
         } else if (menu_selected == MENU_JS_FRONTEND) {
            launch_js_frontend();
         } else if (menu_selected == MENU_GAMBATTE_TEST) {
            launch_gambatte_test_rom();
         } else if (menu_selected == MENU_VIDEO) {
            view = VIEW_VIDEO;
            video_probe();
         } else if (menu_selected == MENU_REPORT) {
            start_performance_report_view();
         } else if (menu_selected == MENU_PERF_LAB) {
            view = VIEW_PERF_LAB;
            report_done = 0;
            unifrog_text_copy(status_line, sizeof(status_line), "PERF LAB READY");
         } else if (menu_selected == MENU_MEMORY_ABI) {
            start_memory_abi_report_view();
         } else if (menu_selected == MENU_AUDIO_DIAG) {
            view = VIEW_AUDIO_DIAG;
            unifrog_text_copy(status_line, sizeof(status_line), "AUDIO DIAG READY");
         } else if (menu_selected == MENU_BRIGHTNESS) {
            view = VIEW_BRIGHTNESS;
            change_brightness(0);
         } else if (menu_selected == MENU_WIRELESS) {
            view = VIEW_DISCOVERY;
            report_done = 0;
            unifrog_text_copy(status_line, sizeof(status_line), "RUNNING WIRELESS DIAG");
            draw_screen();
            run_wireless_diagnostics();
         } else if (menu_selected == MENU_STANDBY) {
            standby_device();
         } else if (menu_selected == MENU_REBOOT) {
            reboot_device();
         }
         dirty = 1;
      }
   } else if (view == VIEW_BROWSER) {
      if (button_pressed(BTN_UP) && browser_selected > 0) {
         browser_selected--;
         if (browser_selected < browser_scroll)
            browser_scroll = browser_selected;
         dirty = 1;
      }
      if (button_pressed(BTN_DOWN) && browser_selected < browser_count - 1) {
         browser_selected++;
         if (browser_selected >= browser_scroll + 8)
            browser_scroll = browser_selected - 7;
         dirty = 1;
      }
      if (button_pressed(BTN_A)) {
         browser_enter_selected();
         dirty = 1;
      }
   } else if (view == VIEW_STORAGE && button_pressed(BTN_A)) {
      write_storage_probe();
      dirty = 1;
   } else if (view == VIEW_VIDEO && button_pressed(BTN_A)) {
      video_probe();
      dirty = 1;
   } else if (view == VIEW_REPORT && button_pressed(BTN_A)) {
      rerun_report_view();
   } else if (view == VIEW_PERF_LAB) {
      if (button_pressed(BTN_UP) && perf_selected > 0) {
         perf_selected--;
         if (perf_selected < perf_scroll)
            perf_scroll = perf_selected;
         dirty = 1;
      }
      if (button_pressed(BTN_DOWN) && perf_selected < PERF_LAB_COUNT - 1) {
         perf_selected++;
         if (perf_selected >= perf_scroll + 8)
            perf_scroll = perf_selected - 7;
         dirty = 1;
      }
      if (button_pressed(BTN_A))
         run_perf_lab_selected(0);
      if (button_pressed(BTN_X))
         run_perf_lab_selected(1);
   } else if (view == VIEW_AUDIO_DIAG) {
      if (button_pressed(BTN_UP) && audio_diag_selected > 0) {
         audio_diag_selected--;
         if (audio_diag_selected < audio_diag_scroll)
            audio_diag_scroll = audio_diag_selected;
         dirty = 1;
      }
      if (button_pressed(BTN_DOWN) && audio_diag_selected < (int)ARRAY_SIZE(audio_diag_tests) - 1) {
         audio_diag_selected++;
         if (audio_diag_selected >= audio_diag_scroll + 8)
            audio_diag_scroll = audio_diag_selected - 7;
         dirty = 1;
      }
      if (button_pressed(BTN_A))
         run_audio_diag_test((unsigned)audio_diag_selected);
      if (button_pressed(BTN_X))
         run_audio_diag_all();
   } else if (view == VIEW_DISCOVERY && button_pressed(BTN_A)) {
      report_done = 0;
      unifrog_text_copy(status_line, sizeof(status_line), "RUNNING WIRELESS DIAG");
      draw_screen();
      run_wireless_diagnostics();
      dirty = 1;
   } else if (view == VIEW_BRIGHTNESS) {
      if (button_pressed(BTN_LEFT))
         change_brightness(-5);
      if (button_pressed(BTN_RIGHT))
         change_brightness(5);
      if (button_pressed(BTN_DOWN))
         change_backlight_pwm_level(-5);
      if (button_pressed(BTN_UP))
         change_backlight_pwm_level(5);
      if (button_pressed(BTN_X))
         next_backlight_pwm_profile();
      if (button_pressed(BTN_A))
         next_panel_brightness_level();
      if (button_pressed(BTN_START))
         toggle_led_pattern();
      if (button_pressed(BTN_SELECT))
         set_status_led_green(!led_candidate_on, "manual_toggle");
      if (button_pressed(BTN_L))
         set_lcd_power_candidate(1);
      if (button_pressed(BTN_R))
         set_lcd_power_candidate(0);
   } else if (view == VIEW_PLAYOPTS) {
      if (button_pressed(BTN_UP) && play_preset > 0) {
         play_preset--;
         dirty = 1;
      }
      if (button_pressed(BTN_DOWN) && play_preset < (int)ARRAY_SIZE(playback_presets) - 1) {
         play_preset++;
         dirty = 1;
      }
      if (button_pressed(BTN_LEFT) || button_pressed(BTN_RIGHT)) {
         play_preset += button_pressed(BTN_RIGHT) ? 1 : -1;
         if (play_preset < 0)
            play_preset = (int)ARRAY_SIZE(playback_presets) - 1;
         if (play_preset >= (int)ARRAY_SIZE(playback_presets))
            play_preset = 0;
         dirty = 1;
      }
      if (button_pressed(BTN_X)) {
         play_quick_mode = !play_quick_mode;
         dirty = 1;
      }
      if (button_pressed(BTN_L)) {
         play_audio_enabled = !play_audio_enabled;
         dirty = 1;
      }
      if (button_pressed(BTN_R)) {
         play_buffering_enabled = !play_buffering_enabled;
         dirty = 1;
      }
      if (button_pressed(BTN_A)) {
         start_cached_video();
      }
   }
}

static void draw_header(const char *title)
{
   uint16_t fg = RGB565(230, 238, 245);
   uint16_t muted = RGB565(190, 202, 210);
   char buf[24];
   int x;

   fill_rect(0, 0, var.xres, 28, RGB565(38, 64, 88));
   draw_text(12, 8, title, fg, 2);

   if (battery_status.available == 1) {
      if (battery_status.low)
         snprintf(buf, sizeof(buf), "BAT LOW");
      else
         snprintf(buf, sizeof(buf), "BAT %u/5", battery_status.bars);
   } else {
      snprintf(buf, sizeof(buf), "BAT ?");
   }

   x = (int)var.xres - (int)strlen(buf) * 6 - 12;
   if (x > 180)
      draw_text(x, 10, buf, battery_status.low ? RGB565(255, 120, 90) : muted, 1);
}

static void draw_footer(void)
{
   uint16_t muted = RGB565(120, 138, 150);
   char buf[32];

   draw_hline(0, 218, var.xres, RGB565(32, 45, 58));
   draw_text(12, 224, "A SELECT B BACK Y LOG", muted, 1);
   if (battery_status.available == 1) {
      if (battery_status.low)
         snprintf(buf, sizeof(buf), "BAT LOW");
      else
         snprintf(buf, sizeof(buf), "BAT %u/5", battery_status.bars);
      draw_text((int)var.xres - (int)strlen(buf) * 6 - 12, 224, buf,
         battery_status.low ? RGB565(250, 90, 70) : muted, 1);
   }
}

static void draw_menu(void)
{
   const char *items[] = {
      "FRAMEBUFFER TEST",
      "INPUT TEST",
      "STORAGE LOG TEST",
      "FILE BROWSER",
      "JS2300 FRONTEND",
      "RUN GAMBATTE TEST",
      "VIDEO DISPLAY PROBE",
      "PERFORMANCE REPORT",
      "PERFORMANCE LAB",
      "MEMORY ABI PROBE",
      "AUDIO DIAGNOSTICS",
      "BRIGHTNESS",
      "WIRELESS DISCOVERY",
      "STANDBY",
      "REBOOT DEVICE",
   };
   uint16_t fg = RGB565(230, 238, 245);
   uint16_t muted = RGB565(120, 138, 150);
   uint16_t accent = RGB565(250, 190, 70);
   uint16_t row = RGB565(28, 42, 56);
   char buf[64];

   draw_header("SF2000 NATIVE");

   for (unsigned i = 0; i < ARRAY_SIZE(items); i++) {
      int y = 31 + i * 14;
      fill_rect(12, y, var.xres - 24, 13, (int)i == menu_selected ? accent : row);
      draw_text(22, y + 3, items[i], (int)i == menu_selected ? RGB565(12, 18, 26) : fg, 1);
   }

   snprintf(buf, sizeof(buf), "FRAMES %lu", (unsigned long)frames);
   draw_text(12, 209, buf, muted, 1);
   snprintf(buf, sizeof(buf), "LOG %u", (unsigned)unifrog_log_pending());
   draw_text(100, 209, buf, muted, 1);
   draw_text(160, 209, status_line, muted, 1);
   draw_footer();
}

static void draw_audio_diag(void)
{
   uint16_t fg = RGB565(230, 238, 245);
   uint16_t muted = RGB565(120, 138, 150);
   uint16_t accent = RGB565(250, 190, 70);
   char buf[96];

   draw_header("AUDIO DIAG");
   for (int row_idx = 0; row_idx < 8; row_idx++) {
      int idx = audio_diag_scroll + row_idx;
      int y = 36 + row_idx * 18;
      uint16_t bg = idx == audio_diag_selected ? accent : RGB565(28, 42, 56);

      fill_rect(12, y, var.xres - 24, 15, bg);
      if (idx >= (int)ARRAY_SIZE(audio_diag_tests))
         continue;
      snprintf(buf, sizeof(buf), "%02d %s", idx + 1, audio_diag_tests[idx].name);
      draw_text(18, y + 4, buf,
         idx == audio_diag_selected ? RGB565(12, 18, 26) : fg, 1);
   }

   if (audio_diag_running_name) {
      snprintf(buf, sizeof(buf), "PLAYING %s", audio_diag_running_name);
      draw_text(12, 184, buf, RGB565(250, 190, 70), 1);
   } else {
      draw_text(12, 184, "A START  X RUN ALL  B BACK", muted, 1);
   }
   draw_text(12, 200, status_line, muted, 1);
   draw_footer();
}

static void draw_brightness(void)
{
   char buf[80];
   int bar_w = (var.xres - 48) * fb_brightness / 100;
   uint16_t text = RGB565(242, 246, 248);
   uint16_t muted = RGB565(206, 218, 224);
   uint16_t accent = RGB565(255, 205, 80);

   fill_rect(0, 0, var.xres, var.yres, RGB565(8, 12, 18));
   fill_rect(0, 0, var.xres, 28, RGB565(36, 64, 88));
   draw_text(12, 8, "BRIGHTNESS", text, 2);
   snprintf(buf, sizeof(buf), "LEVEL %d", fb_brightness);
   draw_text(12, 52, buf, text, 2);
   fill_rect(24, 92, var.xres - 48, 18, RGB565(56, 68, 76));
   fill_rect(24, 92, bar_w, 18, accent);
   draw_text(12, 130, "LEFT/RIGHT STEP 5", muted, 1);
   draw_text(12, 146, "UP/DOWN PWM2 LEVEL", muted, 1);
   draw_text(12, 162, "X PWM2 PROFILE", muted, 1);
   draw_text(12, 174, "A PANEL DIM  START LED PATTERN", muted, 1);
   draw_text(12, 186, "SELECT RED/GREEN  L RESTORE R OFF", muted, 1);

   snprintf(buf, sizeof(buf), "FB %s BL %s PWM2 %s",
      fb_brightness_supported > 0 ? "OK" : (fb_brightness_supported == 0 ? "NO" : "?"),
      backlight_supported > 0 ? "OK" : (backlight_supported == 0 ? "NO" : "?"),
      backlight_pwm_supported > 0 ? "OK" : (backlight_pwm_supported == 0 ? "NO" : "?"));
   draw_text(12, 198, buf, text, 1);
   snprintf(buf, sizeof(buf), "PANEL %d CABC %d L25 %s",
      panel_brightness_levels[panel_brightness_index % ARRAY_SIZE(panel_brightness_levels)],
      panel_brightness_index / (int)ARRAY_SIZE(panel_brightness_levels),
      led_candidate_on ? "GREEN" : "RED");
   draw_text(178, 198, buf, text, 1);
   snprintf(buf, sizeof(buf), "PWM2 L%d %s",
      backlight_pwm_level, backlight_pwm_profiles[backlight_pwm_profile].name);
   draw_text(12, 210, buf, text, 1);
   draw_text(140, 210, status_line, accent, 1);
   draw_hline(0, 218, var.xres, RGB565(68, 82, 92));
   draw_text(12, 224, "A SELECT B BACK Y LOG", muted, 1);
}

static void draw_framebuffer_test(void)
{
   uint16_t colors[] = {
      RGB565(255, 0, 0), RGB565(255, 255, 0), RGB565(0, 255, 0),
      RGB565(0, 255, 255), RGB565(0, 0, 255), RGB565(255, 0, 255),
      RGB565(255, 255, 255), RGB565(0, 0, 0)
   };

   for (unsigned i = 0; i < ARRAY_SIZE(colors); i++)
      fill_rect(i * var.xres / ARRAY_SIZE(colors), 34,
         var.xres / ARRAY_SIZE(colors) + 1, 120, colors[i]);

   draw_header("FRAMEBUFFER");
   draw_text(12, 165, "STATIC COLOR BARS", RGB565(230, 238, 245), 1);
   draw_text(12, 182, "THIS VIEW REDRAWS SLOWLY", RGB565(120, 138, 150), 1);
   draw_footer();
}

static void draw_input_test(void)
{
   const char *names[] = {
      "R", "Y", "X", "L", "A", "B", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT"
   };
   char buf[64];
   char pressed[64];
   size_t used = 0;

   draw_header("INPUT TEST");
   for (int i = 0; i < BTN_LAST; i++) {
      int x = 16 + (i % 3) * 98;
      int y = 62 + (i / 3) * 28;
      int down = unifrog_input_down((enum unifrog_button)i);
      fill_rect(x, y, 82, 22, down ? RGB565(250, 190, 70) : RGB565(28, 42, 56));
      draw_text(x + 6, y + 6, names[i],
         down ? RGB565(12, 18, 26) : RGB565(230, 238, 245), 1);
      if (down && used < sizeof(pressed)) {
         int written = snprintf(pressed + used, sizeof(pressed) - used,
            "%s%s", used ? " " : "", names[i]);
         if (written > 0) {
            used += (size_t)written;
            if (used >= sizeof(pressed))
               used = sizeof(pressed) - 1;
         }
      }
   }

   if (used == 0)
      snprintf(pressed, sizeof(pressed), "NONE");
   snprintf(buf, sizeof(buf), "RAW FRAME %lu", (unsigned long)frames);
   draw_text(12, 42, pressed, RGB565(250, 190, 70), 1);
   draw_text(12, 188, buf, RGB565(120, 138, 150), 1);
   draw_footer();
}

static void draw_storage_test(void)
{
   draw_header("STORAGE TEST");
   draw_text(12, 54, "WRITES AND READS", RGB565(230, 238, 245), 1);
   draw_text(12, 72, "/media/mmcblk0/unifrog-storage-test.txt", RGB565(120, 138, 150), 1);
   draw_text(12, 108, status_line, RGB565(250, 190, 70), 1);
   draw_text(12, 144, "PRESS A TO REPEAT", RGB565(120, 138, 150), 1);
   draw_footer();
}

static void draw_browser(void)
{
   char title[64];
   int visible = 8;

   snprintf(title, sizeof(title), "FILES");
   draw_header(title);
   draw_text(12, 34, browser_path, RGB565(120, 138, 150), 1);

   for (int row = 0; row < visible; row++) {
      int idx = browser_scroll + row;
      int y = 52 + row * 20;
      uint16_t bg = idx == browser_selected ? RGB565(250, 190, 70) : RGB565(28, 42, 56);

      if (idx >= browser_count)
         break;

      fill_rect(12, y, var.xres - 24, 16, bg);
      draw_text(18, y + 4, browser_items[idx].is_dir ? "DIR" : "FILE",
         idx == browser_selected ? RGB565(12, 18, 26) : RGB565(120, 138, 150), 1);
      draw_text(50, y + 4, browser_items[idx].name,
         idx == browser_selected ? RGB565(12, 18, 26) : RGB565(230, 238, 245), 1);
   }

   draw_footer();
}

static void draw_video_probe(void)
{
   draw_header("VIDEO PROBE");
   draw_text(12, 52, "PROBES DEVICE NODES", RGB565(230, 238, 245), 1);
   draw_text(12, 72, "/dev/dis /dev/ge /dev/viddec", RGB565(120, 138, 150), 1);
   draw_text(12, 108, status_line, RGB565(250, 190, 70), 1);
   draw_text(12, 144, "PRESS A TO REPEAT", RGB565(120, 138, 150), 1);
   draw_footer();
}

static void draw_perf_lab(void)
{
   uint16_t fg = RGB565(230, 238, 245);
   uint16_t muted = RGB565(120, 138, 150);
   uint16_t accent = RGB565(250, 190, 70);
   uint16_t row = RGB565(28, 42, 56);
   int visible = 8;

   draw_header("PERF LAB");
   draw_text(12, 34, "A RUN  X RUN RISKY  Y LOG", muted, 1);

   for (int row_index = 0; row_index < visible; row_index++) {
      int idx = perf_scroll + row_index;
      int y = 52 + row_index * 19;
      uint16_t bg;

      if (idx >= PERF_LAB_COUNT)
         break;

      bg = idx == perf_selected ? accent : row;
      fill_rect(12, y, var.xres - 24, 15, bg);
      draw_text(18, y + 4, perf_lab_items[idx],
         idx == perf_selected ? RGB565(12, 18, 26) : fg, 1);
   }

   if (perf_selected == PERF_LAB_SCPU_RUNTIME)
      draw_text(12, 204, "RISKY: CHANGES CLOCKS, X ONLY", RGB565(255, 120, 90), 1);
   else
      draw_text(12, 204, status_line, muted, 1);

   draw_footer();
}

static void draw_report(void)
{
   if (view == VIEW_DISCOVERY) {
      draw_header("WIRELESS DIAG");
      draw_text(12, 52, report_done ? "DIAG COMPLETE" : "RUNNING RF PROBES", RGB565(230, 238, 245), 1);
      draw_text(12, 74, "PRESS A TO RERUN", RGB565(120, 138, 150), 1);
      draw_text(12, 96, "PRESS Y TO SAVE LOG.TXT", RGB565(250, 190, 70), 1);
      draw_text(12, 118, status_line, RGB565(230, 238, 245), 1);
      draw_footer();
      return;
   }

   if (report_kind == REPORT_KIND_MEMORY_ABI) {
      draw_header("MEMORY ABI");
      draw_text(12, 52, report_done ? "MEM ABI COMPLETE" : "RUNNING MEM ABI", RGB565(230, 238, 245), 1);
      draw_text(12, 74, "ARENA BOUNDS AND HEAP", RGB565(120, 138, 150), 1);
   } else {
      draw_header("PERFORMANCE REPORT");
      draw_text(12, 52, report_done ? "REPORT COMPLETE" : "RUNNING TESTS", RGB565(230, 238, 245), 1);
      draw_text(12, 74, "BENCHES FB GE DIS CACHE SD", RGB565(120, 138, 150), 1);
   }
   draw_text(12, 96, "PRESS A TO RERUN", RGB565(120, 138, 150), 1);
   draw_text(12, 118, "PRESS Y TO SAVE LOG.TXT", RGB565(250, 190, 70), 1);
   draw_text(12, 156, status_line, RGB565(230, 238, 245), 1);
   draw_footer();
}

static void draw_playopts(void)
{
   char buf[96];
   uint16_t fg = RGB565(230, 238, 245);
   uint16_t muted = RGB565(120, 138, 150);
   uint16_t accent = RGB565(250, 190, 70);

   draw_header("PLAYBACK TEST");
   for (unsigned i = 0; i < ARRAY_SIZE(playback_presets); i++) {
      int y = 42 + i * 22;
      uint16_t bg = (int)i == play_preset ? accent : RGB565(28, 42, 56);

      fill_rect(12, y, var.xres - 24, 17, bg);
      snprintf(buf, sizeof(buf), "%u %s", i + 1, playback_presets[i].name);
      draw_text(18, y + 4, buf, (int)i == play_preset ? RGB565(12, 18, 26) : fg, 1);
   }

   snprintf(buf, sizeof(buf), "X QUICK %s  L AUDIO %s",
      play_quick_mode ? "ON" : "OFF",
      play_audio_enabled ? "ON" : "OFF");
   draw_text(12, 178, buf, muted, 1);
   snprintf(buf, sizeof(buf), "R BUFFER %s  A START",
      play_buffering_enabled ? "ON" : "OFF");
   draw_text(12, 194, buf, muted, 1);
   draw_footer();
}

static void draw_screen(void)
{
   uint16_t bg = RGB565(12, 18, 26);
   unsigned draw_buffer = fb_active_buffer;

   if (fb_base && fb_buffers > 1) {
      draw_buffer = (fb_active_buffer + 1) % fb_buffers;
      fb = fb_base + draw_buffer * line_pixels * var.yres;
   }

   fill_rect(0, 0, var.xres, var.yres, bg);

   if (view == VIEW_MENU)
      draw_menu();
   else if (view == VIEW_FRAMEBUFFER)
      draw_framebuffer_test();
   else if (view == VIEW_INPUT)
      draw_input_test();
   else if (view == VIEW_STORAGE)
      draw_storage_test();
   else if (view == VIEW_BROWSER)
      draw_browser();
   else if (view == VIEW_VIDEO)
      draw_video_probe();
   else if (view == VIEW_REPORT)
      draw_report();
   else if (view == VIEW_PERF_LAB)
      draw_perf_lab();
   else if (view == VIEW_AUDIO_DIAG)
      draw_audio_diag();
   else if (view == VIEW_DISCOVERY)
      draw_report();
   else if (view == VIEW_BRIGHTNESS)
      draw_brightness();
   else if (view == VIEW_PLAYOPTS)
      draw_playopts();

   unifrog_perf_cache_flush(fb, visible_fb_bytes());
   if (fb_buffers > 1) {
      int ret = 0;

      ioctl(fbdev, FBIO_WAITFORVSYNC, &ret);
      var.xoffset = 0;
      var.yoffset = draw_buffer * var.yres;
   }
   ioctl(fbdev, FBIOPAN_DISPLAY, &var);
   fb_active_buffer = draw_buffer;
}

void unifrog_test_frontend_main(void)
{
   unifrog_battery_status_init(&battery_status);
   printf("unifrog test_frontend start commit=%s dirty=%d\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY);
   clear_button_latches();
   {
      const char *startup_devs[] = { "/dev/spidev0", "/dev/xpt2046" };
      for (unsigned i = 0; i < ARRAY_SIZE(startup_devs); i++) {
         int fd = open(startup_devs[i], O_RDWR);
         printf("unifrog startup dev path=%s result=%s\n",
            startup_devs[i], fd >= 0 ? "open" : "missing");
         if (fd >= 0)
            close(fd);
      }
   }

   unifrog_input_wireless_init();
   clear_button_latches();

   if (fb_init() != 0)
      return;

   update_battery_status(1);
   draw_screen();
   dirty = 0;

   while (1) {
      poll_buttons();
      if ((frames % BATTERY_POLL_FRAMES) == 0)
         update_battery_status(0);
      update_state();
      update_led_pattern();
      if (view == VIEW_PLAYER)
         monitor_native_video();
      if (view == VIEW_INPUT)
         dirty = 1;
      if (view != VIEW_PLAYER && (dirty || (frames % 30) == 0)) {
         draw_screen();
         dirty = 0;
      }
      save_button_prev();
      frames++;
      usleep(33333);
   }
}
