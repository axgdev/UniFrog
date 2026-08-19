#include "unifrog_libretro_internal.h"

/* Private libretro runtime state helpers and callbacks. */

const struct quick_memory_file quick_memory_files[] = {
   { RETRO_MEMORY_SAVE_RAM, "srm" },
   { RETRO_MEMORY_RTC, "rtc" },
};

const unsigned quick_backlight_levels[] = {
   1, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
};

size_t libretro_content_read_chunk(void)
{
   return UNIFROG_SD_EXPERIMENTAL ?
      LIBRETRO_CONTENT_READ_CHUNK_EXPERIMENTAL :
      LIBRETRO_CONTENT_READ_CHUNK;
}

unsigned libretro_storage_attempts(void)
{
   return 3u;
}

int libretro_recover_storage(const char *tag)
{
   return unifrog_storage_recover_after_io_error(tag,
      LIBRETRO_STORAGE_RECOVER_ATTEMPTS, LIBRETRO_STORAGE_RECOVER_DELAY_MS);
}

int libretro_log_flush_force_if_safe(void)
{
   if (UNIFROG_SD_EXPERIMENTAL)
      return 0;
   if (host.quick_core) {
      unifrog_log_note_storage_quiet(LIBRETRO_STORAGE_POST_CORE_QUIET_MS);
      return 0;
   }
   return unifrog_log_flush_force();
}

void unifrog_libretro_run_options_init(
   struct unifrog_libretro_run_options *options)
{
   if (!options)
      return;

   options->audio_enabled = 1;
   options->audio_gain = LIBRETRO_AUDIO_DEFAULT_GAIN;
   options->scpu_mhz = 0;
   options->ge_clock = -1;
   options->backlight_level = -1;
   options->frameskip = UNIFROG_LIBRETRO_FRAMESKIP_OFF;
   options->display_mode = UNIFROG_LIBRETRO_DISPLAY_FIT;
   options->framebuffer_format = UNIFROG_LIBRETRO_FB_RGB565;
   options->input_profile = UNIFROG_LIBRETRO_INPUT_DEFAULT;
   options->state_auto_load = 0;
   options->state_auto_save = 0;
   options->rtc_offset_minutes = 0;
   options->state_slot = 0;
   options->max_frames = 0;
   options->core_id[0] = '\0';
   options->core_path[0] = '\0';
}

static enum unifrog_ge_clock sanitize_ge_clock(int clock)
{
   switch (clock) {
   case UNIFROG_GE_CLOCK_198MHZ:
   case UNIFROG_GE_CLOCK_148MHZ:
   case UNIFROG_GE_CLOCK_225MHZ:
   case UNIFROG_GE_CLOCK_238MHZ:
      return (enum unifrog_ge_clock)clock;
   default:
      return UNIFROG_GE_CLOCK_FAST;
   }
}

static int sanitize_frameskip(int frameskip)
{
   switch (frameskip) {
   case UNIFROG_LIBRETRO_FRAMESKIP_OFF:
   case UNIFROG_LIBRETRO_FRAMESKIP_AUTO:
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1:
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2:
      return frameskip;
   default:
      return UNIFROG_LIBRETRO_FRAMESKIP_OFF;
   }
}

static int sanitize_display_mode(int display_mode)
{
   switch (display_mode) {
   case UNIFROG_LIBRETRO_DISPLAY_FIT:
   case UNIFROG_LIBRETRO_DISPLAY_STRETCH:
   case UNIFROG_LIBRETRO_DISPLAY_ORIGINAL:
      return display_mode;
   default:
      return UNIFROG_LIBRETRO_DISPLAY_FIT;
   }
}

unsigned sanitize_fast_forward_multiplier(unsigned multiplier)
{
   switch (multiplier) {
   case 0:
   case 2:
   case 4:
   case 8:
   case 16:
      return multiplier;
   default:
      return 0;
   }
}

const char *display_mode_label(int display_mode)
{
   switch (display_mode) {
   case UNIFROG_LIBRETRO_DISPLAY_STRETCH:
      return "STRETCH";
   case UNIFROG_LIBRETRO_DISPLAY_ORIGINAL:
      return "ORIGINAL";
   default:
      return "FIT";
   }
}

static int sanitize_framebuffer_format(int framebuffer_format)
{
   switch (framebuffer_format) {
   case UNIFROG_LIBRETRO_FB_RGB565:
   case UNIFROG_LIBRETRO_FB_XRGB8888:
      return framebuffer_format;
   default:
      return UNIFROG_LIBRETRO_FB_RGB565;
   }
}

const char *framebuffer_format_label(int framebuffer_format)
{
   return sanitize_framebuffer_format(framebuffer_format) ==
      UNIFROG_LIBRETRO_FB_XRGB8888 ? "XRGB8888" : "RGB565";
}

static bool host_pixel_format_allowed(enum retro_pixel_format format)
{
   if (format == RETRO_PIXEL_FORMAT_RGB565)
      return true;
   if (format == RETRO_PIXEL_FORMAT_XRGB8888)
      return host.framebuffer_format == UNIFROG_LIBRETRO_FB_XRGB8888;
   return false;
}

int sanitize_input_profile(int input_profile)
{
   switch (input_profile) {
   case UNIFROG_LIBRETRO_INPUT_DEFAULT:
   case UNIFROG_LIBRETRO_INPUT_RETROARCH:
   case UNIFROG_LIBRETRO_INPUT_GENESIS:
   case UNIFROG_LIBRETRO_INPUT_SWAP_AB:
   case UNIFROG_LIBRETRO_INPUT_SWAP_XY:
      return input_profile;
   default:
      return UNIFROG_LIBRETRO_INPUT_DEFAULT;
   }
}

static int input_profile_from_text(const char *text, int fallback)
{
   if (!text || !text[0])
      return sanitize_input_profile(fallback);
   if (strcasecmp(text, "default") == 0 ||
       strcasecmp(text, "stock") == 0)
      return UNIFROG_LIBRETRO_INPUT_DEFAULT;
   if (strcasecmp(text, "retroarch") == 0 ||
       strcasecmp(text, "ra") == 0)
      return UNIFROG_LIBRETRO_INPUT_RETROARCH;
   if (strcasecmp(text, "genesis") == 0 ||
       strcasecmp(text, "md") == 0)
      return UNIFROG_LIBRETRO_INPUT_GENESIS;
   if (strcasecmp(text, "swap_ab") == 0 ||
       strcasecmp(text, "swap-ab") == 0)
      return UNIFROG_LIBRETRO_INPUT_SWAP_AB;
   if (strcasecmp(text, "swap_xy") == 0 ||
       strcasecmp(text, "swap-xy") == 0)
      return UNIFROG_LIBRETRO_INPUT_SWAP_XY;
   return sanitize_input_profile(fallback);
}

const char *input_profile_opt_value(int input_profile)
{
   switch (sanitize_input_profile(input_profile)) {
   case UNIFROG_LIBRETRO_INPUT_RETROARCH:
      return "retroarch";
   case UNIFROG_LIBRETRO_INPUT_GENESIS:
      return "genesis";
   case UNIFROG_LIBRETRO_INPUT_SWAP_AB:
      return "swap_ab";
   case UNIFROG_LIBRETRO_INPUT_SWAP_XY:
      return "swap_xy";
   default:
      return "default";
   }
}

unsigned present_flags_for_display_mode(int display_mode)
{
   switch (display_mode) {
   case UNIFROG_LIBRETRO_DISPLAY_STRETCH:
      return 0;
   case UNIFROG_LIBRETRO_DISPLAY_ORIGINAL:
      return UNIFROG_PRESENT_KEEP_SIZE;
   default:
      return UNIFROG_PRESENT_KEEP_ASPECT;
   }
}

void host_configure_options(
   const struct unifrog_libretro_run_options *options)
{
   unifrog_libretro_run_options_init(&host.options);
   if (options)
      host.options = *options;

   host.options.audio_enabled = host.options.audio_enabled == 0 ? 0 : 1;
   host.options.audio_gain =
      unifrog_libretro_policy_audio_gain(host.options.audio_gain);
   if (!unifrog_libretro_policy_cpu_valid(host.options.scpu_mhz))
      host.options.scpu_mhz = 0;
   host.ge_clock = sanitize_ge_clock(host.options.ge_clock);
   host.options.ge_clock = (int)host.ge_clock;
   if (host.options.backlight_level > 100)
      host.options.backlight_level = 100;
   host.options.frameskip = sanitize_frameskip(host.options.frameskip);
   host.options.display_mode = sanitize_display_mode(host.options.display_mode);
   host.options.framebuffer_format =
      sanitize_framebuffer_format(host.options.framebuffer_format);
   host.options.input_profile =
      sanitize_input_profile(host.options.input_profile);
   if (host.options.state_slot >= LIBRETRO_STATE_SLOT_COUNT)
      host.options.state_slot = 0;
   host.options.state_auto_load = host.options.state_auto_load ? 1 : 0;
   host.options.state_auto_save = host.options.state_auto_save ? 1 : 0;
   if (host.options.rtc_offset_minutes < -5270400 ||
       host.options.rtc_offset_minutes > 5270400)
      host.options.rtc_offset_minutes = 0;
   host.options.core_id[sizeof(host.options.core_id) - 1] = '\0';

   host.audio_enabled = host.options.audio_enabled;
   host.audio_gain = host.options.audio_gain;
   host.scpu_target_mhz = host.options.scpu_mhz;
   host.display_mode = host.options.display_mode;
   host.framebuffer_format = host.options.framebuffer_format;
   host.input_profile = host.options.input_profile;
   host.input_profile_dirty = 0;
   host.quick_state_slot = host.options.state_slot;
   host.quick_state_auto_load = host.options.state_auto_load;
   host.quick_state_auto_save = host.options.state_auto_save;
   host.fast_forward_multiplier = 0;
   unifrog_battery_status_init(&host.battery);
   host.battery_check_ms = unifrog_perf_time_ms() - 10000u;
}

uintptr_t host_read_gp(void)
{
#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN
   return 0;
#else
   uintptr_t gp;

   __asm__ volatile("move %0, $28" : "=r"(gp));
   return gp;
#endif
}

static void host_restore_gp(uintptr_t gp)
{
#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN
   (void)gp;
#else
   __asm__ volatile("move $28, %0" :: "r"(gp) : "memory");
#endif
}

uintptr_t host_expected_gp(void)
{
#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN
   return 0;
#else
   return (uintptr_t)_gp;
#endif
}

void host_force_expected_gp(void)
{
   host_restore_gp(host_expected_gp());
}

uintptr_t core_call_gp(const struct libretro_core_api *core)
{
   return core && core->call_gp ? core->call_gp : host_expected_gp();
}

void libretro_activity_set(const struct libretro_core_api *core,
   const char *path, uint32_t phase, uint32_t marker)
{
   uint32_t core_hash = unifrog_exception_activity_hash(
      core && core->id ? core->id : "");
   uint32_t path_hash = unifrog_exception_activity_hash(path ? path : "");

   unifrog_exception_activity_set(phase, marker, core_hash, path_hash);
}

void libretro_activity_set_core_call(
   const struct libretro_core_api *core, uint32_t phase, uint32_t marker,
   const void *fn)
{
   unifrog_exception_activity_set(phase, marker,
      (uint32_t)(uintptr_t)fn, (uint32_t)core_call_gp(core));
}

#include "unifrog_libretro_core_call.h"

void host_apply_runtime_options(void)
{
   if (host.options.backlight_level >= 0) {
      host.backlight_restore_valid =
         unifrog_backlight_get(&host.backlight_restore_level) == 0;
      host.backlight_apply_ret =
         unifrog_backlight_set((unsigned)host.options.backlight_level);
      printf("unifrog libretro backlight target=%d ret=%d restore_valid=%d restore=%u\n",
         host.options.backlight_level, host.backlight_apply_ret,
         host.backlight_restore_valid, host.backlight_restore_level);
   }

   if (host.scpu_target_mhz) {
      host.scpu_restore_valid =
         unifrog_scpu_capture(&host.scpu_restore) == 0 &&
         host.scpu_restore.valid;
      printf("unifrog libretro scpu before target=%u restore_valid=%d current=%u selector=%u pll=%u\n",
         host.scpu_target_mhz, host.scpu_restore_valid,
         host.scpu_restore.mhz, host.scpu_restore.selector,
         host.scpu_restore.pll_enabled);
      (void)unifrog_log_flush();
      host.scpu_apply_ret = unifrog_scpu_apply_mhz(host.scpu_target_mhz);
      printf("unifrog libretro scpu after target=%u ret=%d current=%u restore_valid=%d\n",
         host.scpu_target_mhz, host.scpu_apply_ret,
         unifrog_scpu_current_mhz(), host.scpu_restore_valid);
   }
}

void host_restore_runtime_options(void)
{
   if (host.scpu_restore_valid) {
      int ret = unifrog_scpu_restore(&host.scpu_restore);

      printf("unifrog libretro scpu restore ret=%d current=%u target=%u original=%u\n",
         ret, unifrog_scpu_current_mhz(), host.scpu_target_mhz,
         host.scpu_restore.mhz);
   }

   if (host.backlight_restore_valid) {
      int ret = unifrog_backlight_set(host.backlight_restore_level);

      printf("unifrog libretro backlight restore ret=%d level=%u target=%d\n",
         ret, host.backlight_restore_level, host.options.backlight_level);
   }
}

static void quick_sanitize_component(char *text)
{
   if (!text)
      return;

   for (; *text; text++) {
      char c = *text;

      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')
         continue;
      *text = '_';
   }
}

static void quick_save_component(const char *rom_path, char *out,
   size_t out_size)
{
   const char *base;
   char *dot;

   if (!out || out_size == 0)
      return;

   base = rom_path ? strrchr(rom_path, '/') : NULL;
   base = base ? base + 1 : rom_path;
   if (!base || !base[0])
      base = "game";
   snprintf(out, out_size, "%s", base);
   dot = strrchr(out, '.');
   if (dot && dot != out)
      *dot = '\0';
   quick_sanitize_component(out);
}

static void quick_memory_path(const struct libretro_core_api *core,
   const char *rom_path, const char *extension, char *out, size_t out_size)
{
   char name[80];

   if (!out || out_size == 0)
      return;
   quick_save_component(rom_path, name, sizeof(name));
   snprintf(out, out_size, "%s/%s-%s.%s", LIBRETRO_SAVE_DIR,
      core && core->id ? core->id : "core", name,
      extension && extension[0] ? extension : "sav");
}

static void quick_state_path(const struct libretro_core_api *core,
   const char *rom_path, unsigned slot, char *out, size_t out_size)
{
   char extension[16];

   if (slot >= LIBRETRO_STATE_SLOT_COUNT)
      slot = LIBRETRO_STATE_SLOT_COUNT - 1u;
   snprintf(extension, sizeof(extension), "state%u", slot);
   quick_memory_path(core, rom_path, extension, out, out_size);
}

static int quick_write_buffer_atomic(const char *path, const void *data,
   size_t size, const char *tag)
{
   (void)mkdir(UNIFROG_DIST_ROOT, 0777);
   (void)mkdir(LIBRETRO_SAVE_DIR, 0777);
   return unifrog_storage_write_atomic(path, NULL, data, size, tag,
      libretro_storage_attempts(), LIBRETRO_STORAGE_RECOVER_DELAY_MS);
}

static int quick_memory_data(const struct libretro_core_api *core,
   unsigned id, void **data, size_t *size)
{
   if (data)
      *data = NULL;
   if (size)
      *size = 0;
   if (!core || !core->get_memory_data || !core->get_memory_size)
      return -1;

   if (size)
      *size = CORE_CALL1_RET(core, core->get_memory_size, id);
   if (data)
      *data = CORE_CALL1_RET(core, core->get_memory_data, id);
   if (!data || !size || !*data || *size == 0 ||
       *size > LIBRETRO_MEMORY_FILE_MAX)
      return -1;
   return 0;
}

static int quick_save_memory_file(const struct libretro_core_api *core,
   const char *rom_path, unsigned id, const char *extension, int manual)
{
   char path[UNIFROG_LIBRETRO_CONTENT_PATH_MAX];
   void *data;
   size_t size;
   int ok;

   if (quick_memory_data(core, id, &data, &size) != 0) {
      if (manual)
         snprintf(host.quick_status, sizeof(host.quick_status),
            "SAVE RAM UNSUPPORTED");
      return -1;
   }

   quick_memory_path(core, rom_path, extension, path, sizeof(path));
   ok = quick_write_buffer_atomic(path, data, size, "save_memory") == 0;
   if (manual)
      snprintf(host.quick_status, sizeof(host.quick_status),
         ok ? "SAVE RAM WRITTEN" : "SAVE RAM FAILED");
   printf("unifrog quick save_memory id=%u path=%s size=%u ok=%d manual=%d\n",
      id, path, (unsigned)size, ok, manual ? 1 : 0);
   if (manual)
      (void)libretro_log_flush_force_if_safe();
   return ok ? 0 : -1;
}

static int quick_load_memory_file(const struct libretro_core_api *core,
   const char *rom_path, unsigned id, const char *extension, int manual)
{
   char path[UNIFROG_LIBRETRO_CONTENT_PATH_MAX];
   FILE *file;
   void *data;
   size_t size;
   size_t read_size;
   uint64_t start_us;
   uint64_t open_done_us;
   uint64_t read_done_us;
   int ok;
   unsigned attempts = libretro_storage_attempts();

   if (quick_memory_data(core, id, &data, &size) != 0) {
      if (manual)
         snprintf(host.quick_status, sizeof(host.quick_status),
            "SAVE RAM UNSUPPORTED");
      return -1;
   }

   quick_memory_path(core, rom_path, extension, path, sizeof(path));
   for (unsigned attempt = 0; attempt < attempts; attempt++) {
      int open_errno;
      int read_errno = 0;
      int had_error = 0;

      start_us = host_time_us();
      errno = 0;
      file = fopen(path, "rb");
      open_errno = errno;
      open_done_us = host_time_us();
      if (!file) {
         if (manual)
            snprintf(host.quick_status, sizeof(host.quick_status),
               "NO SAVE RAM FILE");
         printf("unifrog quick load_memory missing id=%u path=%s size=%u open_ms=%u manual=%d attempt=%u errno=%d\n",
            id, path, (unsigned)size,
            host_elapsed_ms(start_us, open_done_us), manual ? 1 : 0,
            attempt + 1u, open_errno);
         if (open_errno == ENOENT || attempt + 1u >= attempts)
            return -1;
         (void)libretro_recover_storage("load_memory_open");
         continue;
      }
      errno = 0;
      read_size = fread(data, 1, size, file);
      read_errno = errno;
      read_done_us = host_time_us();
      had_error = ferror(file) != 0;
      ok = read_size == size && !had_error;
      fclose(file);
      if (manual)
         snprintf(host.quick_status, sizeof(host.quick_status),
            ok ? "SAVE RAM LOADED" : "SAVE RAM READ FAILED");
      printf("unifrog quick load_memory id=%u path=%s size=%u read=%u ok=%d manual=%d open_ms=%u read_ms=%u total_ms=%u attempt=%u errno=%d error=%d\n",
         id, path, (unsigned)size, (unsigned)read_size, ok, manual ? 1 : 0,
         host_elapsed_ms(start_us, open_done_us),
         host_elapsed_ms(open_done_us, read_done_us),
         host_elapsed_ms(start_us, read_done_us), attempt + 1u,
         read_errno, had_error);
      if (ok) {
         if (manual)
            (void)libretro_log_flush_force_if_safe();
         return 0;
      }
      if (!had_error || attempt + 1u >= attempts)
         break;
      (void)libretro_recover_storage("load_memory_read");
   }
   if (manual)
      (void)libretro_log_flush_force_if_safe();
   return -1;
}

void quick_load_all_memory_files(const struct libretro_core_api *core,
   const char *rom_path)
{
   (void)quick_load_memory_file(core, rom_path, RETRO_MEMORY_SAVE_RAM,
      "srm", 0);
   (void)quick_load_memory_file(core, rom_path, RETRO_MEMORY_RTC,
      "rtc", 0);
}

void quick_save_all_memory_files(const struct libretro_core_api *core,
   const char *rom_path)
{
   (void)quick_save_memory_file(core, rom_path, RETRO_MEMORY_SAVE_RAM,
      "srm", 0);
   (void)quick_save_memory_file(core, rom_path, RETRO_MEMORY_RTC,
      "rtc", 0);
}

int quick_save_state_file(void)
{
   const struct libretro_core_api *core = host.quick_core;
   const char *rom_path = host.quick_rom_path;
   unsigned slot = host.quick_state_slot;
   char path[UNIFROG_LIBRETRO_CONTENT_PATH_MAX];
   void *data = NULL;
   size_t size;
   int ok;
   int ret = -1;

   if (!core || !core->serialize_size || !core->serialize) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE UNSUPPORTED");
      printf("unifrog quick save_state unsupported_api core=%s serialize_size=%p serialize=%p slot=%u\n",
         core && core->id ? core->id : "(none)",
         core ? (void *)core->serialize_size : NULL,
         core ? (void *)core->serialize : NULL, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   size = CORE_CALL0_RET(core, core->serialize_size);
   if (size == 0 || size > LIBRETRO_STATE_FILE_MAX) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         size == 0 ? "STATE UNSUPPORTED" : "STATE TOO LARGE");
      printf("unifrog quick save_state unsupported core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   data = malloc(size);
   if (!data) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE OUT OF MEMORY");
      printf("unifrog quick save_state oom core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   ok = CORE_CALL2_RET(core, core->serialize, data, size);
   if (!ok) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE SERIALIZE FAILED");
      goto out;
   }

   quick_state_path(core, rom_path, slot, path, sizeof(path));
   ok = quick_write_buffer_atomic(path, data, size, "save_state") == 0;
   snprintf(host.quick_status, sizeof(host.quick_status),
      ok ? "STATE SAVED" : "STATE WRITE FAILED");
   ret = ok ? (int)slot : -1;
   printf("unifrog quick save_state core=%s path=%s size=%u slot=%u ok=%d\n",
      core->id, path, (unsigned)size, slot, ok);

out:
   free(data);
   (void)libretro_log_flush_force_if_safe();
   return ret;
}

int quick_load_state_file(void)
{
   const struct libretro_core_api *core = host.quick_core;
   const char *rom_path = host.quick_rom_path;
   unsigned slot = host.quick_state_slot;
   char path[UNIFROG_LIBRETRO_CONTENT_PATH_MAX];
   FILE *file = NULL;
   void *data = NULL;
   uint8_t *file_data = NULL;
   struct quick_state_file_header header;
   size_t size;
   size_t read_size;
   size_t file_size;
   int extra;
   int ok;
   int ret = -1;

   if (!core || !core->serialize_size || !core->unserialize) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE UNSUPPORTED");
      printf("unifrog quick load_state unsupported_api core=%s serialize_size=%p unserialize=%p slot=%u\n",
         core && core->id ? core->id : "(none)",
         core ? (void *)core->serialize_size : NULL,
         core ? (void *)core->unserialize : NULL, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   size = CORE_CALL0_RET(core, core->serialize_size);
   if (size == 0 || size > LIBRETRO_STATE_FILE_MAX) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         size == 0 ? "STATE UNSUPPORTED" : "STATE TOO LARGE");
      printf("unifrog quick load_state unsupported core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   data = malloc(size);
   if (!data) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "STATE OUT OF MEMORY");
      printf("unifrog quick load_state oom core=%s size=%u slot=%u\n",
         core->id, (unsigned)size, slot);
      (void)libretro_log_flush_force_if_safe();
      return -1;
   }

   quick_state_path(core, rom_path, slot, path, sizeof(path));
   file = fopen(path, "rb");
   if (!file) {
      snprintf(host.quick_status, sizeof(host.quick_status),
         "NO STATE FILE");
      goto out;
   }

   read_size = fread(&header, 1, sizeof(header), file);
   if (read_size == sizeof(header) && header.magic == LIBRETRO_STATE_MAGIC &&
       header.version == LIBRETRO_STATE_VERSION &&
       header.raw_size == (uint32_t)size &&
       header.data_size > 0 && header.data_size <= LIBRETRO_STATE_FILE_MAX &&
       header.data_size <= LIBRETRO_STATE_FILE_MAX - sizeof(header)) {
      file_size = header.data_size;
      file_data = malloc(file_size);
      if (!file_data) {
         snprintf(host.quick_status, sizeof(host.quick_status),
            "STATE OUT OF MEMORY");
         printf("unifrog quick load_state oom compressed core=%s path=%s size=%u slot=%u data=%u\n",
            core->id, path, (unsigned)size, slot, (unsigned)file_size);
         goto out;
      }
      read_size = fread(file_data, 1, file_size, file);
      extra = fgetc(file);
      ok = read_size == file_size && extra == EOF && ferror(file) == 0;
      fclose(file);
      file = NULL;
      if (!ok) {
         snprintf(host.quick_status, sizeof(host.quick_status),
            "STATE SIZE MISMATCH");
         printf("unifrog quick load_state read_failed compressed core=%s path=%s size=%u read=%u slot=%u extra=%d\n",
            core->id, path, (unsigned)file_size, (unsigned)read_size, slot,
            extra);
         goto out;
      }
      if (header.flags & LIBRETRO_STATE_FLAG_COMPRESSED) {
         uLongf raw_size = (uLongf)size;

         ok = uncompress(data, &raw_size, file_data, (uLongf)file_size) == Z_OK &&
            raw_size == (uLongf)size;
         if (!ok) {
            snprintf(host.quick_status, sizeof(host.quick_status),
               "STATE DECODE FAILED");
            printf("unifrog quick load_state decode_failed core=%s path=%s size=%u compressed=%u slot=%u\n",
               core->id, path, (unsigned)size, (unsigned)file_size, slot);
            goto out;
         }
      } else {
         if (file_size != size) {
            snprintf(host.quick_status, sizeof(host.quick_status),
               "STATE SIZE MISMATCH");
            printf("unifrog quick load_state raw_size_mismatch core=%s path=%s size=%u read=%u slot=%u\n",
               core->id, path, (unsigned)size, (unsigned)file_size, slot);
            goto out;
         }
         memcpy(data, file_data, size);
      }
   } else {
      if (fseek(file, 0, SEEK_SET) != 0) {
         snprintf(host.quick_status, sizeof(host.quick_status),
            "STATE READ FAILED");
         goto out;
      }
      read_size = fread(data, 1, size, file);
      extra = fgetc(file);
      ok = read_size == size && extra == EOF && ferror(file) == 0;
      fclose(file);
      file = NULL;
      if (!ok) {
         snprintf(host.quick_status, sizeof(host.quick_status),
            "STATE SIZE MISMATCH");
         printf("unifrog quick load_state read_failed core=%s path=%s size=%u read=%u slot=%u extra=%d\n",
            core->id, path, (unsigned)size, (unsigned)read_size, slot, extra);
         goto out;
      }
   }

   ok = CORE_CALL2_RET(core, core->unserialize, data, size);
   snprintf(host.quick_status, sizeof(host.quick_status),
      ok ? "STATE LOADED" : "STATE LOAD FAILED");
   if (ok)
      ret = (int)slot;
   printf("unifrog quick load_state core=%s path=%s size=%u slot=%u ok=%d\n",
      core->id, path, (unsigned)size, slot, ok);

out:
   if (file)
      fclose(file);
   free(data);
   free(file_data);
   (void)libretro_log_flush_force_if_safe();
   return ret;
}

static unsigned libretro_watchdog_load_stall_polls(void)
{
   return UNIFROG_SD_EXPERIMENTAL ? 120u :
      LIBRETRO_WATCHDOG_LOAD_STALL_POLLS;
}

static void libretro_watchdog_task(void *arg)
{
   unsigned stable_polls = 0;
   unsigned last_phase = 0;
   unsigned last_marker = 0;
   unsigned last_heartbeat = 0;

   (void)arg;

   while (watchdog_active) {
      unifrog_task_delay_ms(LIBRETRO_WATCHDOG_MS);
      if (!watchdog_phase) {
         stable_polls = 0;
         last_phase = 0;
         last_marker = watchdog_marker;
         last_heartbeat = watchdog_heartbeat;
         continue;
      }
      if (watchdog_phase == last_phase &&
          watchdog_marker == last_marker &&
          watchdog_heartbeat == last_heartbeat) {
         stable_polls++;
      } else {
         stable_polls = 0;
         last_phase = watchdog_phase;
         last_marker = watchdog_marker;
         last_heartbeat = watchdog_heartbeat;
      }
      if (watchdog_phase == LIBRETRO_WATCHDOG_PHASE_LOAD &&
          stable_polls >= libretro_watchdog_load_stall_polls()) {
         unifrog_panic_screen_labeled("UNIFROG CORE HANG",
            "PHASE", watchdog_phase,
            "MARK", watchdog_marker,
            "BEAT", watchdog_heartbeat,
            "STABLE", stable_polls);
      } else if (watchdog_phase == LIBRETRO_WATCHDOG_PHASE_RUN &&
                 stable_polls >= LIBRETRO_WATCHDOG_RUN_STALL_POLLS) {
         unifrog_panic_screen_labeled("UNIFROG CORE HANG",
            "PHASE", watchdog_phase,
            "FRAME", watchdog_marker,
            "BEAT", watchdog_heartbeat,
            "STABLE", stable_polls);
      }
   }

   return;
}

void libretro_watchdog_start(void)
{
   watchdog_active = 1;
   watchdog_phase = 0;
   watchdog_marker = 0;
   watchdog_heartbeat = 1;
   if (unifrog_task_create(libretro_watchdog_task, NULL, "lr_wdog",
       UNIFROG_TASK_PRIORITY_HIGH, NULL) != 0) {
      watchdog_active = 0;
      printf("unifrog libretro watchdog_create_failed\n");
   }
}

void libretro_watchdog_stop(void)
{
   watchdog_phase = 0;
   watchdog_active = 0;
}

void libretro_watchdog_enter(unsigned phase, unsigned marker)
{
   watchdog_marker = marker;
   watchdog_phase = phase;
   watchdog_heartbeat++;
}

void libretro_watchdog_leave(void)
{
   watchdog_phase = 0;
   watchdog_heartbeat++;
}

static unsigned load_stage_hash(const char *stage)
{
   unsigned hash = 2166136261u;

   if (stage) {
      while (*stage) {
         hash ^= (unsigned)(uint8_t)*stage++;
         hash *= 16777619u;
      }
   }
   return hash;
}

static unsigned load_progress_marker(const char *stage, unsigned current,
   unsigned total)
{
   unsigned hash = load_stage_hash(stage);
   unsigned percent = total ? (unsigned)(((uint64_t)current * 100u) / total) :
      0xffu;

   if (percent > 100u)
      percent = 100u;

   return ((hash & 0xffu) << 24) | ((percent & 0xffu) << 16) |
      (current & 0xffffu);
}

void libretro_watchdog_load_progress(const char *stage,
   unsigned current, unsigned total)
{
   if (watchdog_phase != LIBRETRO_WATCHDOG_PHASE_LOAD)
      return;

   watchdog_marker = load_progress_marker(stage, current, total);
   watchdog_heartbeat++;
}

void unifrog_core_load_progress(const char *stage, unsigned current,
   unsigned total)
{
   char detail[64];
   unsigned stage_hash;
   unsigned percent;
   unsigned percent_bucket;
   int log_progress = 0;

   host_force_expected_gp();
   percent = total ? (unsigned)(((uint64_t)current * 100u) / total) :
      host.loading_percent;
   if (percent > 100u)
      percent = 100u;
   host.loading_percent = percent;
   stage_hash = load_stage_hash(stage);
   libretro_watchdog_load_progress(stage, current, total);
   if (stage && stage[0]) {
      if (total)
         snprintf(detail, sizeof(detail), "%s %u%%", stage, percent);
      else
         snprintf(detail, sizeof(detail), "%s", stage);
   } else if (total) {
      snprintf(detail, sizeof(detail), "%u%%", percent);
   } else {
      snprintf(detail, sizeof(detail), "CORE LOAD");
   }
   loading_draw(host.loading_title[0] ? host.loading_title : "LOADING GAME",
      detail, percent);

   percent_bucket = total ? percent / LIBRETRO_LOAD_LOG_PERCENT_STEP : 0;
   if (stage_hash != host.loading_log_stage_hash ||
       !total || current == 0 || current == total ||
       percent_bucket != host.loading_log_percent_bucket) {
      host.loading_log_stage_hash = stage_hash;
      host.loading_log_percent_bucket = percent_bucket;
      log_progress = 1;
   }

   if (log_progress) {
      printf("unifrog libretro load_progress stage=%s current=%u total=%u percent=%u\n",
         stage && stage[0] ? stage : "?", current, total, percent);
      if (watchdog_phase == LIBRETRO_WATCHDOG_PHASE_LOAD)
         (void)unifrog_log_flush();
   }
}

static void loading_draw_frame(const char *title, const char *detail,
   unsigned percent)
{
   static const char spin[] = "|/-\\";
   struct unifrog_surface surface;
   unsigned buffer = 0;
   char detail_spin[80];
   int bar_x;
   int bar_y;
   int bar_w;
   int bar_h;
   int fill_w;

   if (!host.loading_open) {
      if (unifrog_fb_open(&host.loading_fb,
          UNIFROG_FB_OPEN_BUFFERS_2) != 0)
         return;
      if (unifrog_fb_set_buffer_count(&host.loading_fb, 2) != 0)
         (void)unifrog_fb_set_buffer_count(&host.loading_fb, 1);
      host.loading_open = 1;
   }

   if (percent > 100)
      percent = 100;
   libretro_watchdog_load_progress(detail ? detail : title, percent, 100);
   buffer = host.loading_fb.current_buffer;
   if (host.loading_fb.buffer_count > 1)
      buffer = (host.loading_fb.current_buffer + 1) % host.loading_fb.buffer_count;
   surface = unifrog_fb_surface_for_buffer(&host.loading_fb, buffer);
   unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height,
      UNIFROG_RGB565(0, 0, 0));
   unifrog_gfx_draw_text(&surface, 18, 54, title ? title : "LOADING",
      UNIFROG_RGB565(236, 241, 246), 2);
   if (detail && detail[0])
      snprintf(detail_spin, sizeof(detail_spin), "%c %s",
         spin[host.loading_anim_tick % 4u], detail);
   else
      snprintf(detail_spin, sizeof(detail_spin), "%c",
         spin[host.loading_anim_tick % 4u]);
   if (detail_spin[0])
      unifrog_gfx_draw_text(&surface, 18, 86, detail_spin,
         UNIFROG_RGB565(160, 174, 188), 1);

   bar_x = 18;
   bar_y = (int)surface.height - 54;
   bar_w = (int)surface.width - 36;
   bar_h = 14;
   fill_w = (bar_w - 4) * (int)percent / 100;
   unifrog_gfx_fill_rect(&surface, bar_x, bar_y, bar_w, bar_h,
      UNIFROG_RGB565(42, 50, 60));
   unifrog_gfx_fill_rect(&surface, bar_x + 2, bar_y + 2, fill_w, bar_h - 4,
      UNIFROG_RGB565(68, 188, 136));
   unifrog_fb_flush_buffer(&host.loading_fb, buffer);
   unifrog_fb_pan(&host.loading_fb, buffer);
}

void loading_task(void *arg)
{
   (void)arg;
   host.loading_task_running = 1;
   while (!host.loading_task_stop) {
      unifrog_task_delay_ms(250u);
      if (host.loading_open) {
         host.loading_anim_tick++;
         if (host.loading_visual_percent < 95u &&
             host.loading_visual_percent <= host.loading_percent + 12u)
            host.loading_visual_percent++;
         loading_draw_frame(host.loading_title, host.loading_detail,
            host.loading_visual_percent);
      }
   }
   host.loading_task_running = 0;
   return;
}

static void loading_start_task(void)
{
#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN
   return;
#else
   if (host.loading_task_running || host.loading_task)
      return;
   host.loading_task_stop = 0;
   if (unifrog_task_create(loading_task, NULL, "lr_load_anim",
       UNIFROG_TASK_PRIORITY_NORMAL, &host.loading_task) != 0) {
      host.loading_task = NULL;
      host.loading_task_running = 0;
   }
#endif
}

void loading_close(void)
{
   if (host.loading_task) {
      host.loading_task_stop = 1;
      while (host.loading_task_running)
         unifrog_task_delay_ms(10u);
      host.loading_task = NULL;
   }
   if (host.loading_open) {
      unifrog_fb_close(&host.loading_fb);
      host.loading_open = 0;
   }
}

void loading_draw(const char *title, const char *detail, unsigned percent)
{
   unsigned old_percent = host.loading_percent;

   if (percent > 100u)
      percent = 100u;
   host.loading_percent = percent;
   if (!host.loading_open || percent < old_percent ||
       host.loading_visual_percent < percent || percent >= 100u)
      host.loading_visual_percent = percent;
   unifrog_text_copy(host.loading_title, sizeof(host.loading_title),
      title ? title : "LOADING");
   unifrog_text_copy(host.loading_detail, sizeof(host.loading_detail),
      detail ? detail : "");
   loading_start_task();
   loading_draw_frame(title, detail, percent);
}

void unifrog_libretro_log_cb(enum retro_log_level level, const char *fmt, ...)
{
   char msg[192];
   enum unifrog_log_level unifrog_level;
   va_list ap;
   uintptr_t caller_gp = host_read_gp();

   host_force_expected_gp();
   if (!fmt) {
      host_restore_gp(caller_gp);
      return;
   }

   va_start(ap, fmt);
   vsnprintf(msg, sizeof(msg), fmt, ap);
   va_end(ap);
   switch (level) {
   case RETRO_LOG_DEBUG:
      unifrog_level = UNIFROG_LOG_DEBUG;
      break;
   case RETRO_LOG_WARN:
      unifrog_level = UNIFROG_LOG_WARN;
      break;
   case RETRO_LOG_ERROR:
      unifrog_level = UNIFROG_LOG_ERROR;
      break;
   default:
      unifrog_level = UNIFROG_LOG_INFO;
      break;
   }
   (void)unifrog_log_at(unifrog_level, "libretro.core", "%s", msg);
   host_restore_gp(caller_gp);
}

static void core_option_copy(char *dst, size_t size, const char *src)
{
   if (!dst || size == 0)
      return;
   if (!src)
      src = "";
   snprintf(dst, size, "%s", src);
}

static int core_option_find(const char *key)
{
   if (!key || !key[0])
      return -1;
   for (unsigned i = 0; i < host.core_option_count; i++) {
      if (strcmp(host.core_options[i].key, key) == 0)
         return (int)i;
   }
   return -1;
}

static void core_option_set_selected(struct quick_core_option *option,
   const char *value)
{
   if (!option || !value)
      return;
   for (unsigned i = 0; i < option->value_count; i++) {
      if (strcmp(option->values[i], value) == 0) {
         option->selected = i;
         return;
      }
   }
}

static void core_option_apply_value(const char *key, const char *value)
{
   int index;

   if (strcmp(key, "unifrog_keymap") == 0 ||
       strcmp(key, "input_profile") == 0) {
      host.input_profile = input_profile_from_text(value, host.input_profile);
      host.options.input_profile = host.input_profile;
      return;
   }
   index = core_option_find(key);
   if (index >= 0)
      core_option_set_selected(&host.core_options[index], value);
}

struct core_option_scope {
   char section[320];
};

static int core_option_config_entry(void *userdata, const char *section,
   const char *key, const char *value, unsigned line_number)
{
   const struct core_option_scope *scope = userdata;

   (void)line_number;
   if (strcmp(section, scope->section) == 0)
      core_option_apply_value(key, value);
   return 0;
}

static void core_options_load_unified_scope(const char *prefix,
   const char *target)
{
   struct core_option_scope scope;

   if (!target || !target[0])
      return;
   if (snprintf(scope.section, sizeof(scope.section), "%s%s", prefix,
       target) >= (int)sizeof(scope.section))
      return;
   (void)unifrog_config_read(UNIFROG_CONFIG_PATH, core_option_config_entry,
      &scope, NULL);
}

static void core_options_load_config(void)
{
   if (host.core_options_loaded || !host.core_id || !host.core_id[0])
      return;
   host.core_options_loaded = 1;
   core_options_load_unified_scope("core-options.", host.core_id);
   core_options_load_unified_scope("rom-options.", host.quick_rom_path);
   printf("unifrog core_options config_load core=%s count=%u\n",
      host.core_id, host.core_option_count);
}

static int core_options_write_config(FILE *file, void *userdata)
{
   (void)userdata;
   for (unsigned i = 0; i < host.core_option_count; i++) {
      const struct quick_core_option *option = &host.core_options[i];
      const char *value = option->selected < option->value_count ?
         option->values[option->selected] : "";
      if (option->key[0] && value[0]) {
         fprintf(file, "# %s; values:", option->label[0] ?
            option->label : option->key);
         for (unsigned j = 0; j < option->value_count; j++)
            fprintf(file, "%s%s", j ? ", " : " ", option->values[j]);
         fputc('\n', file);
         fprintf(file, "%s=%s\n", option->key, value);
      }
   }
   fprintf(file, "unifrog_keymap=%s\n",
      input_profile_opt_value(host.input_profile));
   return ferror(file) ? -1 : 0;
}

static int core_options_section(char *section, size_t size, int content_scope)
{
   const char *target = content_scope ? host.quick_rom_path : host.core_id;
   const char *prefix = content_scope ? "rom-options." : "core-options.";
   int written;

   if (!target || !target[0])
      return -1;
   written = snprintf(section, size, "%s%s", prefix, target);
   return written >= 0 && (size_t)written < size ? 0 : -1;
}

int core_options_save_scope(int content_scope)
{
   char section[320];
   int ret;

   if (core_options_section(section, sizeof(section), content_scope) != 0)
      return -1;
   (void)mkdir(UNIFROG_DATA_ROOT, 0777);
   ret = unifrog_config_replace_section(UNIFROG_CONFIG_PATH, section,
      core_options_write_config, NULL);
   if (ret != 0) {
      printf("unifrog core_options config_save_failed core=%s section=%s ret=%d\n",
         host.core_id, section, ret);
      return ret;
   }
   host.core_options_dirty = 0;
   host.input_profile_dirty = 0;
   printf("unifrog core_options config_save core=%s section=%s count=%u\n",
      host.core_id, section, host.core_option_count);
   return 0;
}

int core_options_clear_scope(int content_scope)
{
   char section[320];
   int ret;

   if (core_options_section(section, sizeof(section), content_scope) != 0)
      return -1;
   ret = unifrog_config_remove_section(UNIFROG_CONFIG_PATH, section);
   printf("unifrog core_options config_clear core=%s section=%s ret=%d\n",
      host.core_id, section, ret);
   return ret;
}

void core_options_reset(void)
{
   memset(host.core_options, 0, sizeof(host.core_options));
   host.core_option_count = 0;
   host.core_options_dirty = 0;
   host.core_options_loaded = 0;
   host.input_profile_dirty = 0;
}

static void core_option_add_value(struct quick_core_option *option,
   const char *value, const char *label)
{
   if (!option || !value || !value[0] ||
       option->value_count >= LIBRETRO_CORE_OPTION_VALUE_MAX)
      return;
   core_option_copy(option->values[option->value_count],
      sizeof(option->values[0]), value);
   core_option_copy(option->value_labels[option->value_count],
      sizeof(option->value_labels[0]), label && label[0] ? label : value);
   option->value_count++;
}

static void core_options_register_one(const char *key, const char *label,
   const struct retro_core_option_value *values, const char *default_value)
{
   struct quick_core_option *option;

   if (!key || !key[0] || host.core_option_count >= LIBRETRO_CORE_OPTION_MAX)
      return;
   if (core_option_find(key) >= 0)
      return;
   option = &host.core_options[host.core_option_count];
   memset(option, 0, sizeof(*option));
   core_option_copy(option->key, sizeof(option->key), key);
   core_option_copy(option->label, sizeof(option->label),
      label && label[0] ? label : key);
   option->visible = 1;
   if (values) {
      for (unsigned i = 0;
           i < RETRO_NUM_CORE_OPTION_VALUES_MAX &&
           values[i].value && option->value_count < LIBRETRO_CORE_OPTION_VALUE_MAX;
           i++)
         core_option_add_value(option, values[i].value, values[i].label);
   }
   if (option->value_count == 0)
      core_option_add_value(option, default_value ? default_value : "enabled",
         default_value ? default_value : "enabled");
   if (default_value)
      core_option_set_selected(option, default_value);
   host.core_option_count++;
}

static void core_options_register_definitions(
   const struct retro_core_option_definition *defs)
{
   if (!defs)
      return;
   for (unsigned i = 0; defs[i].key && i < LIBRETRO_CORE_OPTION_MAX; i++)
      core_options_register_one(defs[i].key, defs[i].desc,
         defs[i].values, defs[i].default_value);
   core_options_load_config();
   printf("unifrog core_options registered v1 core=%s count=%u\n",
      host.core_id ? host.core_id : "", host.core_option_count);
}

static void core_options_register_v2(
   const struct retro_core_option_v2_definition *defs)
{
   if (!defs)
      return;
   for (unsigned i = 0; defs[i].key && i < LIBRETRO_CORE_OPTION_MAX; i++)
      core_options_register_one(defs[i].key,
         defs[i].desc_categorized && defs[i].desc_categorized[0] ?
            defs[i].desc_categorized : defs[i].desc,
         defs[i].values, defs[i].default_value);
   core_options_load_config();
   printf("unifrog core_options registered v2 core=%s count=%u\n",
      host.core_id ? host.core_id : "", host.core_option_count);
}

static void core_options_register_variables(const struct retro_variable *vars)
{
   if (!vars)
      return;
   for (unsigned i = 0; vars[i].key && i < LIBRETRO_CORE_OPTION_MAX; i++) {
      struct retro_core_option_value values[LIBRETRO_CORE_OPTION_VALUE_MAX];
      char buffer[256];
      char *semi;
      char *token;
      unsigned count = 0;

      memset(values, 0, sizeof(values));
      core_option_copy(buffer, sizeof(buffer), vars[i].value);
      semi = strchr(buffer, ';');
      token = semi ? semi + 1 : buffer;
      while (*token == ' ')
         token++;
      while (token && *token && count < LIBRETRO_CORE_OPTION_VALUE_MAX - 1u) {
         char *next = strchr(token, '|');
         if (next)
            *next++ = '\0';
         while (*token == ' ')
            token++;
         if (*token) {
            values[count].value = token;
            values[count].label = token;
            count++;
         }
         token = next;
      }
      core_options_register_one(vars[i].key, buffer, values,
         count ? values[0].value : NULL);
   }
   core_options_load_config();
   printf("unifrog core_options registered variables core=%s count=%u\n",
      host.core_id ? host.core_id : "", host.core_option_count);
}

static bool host_get_variable(struct retro_variable *var)
{
   int option_index;

   if (!var || !var->key)
      return false;

   if (strcmp(var->key, "gpsp_drc") == 0) {
      var->value = "enabled";
      printf("unifrog libretro variable %s=%s\n", var->key, var->value);
      return true;
   }
   if (host.core_id && (strcmp(host.core_id, "gpsp") == 0 ||
       strcmp(host.core_id, "gpsp-gbac-prosty") == 0)) {
      if (strcmp(var->key, "gpsp_frameskip") == 0) {
         if (host.fast_forward && host.fast_forward_multiplier > 1)
            var->value = "fixed_interval";
         else if (host.options.frameskip == UNIFROG_LIBRETRO_FRAMESKIP_AUTO)
            var->value = "auto_threshold";
         else if (host.options.frameskip ==
                  UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1 ||
                  host.options.frameskip ==
                  UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2)
            var->value = "fixed_interval";
         else
            var->value = "disabled";
         printf("unifrog libretro variable %s=%s\n", var->key, var->value);
         return true;
      }
      if (strcmp(var->key, "gpsp_frameskip_threshold") == 0) {
         /*
          * v054/0012 showed gpSP auto mode skipping too late for Advance Wars:
          * the audio buffer dipped hard only after core time was already over
          * budget. 60 is the core's highest declared threshold and makes auto
          * protect pacing earlier without forcing the fixed 30 FPS mode.
          */
         var->value = "60";
         printf("unifrog libretro variable %s=%s\n", var->key, var->value);
         return true;
      }
      if (strcmp(var->key, "gpsp_frameskip_interval") == 0) {
         if (host.fast_forward && host.fast_forward_multiplier > 1) {
            switch (sanitize_fast_forward_multiplier(
               host.fast_forward_multiplier)) {
            case 16:
               var->value = "15";
               break;
            case 8:
               var->value = "7";
               break;
            case 4:
               var->value = "3";
               break;
            default:
               var->value = "1";
               break;
            }
         } else if (host.options.frameskip ==
             UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2)
            var->value = "2";
         else if (host.options.frameskip ==
                  UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1)
            var->value = "1";
         else
            var->value = "0";
         printf("unifrog libretro variable %s=%s\n", var->key, var->value);
         return true;
      }
   }

   option_index = core_option_find(var->key);
   if (option_index >= 0) {
      struct quick_core_option *option = &host.core_options[option_index];
      if (option->selected < option->value_count) {
         var->value = option->values[option->selected];
         printf("unifrog libretro variable %s=%s source=core_options\n",
            var->key, var->value);
         return true;
      }
   }

   return false;
}

bool unifrog_libretro_rumble_cb(unsigned port, enum retro_rumble_effect effect,
   uint16_t strength)
{
   (void)port;
   (void)effect;
   (void)strength;
   return false;
}

static uint16_t joypad_mask_from_buttons(uint32_t buttons)
{
   uint16_t mask = 0;
   enum unifrog_button b_button =
      host.input_profile == UNIFROG_LIBRETRO_INPUT_SWAP_AB ?
      UNIFROG_BUTTON_A : UNIFROG_BUTTON_B;
   enum unifrog_button a_button =
      host.input_profile == UNIFROG_LIBRETRO_INPUT_SWAP_AB ?
      UNIFROG_BUTTON_B : UNIFROG_BUTTON_A;
   enum unifrog_button y_button =
      host.input_profile == UNIFROG_LIBRETRO_INPUT_SWAP_XY ?
      UNIFROG_BUTTON_X : UNIFROG_BUTTON_Y;
   enum unifrog_button x_button =
      host.input_profile == UNIFROG_LIBRETRO_INPUT_SWAP_XY ?
      UNIFROG_BUTTON_Y : UNIFROG_BUTTON_X;

#define MAP_JOYPAD_BUTTON(retro_id, unifrog_id) do { \
   if (buttons & UNIFROG_BUTTON_MASK(unifrog_id)) \
      mask |= (uint16_t)(1u << (retro_id)); \
} while (0)
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_B, b_button);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_Y, y_button);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_SELECT, UNIFROG_BUTTON_SELECT);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_START, UNIFROG_BUTTON_START);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_UP, UNIFROG_BUTTON_UP);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_DOWN, UNIFROG_BUTTON_DOWN);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_LEFT, UNIFROG_BUTTON_LEFT);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_RIGHT, UNIFROG_BUTTON_RIGHT);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_A, a_button);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_X, x_button);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_L, UNIFROG_BUTTON_L);
   MAP_JOYPAD_BUTTON(RETRO_DEVICE_ID_JOYPAD_R, UNIFROG_BUTTON_R);
#undef MAP_JOYPAD_BUTTON
   return mask;
}

void unifrog_libretro_input_poll_cb(void)
{
   unsigned frame = host.run_frames + 1u;

   if (host.input_poll_frame != frame) {
      unifrog_input_save_previous();
      unifrog_input_poll_with_wireless_divisor(LIBRETRO_WIRELESS_POLL_DIVISOR);
      host.local_buttons = unifrog_input_local_buttons();
      host.buttons = unifrog_input_buttons();
      for (unsigned port = 0; port < UNIFROG_INPUT_MAX_PORTS; port++) {
         host.port_buttons[port] = host.local_buttons |
            unifrog_input_wireless_buttons(port);
         /*
          * Most cores query every libretro button separately. Convert the
          * platform mask once per poll so each callback is only a bit test.
          */
         host.port_joypad_masks[port] =
            joypad_mask_from_buttons(host.port_buttons[port]);
      }
      host.input_poll_frame = frame;
   }
}

int16_t unifrog_libretro_input_state_cb(unsigned port, unsigned device,
   unsigned index, unsigned id)
{
   uint16_t mask;
   unsigned frame = host.run_frames + 1u;

   (void)index;
   if (device != RETRO_DEVICE_JOYPAD)
      return 0;
   if (host.input_poll_frame != frame)
      unifrog_libretro_input_poll_cb();

   if (port < UNIFROG_INPUT_MAX_PORTS) {
      mask = host.port_joypad_masks[port];
   } else {
      return 0;
   }

   if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
      return (int16_t)mask;
   if (id > RETRO_DEVICE_ID_JOYPAD_R3)
      return 0;
   return (mask & (uint16_t)(1u << id)) ? 1 : 0;
}

static void host_audio_configure_write_policy(void)
{
   unsigned attempts = LIBRETRO_AUDIO_WRITE_ATTEMPTS;
   unsigned poll_ms = LIBRETRO_AUDIO_WRITE_POLL_MS;
   unsigned rate = host.audio_output_rate ? host.audio_output_rate :
      DEFAULT_SAMPLE_RATE;
   int qpsx = host.core_id && strcmp(host.core_id, "qpsx") == 0;

   if (qpsx)
      attempts = 1;
   if (qpsx)
      poll_ms = 0;
   /*
    * Do not lengthen AUDSINK retries merely because GB300 uses stereo
    * transport. A rejected stereo write otherwise stalls the emulation thread
    * for roughly 50ms. The direct GB300 SND fallback enforces its own longer
    * retry budget in unifrog_audio_write_timeout().
    */
   host.audio_write_attempts = attempts;
   host.audio_write_poll_ms = poll_ms;
   host.audio_gate_close_frames =
      (unsigned)(((uint64_t)rate * LIBRETRO_AUDIO_GATE_CLOSE_MS) / 1000u);
}

unsigned host_audio_output_rate(unsigned input_rate)
{
   if (input_rate >= LIBRETRO_AUDIO_MIN_OUTPUT_RATE &&
       input_rate <= LIBRETRO_AUDIO_MAX_OUTPUT_RATE)
      return input_rate;
   return DEFAULT_SAMPLE_RATE;
}

unsigned host_audio_output_channels(void)
{
   return unifrog_audio_output_channels();
}

const char *host_audio_route_name(void)
{
   if (unifrog_audio_prefers_stereo_output())
      return host.audio_channels > 1u ? "gb300_snd_legacy_stereo" :
         "gb300_snd_legacy_mono";
   return LIBRETRO_AUDIO_ROUTE;
}

int host_audio_backend(void)
{
   /*
    * GB300 AUDSINK repeatedly rejects period-sized stereo writes during
    * active playback. The direct legacy SND route is hardware-proven to
    * accept coalesced 2048-frame transfers and avoids AUDSINK E105 gaps.
    */
   if (unifrog_audio_prefers_stereo_output())
      return UNIFROG_AUDIO_BACKEND_SND;
   return LIBRETRO_AUDIO_BACKEND;
}

unsigned host_audio_period_bytes(void)
{
   return unifrog_audio_prefers_stereo_output() ?
      LIBRETRO_AUDIO_GB300_PERIOD_BYTES : LIBRETRO_AUDIO_PERIOD_BYTES;
}

unsigned host_audio_periods(void)
{
   return unifrog_audio_prefers_stereo_output() ?
      LIBRETRO_AUDIO_GB300_PERIODS : LIBRETRO_AUDIO_PERIODS;
}

unsigned host_audio_runtime_volume(void)
{
   return unifrog_audio_prefers_stereo_output() ? 90u : LIBRETRO_AUDIO_VOLUME;
}

static inline __attribute__((always_inline))
void host_audio_store_mono(int16_t *buffer, unsigned frame,
   int16_t sample)
{
   if (host.audio_channels == 2u) {
      buffer[frame * 2u] = sample;
      buffer[frame * 2u + 1u] = sample;
   } else {
      buffer[frame] = sample;
   }
}

static inline __attribute__((always_inline))
int host_audio_scale_mono(int left, int right, unsigned *abs_out)
{
   int scaled = (left + right) >> 1;
   unsigned abs_value;

   /*
    * host_configure_options() deliberately fixes libretro gain at 1x. Keep
    * this per-sample path specialized to that quality-preserving contract.
    */
   abs_value = scaled < 0 ? (unsigned)-scaled : (unsigned)scaled;
   if (abs_value > host.audio_peak_max)
      host.audio_peak_max = abs_value;
   if (abs_out)
      *abs_out = abs_value;
   return scaled;
}

static void host_audio_update_gate(unsigned peak_out, unsigned frames)
{
   if (!host.audio_write_attempts)
      host_audio_configure_write_policy();
   if (peak_out >= LIBRETRO_AUDIO_GATE_OPEN_LEVEL) {
      if (!host.audio_gate_open) {
         unsigned settle_us = unifrog_audio_prefers_stereo_output() ?
            LIBRETRO_AUDIO_GB300_GATE_SETTLE_US :
            LIBRETRO_AUDIO_SF2000_GATE_SETTLE_US;

         (void)unifrog_audio_set_output_enabled(&host.audio, 1);
         host.audio_gate_open = 1;
         /*
          * Keep the first post-gate write at the normal chunk size. The
          * previous silent lead-in made AUDSINK receive an oversized first
          * transfer and also violated GB300's "muted until real PCM" rule.
          */
         if (settle_us > 0)
            unifrog_perf_delay_us(settle_us);
      }
      host.audio_quiet_frames = 0;
   } else if (peak_out <= LIBRETRO_AUDIO_GATE_CLOSE_LEVEL) {
      /*
       * Count audio time, not callbacks. Libretro cores submit very different
       * batch sizes, so callback-count gating could mute a small-batch core in
       * a few milliseconds and clip its next attack transient.
       */
      if (UINT32_MAX - host.audio_quiet_frames < frames)
         host.audio_quiet_frames = UINT32_MAX;
      else
         host.audio_quiet_frames += frames;
      if (host.audio_gate_open &&
          host.audio_quiet_frames >= host.audio_gate_close_frames) {
         (void)unifrog_audio_set_output_enabled(&host.audio, 0);
         host.audio_gate_open = 0;
      }
   } else {
      host.audio_quiet_frames = 0;
   }
}

static int host_audio_write_frames(const int16_t *frames, unsigned frame_count,
   unsigned batch_index)
{
   const int16_t *write_frames = frames;
   unsigned write_frame_count = frame_count;
   static unsigned audsink_recover_log_count;
   uint32_t write_start;
   unsigned write_count;
   int write_ret;

   if (!frame_count)
      return 0;
   if (!host.audio_write_attempts)
      host_audio_configure_write_policy();
   write_start = unifrog_perf_count();
   write_ret = unifrog_audio_write_timeout(&host.audio, write_frames,
      write_frame_count, host.audio_write_attempts,
      host.audio_write_poll_ms);
   write_count = unifrog_perf_elapsed(write_start, unifrog_perf_count());

   host.audio_write_total_count += write_count;
   if (write_count > host.audio_write_max_count)
      host.audio_write_max_count = write_count;
   host.audio_write_count++;
   if (write_ret != 0) {
      if (host.audio_failures < 8) {
         printf("unifrog libretro audio_write_fail batch=%u frames=%u count=%u\n",
            batch_index, frame_count, write_count);
         (void)unifrog_log_flush();
      }
      host.audio_failures++;
      if (host.audio.backend == UNIFROG_AUDIO_BACKEND_AUDSINK) {
         int drop_ret = unifrog_audio_drop(&host.audio);
         int start_ret = unifrog_audio_start(&host.audio);

         /*
          * AUDSINK E105 means the current buffer was not accepted. Flushing
          * after that loss prevents a saturated queue from turning one miss
          * into a burst of later misses while preserving the GB300 SND route.
          */
         if (audsink_recover_log_count < 8) {
            audsink_recover_log_count++;
            printf("unifrog libretro audio_audsink_recover batch=%u failures=%u drop=%d start=%d\n",
               batch_index, host.audio_failures, drop_ret, start_ret);
         }
      }
   }
   return write_ret;
}

static int host_audio_flush_sample_buffer_internal(int force)
{
   unsigned peak_out = host.audio_sample_buffer_peak;
   unsigned buffered_frames = host.audio_sample_buffer_frames;

   if (!buffered_frames)
      return 0;
   if (!force && host.audio_open && host.audio_input_rate !=
       host.audio_output_rate && buffered_frames <
       LIBRETRO_AUDIO_WRITE_CHUNK_FRAMES) {
      /*
       * Keep resampler tail fragments out of AUDSINK. gpSP's 65536->32000 Hz
       * path leaves a 23-25 frame remainder most video frames; writing those
       * tiny buffers caused E105 backpressure even after the period queue was
       * introduced. Carrying the tail forward preserves low latency while
       * making the normal write path period-sized.
       */
      return 0;
   }
   if (!host.audio_open) {
      host.audio_frames += buffered_frames;
      host.audio_sample_buffer_frames = 0;
      host.audio_sample_buffer_peak = 0;
      return 0;
   }
   host_audio_update_gate(peak_out, buffered_frames);
   /*
    * GB300 AUDSINK does not reliably consume queued silence while its L15
    * route is closed. Drop those inaudible buffers until real PCM opens the
    * route; otherwise the tiny queue rejects about every second write and the
    * retry delay makes audio and emulation visibly choppy.
    */
   if (unifrog_audio_prefers_stereo_output() && !host.audio_gate_open) {
      host.audio_frames += buffered_frames;
      host.audio_batches++;
      host.audio_sample_buffer_frames = 0;
      host.audio_sample_buffer_peak = 0;
      return 0;
   }
   if (host_audio_write_frames(host.audio_sample_buffer,
       buffered_frames, host.audio_batches) != 0) {
      host.audio_sample_buffer_frames = 0;
      host.audio_sample_buffer_peak = 0;
      return -1;
   }
   host.audio_frames += buffered_frames;
   host.audio_batches++;
   host.audio_sample_buffer_frames = 0;
   host.audio_sample_buffer_peak = 0;
   return 0;
}

int host_audio_flush_sample_buffer(void)
{
   return host_audio_flush_sample_buffer_internal(0);
}

int host_audio_flush_sample_buffer_force(void)
{
   return host_audio_flush_sample_buffer_internal(1);
}

static void host_audio_queue_mono(int scaled, unsigned abs_out)
{
   /*
    * Resampled cores often produce a small tail after each conversion pass
    * (gpSP at 65536 Hz -> 32000 Hz showed repeated 36-frame writes). Queueing
    * through the regular 512-frame buffer keeps AUDSINK writes period-sized
    * when possible and avoids needless backpressure without changing latency
    * by more than one host audio period.
    */
   if (abs_out > host.audio_sample_buffer_peak)
      host.audio_sample_buffer_peak = abs_out;
   host_audio_store_mono(host.audio_sample_buffer,
      host.audio_sample_buffer_frames, (int16_t)scaled);
   host.audio_sample_buffer_frames++;
   if (host.audio_sample_buffer_frames >= LIBRETRO_AUDIO_SAMPLE_BUFFER_FRAMES)
      (void)host_audio_flush_sample_buffer();
}

void unifrog_libretro_audio_sample_cb(int16_t left, int16_t right)
{
   unsigned abs_out = 0;
   int scaled;

   if (!host.audio_enabled || host.fast_forward)
      return;
   scaled = host_audio_scale_mono(left, right, &abs_out);
   host_audio_queue_mono(scaled, abs_out);
}

size_t unifrog_libretro_audio_batch_cb(const int16_t *data, size_t frames)
{
   size_t offset = 0;
   size_t input_offset = 0;
   unsigned input_rate;
   unsigned output_rate;

   if (!host.audio_enabled || host.fast_forward) {
      host.audio_batches++;
      host.audio_frames += (unsigned)frames;
      return frames;
   }
   if (!host.audio_open) {
      host.audio_batches++;
      host.audio_frames += (unsigned)frames;
      return frames;
   }

   input_rate = host.audio_input_rate ? host.audio_input_rate :
      DEFAULT_SAMPLE_RATE;
   output_rate = host.audio_output_rate ? host.audio_output_rate :
      input_rate;

   if (input_rate == output_rate) {
      host.audio_resample_accum = 0;
      while (input_offset < frames) {
         size_t out_frames = 0;
         unsigned peak_out = 0;

         while (input_offset < frames &&
                out_frames < LIBRETRO_AUDIO_WRITE_CHUNK_FRAMES) {
            int left = data[input_offset * 2];
            int right = data[input_offset * 2 + 1];
            int scaled;
            unsigned abs_out;

            input_offset++;
            scaled = host_audio_scale_mono(left, right, &abs_out);
            if (abs_out > peak_out)
               peak_out = abs_out;
            host_audio_store_mono(audio_mix_buffer, (unsigned)out_frames,
               (int16_t)scaled);
            out_frames++;
         }
         if (out_frames == 0)
            continue;
         host_audio_update_gate(peak_out, (unsigned)out_frames);
         /*
          * Match the resampled path: GB300 should not fill AUDSINK with
          * inaudible PCM while its physical output route remains closed.
          */
         if (unifrog_audio_prefers_stereo_output() && !host.audio_gate_open) {
            offset += out_frames;
            continue;
         }
         if (host_audio_write_frames(audio_mix_buffer, (unsigned)out_frames,
             host.audio_batches) != 0)
            break;
         offset += out_frames;
      }

      host.audio_batches++;
      host.audio_frames += (unsigned)offset;
      return input_offset ? input_offset : frames;
   }

   while (input_offset < frames) {
      while (input_offset < frames) {
         int left = data[input_offset * 2];
         int right = data[input_offset * 2 + 1];
         int scaled;
         unsigned abs_out;

         input_offset++;
         host.audio_resample_accum += output_rate;
         if (host.audio_resample_accum < input_rate)
            continue;
         host.audio_resample_accum -= input_rate;
         scaled = host_audio_scale_mono(left, right, &abs_out);
         host_audio_queue_mono(scaled, abs_out);
         offset++;
      }
   }

   return input_offset ? input_offset : frames;
}

void unifrog_libretro_video_refresh_cb(const void *data, unsigned width,
   unsigned height, size_t pitch)
{
   if (!data || width == 0 || height == 0)
      return;
   if (host.fast_forward) {
      if (host.fast_forward_force_present) {
         host.fast_forward_force_present = 0;
      } else if ((host.video_frames %
          sanitize_fast_forward_multiplier(host.fast_forward_multiplier)) != 0) {
         host.video_frames++;
         if (host.presenter_open)
            host.presenter.last_vsync_count = 0;
         return;
      }
   }

   if (host.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
      if (pitch < (size_t)width * 4u)
         return;
   } else if (host.pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
      if (pitch < (size_t)width * 2u)
         return;
   } else {
      return;
   }

   if (!host.video_seen) {
      printf("unifrog libretro first_video %ux%u pitch=%u fmt=%u\n",
         width, height, (unsigned)pitch, host.pixel_format);
      (void)unifrog_log_flush();
      host.video_seen = 1;
   }
   host.video_width = width;
   host.video_height = height;
   host.video_pitch = (unsigned)pitch;
   host.video_frames++;
   if (data == host.software_framebuffer &&
       pitch == host.software_framebuffer_pitch)
      host.software_framebuffer_presents++;
   if (host.presenter_open) {
      if (host.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
         (void)unifrog_presenter_present_xrgb8888(&host.presenter,
            data, width, height, (unsigned)pitch);
      else
         (void)unifrog_presenter_present_rgb565(&host.presenter,
            data, width, height, (unsigned)pitch);
   }
}

static unsigned host_pixel_format_bytes(enum retro_pixel_format format)
{
   switch (format) {
   case RETRO_PIXEL_FORMAT_RGB565:
      return 2u;
   case RETRO_PIXEL_FORMAT_XRGB8888:
      return 4u;
   default:
      return 0;
   }
}

static int host_software_framebuffer_ensure(unsigned width, unsigned height,
   enum retro_pixel_format format)
{
   unsigned bpp = host_pixel_format_bytes(format);
   unsigned pitch;
   size_t bytes;
   void *buffer;

   if (!width || !height || !bpp)
      return -1;
   if (width > 640u || height > 480u)
      return -1;
   pitch = width * bpp;
   if (pitch & 63u)
      pitch = (pitch + 63u) & ~63u;
   bytes = (size_t)pitch * height;
   if (host.software_framebuffer &&
       host.software_framebuffer_bytes >= bytes &&
       host.software_framebuffer_width == width &&
       host.software_framebuffer_height == height &&
       host.software_framebuffer_pitch == pitch &&
       host.software_framebuffer_format == format)
      return 0;

   buffer = unifrog_surface_memalign(64u, bytes);
   if (!buffer)
      return -1;
   unifrog_surface_free(host.software_framebuffer);
   host.software_framebuffer = buffer;
   host.software_framebuffer_bytes = bytes;
   host.software_framebuffer_width = width;
   host.software_framebuffer_height = height;
   host.software_framebuffer_pitch = pitch;
   host.software_framebuffer_format = format;
   printf("unifrog libretro software_fb alloc width=%u height=%u pitch=%u fmt=%u bytes=%u mmz=%d\n",
      width, height, pitch, format, (unsigned)bytes,
      unifrog_surface_is_mmz(buffer));
   return 0;
}

static bool host_get_current_software_framebuffer(struct retro_framebuffer *fb)
{
   unsigned width;
   unsigned height;
   enum retro_pixel_format format;

   if (!fb)
      return false;
   host.software_framebuffer_requests++;
   width = fb->width ? fb->width : host.video_max_width;
   height = fb->height ? fb->height : host.video_max_height;
   if (!width)
      width = 320u;
   if (!height)
      height = 240u;
   format = host.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888 ?
      RETRO_PIXEL_FORMAT_XRGB8888 : RETRO_PIXEL_FORMAT_RGB565;
   if (host_software_framebuffer_ensure(width, height, format) != 0)
      return false;
   fb->data = host.software_framebuffer;
   fb->width = width;
   fb->height = height;
   fb->pitch = host.software_framebuffer_pitch;
   fb->format = format;
   fb->memory_flags = RETRO_MEMORY_TYPE_CACHED;
   host.software_framebuffer_hits++;
   return true;
}

bool unifrog_libretro_environment_cb(unsigned cmd, void *data)
{
   if (cmd == UNIFROG_ENVIRONMENT_LAUNCH_CONTENT) {
      const struct unifrog_libretro_launch_content *request = data;

      if (!request ||
          request->size < sizeof(struct unifrog_libretro_launch_content) ||
          request->version != UNIFROG_LIBRETRO_LAUNCH_VERSION ||
          !memchr(request->core_id, '\0', sizeof(request->core_id)) ||
          !memchr(request->path, '\0', sizeof(request->path)) ||
          !request->core_id[0] || !request->path[0])
         return false;
      unifrog_text_copy(host.launch_core_id, sizeof(host.launch_core_id),
         request->core_id);
      unifrog_text_copy(host.launch_path, sizeof(host.launch_path),
         request->path);
      host.launch_requested = 1;
      UF_LOG_INFO("libretro",
         "event=launch_request source=%s core=%s path=%s",
         host.core_id ? host.core_id : "", host.launch_core_id,
         host.launch_path);
      return true;
   }

   switch (cmd) {
   case RETRO_ENVIRONMENT_SHUTDOWN:
      host.shutdown_requested = 1;
      UF_LOG_INFO("libretro", "event=shutdown_request core=%s",
         host.core_id ? host.core_id : "");
      return true;
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      if (!data)
         return false;
      ((struct retro_log_callback *)data)->log = unifrog_libretro_log_cb;
      printf("unifrog libretro env=get_log_interface\n");
      (void)unifrog_log_flush();
      return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      if (!data)
         return false;
      if (!host_pixel_format_allowed(
          *(const enum retro_pixel_format *)data)) {
         printf("unifrog libretro reject_pixel_format=%u framebuffer=%s\n",
            *(const enum retro_pixel_format *)data,
            framebuffer_format_label(host.framebuffer_format));
         (void)unifrog_log_flush();
         return false;
      }
      host.pixel_format = *(const enum retro_pixel_format *)data;
      printf("unifrog libretro set_pixel_format=%u framebuffer=%s\n",
         host.pixel_format, framebuffer_format_label(host.framebuffer_format));
      (void)unifrog_log_flush();
      return true;
   case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      if (!data)
         return false;
      *(bool *)data = true;
      return true;
   case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
   case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
      if (!data)
         return false;
      *(unsigned *)data = 2;
      return true;
   case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      if (!data)
         return false;
      if (host.fast_forward) {
         *(int *)data = RETRO_AV_ENABLE_VIDEO |
            RETRO_AV_ENABLE_HARD_DISABLE_AUDIO;
      } else {
         *(int *)data = RETRO_AV_ENABLE_VIDEO |
            (host.audio_enabled ? RETRO_AV_ENABLE_AUDIO :
             RETRO_AV_ENABLE_HARD_DISABLE_AUDIO);
      }
      return true;
   case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
      if (!data)
         return false;
      ((struct retro_rumble_interface *)data)->set_rumble_state =
         unifrog_libretro_rumble_trampoline;
      return true;
   case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      if (!data)
         return false;
      *(unsigned *)data = 2;
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      if (!data)
         return false;
      *(bool *)data = host.variables_dirty ? true : false;
      host.variables_dirty = 0;
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
      return host_get_variable((struct retro_variable *)data);
   case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER:
      return host_get_current_software_framebuffer(
         (struct retro_framebuffer *)data);
   case RETRO_ENVIRONMENT_SET_VARIABLE:
      return false;
   case RETRO_ENVIRONMENT_SET_VARIABLES:
      core_options_register_variables((const struct retro_variable *)data);
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
      core_options_register_definitions(
         (const struct retro_core_option_definition *)data);
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
      if (data) {
         const struct retro_core_options_intl *intl =
            (const struct retro_core_options_intl *)data;
         core_options_register_definitions(intl->us ? intl->us : intl->local);
      }
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      if (data) {
         const struct retro_core_options_v2 *options =
            (const struct retro_core_options_v2 *)data;
         core_options_register_v2(options->definitions);
      }
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
      if (data) {
         const struct retro_core_options_v2_intl *intl =
            (const struct retro_core_options_v2_intl *)data;
         const struct retro_core_options_v2 *options =
            intl->us ? intl->us : intl->local;
         if (options)
            core_options_register_v2(options->definitions);
      }
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      if (data) {
         const struct retro_core_option_display *display =
            (const struct retro_core_option_display *)data;
         int index = core_option_find(display->key);
         if (index >= 0)
            host.core_options[index].visible = display->visible ? 1 : 0;
      }
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
   case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
   case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
   case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
      return true;
   case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      if (data) {
         const struct retro_memory_map *map =
            (const struct retro_memory_map *)data;
         printf("unifrog libretro env=set_memory_maps count=%u\n",
            (unsigned)map->num_descriptors);
      }
      return true;
   case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
      return data != NULL;
   case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
      if (data &&
          ((const struct retro_audio_buffer_status_callback *)data)->callback) {
         host.audio_status =
            *(const struct retro_audio_buffer_status_callback *)data;
         host.audio_status_enabled = 1;
      } else {
         memset(&host.audio_status, 0, sizeof(host.audio_status));
         host.audio_status_enabled = 0;
      }
      printf("unifrog libretro env=audio_buffer_status enabled=%d\n",
         host.audio_status_enabled);
      return true;
   case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
      if (data) {
         printf("unifrog libretro env=set_minimum_audio_latency ms=%u\n",
            *(const unsigned *)data);
      }
      return true;
   case RETRO_ENVIRONMENT_GET_LANGUAGE:
      if (!data)
         return false;
      *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
      return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
   case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
      if (!data)
         return false;
      *(const char **)data = UNIFROG_SD_ROOT;
      return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      if (!data)
         return false;
      *(const char **)data = UNIFROG_BIOS_ROOT;
      return true;
   case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
      if (!data)
         return false;
      *(unsigned *)data = 1;
      return true;
   case RETRO_ENVIRONMENT_SET_MESSAGE:
      if (data) {
         const struct retro_message *msg = (const struct retro_message *)data;
         printf("unifrog libretro message=%s\n",
            msg && msg->msg ? msg->msg : "");
      }
      return true;
   case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
      if (data) {
         const struct retro_message_ext *msg = (const struct retro_message_ext *)data;
         printf("unifrog libretro message_ext=%s\n",
            msg && msg->msg ? msg->msg : "");
      }
      return true;
   default:
      if (cmd < 128) {
         printf("unifrog libretro env unsupported cmd=%u\n", cmd);
         (void)unifrog_log_flush();
      }
      return false;
   }
}
