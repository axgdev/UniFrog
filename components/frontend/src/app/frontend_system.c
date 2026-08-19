#include "frontend_internal.h"

void frontend_show_power(struct frontend_state *fe)
{
   char detail[48];

   reset_items(fe, "Power");
   fe->view = FRONTEND_VIEW_POWER;
   add_item(fe, "Reboot", "restart UniFrog", FRONTEND_ITEM_ACTION,
      "reboot", NULL);
   add_item(fe, "Firmware Boot", "select .asd", FRONTEND_ITEM_ACTION,
      "firmware", NULL);
   add_item(fe, "Default Boot", fe->default_boot, FRONTEND_ITEM_ACTION,
      "firmware", NULL);
   add_item(fe, "Default to UniFrog", "B cancels a firmware default",
      FRONTEND_ITEM_ACTION, "default_boot_clear", NULL);
   add_item(fe, "Date & Time", "set pseudo RTC", FRONTEND_ITEM_ACTION,
      "clock_settings", NULL);
   add_item(fe, "Safe Shutdown", "flush writes, then power off",
      FRONTEND_ITEM_ACTION, "safe_shutdown", NULL);
   if (unifrog_battery_update(&fe->battery, 0) != 0)
      unifrog_battery_status_init(&fe->battery);
   if (fe->battery.available)
      snprintf(detail, sizeof(detail), "%u%%  %u mV",
         fe->battery.percent, fe->battery.millivolts);
   else
      snprintf(detail, sizeof(detail), "not available");
   add_item(fe, "Battery", detail, FRONTEND_ITEM_ACTION, "battery_refresh",
      NULL);
   for (unsigned i = 0; i < UNIFROG_BATTERY_POINT_COUNT; i++) {
      static const char *const labels[] = {
         "Battery Empty", "Battery 25%", "Battery 50%", "Battery 75%",
         "Battery Full",
      };
      char point[16];
      char index[4];

      snprintf(point, sizeof(point), "%u mV",
         fe->battery_calibration.millivolts[i]);
      snprintf(index, sizeof(index), "%u", i);
      add_item(fe, labels[i], point, FRONTEND_ITEM_ACTION, "battery_point",
         index);
   }
   snprintf(detail, sizeof(detail), "%u mV/hour",
      fe->battery_calibration.discharge_mv_per_hour);
   add_item(fe, "Discharge Rate", detail, FRONTEND_ITEM_ACTION,
      "battery_rate", NULL);
   add_item(fe, "Learn Discharge Rate",
      on_off_label(fe->battery_calibration.estimate_discharge),
      FRONTEND_ITEM_ACTION, "battery_estimate", NULL);
   if (fe->battery.estimated_minutes)
      snprintf(detail, sizeof(detail), "%u minutes",
         fe->battery.estimated_minutes);
   else
      snprintf(detail, sizeof(detail), "not available");
   add_info(fe, "Estimated Remaining", detail);
   add_item(fe, "Storage Recover", "remount SD", FRONTEND_ITEM_ACTION,
      "storage_recover", NULL);
}

void frontend_show_clock(struct frontend_state *fe)
{
   struct tm value;
   char date[32];
   char detail[24];

   reset_items(fe, "Date & Time");
   fe->view = FRONTEND_VIEW_CLOCK;
   if (unifrog_clock_format(date, sizeof(date)) != 0)
      unifrog_text_copy(date, sizeof(date), "unavailable");
   add_info(fe, "Current", date);
   if (unifrog_clock_get_local(&value) != 0)
      memset(&value, 0, sizeof(value));
   snprintf(detail, sizeof(detail), "%04d", value.tm_year + 1900);
   add_item(fe, "Year", detail, FRONTEND_ITEM_ACTION, "clock_year", NULL);
   snprintf(detail, sizeof(detail), "%02d", value.tm_mon + 1);
   add_item(fe, "Month", detail, FRONTEND_ITEM_ACTION, "clock_month", NULL);
   snprintf(detail, sizeof(detail), "%02d", value.tm_mday);
   add_item(fe, "Day", detail, FRONTEND_ITEM_ACTION, "clock_day", NULL);
   snprintf(detail, sizeof(detail), "%02d", value.tm_hour);
   add_item(fe, "Hour", detail, FRONTEND_ITEM_ACTION, "clock_hour", NULL);
   snprintf(detail, sizeof(detail), "%02d", value.tm_min);
   add_item(fe, "Minute", detail, FRONTEND_ITEM_ACTION, "clock_minute", NULL);
   add_info(fe, "Controls", "Left/Right adjust; changes save immediately");
}

void frontend_show_storage(struct frontend_state *fe)
{
   struct unifrog_frontend_model model;
   struct unifrog_frontend_model_settings settings;

   reset_items(fe, "Storage");
   fe->view = FRONTEND_VIEW_STORAGE;
   frontend_model_settings(fe, &settings);
   unifrog_frontend_model_build(&model, UNIFROG_FRONTEND_MODEL_STORAGE,
      &settings);
   add_model_items(fe, &model);
   add_item(fe, "Auto Normal", storage_profile_label(fe->storage_normal_profile),
      FRONTEND_ITEM_ACTION, "storage_auto_normal", NULL);
   add_item(fe, "Auto Fallback",
      storage_profile_label(fe->storage_fallback_profile),
      FRONTEND_ITEM_ACTION, "storage_auto_fallback", NULL);
   add_info(fe, "Low Battery", "SD mode stays unchanged; save and charge");
   frontend_set_status(fe, "SD %s", unifrog_platform_sd_active_profile());
}

void frontend_show_storage_mode(struct frontend_state *fe)
{
   struct unifrog_frontend_model model;
   struct unifrog_frontend_model_settings settings;

   reset_items(fe, "SD Mode");
   fe->view = FRONTEND_VIEW_STORAGE_MODE;
   frontend_model_settings(fe, &settings);
   unifrog_frontend_model_build(&model, UNIFROG_FRONTEND_MODEL_STORAGE_MODE,
      &settings);
   add_model_items(fe, &model);
}

void frontend_show_storage_confirm(struct frontend_state *fe,
   const char *profile)
{
   unifrog_text_copy(fe->storage_pending_profile,
      sizeof(fe->storage_pending_profile), profile ? profile : "boot");
   reset_items(fe, "Apply SD Mode");
   fe->view = FRONTEND_VIEW_STORAGE_CONFIRM;
   add_item(fe, "Apply", storage_profile_label(fe->storage_pending_profile),
      FRONTEND_ITEM_ACTION, "storage_apply_pending", NULL);
   add_item(fe, "Cancel", "no change", FRONTEND_ITEM_ACTION, "storage_mode",
      NULL);
   add_info(fe, "Active", active_storage_label());
   add_info(fe, "Configured", storage_profile_label(fe->storage_profile));
   frontend_set_status(fe, "confirm SD switch");
}

void frontend_sound_shutdown(void)
{
   unifrog_audio_set_system_output_enabled(0);
}

void frontend_prepare_safe_shutdown(struct frontend_state *fe)
{
   int settings_ret;
   int clock_ret;
   int log_ret;

   if (fe->shutdown_safe)
      return;
   frontend_loading_show(fe, "Safe Shutdown", "saving settings",
      "writing configuration", 20);
   settings_ret = save_settings(fe);
   frontend_loading_show(fe, "Safe Shutdown", "saving clock",
      "writing clock state", 40);
   clock_ret = unifrog_clock_persist();
   frontend_loading_show(fe, "Safe Shutdown", "flushing logs",
      "committing SD writes", 70);
   UF_LOG_INFO("shutdown", "event=prepare settings_ret=%d clock_ret=%d",
      settings_ret, clock_ret);
   log_ret = unifrog_log_flush_force();
   if (settings_ret != 0 || clock_ret != 0 || log_ret != 0) {
      frontend_set_status(fe, "shutdown not safe: settings %d clock %d log %d",
         settings_ret, clock_ret, log_ret);
      fe->needs_draw = 1;
      return;
   }

   frontend_loading_show(fe, "Safe Shutdown", "finishing",
      "stopping output and logging", 95);
   frontend_sound_shutdown();
   (void)unifrog_scpu_apply_mhz(198u);
   (void)unifrog_ge_set_clock(&fe->ui.ge, UNIFROG_GE_CLOCK_148MHZ);
   unifrog_platform_set_storage_log_suspended(1);
   fe->shutdown_safe = 1;
   /* Present the completion screen synchronously.  The old path waited for
    * the next frontend loop, leaving the 40% progress frame visible while
    * users were deciding whether it was safe to remove power. */
   frontend_draw(fe);
}

int frontend_adjust_clock_item(struct frontend_state *fe, const char *path,
   int amount)
{
   enum unifrog_clock_field field;
   unsigned selected = fe->selected;
   int ret;

   if (strcmp(path, "clock_year") == 0)
      field = UNIFROG_CLOCK_YEAR;
   else if (strcmp(path, "clock_month") == 0)
      field = UNIFROG_CLOCK_MONTH;
   else if (strcmp(path, "clock_day") == 0)
      field = UNIFROG_CLOCK_DAY;
   else if (strcmp(path, "clock_hour") == 0)
      field = UNIFROG_CLOCK_HOUR;
   else if (strcmp(path, "clock_minute") == 0)
      field = UNIFROG_CLOCK_MINUTE;
   else
      return 0;
   ret = unifrog_clock_adjust(field, amount);
   frontend_show_clock(fe);
   restore_view_selection(fe, selected, fe->scroll);
   frontend_set_status(fe, ret == 0 ? "date/time saved" :
      "date/time update failed");
   return 1;
}

static void frontend_apply_pending_storage_profile(struct frontend_state *fe)
{
   char previous_profile[sizeof(fe->storage_profile)];
   int ret;

   if (!fe->storage_pending_profile[0])
      unifrog_text_copy(fe->storage_pending_profile,
         sizeof(fe->storage_pending_profile), "boot");
   unifrog_text_copy(previous_profile, sizeof(previous_profile),
      fe->storage_profile);
   unifrog_text_copy(fe->storage_profile, sizeof(fe->storage_profile),
      fe->storage_pending_profile);
   frontend_loading_show(fe, "Storage", fe->storage_profile,
      storage_profile_label(fe->storage_profile), 20);
   ret = apply_storage_profile(fe, "menu");
   if (ret == 0) {
      save_settings(fe);
   } else {
      unifrog_text_copy(fe->storage_profile, sizeof(fe->storage_profile),
         previous_profile);
      (void)apply_storage_profile(fe, "menu-rollback");
      frontend_set_status(fe, "SD switch failed %d", ret);
   }
   frontend_show_storage_mode(fe);
}

static void frontend_cycle_storage_auto_profile(struct frontend_state *fe,
   char *profile, size_t profile_size, const char *reason,
   const char *const *choices, unsigned count, unsigned selected)
{
   frontend_cycle_string_choice(profile, profile_size, choices, count);
   save_settings(fe);
   if (strcmp(fe->storage_profile, "auto") == 0)
      (void)apply_storage_profile(fe, reason);
   frontend_show_storage(fe);
   restore_view_selection(fe, selected, fe->scroll);
}

static void frontend_cycle_battery_point(struct frontend_state *fe,
   const char *index_text, unsigned selected)
{
   unsigned index = frontend_parse_unsigned_setting(index_text,
      UNIFROG_BATTERY_POINT_COUNT);
   unsigned minimum;
   unsigned maximum;
   unsigned next;

   if (index >= UNIFROG_BATTERY_POINT_COUNT)
      return;
   minimum = index ? fe->battery_calibration.millivolts[index - 1] + 20u :
      2500u;
   maximum = index + 1u < UNIFROG_BATTERY_POINT_COUNT ?
      fe->battery_calibration.millivolts[index + 1] - 20u : 5000u;
   next = fe->battery_calibration.millivolts[index] + 20u;
   fe->battery_calibration.millivolts[index] =
      next > maximum ? minimum : next;
   unifrog_battery_set_calibration(&fe->battery_calibration);
   save_settings(fe);
   frontend_show_power(fe);
   restore_view_selection(fe, selected, fe->scroll);
}

static void frontend_cycle_battery_rate(struct frontend_state *fe,
   unsigned selected)
{
   static const unsigned rates[] = { 0u, 60u, 90u, 120u, 180u, 240u, 360u };
   unsigned index = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(rates); i++) {
      if (fe->battery_calibration.discharge_mv_per_hour == rates[i]) {
         index = i;
         break;
      }
   }
   fe->battery_calibration.discharge_mv_per_hour =
      rates[(index + 1u) % ARRAY_SIZE(rates)];
   unifrog_battery_set_calibration(&fe->battery_calibration);
   save_settings(fe);
   frontend_show_power(fe);
   restore_view_selection(fe, selected, fe->scroll);
}

int frontend_system_activate(struct frontend_state *fe,
   const struct frontend_item *item, unsigned selected)
{
   if (!fe || !item)
      return 0;
   if (strcmp(item->path, "storage_apply_pending") == 0) {
      frontend_apply_pending_storage_profile(fe);
   } else if (strcmp(item->path, "storage_auto_normal") == 0) {
      static const char *const choices[] = { "wide25", "safe", "uhs25" };

      frontend_cycle_storage_auto_profile(fe, fe->storage_normal_profile,
         sizeof(fe->storage_normal_profile), "auto-normal-menu", choices,
         ARRAY_SIZE(choices), selected);
   } else if (strcmp(item->path, "storage_auto_fallback") == 0) {
      static const char *const choices[] = { "safe", "wide25", "uhs25" };

      frontend_cycle_storage_auto_profile(fe, fe->storage_fallback_profile,
         sizeof(fe->storage_fallback_profile), "auto-fallback-menu", choices,
         ARRAY_SIZE(choices), selected);
   } else if (strcmp(item->path, "storage_recover") == 0) {
      int ret = unifrog_storage_recover_after_io_error("frontend", 16, 250);

      frontend_set_status(fe, "storage recover %d", ret);
   } else if (strcmp(item->path, "battery_point") == 0) {
      frontend_cycle_battery_point(fe, item->core, selected);
   } else if (strcmp(item->path, "battery_rate") == 0) {
      frontend_cycle_battery_rate(fe, selected);
   } else if (strcmp(item->path, "battery_estimate") == 0) {
      fe->battery_calibration.estimate_discharge =
         !fe->battery_calibration.estimate_discharge;
      unifrog_battery_set_calibration(&fe->battery_calibration);
      save_settings(fe);
      frontend_show_power(fe);
      restore_view_selection(fe, selected, fe->scroll);
   } else if (strcmp(item->path, "battery_refresh") == 0) {
      int ret = unifrog_battery_update(&fe->battery, 0);

      frontend_set_status(fe, "battery refresh %d", ret);
      frontend_show_power(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item->path, "default_boot_clear") == 0) {
      unifrog_text_copy(fe->default_boot, sizeof(fe->default_boot), "unifrog");
      if (sync_default_boot(fe) == 0 && save_settings(fe) == 0)
         frontend_set_status(fe, "UniFrog will boot by default");
      else
         frontend_set_status(fe, "default boot update failed");
      frontend_show_power(fe);
   } else if (strcmp(item->path, "clock_settings") == 0) {
      frontend_parent_view_push(fe);
      frontend_show_clock(fe);
   } else if (strcmp(item->path, "safe_shutdown") == 0) {
      frontend_prepare_safe_shutdown(fe);
   } else if (strcmp(item->path, "back_storage") == 0) {
      frontend_show_storage(fe);
   } else {
      return 0;
   }
   return 1;
}
