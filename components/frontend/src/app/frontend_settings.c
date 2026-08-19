#include "frontend_internal.h"

/* Private frontend settings, storage-profile, and media-tuning helpers. */

const char *storage_profile_label(const char *profile)
{
   return unifrog_storage_profile_info(profile)->label;
}

static unsigned storage_profile_index(const char *profile)
{
   for (unsigned i = 0; i < unifrog_storage_profile_count(); i++) {
      if (strcmp(profile ? profile : "",
          unifrog_storage_profile_name(i)) == 0)
         return i;
   }
   return unifrog_storage_profile_count();
}

static void normalize_storage_profile(struct frontend_state *fe)
{
   if (!fe->storage_profile[0] ||
       storage_profile_index(fe->storage_profile) >=
          unifrog_storage_profile_count())
      unifrog_text_copy(fe->storage_profile, sizeof(fe->storage_profile), "auto");
   if (storage_profile_index(fe->storage_normal_profile) >=
       unifrog_storage_profile_count() ||
       strcmp(fe->storage_normal_profile, "auto") == 0)
      unifrog_text_copy(fe->storage_normal_profile,
         sizeof(fe->storage_normal_profile), "wide25");
   if (storage_profile_index(fe->storage_fallback_profile) >=
       unifrog_storage_profile_count() ||
       strcmp(fe->storage_fallback_profile, "auto") == 0)
      unifrog_text_copy(fe->storage_fallback_profile,
         sizeof(fe->storage_fallback_profile), "safe");
}

static void normalize_battery_calibration(struct frontend_state *fe)
{
   if (!unifrog_battery_calibration_valid(&fe->battery_calibration))
      unifrog_battery_calibration_defaults(&fe->battery_calibration);
   unifrog_battery_set_calibration(&fe->battery_calibration);
}

static int apply_named_storage_profile(const char *profile, char *detail,
   size_t detail_size)
{
   if (strcmp(profile, "boot") == 0)
      return unifrog_platform_sd_restore_boot(4, 100, detail, detail_size);
   return unifrog_platform_sd_apply_profile(profile, 4, 100, detail,
      detail_size);
}

static int apply_storage_profile_internal(struct frontend_state *fe,
   const char *reason)
{
   char detail[192];
   const char *applied_profile;
   size_t old_auto_flush;
   int ret;

   normalize_storage_profile(fe);
   applied_profile = fe->storage_profile;
   if (strcmp(fe->storage_profile, "auto") == 0)
      applied_profile = fe->storage_normal_profile;
   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_platform_set_storage_log_suspended(1);
   ret = apply_named_storage_profile(applied_profile, detail, sizeof(detail));
   if (ret != 0 && strcmp(fe->storage_profile, "auto") == 0 &&
       strcmp(applied_profile, "safe") != 0 &&
       strcmp(applied_profile, fe->storage_fallback_profile) != 0) {
      unifrog_log("frontend storage auto fallback from=%s to=%s ret=%d\n",
         applied_profile, fe->storage_fallback_profile, ret);
      applied_profile = fe->storage_fallback_profile;
      ret = apply_named_storage_profile(applied_profile, detail, sizeof(detail));
   }
   if (ret != 0 && strcmp(fe->storage_profile, "auto") == 0 &&
       strcmp(applied_profile, "safe") != 0) {
      unifrog_log("frontend storage auto final_fallback from=%s to=safe ret=%d\n",
         applied_profile, ret);
      applied_profile = "safe";
      ret = apply_named_storage_profile(applied_profile, detail, sizeof(detail));
   }
   unifrog_platform_set_storage_log_suspended(0);
   unifrog_log_set_auto_flush_bytes(old_auto_flush);
   unifrog_log("frontend storage_profile apply reason=%s configured=%s applied=%s "
      "ret=%d detail=%s\n",
      reason ? reason : "", fe->storage_profile, applied_profile,
      ret, detail);
   (void)unifrog_log_flush();
   frontend_set_status(fe, "SD %s %d", applied_profile, ret);
   return ret;
}

int apply_storage_profile(struct frontend_state *fe, const char *reason)
{
   return apply_storage_profile_internal(fe, reason);
}

const char *active_storage_label(void)
{
   return storage_profile_label(unifrog_platform_sd_active_profile());
}

int sync_default_boot(struct frontend_state *fe)
{
   char text[UNIFROG_BOOT_PATH_MAX + 2u];
   int len;

   if (!fe)
      return -1;
   if (!fe->default_boot[0] || strcmp(fe->default_boot, "unifrog") == 0) {
      unifrog_text_copy(fe->default_boot, sizeof(fe->default_boot), "unifrog");
      return unlink(UNIFROG_DEFAULT_BOOT_PATH) == 0 || errno == ENOENT ? 0 : -1;
   }
   if (!unifrog_boot_asd_path_supported(fe->default_boot))
      return -1;
   (void)mkdir(UNIFROG_FIRMWARE_ROOT, 0777);
   len = snprintf(text, sizeof(text), "%s\n", fe->default_boot);
   if (len <= 0 || (size_t)len >= sizeof(text))
      return -1;
   return unifrog_storage_write_atomic(UNIFROG_DEFAULT_BOOT_PATH, NULL,
      text, (size_t)len, "default_boot", 4, 100);
}

enum media_setting_type {
   MEDIA_SETTING_UNSIGNED = 0,
   MEDIA_SETTING_SIZE,
   MEDIA_SETTING_INT,
   MEDIA_SETTING_BOOL,
};

struct media_setting_spec {
   const char *key;
   const char *description;
   size_t offset;
   enum media_setting_type type;
};

#define MEDIA_U(key, field, description) \
   { key, description, offsetof(struct unifrog_media_tuning, field), \
     MEDIA_SETTING_UNSIGNED }
#define MEDIA_Z(key, field, description) \
   { key, description, offsetof(struct unifrog_media_tuning, field), \
     MEDIA_SETTING_SIZE }
#define MEDIA_I(key, field, description) \
   { key, description, offsetof(struct unifrog_media_tuning, field), \
     MEDIA_SETTING_INT }
#define MEDIA_B(key, field, description) \
   { key, description, offsetof(struct unifrog_media_tuning, field), \
     MEDIA_SETTING_BOOL }

static const struct media_setting_spec media_settings[] = {
   MEDIA_U("media_video_feed_lead_ms", video_feed_lead_ms,
      "unsigned ms; target compressed-video lead"),
   MEDIA_U("media_audio_feed_lead_ms", audio_feed_lead_ms,
      "unsigned ms; target compressed-audio lead"),
   MEDIA_U("media_video_kshm_size", video_kshm_size,
      "unsigned bytes; decoder compressed ring"),
   MEDIA_U("media_video_lowres_kshm_size", video_lowres_kshm_size,
      "unsigned bytes; low-resolution decoder ring"),
   MEDIA_Z("media_file_buffer_size", file_buffer_size,
      "bytes; general file buffer"),
   MEDIA_Z("media_file_buffer_min_size", file_buffer_min_size,
      "bytes; minimum general file buffer"),
   MEDIA_Z("media_file_readahead_size", file_readahead_size,
      "bytes; general readahead target"),
   MEDIA_Z("media_file_readahead_min_size", file_readahead_min_size,
      "bytes; minimum general readahead"),
   MEDIA_U("media_file_readahead_slots", file_readahead_slots,
      "unsigned count; general readahead slots"),
   MEDIA_Z("media_video_readahead_size", video_readahead_size,
      "bytes; video readahead target"),
   MEDIA_Z("media_video_readahead_min_size", video_readahead_min_size,
      "bytes; minimum video readahead"),
   MEDIA_U("media_video_readahead_slots", video_readahead_slots,
      "unsigned count; video readahead slots"),
   MEDIA_U("media_video_prefill_target_ms", video_prefill_target_ms,
      "unsigned ms; prefill duration target"),
   MEDIA_Z("media_video_prefill_min_bytes", video_prefill_min_bytes,
      "bytes; minimum video prefill"),
   MEDIA_Z("media_video_prefill_max_bytes", video_prefill_max_bytes,
      "bytes; maximum video prefill"),
   MEDIA_Z("media_video_preload_max_bytes", video_preload_max_bytes,
      "bytes; full-file preload cap, 0 disables"),
   MEDIA_U("media_audio_max_hw_ahead_ms", audio_max_hw_ahead_ms,
      "unsigned ms; maximum queued hardware audio"),
   MEDIA_U("media_video_max_hw_ahead_ms", video_max_hw_ahead_ms,
      "unsigned ms; maximum queued hardware video"),
   MEDIA_U("media_seek_warmup_packets", seek_warmup_packets,
      "unsigned packet count after seek"),
   MEDIA_U("media_seek_video_warmup_packets", seek_video_warmup_packets,
      "unsigned video packet count after seek"),
   MEDIA_U("media_seek_video_recover_warmup_packets",
      seek_video_recover_warmup_packets,
      "unsigned video packet count after recovery"),
   MEDIA_U("media_hw_ahead_max_wait_ms", hw_ahead_max_wait_ms,
      "unsigned ms; hardware queue wait limit"),
   MEDIA_U("media_seek_settle_ms", seek_settle_ms,
      "unsigned ms; input settle delay after seek"),
   MEDIA_B("media_seek_accelerate_frames", seek_accelerate_frames,
      "boolean 0/1; accelerate seek catch-up"),
   MEDIA_U("media_seek_keyframe_drop_limit", seek_keyframe_drop_limit,
      "unsigned packet count; keyframe drop limit"),
   MEDIA_I("media_seek_preroll_decode_ms", seek_preroll_decode_ms,
      "signed ms; normal seek preroll"),
   MEDIA_I("media_seek_preroll_hd_decode_ms", seek_preroll_hd_decode_ms,
      "signed ms; HD seek preroll"),
   MEDIA_I("media_seek_preroll_keyframe_max_bytes",
      seek_preroll_keyframe_max_bytes,
      "signed bytes; preroll keyframe cap"),
   MEDIA_U("media_video_stuck_behind_ms", video_stuck_behind_ms,
      "unsigned ms; late-video threshold"),
   MEDIA_U("media_video_stall_recover_ms", video_stall_recover_ms,
      "unsigned ms; stall recovery threshold"),
   MEDIA_U("media_video_recover_gap_ms", video_recover_gap_ms,
      "unsigned ms; recovery gap threshold"),
   MEDIA_U("media_video_write_recover_max", video_write_recover_max,
      "unsigned count; decoder write recovery attempts"),
   MEDIA_U("media_video_write_eperm_recover_ms",
      video_write_eperm_recover_ms,
      "unsigned ms; decoder EPERM recovery delay"),
   MEDIA_U("media_file_slow_read_log_ms", file_slow_read_log_ms,
      "unsigned ms; slow-read logging threshold"),
   MEDIA_I("media_audio_buffering_start_ms", audio_buffering_start_ms,
      "signed ms; audio buffering start"),
   MEDIA_I("media_audio_buffering_end_ms", audio_buffering_end_ms,
      "signed ms; audio buffering end"),
   MEDIA_I("media_video_buffering_start_ms", video_buffering_start_ms,
      "signed ms; video buffering start"),
   MEDIA_I("media_video_buffering_end_ms", video_buffering_end_ms,
      "signed ms; video buffering end"),
   MEDIA_B("media_reset_viddec_on_fail", reset_viddec_on_fail,
      "boolean 0/1; reset video decoder after failure"),
   MEDIA_B("media_gb300_auddec_probe_once", gb300_auddec_probe_once,
      "boolean 0/1; limit GB300 audio probe"),
};

static void save_media_tuning(FILE *file,
   const struct unifrog_media_tuning *tuning)
{
   const unsigned char *base = (const unsigned char *)tuning;

   if (!file || !tuning)
      return;
   for (unsigned i = 0; i < ARRAY_SIZE(media_settings); i++) {
      const struct media_setting_spec *spec = &media_settings[i];
      const void *field = base + spec->offset;

      fprintf(file, "# %s\n%s=", spec->description, spec->key);
      if (spec->type == MEDIA_SETTING_UNSIGNED)
         fprintf(file, "%u\n", *(const unsigned *)field);
      else if (spec->type == MEDIA_SETTING_SIZE)
         fprintf(file, "%lu\n", (unsigned long)*(const size_t *)field);
      else
         fprintf(file, "%d\n", spec->type == MEDIA_SETTING_BOOL ?
            (*(const int *)field ? 1 : 0) : *(const int *)field);
   }
}

static int load_media_tuning_value(struct unifrog_media_tuning *tuning,
   const char *key, const char *value)
{
   unsigned char *base = (unsigned char *)tuning;

   if (!tuning || !key || !value)
      return 0;
   for (unsigned i = 0; i < ARRAY_SIZE(media_settings); i++) {
      const struct media_setting_spec *spec = &media_settings[i];
      void *field;

      if (strcmp(key, spec->key) != 0)
         continue;
      field = base + spec->offset;
      if (spec->type == MEDIA_SETTING_UNSIGNED)
         *(unsigned *)field = frontend_parse_unsigned_setting(value,
            *(unsigned *)field);
      else if (spec->type == MEDIA_SETTING_SIZE)
         *(size_t *)field = frontend_parse_size_setting(value, *(size_t *)field);
      else if (spec->type == MEDIA_SETTING_BOOL) {
         int parsed = frontend_parse_int(value, -1);

         if (parsed == 0 || parsed == 1)
            *(int *)field = parsed;
      }
      else
         *(int *)field = frontend_parse_int(value, *(int *)field);
      return 1;
   }
   return 0;
}

enum frontend_setting_type {
   FRONTEND_SETTING_BOOL = 0,
   FRONTEND_SETTING_INT,
   FRONTEND_SETTING_UNSIGNED,
   FRONTEND_SETTING_CPU,
   FRONTEND_SETTING_TEXT,
};

struct frontend_setting_spec {
   const char *key;
   size_t offset;
   size_t size;
   int minimum;
   int maximum;
   enum frontend_setting_type type;
   int run_option;
};

#define FE_BOOL(key, field) \
   { key, offsetof(struct frontend_state, field), sizeof(int), 0, 1, \
     FRONTEND_SETTING_BOOL, 0 }
#define FE_INT(key, field, minimum, maximum) \
   { key, offsetof(struct frontend_state, field), sizeof(int), minimum, \
     maximum, FRONTEND_SETTING_INT, 0 }
#define FE_UINT(key, field, minimum, maximum) \
   { key, offsetof(struct frontend_state, field), sizeof(unsigned), minimum, \
     maximum, FRONTEND_SETTING_UNSIGNED, 0 }
#define FE_TEXT(key, field) \
   { key, offsetof(struct frontend_state, field), \
     sizeof(((struct frontend_state *)0)->field), 0, 0, \
     FRONTEND_SETTING_TEXT, 0 }
#define OPT_BOOL(key, field) \
   { key, offsetof(struct unifrog_libretro_run_options, field), sizeof(int), \
     0, 1, FRONTEND_SETTING_BOOL, 1 }
#define OPT_INT(key, field, minimum, maximum) \
   { key, offsetof(struct unifrog_libretro_run_options, field), sizeof(int), \
     minimum, maximum, FRONTEND_SETTING_INT, 1 }
#define OPT_UINT(key, field, minimum, maximum) \
   { key, offsetof(struct unifrog_libretro_run_options, field), \
     sizeof(unsigned), minimum, maximum, FRONTEND_SETTING_UNSIGNED, 1 }
#define OPT_CPU(key, field) \
   { key, offsetof(struct unifrog_libretro_run_options, field), \
     sizeof(unsigned), 0, 0, FRONTEND_SETTING_CPU, 1 }

static const struct frontend_setting_spec frontend_settings[] = {
   OPT_BOOL("audio", audio_enabled),
   OPT_UINT("gain", audio_gain, 0, 4),
   OPT_CPU("cpu", scpu_mhz),
   OPT_INT("ge_clock", ge_clock, -1, 3),
   OPT_INT("backlight", backlight_level, -1, 100),
   OPT_INT("frameskip", frameskip, 0, 3),
   OPT_INT("display", display_mode, 0, 2),
   OPT_INT("framebuffer", framebuffer_format, 0, 1),
   OPT_INT("keymap", input_profile, 0, 4),
   OPT_BOOL("state_auto_load", state_auto_load),
   OPT_BOOL("state_auto_save", state_auto_save),
   OPT_INT("rtc_offset_minutes", rtc_offset_minutes, -5270400, 5270400),
   OPT_UINT("state_slot", state_slot, 0, 9),
   FE_BOOL("sort_desc", sort_desc),
   FE_BOOL("show_hidden", show_hidden),
   FE_BOOL("folder_counts", folder_counts),
   FE_BOOL("mixed_content", mixed_content),
   FE_BOOL("display_empty_folder", display_empty_folder),
   FE_BOOL("menu_counter_folder", menu_counter_folder),
   FE_BOOL("menu_counter_file", menu_counter_file),
   FE_BOOL("content_collect", content_collect),
   FE_BOOL("content_history", content_history),
   FE_BOOL("clock_enabled", clock_enabled),
   FE_BOOL("title_include_root", title_include_root),
   FE_BOOL("theme_alternate", theme_alternate),
   FE_BOOL("boxart_hidden", boxart_hidden),
   FE_BOOL("launch_splash", launch_splash),
   FE_BOOL("sound_enabled", sound_enabled),
   FE_TEXT("log_level", log_level),
   FE_TEXT("language_name", language_name),
   FE_TEXT("theme_name", theme_name),
   FE_TEXT("artwork_layout", artwork_layout),
   FE_TEXT("artwork_box_templates", artwork_box_templates),
   FE_TEXT("artwork_preview_templates", artwork_preview_templates),
   FE_TEXT("artwork_text_templates", artwork_text_templates),
   FE_TEXT("device_board", device_board),
   FE_TEXT("storage_profile", storage_profile),
   FE_TEXT("storage_normal_profile", storage_normal_profile),
   FE_TEXT("storage_fallback_profile", storage_fallback_profile),
   FE_UINT("battery_mv_empty", battery_calibration.millivolts[0], 2500, 5000),
   FE_UINT("battery_mv_25", battery_calibration.millivolts[1], 2500, 5000),
   FE_UINT("battery_mv_50", battery_calibration.millivolts[2], 2500, 5000),
   FE_UINT("battery_mv_75", battery_calibration.millivolts[3], 2500, 5000),
   FE_UINT("battery_mv_full", battery_calibration.millivolts[4], 2500, 5000),
   FE_UINT("battery_discharge_mv_per_hour",
      battery_calibration.discharge_mv_per_hour, 0, 2000),
   FE_BOOL("battery_estimate_discharge", battery_calibration.estimate_discharge),
   FE_TEXT("default_boot", default_boot),
};

static int frontend_cpu_valid(int value)
{
   return value >= 0 && unifrog_libretro_policy_cpu_valid((unsigned)value);
}

static int load_simple_setting(struct frontend_state *fe, const char *key,
   const char *value)
{
   for (unsigned i = 0; i < ARRAY_SIZE(frontend_settings); i++) {
      const struct frontend_setting_spec *spec = &frontend_settings[i];
      unsigned char *base;
      void *field;
      int parsed;

      if (strcmp(key, spec->key) != 0)
         continue;
      base = spec->run_option ? (unsigned char *)&fe->run_options :
         (unsigned char *)fe;
      field = base + spec->offset;
      if (spec->type == FRONTEND_SETTING_TEXT) {
         unifrog_text_copy(field, spec->size, value);
         return 1;
      }
      parsed = frontend_parse_int(value, INT_MIN);
      if (parsed == INT_MIN ||
          (spec->type == FRONTEND_SETTING_CPU && !frontend_cpu_valid(parsed)) ||
          (spec->type != FRONTEND_SETTING_CPU &&
           (parsed < spec->minimum || parsed > spec->maximum)))
         return 1;
      if (spec->type == FRONTEND_SETTING_UNSIGNED ||
          spec->type == FRONTEND_SETTING_CPU)
         *(unsigned *)field = (unsigned)parsed;
      else
         *(int *)field = parsed;
      return 1;
   }
   return 0;
}

static void load_rom_roots(struct frontend_state *fe, const char *value)
{
   char roots[FRONTEND_MAX_LINE];
   char normalized[FRONTEND_MAX_PATH];
   char *part;

   fe->rom_root_count = 0;
   unifrog_text_copy(roots, sizeof(roots), value);
   part = roots;
   while (part && *part) {
      char *sep = strpbrk(part, "|;");

      if (sep)
         *sep = '\0';
      part = frontend_trim_ascii(part);
      if (frontend_normalize_path(normalized, sizeof(normalized), part) == 0)
         (void)frontend_rom_root_add(fe, normalized);
      part = sep ? sep + 1 : NULL;
   }
}

static void load_rom_system(struct frontend_state *fe, const char *value)
{
   char entry[96];
   char *name;
   char *core;
   char *sep;

   unifrog_text_copy(entry, sizeof(entry), value);
   sep = strchr(entry, ':');
   if (!sep)
      sep = strchr(entry, '=');
   if (!sep)
      return;
   *sep++ = '\0';
   name = frontend_trim_ascii(entry);
   core = frontend_trim_ascii(sep);
   if (!name[0] || !core[0])
      return;
   for (unsigned i = 0; i < fe->rom_system_count; i++) {
      if (strcasecmp(fe->rom_system_name[i], name) == 0) {
         unifrog_text_copy(fe->rom_system_core[i],
            sizeof(fe->rom_system_core[0]), core);
         return;
      }
   }
   if (fe->rom_system_count >= FRONTEND_ROM_SYSTEM_MAP_MAX)
      return;
   unifrog_text_copy(fe->rom_system_name[fe->rom_system_count],
      sizeof(fe->rom_system_name[0]), name);
   unifrog_text_copy(fe->rom_system_core[fe->rom_system_count],
      sizeof(fe->rom_system_core[0]), core);
   if (fe->rom_system_name[fe->rom_system_count][0] &&
       fe->rom_system_core[fe->rom_system_count][0])
      fe->rom_system_count++;
}

struct settings_load_context {
   struct frontend_state *fe;
   char primary_rom_root[FRONTEND_MAX_PATH];
   int primary_rom_root_seen;
};

static int load_settings_entry(void *userdata, const char *section,
   const char *key, const char *value, unsigned line_number)
{
   struct settings_load_context *context = userdata;
   struct frontend_state *fe = context->fe;

   (void)line_number;
   if (unifrog_frontend_config_parse_entry(&fe->scoped_config, section,
       key, value) != 0 || section[0])
      return 0;
   if (load_simple_setting(fe, key, value) ||
       load_media_tuning_value(&fe->media_tuning, key, value))
      return 0;
   if (strcmp(key, "rom_root") == 0) {
      char normalized[FRONTEND_MAX_PATH];

      if (frontend_normalize_path(normalized, sizeof(normalized), value) == 0) {
         unifrog_text_copy(context->primary_rom_root,
            sizeof(context->primary_rom_root), normalized);
         context->primary_rom_root_seen = 1;
      }
   } else if (strcmp(key, "rom_roots") == 0) {
      load_rom_roots(fe, value);
   } else if (strcmp(key, "rom_root_label") == 0) {
      unifrog_text_copy(fe->rom_root_label, sizeof(fe->rom_root_label), value);
   } else if (strcmp(key, "rom_system") == 0) {
      load_rom_system(fe, value);
   } else if (strcmp(key, "last_path") == 0) {
      unifrog_text_copy(fe->last_path, sizeof(fe->last_path), value);
   } else if (strcmp(key, "last_core") == 0) {
      unifrog_text_copy(fe->last_core, sizeof(fe->last_core), value);
   }
   return 0;
}

static int load_session_entry(void *userdata, const char *section,
   const char *key, const char *value, unsigned line_number)
{
   struct frontend_state *fe = userdata;

   (void)line_number;
   if (section[0])
      return 0;
   if (strcmp(key, "last_path") == 0)
      unifrog_text_copy(fe->last_path, sizeof(fe->last_path), value);
   else if (strcmp(key, "last_core") == 0)
      unifrog_text_copy(fe->last_core, sizeof(fe->last_core), value);
   return 0;
}

static void save_associations(FILE *file, const struct frontend_state *fe)
{
   fprintf(file,
      "\n# File associations. Handler IDs are installed core IDs, media, or\n"
      "# reader. The ordered handlers are shown in Open With; default is used\n"
      "# when A is pressed. Press X in Open With to update these on-device.\n"
      "# Extensions may be compound and contain at most 15 characters. Lists\n"
      "# contain at most 8 unique handler IDs of at most 31 characters each.\n"
      "# Media routes are native, ffmpeg, wav-auddec (WAV only), hcplayer,\n"
      "# hcplayer-audio, and hcplayer-muted. HCPlayer routes require firmware\n"
      "# media support; native and ffmpeg do not open image files.\n");
   for (unsigned i = 0; i < fe->association_count; i++) {
      const struct frontend_association *association = &fe->associations[i];

      fprintf(file, "\n# .%s handlers\nextension.%s.handlers=",
         association->extension, association->extension);
      for (unsigned j = 0; j < association->handler_count; j++)
         fprintf(file, "%s%s", j ? "," : "", association->handlers[j]);
      fprintf(file, "\nextension.%s.default=%s\n", association->extension,
         association->default_handler);
   }
}

static void save_preserved_sections(FILE *out)
{
   FILE *in = fopen(UNIFROG_CONFIG_PATH, "rb");
   char line[FRONTEND_MAX_LINE];
   int copy = 0;

   if (!in)
      return;
   while (fgets(line, sizeof(line), in)) {
      char *text = line;

      while (*text == ' ' || *text == '\t')
         text++;
      if (*text == '[') {
         char *end = strchr(text + 1, ']');

         copy = 0;
         if (end) {
            *end = '\0';
            copy = text[1] != '\0';
            *end = ']';
         }
      }
      if (copy)
         fputs(line, out);
   }
   fclose(in);
}

int save_settings(struct frontend_state *fe)
{
   FILE *file;
   char tmp[FRONTEND_MAX_PATH];
   int config_ret = -1;

   frontend_ensure_data_dirs();
   snprintf(tmp, sizeof(tmp), "%s.tmp", UNIFROG_CONFIG_PATH);
   file = fopen(tmp, "wb");
   if (!file)
      return -1;
   fprintf(file,
      "# UniFrog configuration. Edit /unifrog_data/unifrog.ini or use Config\n"
      "# on the device. Lines beginning with # or ; are comments. Boolean\n"
      "# values use 0=off and 1=on. Unknown keys are ignored.\n\n"
      "# Gameplay and libretro launch defaults.\n"
      "# audio: 0 or 1. gain: integer multiplier from 0 through 4.\n"
      "# cpu: target MHz: 0, 198, 297, 396, 594, 702, 756, 808, 810, 864,\n"
      "# or 918. Zero uses the platform default.\n"
      "# frameskip: 0=off, 1=auto, 2=fixed one, 3=fixed two.\n"
      "# display: 0=fit, 1=stretch, 2=original. framebuffer: 0=RGB565,\n"
      "# 1=XRGB8888. ge_clock/backlight: -1=platform default.\n"
      "# keymap: 0=default, 1=RetroArch, 2=Genesis, 3=swap AB, 4=swap XY.\n"
      "# state_slot: 0..9; state_auto_load/state_auto_save: 0 or 1.\n"
      "# rtc_offset_minutes: -5270400..5270400. This changes the clock seen\n"
      "# by RTC-dependent games without changing the device clock. The device\n"
      "# menus adjust it in one-day steps. Zero uses the device clock.\n");
   fprintf(file, "audio=%d\n", fe->run_options.audio_enabled);
   fprintf(file, "cpu=%u\n", fe->run_options.scpu_mhz);
   fprintf(file, "frameskip=%d\n", fe->run_options.frameskip);
   fprintf(file, "display=%d\n", fe->run_options.display_mode);
   fprintf(file, "framebuffer=%d\n", fe->run_options.framebuffer_format);
   fprintf(file, "gain=%u\n", fe->run_options.audio_gain);
   fprintf(file, "ge_clock=%d\n", fe->run_options.ge_clock);
   fprintf(file, "backlight=%d\n", fe->run_options.backlight_level);
   fprintf(file, "keymap=%d\n", fe->run_options.input_profile);
   fprintf(file, "state_slot=%u\n",
      clamp_state_slot(fe->run_options.state_slot));
   fprintf(file, "state_auto_load=%d\n",
      fe->run_options.state_auto_load ? 1 : 0);
   fprintf(file, "state_auto_save=%d\n",
      fe->run_options.state_auto_save ? 1 : 0);
   fprintf(file, "rtc_offset_minutes=%d\n",
      fe->run_options.rtc_offset_minutes);
   fprintf(file,
      "\n# Browser, collection, and menu behavior. All values below are 0 or 1.\n"
      "# sort_desc reverses sorting; show_hidden displays dot files;\n"
      "# mixed_content includes media beside ROMs; content_collect and\n"
      "# content_history control favorites and recent-history recording.\n");
   fprintf(file, "sort_desc=%d\n", fe->sort_desc);
   fprintf(file, "show_hidden=%d\n", fe->show_hidden);
   fprintf(file, "folder_counts=%d\n", fe->folder_counts);
   fprintf(file, "mixed_content=%d\n", fe->mixed_content);
   fprintf(file, "display_empty_folder=%d\n", fe->display_empty_folder);
   fprintf(file, "menu_counter_folder=%d\n", fe->menu_counter_folder);
   fprintf(file, "menu_counter_file=%d\n", fe->menu_counter_file);
   fprintf(file, "content_collect=%d\n", fe->content_collect);
   fprintf(file, "content_history=%d\n", fe->content_history);
   fprintf(file, "clock_enabled=%d\n", fe->clock_enabled);
   fprintf(file, "title_include_root=%d\n", fe->title_include_root);
   fprintf(file, "theme_alternate=%d\n", fe->theme_alternate);
   fprintf(file, "boxart_hidden=%d\n", fe->boxart_hidden);
   fprintf(file, "launch_splash=%d\n", fe->launch_splash);
   fprintf(file, "sound_enabled=%d\n", fe->sound_enabled);
   fprintf(file,
      "\n# Logging. level: trace, debug, info, warn, error, or off. Trace is\n"
      "# the most detailed. Logs stay in retained RAM during gameplay and are\n"
      "# flushed when a game is paused or closed.\n");
   fprintf(file, "log_level=%s\n", fe->log_level);
   fprintf(file,
      "\n# Appearance and language. Names select files under themes/ and languages/.\n");
   fprintf(file, "language_name=%s\n", active_language_label(fe));
   fprintf(file, "theme_name=%s\n", active_theme_label(fe));
   fprintf(file,
      "\n# ROM artwork. A | separates fallback templates; the first readable\n"
      "# file is used. Tokens: {rom_dir}, {system} (ROM parent folder),\n"
      "# {name} (filename without extension), and {filename}. Relative paths\n"
      "# start at the SD root. artwork_layout is muos, skraper, beside, or\n"
      "# custom. Selecting a preset on-device replaces all three templates.\n");
   fprintf(file, "artwork_layout=%s\n", fe->artwork_layout);
   fprintf(file, "artwork_box_templates=%s\n", fe->artwork_box_templates);
   fprintf(file, "artwork_preview_templates=%s\n",
      fe->artwork_preview_templates);
   fprintf(file, "artwork_text_templates=%s\n", fe->artwork_text_templates);
   fprintf(file, "\n# Hardware selection and SD-card behavior.\n"
      "# device_board: auto, sf2000, or gb300.\n"
      "# Storage profiles are runtime settings; the build boots with wide25.\n"
      "# Profiles: auto, boot, safe, wide1, wide2, wide4, wide8,\n"
      "# wide10, wide12, wide14, wide16, wide18, wide20, wide22, wide24,\n"
      "# wide25, wide37, hs1, wide50, wide, uhs12, uhs25, or uhs.\n"
      "# storage_profile=auto uses storage_normal_profile and tries\n"
      "# storage_fallback_profile only when the normal profile fails. Battery\n"
      "# warnings require three consecutive low samples, never change the SD\n"
      "# mode, and ask you to save your game and charge.\n");
   fprintf(file, "device_board=%s\n", fe->device_board);
   fprintf(file, "storage_profile=%s\n", fe->storage_profile);
   fprintf(file, "storage_normal_profile=%s\n", fe->storage_normal_profile);
   fprintf(file, "storage_fallback_profile=%s\n",
      fe->storage_fallback_profile);
   fprintf(file,
      "\n# Battery calibration in millivolts. Values must increase from empty\n"
      "# through full and accept 2500..5000. These define 0/25/50/75/100%%.\n"
      "# battery_discharge_mv_per_hour accepts 0..2000; 0 hides remaining time.\n"
      "# battery_estimate_discharge: 1 learns a noise-filtered rate while\n"
      "# discharging; 0 always uses the configured rate.\n");
   fprintf(file, "battery_mv_empty=%u\n",
      fe->battery_calibration.millivolts[0]);
   fprintf(file, "battery_mv_25=%u\n",
      fe->battery_calibration.millivolts[1]);
   fprintf(file, "battery_mv_50=%u\n",
      fe->battery_calibration.millivolts[2]);
   fprintf(file, "battery_mv_75=%u\n",
      fe->battery_calibration.millivolts[3]);
   fprintf(file, "battery_mv_full=%u\n",
      fe->battery_calibration.millivolts[4]);
   fprintf(file, "battery_discharge_mv_per_hour=%u\n",
      fe->battery_calibration.discharge_mv_per_hour);
   fprintf(file, "battery_estimate_discharge=%d\n",
      fe->battery_calibration.estimate_discharge ? 1 : 0);
   fprintf(file,
      "\n# Default boot target. Use unifrog, or an SD-root-relative .asd path\n"
      "# without spaces or dot components. Hold B during startup to cancel a\n"
      "# configured default and open UniFrog. Set this from Power/Firmware Boot\n"
      "# by highlighting firmware and pressing X.\n");
   fprintf(file, "default_boot=%s\n", fe->default_boot);
   fprintf(file, "rom_root=%s\n", frontend_rom_root(fe));
   fprintf(file, "rom_roots=");
   for (unsigned i = 0; i < frontend_rom_root_count(fe); i++) {
      const char *root = frontend_rom_root_at(fe, i);

      if (i)
         fputc('|', file);
      fprintf(file, "%s", root ? root : "");
   }
   fputc('\n', file);
   fprintf(file, "rom_root_label=%s\n", frontend_rom_root_label(fe));
   fprintf(file,
      "# rom_system=FOLDER:CORE_ID selects a core below a folder. Names are\n"
      "# case-insensitive; later duplicates replace earlier entries. Folder\n"
      "# mappings override generic extension rules, including .zip. Built-in\n"
      "# fallbacks cover common names such as ARCADE, NEOGEO, ATARI, GB, GBC,\n"
      "# GBA, FC/NES, SFC/SNES, GG, PCE/TG16, and PS/PS1/PSX.\n");
   for (unsigned i = 0; i < fe->rom_system_count; i++) {
      if (fe->rom_system_name[i][0] && fe->rom_system_core[i][0])
         fprintf(file, "rom_system=%s:%s\n", fe->rom_system_name[i],
            fe->rom_system_core[i]);
   }
   fprintf(file,
      "\n# Advanced media tuning. Ordinary users should leave these unchanged.\n"
      "# Values ending in _ms are signed/unsigned milliseconds; _size and\n"
      "# _bytes are byte counts; _slots and _packets are non-negative counts.\n"
      "# Unsigned/size values accept 0..4294967295 on the device; signed\n"
      "# values accept -2147483648..2147483647; booleans accept only 0 or 1.\n"
      "# Delete overrides to use defaults.\n");
   save_media_tuning(file, &fe->media_tuning);
   save_associations(file, fe);
   fprintf(file,
      "\n# Optional scoped launch settings. Add [core.CORE_ID] or\n"
      "# [rom.ABSOLUTE_PATH] sections. Core sections apply first and exact\n"
      "# ROM-path sections override them. Paths are case-sensitive.\n"
      "# Supported keys: core, audio,\n"
      "# gain, cpu, ge_clock, backlight, frameskip, display, framebuffer,\n"
      "# keymap, state_slot, state_auto_load, state_auto_save, and\n"
      "# rtc_offset_minutes. The core\n"
      "# key is valid only in a rom section. Value types and ranges are the\n"
      "# same as the global gameplay settings documented above. The in-game\n"
      "# UniFrog menu can save or clear these sections for a core or game.\n");
   fprintf(file,
      "\n# Existing scoped and libretro-option sections are preserved below.\n"
      "# Libretro option names and values are supplied by each installed core.\n");
   save_preserved_sections(file);
   if (fclose(file) == 0) {
      config_ret = unifrog_config_commit(tmp, UNIFROG_CONFIG_PATH);
      if (config_ret != 0) {
         unlink(tmp);
         unifrog_log("frontend config save commit_failed path=%s\n",
            UNIFROG_CONFIG_PATH);
      }
   } else {
      unlink(tmp);
   }

   snprintf(tmp, sizeof(tmp), "%s.tmp", UNIFROG_SESSION_PATH);
   file = fopen(tmp, "wb");
   if (file) {
      fprintf(file,
         "# UniFrog session state. Automatically updated; deleting it is safe.\n"
         "last_path=%s\nlast_core=%s\n", fe->last_path, fe->last_core);
      if (fclose(file) == 0) {
         if (unifrog_config_commit(tmp, UNIFROG_SESSION_PATH) != 0)
            unlink(tmp);
      } else {
         unlink(tmp);
      }
   }
   return config_ret;
}

void load_settings(struct frontend_state *fe)
{
   struct settings_load_context context;
   unsigned errors = 0;

   unifrog_frontend_config_init(&fe->scoped_config);
   memset(&context, 0, sizeof(context));
   context.fe = fe;
   if (unifrog_config_read(UNIFROG_CONFIG_PATH, load_settings_entry, &context,
       &errors) == 0 && errors)
      unifrog_log("frontend config parse_errors=%u path=%s\n", errors,
         UNIFROG_CONFIG_PATH);
   if (context.primary_rom_root_seen)
      (void)frontend_rom_root_set_primary(fe, context.primary_rom_root);
   if (fe->scoped_config.overflowed)
      unifrog_log("frontend scoped_config capacity=%u path=%s\n",
         UNIFROG_FRONTEND_SCOPED_CONFIG_MAX, UNIFROG_CONFIG_PATH);
   fe->run_options.state_slot = clamp_state_slot(fe->run_options.state_slot);
   frontend_rom_root_sync_primary(fe);
   normalize_storage_profile(fe);
   normalize_battery_calibration(fe);
   if (unifrog_device_set_board_override(fe->device_board) != 0)
      unifrog_text_copy(fe->device_board, sizeof(fe->device_board), "auto");
   normalize_storage_profile(fe);
   normalize_battery_calibration(fe);
   frontend_rom_root_sync_primary(fe);
   errors = 0;
   if (unifrog_config_read(UNIFROG_SESSION_PATH, load_session_entry, fe,
       &errors) == 0 && errors)
      unifrog_log("frontend session parse_errors=%u path=%s\n", errors,
         UNIFROG_SESSION_PATH);
}
