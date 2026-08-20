#include "unifrog_libretro_internal.h"

#include <unifrog/libretro_policy.h>
#include <unifrog/libretro_session.h>

int exit_combo_down(void)
{
   return (host.buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT)) &&
      (host.buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START));
}

static unsigned session_backlight(void)
{
   unsigned current = 0;

   if (unifrog_backlight_get(&current) != 0)
      current = host.options.backlight_level >= 0 ?
         (unsigned)host.options.backlight_level : 50u;
   return current > 100u ? 100u : current;
}

static unsigned session_scpu_mhz(void)
{
   return host.scpu_target_mhz ? host.scpu_target_mhz :
      unifrog_scpu_current_mhz();
}

static void session_audio_gate_close(void)
{
   host.audio_gate_open = 0;
   host.audio_quiet_frames = 0;
   if (host.audio_open)
      (void)unifrog_audio_set_output_enabled(&host.audio, 0);
}

static int session_cycle_fast_forward(int delta)
{
   host.fast_forward_multiplier = unifrog_libretro_policy_fast_forward(
      sanitize_fast_forward_multiplier(host.fast_forward_multiplier), delta);
   host.fast_forward = host.fast_forward_multiplier > 0;
   host.fast_forward_force_present = host.fast_forward;
   host.variables_dirty = 1;
   host.frame_deadline_us = host_time_us();
   session_audio_gate_close();
   return (int)host.fast_forward_multiplier;
}

static int session_cycle_frameskip(int delta)
{
   host.options.frameskip = unifrog_libretro_policy_frameskip(
      host.options.frameskip, delta);
   host.variables_dirty = 1;
   return host.options.frameskip;
}

static int session_cycle_display(int delta)
{
   host.display_mode = unifrog_libretro_policy_display(host.display_mode,
      delta);
   host.options.display_mode = host.display_mode;
   if (host.presenter_open) {
      host.presenter.flags = present_flags_for_display_mode(host.display_mode);
      host.presenter.cleared_buffer_mask = 0;
   }
   return host.display_mode;
}

static int session_cycle_input(int delta)
{
   host.input_profile = sanitize_input_profile(host.input_profile);
   host.input_profile = unifrog_libretro_policy_input_profile(
      host.input_profile, delta);
   host.options.input_profile = host.input_profile;
   host.input_poll_frame = 0;
   host.input_profile_dirty = 1;
   host.core_options_dirty = 1;
   return host.input_profile;
}

static int session_cycle_backlight(int delta)
{
   unsigned current = session_backlight();
   unsigned next = unifrog_libretro_policy_level(quick_backlight_levels,
      ARRAY_SIZE(quick_backlight_levels), current, delta);
   (void)unifrog_backlight_set(next);
   host.options.backlight_level = (int)next;
   return (int)next;
}

static int session_cycle_scpu(int delta)
{
   unsigned current = session_scpu_mhz();
   unsigned next = unifrog_libretro_policy_cpu(current, delta, 0);
   if (!host.scpu_restore_valid)
      host.scpu_restore_valid =
         unifrog_scpu_capture(&host.scpu_restore) == 0 &&
         host.scpu_restore.valid;
   host.scpu_apply_ret = unifrog_scpu_apply_mhz(next);
   if (host.scpu_apply_ret != 0)
      return -1;
   host.scpu_target_mhz = next;
   host.options.scpu_mhz = next;
   if (host.fps)
      host.frame_budget_count = host_compute_frame_budget(host.fps,
         &host.scpu_mhz_est, &host.count_hz_est, &host.count_hz_calibrated);
   (void)libretro_log_flush_force_if_safe();
   return (int)next;
}

static int session_cycle_ge(int delta)
{
   host.ge_clock = (enum unifrog_ge_clock)unifrog_libretro_policy_ge_clock(
      host.ge_clock, delta);
   host.options.ge_clock = (int)host.ge_clock;
   if (host.presenter_open)
      (void)unifrog_ge_set_clock(&host.presenter.ge, host.ge_clock);
   return host.options.ge_clock;
}

static int session_cycle_rtc_offset(int delta)
{
   int next = host.options.rtc_offset_minutes +
      (delta < 0 ? -(24 * 60) : (24 * 60));

   if (next < -5270400)
      next = 5270400;
   else if (next > 5270400)
      next = -5270400;
   if (unifrog_clock_set_runtime_offset_minutes(next) != 0)
      return -1;
   host.options.rtc_offset_minutes = next;
   return next;
}

int unifrog_libretro_session_menu_begin(const void *core,
   const char *content_path)
{
   if (!host.presenter_open)
      return -1;
   session_audio_gate_close();
   host.quick_menu_action = QUICK_MENU_ACTION_RESUME;
   host.quick_combo_armed = 0;
   host.quick_status[0] = '\0';
   host.quick_core = (const struct libretro_core_api *)core;
   host.quick_rom_path = content_path;
   if (host.quick_state_slot >= LIBRETRO_STATE_SLOT_COUNT)
      host.quick_state_slot = 0;
   return 0;
}

int unifrog_libretro_session_menu_finish(int return_to_frontend)
{
   host.quick_menu_action = return_to_frontend ?
      QUICK_MENU_ACTION_RETURN_MENU : QUICK_MENU_ACTION_RESUME;
   host.presenter.cleared_buffer_mask = 0;
   if (host.fast_forward)
      host.fast_forward_force_present = 1;
   return return_to_frontend ? 1 : 0;
}

void unifrog_libretro_session_snapshot(
   struct unifrog_libretro_session_snapshot *snapshot)
{
   if (!snapshot)
      return;
   memset(snapshot, 0, sizeof(*snapshot));
   snapshot->state_slot = host.quick_state_slot;
   snapshot->fast_forward_multiplier =
      sanitize_fast_forward_multiplier(host.fast_forward_multiplier);
   snapshot->scpu_mhz = session_scpu_mhz();
   snapshot->backlight = session_backlight();
   snapshot->frameskip = host.options.frameskip;
   snapshot->audio_enabled = host.audio_enabled;
   snapshot->display_mode = host.display_mode;
   snapshot->input_profile = host.input_profile;
   snapshot->ge_clock = host.ge_clock;
   snapshot->rtc_offset_minutes = host.options.rtc_offset_minutes;
   snprintf(snapshot->frameskip_label, sizeof(snapshot->frameskip_label), "%s",
      unifrog_libretro_policy_frameskip_label(host.options.frameskip));
   snprintf(snapshot->display_label, sizeof(snapshot->display_label), "%s",
      display_mode_label(host.display_mode));
   snprintf(snapshot->input_profile_label,
      sizeof(snapshot->input_profile_label), "%s",
      input_profile_opt_value(host.input_profile));
   snprintf(snapshot->ge_label, sizeof(snapshot->ge_label), "%s",
      unifrog_libretro_policy_ge_label(host.ge_clock));
   snprintf(snapshot->status, sizeof(snapshot->status), "%s",
      host.quick_status);
}

int unifrog_libretro_session_adjust(
   enum unifrog_libretro_session_adjustment adjustment, int delta)
{
   switch (adjustment) {
   case UNIFROG_LIBRETRO_SESSION_FAST_FORWARD:
      return session_cycle_fast_forward(delta);
   case UNIFROG_LIBRETRO_SESSION_FRAMESKIP:
      return session_cycle_frameskip(delta);
   case UNIFROG_LIBRETRO_SESSION_AUDIO:
      host.audio_enabled = !host.audio_enabled;
      host.options.audio_enabled = host.audio_enabled;
      session_audio_gate_close();
      return host.audio_enabled;
   case UNIFROG_LIBRETRO_SESSION_DISPLAY:
      return session_cycle_display(delta);
   case UNIFROG_LIBRETRO_SESSION_INPUT_PROFILE:
      return session_cycle_input(delta);
   case UNIFROG_LIBRETRO_SESSION_CPU:
      return session_cycle_scpu(delta);
   case UNIFROG_LIBRETRO_SESSION_GE:
      return session_cycle_ge(delta);
   case UNIFROG_LIBRETRO_SESSION_BACKLIGHT:
      return session_cycle_backlight(delta);
   case UNIFROG_LIBRETRO_SESSION_RTC_OFFSET:
      return session_cycle_rtc_offset(delta);
   default:
      return -1;
   }
}

unsigned unifrog_libretro_session_option_count(void)
{
   unsigned count = 0;

   for (unsigned i = 0; i < host.core_option_count; i++) {
      if (host.core_options[i].visible && host.core_options[i].value_count)
         count++;
   }
   return count;
}

static struct quick_core_option *session_option(unsigned visible_index)
{
   unsigned count = 0;

   for (unsigned i = 0; i < host.core_option_count; i++) {
      if (!host.core_options[i].visible || !host.core_options[i].value_count)
         continue;
      if (count++ == visible_index)
         return &host.core_options[i];
   }
   return NULL;
}

int unifrog_libretro_session_option_get(unsigned index,
   struct unifrog_libretro_session_option *option)
{
   struct quick_core_option *source = session_option(index);
   const char *value;

   if (!source || !option || source->selected >= source->value_count)
      return -1;
   value = source->value_labels[source->selected][0] ?
      source->value_labels[source->selected] :
      source->values[source->selected];
   snprintf(option->label, sizeof(option->label), "%s", source->label);
   snprintf(option->value, sizeof(option->value), "%s", value);
   return 0;
}

int unifrog_libretro_session_option_adjust(unsigned index, int delta)
{
   struct quick_core_option *option = session_option(index);

   if (!option)
      return -1;
   option->selected = delta < 0 ?
      (option->selected == 0 ? option->value_count - 1u :
       option->selected - 1u) :
      (option->selected + 1u) % option->value_count;
   host.variables_dirty = 1;
   host.core_options_dirty = 1;
   return 0;
}

int unifrog_libretro_session_state(
   enum unifrog_libretro_session_state_action action, unsigned slot)
{
   if (slot >= LIBRETRO_STATE_SLOT_COUNT)
      return -1;
   host.quick_state_slot = slot;
   return action == UNIFROG_LIBRETRO_SESSION_STATE_LOAD ?
      quick_load_state_file() : quick_save_state_file();
}

static int session_settings_section(char *section, size_t size,
   enum unifrog_libretro_session_scope scope, const char *prefix)
{
   const char *target = scope == UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT ?
      host.quick_rom_path : host.core_id;
   int written;

   if (!section || !size || !prefix || !target || !target[0])
      return -1;
   written = snprintf(section, size, "%s%s", prefix, target);
   return written >= 0 && (size_t)written < size ? 0 : -1;
}

static int session_settings_write(FILE *file, void *userdata)
{
   enum unifrog_libretro_session_scope scope =
      *(const enum unifrog_libretro_session_scope *)userdata;

   fprintf(file, "# Saved from the in-game UniFrog settings menu.\n");
   if (scope == UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT)
      fprintf(file, "core=%s\n", host.core_id ? host.core_id : "");
   fprintf(file, "audio=%d\n", host.audio_enabled ? 1 : 0);
   fprintf(file, "gain=%u\n", host.audio_gain);
   fprintf(file, "cpu=%u\n", session_scpu_mhz());
   fprintf(file, "ge_clock=%d\n", host.ge_clock);
   fprintf(file, "backlight=%u\n", session_backlight());
   fprintf(file, "frameskip=%d\n", host.options.frameskip);
   fprintf(file, "display=%d\n", host.display_mode);
   fprintf(file, "framebuffer=%d\n", host.options.framebuffer_format);
   fprintf(file, "keymap=%d\n", host.input_profile);
   fprintf(file, "state_slot=%u\n", host.quick_state_slot);
   fprintf(file, "state_auto_load=%d\n",
      host.options.state_auto_load ? 1 : 0);
   fprintf(file, "state_auto_save=%d\n",
      host.options.state_auto_save ? 1 : 0);
   fprintf(file, "rtc_offset_minutes=%d\n",
      host.options.rtc_offset_minutes);
   return ferror(file) ? -1 : 0;
}

int unifrog_libretro_session_save_settings(
   enum unifrog_libretro_session_scope scope)
{
   char section[320];

   if (session_settings_section(section, sizeof(section), scope,
       scope == UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT ? "rom." :
       "core.") != 0)
      return -1;
   (void)mkdir(UNIFROG_DATA_ROOT, 0777);
   return unifrog_config_replace_section(UNIFROG_CONFIG_PATH, section,
      session_settings_write, &scope);
}

int unifrog_libretro_session_clear_settings(
   enum unifrog_libretro_session_scope scope)
{
   char section[320];

   if (session_settings_section(section, sizeof(section), scope,
       scope == UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT ? "rom." :
       "core.") != 0)
      return -1;
   return unifrog_config_remove_section(UNIFROG_CONFIG_PATH, section);
}

int unifrog_libretro_session_save_core_options(
   enum unifrog_libretro_session_scope scope)
{
   return core_options_save_scope(
      scope == UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT);
}

int unifrog_libretro_session_clear_core_options(
   enum unifrog_libretro_session_scope scope)
{
   return core_options_clear_scope(
      scope == UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT);
}

void unifrog_libretro_session_status(const char *status)
{
   snprintf(host.quick_status, sizeof(host.quick_status), "%s",
      status ? status : "");
}

int unifrog_libretro_session_surface(struct unifrog_surface *surface,
   unsigned *buffer)
{
   unsigned target;

   if (!surface || !buffer || !host.presenter_open)
      return -1;
   target = host.presenter.active_buffer;
   if (host.presenter.buffer_count > 1)
      target = (target + 1u) % host.presenter.buffer_count;
   *surface = unifrog_fb_surface_for_buffer(&host.presenter.fb, target);
   *buffer = target;
   return 0;
}

void unifrog_libretro_session_present(unsigned buffer)
{
   unifrog_fb_flush_buffer(&host.presenter.fb, buffer);
   if (unifrog_fb_pan(&host.presenter.fb, buffer) == 0)
      host.presenter.active_buffer = buffer;
}

void unifrog_libretro_session_note_buttons(uint32_t buttons)
{
   host.buttons = buttons;
}
