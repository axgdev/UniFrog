#include <unifrog/frontend_config.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <unifrog/config.h>
#include <unifrog/libretro_policy.h>

enum scoped_option_mask {
   SCOPED_CORE = 1u << 0,
   SCOPED_AUDIO = 1u << 1,
   SCOPED_GAIN = 1u << 2,
   SCOPED_CPU = 1u << 3,
   SCOPED_GE_CLOCK = 1u << 4,
   SCOPED_BACKLIGHT = 1u << 5,
   SCOPED_FRAMESKIP = 1u << 6,
   SCOPED_DISPLAY = 1u << 7,
   SCOPED_FRAMEBUFFER = 1u << 8,
   SCOPED_KEYMAP = 1u << 9,
   SCOPED_STATE_SLOT = 1u << 10,
   SCOPED_STATE_AUTO_LOAD = 1u << 11,
   SCOPED_STATE_AUTO_SAVE = 1u << 12,
   SCOPED_RTC_OFFSET = 1u << 13,
};

static void text_copy(char *dst, size_t size, const char *src)
{
   if (size)
      snprintf(dst, size, "%s", src ? src : "");
}

static int parse_int(const char *text, int minimum, int maximum, int *out)
{
   char *end;
   long value;

   if (!text || !text[0] || !out)
      return -1;
   errno = 0;
   value = strtol(text, &end, 10);
   if (errno || !end || *end || value < minimum || value > maximum)
      return -1;
   *out = (int)value;
   return 0;
}

static void set_int_option(const char *value, int *field, unsigned *mask,
   unsigned bit, int minimum, int maximum)
{
   int parsed;

   if (parse_int(value, minimum, maximum, &parsed) == 0) {
      *field = parsed;
      *mask |= bit;
   }
}

static void set_unsigned_option(const char *value, unsigned *field,
   unsigned *mask, unsigned bit, unsigned maximum)
{
   int parsed;

   if (maximum <= (unsigned)INT_MAX &&
       parse_int(value, 0, (int)maximum, &parsed) == 0) {
      *field = (unsigned)parsed;
      *mask |= bit;
   }
}

static struct unifrog_frontend_scoped_config *find_or_add(
   struct unifrog_frontend_config *config,
   enum unifrog_frontend_config_scope scope, const char *target)
{
   for (unsigned i = 0; i < config->count; i++) {
      if (config->entries[i].scope == scope &&
          strcmp(config->entries[i].target, target) == 0)
         return &config->entries[i];
   }
   if (config->count >= UNIFROG_FRONTEND_SCOPED_CONFIG_MAX) {
      config->overflowed = 1;
      return NULL;
   }
   struct unifrog_frontend_scoped_config *entry =
      &config->entries[config->count++];
   memset(entry, 0, sizeof(*entry));
   entry->scope = scope;
   text_copy(entry->target, sizeof(entry->target), target);
   return entry;
}

int unifrog_frontend_config_parse_entry(
   struct unifrog_frontend_config *config, const char *section,
   const char *key, const char *value)
{
   struct unifrog_frontend_scoped_config *entry;
   enum unifrog_frontend_config_scope scope;
   const char *target;

   if (!config || !section || !key || !value)
      return -1;
   if (strncmp(section, "core.", 5) == 0) {
      scope = UNIFROG_FRONTEND_CONFIG_CORE;
      target = section + 5;
   } else if (strncmp(section, "rom.", 4) == 0) {
      scope = UNIFROG_FRONTEND_CONFIG_ROM;
      target = section + 4;
   } else {
      return 0;
   }
   if (!target[0])
      return 0;
   entry = find_or_add(config, scope, target);
   if (!entry)
      return 0;
   if (strcmp(key, "core") == 0 && scope == UNIFROG_FRONTEND_CONFIG_ROM) {
      text_copy(entry->options.core_id, sizeof(entry->options.core_id), value);
      entry->option_mask |= SCOPED_CORE;
   } else if (strcmp(key, "audio") == 0) {
      set_int_option(value, &entry->options.audio_enabled,
         &entry->option_mask, SCOPED_AUDIO, 0, 1);
   } else if (strcmp(key, "gain") == 0) {
      set_unsigned_option(value, &entry->options.audio_gain,
         &entry->option_mask, SCOPED_GAIN, 4u);
   } else if (strcmp(key, "cpu") == 0) {
      int parsed;

      if (parse_int(value, 0, INT_MAX, &parsed) == 0 &&
          unifrog_libretro_policy_cpu_valid((unsigned)parsed)) {
         entry->options.scpu_mhz = (unsigned)parsed;
         entry->option_mask |= SCOPED_CPU;
      }
   } else if (strcmp(key, "ge_clock") == 0) {
      set_int_option(value, &entry->options.ge_clock, &entry->option_mask,
         SCOPED_GE_CLOCK, -1, 3);
   } else if (strcmp(key, "backlight") == 0) {
      set_int_option(value, &entry->options.backlight_level,
         &entry->option_mask, SCOPED_BACKLIGHT, -1, 100);
   } else if (strcmp(key, "frameskip") == 0) {
      set_int_option(value, &entry->options.frameskip, &entry->option_mask,
         SCOPED_FRAMESKIP, 0, 3);
   } else if (strcmp(key, "display") == 0) {
      set_int_option(value, &entry->options.display_mode,
         &entry->option_mask, SCOPED_DISPLAY, 0, 2);
   } else if (strcmp(key, "framebuffer") == 0) {
      set_int_option(value, &entry->options.framebuffer_format,
         &entry->option_mask, SCOPED_FRAMEBUFFER, 0, 1);
   } else if (strcmp(key, "keymap") == 0) {
      set_int_option(value, &entry->options.input_profile,
         &entry->option_mask, SCOPED_KEYMAP, 0, 4);
   } else if (strcmp(key, "state_slot") == 0) {
      set_unsigned_option(value, &entry->options.state_slot,
         &entry->option_mask, SCOPED_STATE_SLOT, 9u);
   } else if (strcmp(key, "state_auto_load") == 0) {
      set_int_option(value, &entry->options.state_auto_load,
         &entry->option_mask, SCOPED_STATE_AUTO_LOAD, 0, 1);
   } else if (strcmp(key, "state_auto_save") == 0) {
      set_int_option(value, &entry->options.state_auto_save,
         &entry->option_mask, SCOPED_STATE_AUTO_SAVE, 0, 1);
   } else if (strcmp(key, "rtc_offset_minutes") == 0) {
      set_int_option(value, &entry->options.rtc_offset_minutes,
         &entry->option_mask, SCOPED_RTC_OFFSET, -5270400, 5270400);
   }
   return 0;
}

static int config_entry(void *userdata, const char *section, const char *key,
   const char *value, unsigned line_number)
{
   (void)line_number;
   return unifrog_frontend_config_parse_entry(userdata, section, key, value);
}

void unifrog_frontend_config_init(struct unifrog_frontend_config *config)
{
   if (config)
      memset(config, 0, sizeof(*config));
}

int unifrog_frontend_config_load(struct unifrog_frontend_config *config,
   const char *path, unsigned *error_count)
{
   if (!config)
      return -2;
   unifrog_frontend_config_init(config);
   return unifrog_config_read(path, config_entry, config, error_count);
}

static void apply_entry(const struct unifrog_frontend_scoped_config *entry,
   struct unifrog_libretro_run_options *options, int include_core)
{
   const struct unifrog_frontend_scoped_options *value = &entry->options;
   unsigned mask = entry->option_mask;

   if (include_core && (mask & SCOPED_CORE))
      text_copy(options->core_id, sizeof(options->core_id), value->core_id);
   if (mask & SCOPED_AUDIO) options->audio_enabled = value->audio_enabled;
   if (mask & SCOPED_GAIN) options->audio_gain = value->audio_gain;
   if (mask & SCOPED_CPU) options->scpu_mhz = value->scpu_mhz;
   if (mask & SCOPED_GE_CLOCK) options->ge_clock = value->ge_clock;
   if (mask & SCOPED_BACKLIGHT)
      options->backlight_level = value->backlight_level;
   if (mask & SCOPED_FRAMESKIP) options->frameskip = value->frameskip;
   if (mask & SCOPED_DISPLAY) options->display_mode = value->display_mode;
   if (mask & SCOPED_FRAMEBUFFER)
      options->framebuffer_format = value->framebuffer_format;
   if (mask & SCOPED_KEYMAP) options->input_profile = value->input_profile;
   if (mask & SCOPED_STATE_SLOT) options->state_slot = value->state_slot;
   if (mask & SCOPED_STATE_AUTO_LOAD)
      options->state_auto_load = value->state_auto_load;
   if (mask & SCOPED_STATE_AUTO_SAVE)
      options->state_auto_save = value->state_auto_save;
   if (mask & SCOPED_RTC_OFFSET)
      options->rtc_offset_minutes = value->rtc_offset_minutes;
}

void unifrog_frontend_config_apply(const struct unifrog_frontend_config *config,
   const char *initial_core, const char *path,
   struct unifrog_libretro_run_options *options)
{
   const char *selected_core;

   if (!config || !options)
      return;
   text_copy(options->core_id, sizeof(options->core_id), initial_core);
   for (unsigned i = 0; path && i < config->count; i++) {
      const struct unifrog_frontend_scoped_config *entry = &config->entries[i];

      if (entry->scope == UNIFROG_FRONTEND_CONFIG_ROM &&
          strcmp(entry->target, path) == 0 && (entry->option_mask & SCOPED_CORE))
         text_copy(options->core_id, sizeof(options->core_id),
            entry->options.core_id);
   }
   selected_core = options->core_id;
   for (unsigned i = 0; selected_core[0] && i < config->count; i++) {
      const struct unifrog_frontend_scoped_config *entry = &config->entries[i];

      if (entry->scope == UNIFROG_FRONTEND_CONFIG_CORE &&
          strcmp(entry->target, selected_core) == 0)
         apply_entry(entry, options, 0);
   }
   for (unsigned i = 0; path && i < config->count; i++) {
      const struct unifrog_frontend_scoped_config *entry = &config->entries[i];

      if (entry->scope == UNIFROG_FRONTEND_CONFIG_ROM &&
          strcmp(entry->target, path) == 0)
         apply_entry(entry, options, 1);
   }
}
