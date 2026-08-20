#include <js2300/js2300.h>

#include <stdbool.h>
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libretro.h>
#include <unifrog/abi.h>

#define JS2300_CORE_WIDTH 320u
#define JS2300_CORE_HEIGHT 240u
#define JS2300_CORE_FPS 60.0
#define JS2300_CORE_SAMPLE_RATE 32000.0
#define JS2300_CORE_HEAP_BYTES (16u * 1024u * 1024u)
#define JS2300_CORE_AUDIO_CHUNK_FRAMES 512u
#define JS2300_CORE_FORMAT_RGB565 0u
#define JS2300_CORE_DEFAULT_SCRIPT_ROOT \
   "/media/mmcblk0/unifrog_data/scripts/js2300-cores"
#define JS2300_CORE_CHIP8_SCRIPT "chip8.js"

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static struct js2300_runtime *runtime;
static uint16_t framebuffer[JS2300_CORE_WIDTH * JS2300_CORE_HEIGHT]
   __attribute__((aligned(64)));
static int16_t audio_stereo_chunk[JS2300_CORE_AUDIO_CHUNK_FRAMES * 2u]
   __attribute__((aligned(16)));
static char content_path[256];
static char script_root[256];
static char script_entry[96];
static uint32_t frame_count;
static uint32_t input_mask;
static int framebuffer_dirty;
static int frame_presented;
static void *runtime_heap;

extern int fs_opendir(const char *path);
extern int fs_closedir(int fd);
extern ssize_t fs_readdir(int fd, void *buffer);

void retro_unload_game(void);

static char ascii_lower(char c)
{
   if (c >= 'A' && c <= 'Z')
      return (char)(c - 'A' + 'a');
   return c;
}

static int path_ends_with_ci(const char *path, const char *suffix)
{
   size_t path_len;
   size_t suffix_len;

   if (!path || !suffix)
      return 0;
   path_len = strlen(path);
   suffix_len = strlen(suffix);
   if (suffix_len > path_len)
      return 0;
   path += path_len - suffix_len;
   for (size_t i = 0; i < suffix_len; i++) {
      if (ascii_lower(path[i]) != ascii_lower(suffix[i]))
         return 0;
   }
   return 1;
}

static int is_js_script_path(const char *path)
{
   return path_ends_with_ci(path, ".js") || path_ends_with_ci(path, ".mjs");
}

static int is_chip8_path(const char *path)
{
   return path_ends_with_ci(path, ".ch8") ||
      path_ends_with_ci(path, ".chip8");
}

static int split_script_path(const char *path)
{
   const char *slash;
   size_t root_len;
   size_t entry_len;

   if (!path || !path[0])
      return -1;
   slash = strrchr(path, '/');
   if (!slash) {
      root_len = 1;
      entry_len = strlen(path);
      if (entry_len >= sizeof(script_entry))
         return -1;
      memcpy(script_root, ".", 2);
      memcpy(script_entry, path, entry_len + 1);
      return 0;
   }

   root_len = (size_t)(slash - path);
   entry_len = strlen(slash + 1);
   if (!root_len || root_len >= sizeof(script_root) ||
       !entry_len || entry_len >= sizeof(script_entry))
      return -1;
   memcpy(script_root, path, root_len);
   script_root[root_len] = '\0';
   memcpy(script_entry, slash + 1, entry_len + 1);
   return 0;
}

static int set_default_script(const char *root, const char *entry)
{
   if (!root || !entry ||
       strlen(root) >= sizeof(script_root) ||
       strlen(entry) >= sizeof(script_entry))
      return -1;
   snprintf(script_root, sizeof(script_root), "%s", root);
   snprintf(script_entry, sizeof(script_entry), "%s", entry);
   return 0;
}

static uint32_t now_ms(void)
{
   struct timeval tv;

   if (gettimeofday(&tv, NULL) != 0)
      return frame_count * (uint32_t)(1000.0 / JS2300_CORE_FPS);
   return (uint32_t)((uint32_t)tv.tv_sec * 1000u +
      (uint32_t)(tv.tv_usec / 1000u));
}

static void host_log(void *opaque, const char *message)
{
   (void)opaque;
   printf("js2300 core: %s\n", message ? message : "");
}

static int host_flush_log(void *opaque)
{
   (void)opaque;
   return 0;
}

static uint32_t host_millis(void *opaque)
{
   (void)opaque;
   return now_ms();
}

static void host_sleep_ms(void *opaque, uint32_t ms)
{
   (void)opaque;
   (void)ms;
}

static uint32_t poll_buttons(void)
{
   uint32_t mask = 0;

   if (input_poll_cb)
      input_poll_cb();
   if (!input_state_cb)
      return input_mask;

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_UP))
      mask |= 1u << 0;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_DOWN))
      mask |= 1u << 1;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_LEFT))
      mask |= 1u << 2;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_RIGHT))
      mask |= 1u << 3;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_A))
      mask |= 1u << 4;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_B))
      mask |= 1u << 5;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_X))
      mask |= 1u << 6;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_Y))
      mask |= 1u << 7;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_L))
      mask |= 1u << 8;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_R))
      mask |= 1u << 9;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_SELECT))
      mask |= 1u << 10;
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
       RETRO_DEVICE_ID_JOYPAD_START))
      mask |= 1u << 11;

   input_mask = mask;
   return mask;
}

static uint32_t host_input_poll(void *opaque)
{
   (void)opaque;
   return poll_buttons();
}

static int core_fs_read_bytes(void *opaque, const char *path,
                              uint8_t *out, size_t out_size)
{
   FILE *file;
   size_t got;
   (void)opaque;

   if (!path || !out || out_size == 0)
      return -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   got = fread(out, 1, out_size, file);
   if (ferror(file)) {
      fclose(file);
      return -1;
   }
   fclose(file);
   return (int)got;
}

static int core_fs_read_text(void *opaque, const char *path,
                             char *out, size_t out_size)
{
   int got;

   if (!out || out_size == 0)
      return -1;
   got = core_fs_read_bytes(opaque, path, (uint8_t *)out, out_size - 1u);
   if (got < 0)
      return got;
   out[got] = '\0';
   return got;
}

static int core_fs_write_bytes(void *opaque, const char *path,
                               const uint8_t *data, size_t size)
{
   char temporary[320];
   FILE *file;
   int write_ok;
   int close_ret;
   int ret = -1;
   (void)opaque;

   if (!path || !path[0] || !data ||
       snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
       (int)sizeof(temporary))
      return -1;
   file = fopen(temporary, "wb");
   if (!file)
      return -1;
   write_ok = (!size || fwrite(data, 1, size, file) == size) &&
      fflush(file) == 0;
   close_ret = fclose(file);
   if (write_ok && close_ret == 0) {
      if (rename(temporary, path) == 0)
         ret = 0;
   }
   if (ret != 0)
      remove(temporary);
   return ret;
}

static int core_fs_write_text(void *opaque, const char *path,
                              const char *text, size_t size)
{
   return core_fs_write_bytes(opaque, path, (const uint8_t *)text, size);
}

static int core_fs_list(void *opaque, const char *path,
                        struct js2300_fs_entry *entries, size_t max_entries)
{
   int dir;
   size_t count = 0;
   (void)opaque;

   if (!path || !entries || max_entries == 0 ||
       (dir = fs_opendir(path)) < 0)
      return -1;
   while (count < max_entries) {
      uint8_t raw[0x428];
      const char *name;
      char full[320];
      struct stat st;
      size_t name_len;

      if (fs_readdir(dir, raw) < 0)
         break;
      name = raw[0x22] ? (const char *)raw + 0x22 :
         (const char *)raw + 4;
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
         continue;
      name_len = strlen(name);
      if (name_len >= sizeof(entries[count].name))
         name_len = sizeof(entries[count].name) - 1u;
      memcpy(entries[count].name, name, name_len);
      entries[count].name[name_len] = '\0';
      entries[count].is_dir = 0;
      if (snprintf(full, sizeof(full), "%s/%s", path, name) <
          (int)sizeof(full) && stat(full, &st) == 0 && S_ISDIR(st.st_mode))
         entries[count].is_dir = 1;
      count++;
   }
   fs_closedir(dir);
   return (int)count;
}

static int core_fs_stat(void *opaque, const char *path,
                        struct js2300_fs_stat *status)
{
   struct stat st;
   (void)opaque;

   if (!path || !status || stat(path, &st) != 0)
      return -1;
   memset(status, 0, sizeof(*status));
   status->exists = 1;
   status->is_dir = S_ISDIR(st.st_mode) ? 1u : 0u;
   status->is_file = S_ISREG(st.st_mode) ? 1u : 0u;
   status->size = st.st_size > 0 ? (uint64_t)st.st_size : 0;
   status->modified_time = st.st_mtime;
   return 0;
}

static int core_fs_mkdir(void *opaque, const char *path)
{
   struct stat st;
   (void)opaque;

   if (!path || !path[0])
      return -1;
   if (mkdir(path, 0777) == 0)
      return 0;
   return errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode) ?
      0 : -1;
}

static int core_fs_remove(void *opaque, const char *path)
{
   (void)opaque;

   if (!path || !path[0])
      return -1;
   return remove(path);
}

static int core_fs_rename(void *opaque, const char *from, const char *to)
{
   (void)opaque;
   return from && from[0] && to && to[0] ? rename(from, to) : -1;
}

static int clip_rect(int *x, int *y, int *w, int *h,
                     int *src_x, int *src_y)
{
   if (!x || !y || !w || !h || *w <= 0 || *h <= 0)
      return 0;
   if (*x < 0) {
      if (src_x)
         *src_x -= *x;
      *w += *x;
      *x = 0;
   }
   if (*y < 0) {
      if (src_y)
         *src_y -= *y;
      *h += *y;
      *y = 0;
   }
   if (*x >= (int)JS2300_CORE_WIDTH || *y >= (int)JS2300_CORE_HEIGHT)
      return 0;
   if (*x + *w > (int)JS2300_CORE_WIDTH)
      *w = (int)JS2300_CORE_WIDTH - *x;
   if (*y + *h > (int)JS2300_CORE_HEIGHT)
      *h = (int)JS2300_CORE_HEIGHT - *y;
   return *w > 0 && *h > 0;
}

static void fill_u16(uint16_t *dst, size_t count, uint16_t color)
{
   uint32_t packed = (uint32_t)color | ((uint32_t)color << 16);

   while (((uintptr_t)dst & 3u) && count) {
      *dst++ = color;
      count--;
   }
   while (count >= 2u) {
      *(uint32_t *)dst = packed;
      dst += 2;
      count -= 2u;
   }
   if (count)
      *dst = color;
}

static void core_frame_info(void *opaque, struct js2300_core_frame_info *info)
{
   (void)opaque;
   if (!info)
      return;
   info->width = JS2300_CORE_WIDTH;
   info->height = JS2300_CORE_HEIGHT;
   info->pitch_pixels = JS2300_CORE_WIDTH;
   info->format = JS2300_CORE_FORMAT_RGB565;
   info->frame = frame_count;
}

static void core_clear(void *opaque, uint16_t color)
{
   (void)opaque;
   fill_u16(framebuffer, JS2300_CORE_WIDTH * JS2300_CORE_HEIGHT, color);
   framebuffer_dirty = 1;
}

static void core_set_pixel(void *opaque, int x, int y, uint16_t color)
{
   (void)opaque;
   if ((unsigned)x >= JS2300_CORE_WIDTH ||
       (unsigned)y >= JS2300_CORE_HEIGHT)
      return;
   framebuffer[(unsigned)y * JS2300_CORE_WIDTH + (unsigned)x] = color;
   framebuffer_dirty = 1;
}

static void core_fill_rect(void *opaque, int x, int y, int w, int h,
                           uint16_t color)
{
   (void)opaque;
   if (!clip_rect(&x, &y, &w, &h, NULL, NULL))
      return;
   for (int row = 0; row < h; row++) {
      uint16_t *dst = framebuffer + (unsigned)(y + row) *
         JS2300_CORE_WIDTH + (unsigned)x;
      fill_u16(dst, (size_t)w, color);
   }
   framebuffer_dirty = 1;
}

static int core_blit_rgb565(void *opaque, int x, int y, int w, int h,
                            const uint16_t *pixels, size_t count)
{
   int src_x = 0;
   int src_y = 0;
   int src_w = w;
   (void)opaque;

   if (!pixels || w <= 0 || h <= 0 ||
       count < (size_t)((unsigned)w * (unsigned)h))
      return -1;
   if (!clip_rect(&x, &y, &w, &h, &src_x, &src_y))
      return 0;
   for (int row = 0; row < h; row++) {
      uint16_t *dst = framebuffer + (unsigned)(y + row) *
         JS2300_CORE_WIDTH + (unsigned)x;
      const uint16_t *src = pixels + (unsigned)(src_y + row) *
         (unsigned)src_w + (unsigned)src_x;
      memcpy(dst, src, (size_t)w * sizeof(*dst));
   }
   framebuffer_dirty = 1;
   return 0;
}

static int core_blit_indexed8(void *opaque, int x, int y, int w, int h,
                              const uint8_t *pixels, size_t count,
                              const uint16_t *palette, size_t palette_count)
{
   int src_x = 0;
   int src_y = 0;
   int src_w = w;
   (void)opaque;

   if (!pixels || !palette || !palette_count || w <= 0 || h <= 0 ||
       count < (size_t)((unsigned)w * (unsigned)h))
      return -1;
   if (!clip_rect(&x, &y, &w, &h, &src_x, &src_y))
      return 0;
   for (int row = 0; row < h; row++) {
      uint16_t *dst = framebuffer + (unsigned)(y + row) *
         JS2300_CORE_WIDTH + (unsigned)x;
      const uint8_t *src = pixels + (unsigned)(src_y + row) *
         (unsigned)src_w + (unsigned)src_x;
      if (palette_count >= 256u) {
         for (int col = 0; col < w; col++)
            dst[col] = palette[src[col]];
      } else {
         for (int col = 0; col < w; col++) {
            unsigned idx = src[col];
            dst[col] = idx < palette_count ? palette[idx] : 0;
         }
      }
   }
   framebuffer_dirty = 1;
   return 0;
}

static int core_blit_indexed4(void *opaque, int x, int y, int w, int h,
                              const uint8_t *pixels, size_t count,
                              const uint16_t *palette, size_t palette_count)
{
   int src_x = 0;
   int src_y = 0;
   int src_w = w;
   (void)opaque;

   if (!pixels || !palette || !palette_count || w <= 0 || h <= 0 ||
       count < (((size_t)w * (size_t)h + 1u) >> 1))
      return -1;
   if (!clip_rect(&x, &y, &w, &h, &src_x, &src_y))
      return 0;
   for (int row = 0; row < h; row++) {
      uint16_t *dst = framebuffer + (unsigned)(y + row) *
         JS2300_CORE_WIDTH + (unsigned)x;
      unsigned base = (unsigned)(src_y + row) * (unsigned)src_w +
         (unsigned)src_x;
      if (palette_count >= 16u) {
         for (int col = 0; col < w; col++) {
            unsigned pixel = base + (unsigned)col;
            uint8_t packed = pixels[pixel >> 1];
            unsigned idx = (pixel & 1u) ? (packed & 0x0fu) : (packed >> 4);
            dst[col] = palette[idx];
         }
      } else {
         for (int col = 0; col < w; col++) {
            unsigned pixel = base + (unsigned)col;
            uint8_t packed = pixels[pixel >> 1];
            unsigned idx = (pixel & 1u) ? (packed & 0x0fu) : (packed >> 4);
            dst[col] = idx < palette_count ? palette[idx] : 0;
         }
      }
   }
   framebuffer_dirty = 1;
   return 0;
}

static void core_fill_scaled_pixel(int x, int y, unsigned scale,
                                   uint16_t color)
{
   int w = (int)scale;
   int h = (int)scale;

   if (!clip_rect(&x, &y, &w, &h, NULL, NULL))
      return;
   for (int row = 0; row < h; row++) {
      uint16_t *dst = framebuffer + (unsigned)(y + row) *
         JS2300_CORE_WIDTH + (unsigned)x;
      fill_u16(dst, (size_t)w, color);
   }
}

static int core_scaled_bounds_ok(int x, int y, int w, int h, unsigned scale)
{
   uint32_t out_w;
   uint32_t out_h;

   if (w <= 0 || h <= 0 || scale == 0)
      return 0;
   if ((uint32_t)w > UINT32_MAX / scale ||
       (uint32_t)h > UINT32_MAX / scale)
      return 0;
   out_w = (uint32_t)w * scale;
   out_h = (uint32_t)h * scale;
   return x >= 0 && y >= 0 &&
      (uint32_t)x + out_w <= JS2300_CORE_WIDTH &&
      (uint32_t)y + out_h <= JS2300_CORE_HEIGHT;
}

static int core_blit_indexed8_scaled(void *opaque, int x, int y, int w,
                                     int h, unsigned scale,
                                     const uint8_t *pixels, size_t count,
                                     const uint16_t *palette,
                                     size_t palette_count)
{
   size_t required;
   (void)opaque;

   if (!pixels || !palette || !palette_count || w <= 0 || h <= 0 ||
       scale == 0 || scale > 32u)
      return -1;
   required = (size_t)w * (size_t)h;
   if ((size_t)w != 0 && required / (size_t)w != (size_t)h)
      return -1;
   if (count < required)
      return -1;
   if (core_scaled_bounds_ok(x, y, w, h, scale)) {
      for (int row = 0; row < h; row++) {
         const uint8_t *src = pixels + (unsigned)row * (unsigned)w;
         for (unsigned sy = 0; sy < scale; sy++) {
            uint16_t *dst = framebuffer +
               ((unsigned)y + (unsigned)row * scale + sy) *
               JS2300_CORE_WIDTH + (unsigned)x;
            for (int col = 0; col < w; col++) {
               unsigned idx = src[col];
               uint16_t color =
                  idx < palette_count ? palette[idx] : 0;
               fill_u16(dst + (unsigned)col * scale, scale, color);
            }
         }
      }
      framebuffer_dirty = 1;
      return 0;
   }

   for (int row = 0; row < h; row++) {
      const uint8_t *src = pixels + (unsigned)row * (unsigned)w;
      for (int col = 0; col < w; col++) {
         unsigned idx = src[col];
         uint16_t color = idx < palette_count ? palette[idx] : 0;
         core_fill_scaled_pixel(x + col * (int)scale,
            y + row * (int)scale, scale, color);
      }
   }
   framebuffer_dirty = 1;
   return 0;
}

static int core_blit_indexed4_scaled(void *opaque, int x, int y, int w,
                                     int h, unsigned scale,
                                     const uint8_t *pixels, size_t count,
                                     const uint16_t *palette,
                                     size_t palette_count)
{
   size_t required;
   (void)opaque;

   if (!pixels || !palette || !palette_count || w <= 0 || h <= 0 ||
       scale == 0 || scale > 32u)
      return -1;
   required = (((size_t)w * (size_t)h + 1u) >> 1);
   if ((size_t)w != 0 &&
       ((size_t)w * (size_t)h) / (size_t)w != (size_t)h)
      return -1;
   if (count < required)
      return -1;
   if (core_scaled_bounds_ok(x, y, w, h, scale)) {
      for (int row = 0; row < h; row++) {
         unsigned base = (unsigned)row * (unsigned)w;
         for (unsigned sy = 0; sy < scale; sy++) {
            uint16_t *dst = framebuffer +
               ((unsigned)y + (unsigned)row * scale + sy) *
               JS2300_CORE_WIDTH + (unsigned)x;
            for (int col = 0; col < w; col++) {
               unsigned pixel = base + (unsigned)col;
               uint8_t packed = pixels[pixel >> 1];
               unsigned idx =
                  (pixel & 1u) ? (packed & 0x0fu) : (packed >> 4);
               uint16_t color =
                  idx < palette_count ? palette[idx] : 0;
               fill_u16(dst + (unsigned)col * scale, scale, color);
            }
         }
      }
      framebuffer_dirty = 1;
      return 0;
   }

   for (int row = 0; row < h; row++) {
      unsigned base = (unsigned)row * (unsigned)w;
      for (int col = 0; col < w; col++) {
         unsigned pixel = base + (unsigned)col;
         uint8_t packed = pixels[pixel >> 1];
         unsigned idx = (pixel & 1u) ? (packed & 0x0fu) : (packed >> 4);
         uint16_t color = idx < palette_count ? palette[idx] : 0;
         core_fill_scaled_pixel(x + col * (int)scale,
            y + row * (int)scale, scale, color);
      }
   }
   framebuffer_dirty = 1;
   return 0;
}

static void core_present(void *opaque)
{
   (void)opaque;
   if (video_cb)
      video_cb(framebuffer, JS2300_CORE_WIDTH, JS2300_CORE_HEIGHT,
         JS2300_CORE_WIDTH * sizeof(framebuffer[0]));
   frame_presented = 1;
   framebuffer_dirty = 0;
}

static int core_audio_s16(void *opaque, const int16_t *samples,
                          size_t sample_count, unsigned channels)
{
   size_t pos = 0;
   (void)opaque;

   if (!samples || (channels != 1u && channels != 2u))
      return -1;
   if (channels == 2u) {
      size_t frames = sample_count >> 1;
      if (audio_batch_cb)
         audio_batch_cb(samples, frames);
      else if (audio_cb) {
         for (size_t i = 0; i < frames; i++)
            audio_cb(samples[i * 2u], samples[i * 2u + 1u]);
      }
      return 0;
   }

   while (pos < sample_count) {
      size_t chunk = sample_count - pos;
      if (chunk > JS2300_CORE_AUDIO_CHUNK_FRAMES)
         chunk = JS2300_CORE_AUDIO_CHUNK_FRAMES;
      for (size_t i = 0; i < chunk; i++) {
         int16_t s = samples[pos + i];
         audio_stereo_chunk[i * 2u] = s;
         audio_stereo_chunk[i * 2u + 1u] = s;
      }
      if (audio_batch_cb)
         audio_batch_cb(audio_stereo_chunk, chunk);
      else if (audio_cb) {
         for (size_t i = 0; i < chunk; i++)
            audio_cb(audio_stereo_chunk[i * 2u],
               audio_stereo_chunk[i * 2u + 1u]);
      }
      pos += chunk;
   }
   return 0;
}

static void configure_host(struct js2300_host *host)
{
   memset(host, 0, sizeof(*host));
   host->size = sizeof(*host);
   host->mode = "libretro-core";
   host->content_path = content_path;
   host->log = host_log;
   host->flush_log = host_flush_log;
   host->millis = host_millis;
   host->sleep_ms = host_sleep_ms;
   host->video_clear = core_clear;
   host->video_present = core_present;
   host->input_poll = host_input_poll;
   host->fs_list = core_fs_list;
   host->fs_read_text = core_fs_read_text;
   host->fs_read_bytes = core_fs_read_bytes;
   host->fs_write_text = core_fs_write_text;
   host->core_frame_info = core_frame_info;
   host->core_clear = core_clear;
   host->core_set_pixel = core_set_pixel;
   host->core_fill_rect = core_fill_rect;
   host->core_blit_rgb565 = core_blit_rgb565;
   host->core_blit_indexed8 = core_blit_indexed8;
   host->core_blit_indexed4 = core_blit_indexed4;
   host->core_blit_indexed8_scaled = core_blit_indexed8_scaled;
   host->core_blit_indexed4_scaled = core_blit_indexed4_scaled;
   host->core_present = core_present;
   host->core_audio_s16 = core_audio_s16;
   host->fs_stat = core_fs_stat;
   host->fs_mkdir = core_fs_mkdir;
   host->fs_remove = core_fs_remove;
   host->fs_rename = core_fs_rename;
   host->fs_write_bytes = core_fs_write_bytes;
}

void retro_set_environment(retro_environment_t cb)
{
   env_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_init(void)
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

   if (env_cb)
      env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
   core_clear(NULL, 0);
}

void retro_deinit(void)
{
   retro_unload_game();
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
   if (!info)
      return;
   memset(info, 0, sizeof(*info));
   info->library_name = "JS2300";
   info->library_version = js2300_version_string();
   info->valid_extensions = "js|mjs|ch8|chip8";
   info->need_fullpath = true;
   info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   if (!info)
      return;
   memset(info, 0, sizeof(*info));
   info->geometry.base_width = JS2300_CORE_WIDTH;
   info->geometry.base_height = JS2300_CORE_HEIGHT;
   info->geometry.max_width = JS2300_CORE_WIDTH;
   info->geometry.max_height = JS2300_CORE_HEIGHT;
   info->geometry.aspect_ratio =
      (float)JS2300_CORE_WIDTH / (float)JS2300_CORE_HEIGHT;
   info->timing.fps = JS2300_CORE_FPS;
   info->timing.sample_rate = JS2300_CORE_SAMPLE_RATE;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

bool retro_load_game(const struct retro_game_info *game)
{
   struct js2300_config config;
   struct js2300_host host;
   int status = 0;
   int runtime_heap_reserved = 0;

   if (!game || !game->path || !game->path[0])
      return false;
   if (snprintf(content_path, sizeof(content_path), "%s", game->path) >=
       (int)sizeof(content_path))
      return false;
   if (is_js_script_path(content_path)) {
      if (split_script_path(content_path) != 0)
         return false;
   } else if (is_chip8_path(content_path)) {
      if (set_default_script(JS2300_CORE_DEFAULT_SCRIPT_ROOT,
          JS2300_CORE_CHIP8_SCRIPT) != 0)
         return false;
   } else {
      return false;
   }
   js2300_config_init(&config);
   config.app_root = script_root;
   config.entry_script = script_entry;
   config.heap_bytes = JS2300_CORE_HEAP_BYTES;
   config.bytecode_cache_bytes = 512u * 1024u;
   if (unifrog_abi_application_memory_reserve_top(config.heap_bytes, 32u,
       &runtime_heap) == 0) {
      config.heap = runtime_heap;
      config.heap_external = 1;
      runtime_heap_reserved = 1;
   }
   configure_host(&host);

   printf("js2300 core load path=%s root=%s entry=%s heap=%u heap_source=%s ptr=0x%08lx\n",
      content_path, script_root, script_entry, (unsigned)config.heap_bytes,
      runtime_heap_reserved ? "appmem" : "module_heap",
      (unsigned long)(uintptr_t)runtime_heap);
   if (js2300_runtime_create(&config, &host, &runtime) != 0)
      goto fail_heap;
   if (js2300_runtime_start(runtime) != 0)
      goto fail;
   if (js2300_runtime_call_global_string(runtime, "retroLoad",
       content_path, &status) != 0)
      goto fail;
   frame_count = 0;
   framebuffer_dirty = 1;
   frame_presented = 0;
   return true;

fail:
   js2300_runtime_destroy(runtime);
   runtime = NULL;
fail_heap:
   if (runtime_heap_reserved)
      unifrog_abi_application_memory_release_top(runtime_heap);
   runtime_heap = NULL;
   return false;
}

void retro_run(void)
{
   int status = 0;

   poll_buttons();
   frame_presented = 0;
   if (runtime &&
       js2300_runtime_call_global0(runtime, "retroRun", &status) != 0) {
      core_fill_rect(NULL, 0, 0, JS2300_CORE_WIDTH, JS2300_CORE_HEIGHT,
         0xf800);
      frame_presented = 0;
   }
   if (!frame_presented && framebuffer_dirty)
      core_present(NULL);
   frame_count++;
}

void retro_unload_game(void)
{
   int status = 0;

   if (runtime) {
      (void)js2300_runtime_call_global0(runtime, "retroUnload", &status);
      js2300_runtime_destroy(runtime);
      runtime = NULL;
   }
   if (runtime_heap) {
      unifrog_abi_application_memory_release_top(runtime_heap);
      runtime_heap = NULL;
   }
   content_path[0] = '\0';
   script_root[0] = '\0';
   script_entry[0] = '\0';
   framebuffer_dirty = 0;
   frame_presented = 0;
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   (void)data;
   return size == 0;
}

bool retro_unserialize(const void *data, size_t size)
{
   (void)data;
   return size == 0;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}
