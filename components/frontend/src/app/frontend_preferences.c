#include "frontend_internal.h"

void frontend_show_launch_settings(struct frontend_state *fe)
{
   char detail[48];

   reset_items(fe, "General");
   fe->view = FRONTEND_VIEW_LAUNCH_SETTINGS;
   add_item(fe, "Audio", fe->run_options.audio_enabled ? "enabled" : "muted",
      FRONTEND_ITEM_ACTION, "audio", NULL);
   snprintf(detail, sizeof(detail), "%ux", fe->run_options.audio_gain);
   add_item(fe, "Volume", detail, FRONTEND_ITEM_ACTION, "gain", NULL);
   if (fe->run_options.scpu_mhz)
      snprintf(detail, sizeof(detail), "%u MHz", fe->run_options.scpu_mhz);
   else
      snprintf(detail, sizeof(detail), "platform default");
   add_item(fe, "CPU", detail, FRONTEND_ITEM_ACTION, "cpu", NULL);
   add_item(fe, "GPU", ge_clock_label(fe->run_options.ge_clock),
      FRONTEND_ITEM_ACTION, "ge_clock", NULL);
   add_item(fe, "Frameskip", frameskip_label(fe->run_options.frameskip),
      FRONTEND_ITEM_ACTION, "frameskip", NULL);
   add_item(fe, "Display", display_label(fe->run_options.display_mode),
      FRONTEND_ITEM_ACTION, "display", NULL);
   add_item(fe, "Framebuffer",
      framebuffer_label(fe->run_options.framebuffer_format),
      FRONTEND_ITEM_ACTION, "framebuffer", NULL);
   add_item(fe, "Keymap", input_profile_label(fe->run_options.input_profile),
      FRONTEND_ITEM_ACTION, "keymap", NULL);
   add_item(fe, "State Slot", state_slot_label(fe->run_options.state_slot),
      FRONTEND_ITEM_ACTION, "state_slot", NULL);
   add_item(fe, "Auto Load State",
      on_off_label(fe->run_options.state_auto_load), FRONTEND_ITEM_ACTION,
      "state_auto_load", NULL);
   add_item(fe, "Auto Save State",
      on_off_label(fe->run_options.state_auto_save), FRONTEND_ITEM_ACTION,
      "state_auto_save", NULL);
   if (fe->run_options.rtc_offset_minutes == 0)
      snprintf(detail, sizeof(detail), "device clock");
   else
      snprintf(detail, sizeof(detail), "%+d days",
         fe->run_options.rtc_offset_minutes / (24 * 60));
   add_item(fe, "Game RTC Offset", detail, FRONTEND_ITEM_ACTION,
      "rtc_offset", NULL);
   if (fe->run_options.backlight_level < 0)
      snprintf(detail, sizeof(detail), "platform default");
   else
      snprintf(detail, sizeof(detail), "%d",
         fe->run_options.backlight_level);
   add_item(fe, "Backlight", detail, FRONTEND_ITEM_ACTION, "backlight", NULL);
   add_item(fe, "Device Board", fe->device_board, FRONTEND_ITEM_ACTION,
      "device_board", NULL);
   add_item(fe, "ROM Roots", frontend_rom_root(fe), FRONTEND_ITEM_ACTION,
      "rom_roots", NULL);
   add_item(fe, "ROM Systems", "defaults", FRONTEND_ITEM_ACTION,
      "rom_systems", NULL);
   add_item(fe, "Back", "config", FRONTEND_ITEM_ACTION, "back_config", NULL);
}

void frontend_change_config(struct frontend_state *fe, int dir)
{
   struct frontend_item *item;
   unsigned selected;

   if (fe->selected >= fe->item_count)
      return;
   selected = fe->selected;
   item = &fe->items[fe->selected];
   if (strcmp(item->path, "rom_systems") == 0 ||
       strcmp(item->path, "rom_roots") == 0 ||
       strcmp(item->path, "back_config") == 0)
      return;
   if (strcmp(item->path, "rom_root") == 0) {
      frontend_cycle_rom_root(fe, dir);
      save_settings(fe);
      frontend_show_launch_settings(fe);
      fe->selected = selected;
      clamp_selection(fe);
      return;
   }
   if (strcmp(item->path, "audio") == 0)
      fe->run_options.audio_enabled = !fe->run_options.audio_enabled;
   else if (strcmp(item->path, "gain") == 0) {
      static const unsigned gains[] = { 0, 1, 2, 3, 4 };
      unsigned index = 1;

      for (unsigned i = 0; i < ARRAY_SIZE(gains); i++) {
         if (gains[i] == fe->run_options.audio_gain)
            index = i;
      }
      if (dir < 0)
         index = index == 0 ? ARRAY_SIZE(gains) - 1u : index - 1u;
      else
         index = (index + 1u) % ARRAY_SIZE(gains);
      fe->run_options.audio_gain = gains[index];
   }
   else if (strcmp(item->path, "cpu") == 0) {
      fe->run_options.scpu_mhz = unifrog_libretro_policy_cpu(
         fe->run_options.scpu_mhz, dir, 1);
   } else if (strcmp(item->path, "ge_clock") == 0) {
      static const int clocks[] = { -1, 0, 1, 2, 3 };
      unsigned index = 0;

      for (unsigned i = 0; i < ARRAY_SIZE(clocks); i++) {
         if (clocks[i] == fe->run_options.ge_clock)
            index = i;
      }
      if (dir < 0)
         index = index == 0 ? ARRAY_SIZE(clocks) - 1u : index - 1u;
      else
         index = (index + 1u) % ARRAY_SIZE(clocks);
      fe->run_options.ge_clock = clocks[index];
   } else if (strcmp(item->path, "frameskip") == 0) {
      fe->run_options.frameskip = unifrog_libretro_policy_frameskip(
         fe->run_options.frameskip, dir);
   } else if (strcmp(item->path, "display") == 0) {
      fe->run_options.display_mode = unifrog_libretro_policy_display(
         fe->run_options.display_mode, dir);
   } else if (strcmp(item->path, "framebuffer") == 0) {
      fe->run_options.framebuffer_format =
         fe->run_options.framebuffer_format == UNIFROG_LIBRETRO_FB_RGB565 ?
         UNIFROG_LIBRETRO_FB_XRGB8888 : UNIFROG_LIBRETRO_FB_RGB565;
   } else if (strcmp(item->path, "keymap") == 0) {
      fe->run_options.input_profile = unifrog_libretro_policy_input_profile(
         fe->run_options.input_profile, dir);
   } else if (strcmp(item->path, "state_slot") == 0) {
      if (dir < 0)
         fe->run_options.state_slot =
            fe->run_options.state_slot == 0 ? 9u :
            fe->run_options.state_slot - 1u;
      else
         fe->run_options.state_slot =
            (fe->run_options.state_slot + 1u) % 10u;
   } else if (strcmp(item->path, "state_auto_load") == 0) {
      fe->run_options.state_auto_load = !fe->run_options.state_auto_load;
   } else if (strcmp(item->path, "state_auto_save") == 0) {
      fe->run_options.state_auto_save = !fe->run_options.state_auto_save;
   } else if (strcmp(item->path, "rtc_offset") == 0) {
      int next = fe->run_options.rtc_offset_minutes +
         (dir < 0 ? -(24 * 60) : (24 * 60));

      if (next < -5270400)
         next = 5270400;
      else if (next > 5270400)
         next = -5270400;
      fe->run_options.rtc_offset_minutes = next;
   } else if (strcmp(item->path, "backlight") == 0) {
      static const int levels[] = {
         -1, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
      };
      unsigned index = 0;

      for (unsigned i = 0; i < ARRAY_SIZE(levels); i++) {
         if (levels[i] == fe->run_options.backlight_level)
            index = i;
      }
      if (dir < 0)
         index = index == 0 ? ARRAY_SIZE(levels) - 1u : index - 1u;
      else
         index = (index + 1u) % ARRAY_SIZE(levels);
      fe->run_options.backlight_level = levels[index];
      if (levels[index] >= 0)
         (void)unifrog_backlight_set((unsigned)levels[index]);
   } else if (strcmp(item->path, "device_board") == 0) {
      static const char *const boards[] = { "auto", "sf2000", "gb300" };
      unsigned index = 0;

      for (unsigned i = 0; i < ARRAY_SIZE(boards); i++) {
         if (strcmp(boards[i], fe->device_board) == 0)
            index = i;
      }
      if (dir < 0)
         index = index == 0 ? ARRAY_SIZE(boards) - 1u : index - 1u;
      else
         index = (index + 1u) % ARRAY_SIZE(boards);
      unifrog_text_copy(fe->device_board, sizeof(fe->device_board),
         boards[index]);
      (void)unifrog_device_set_board_override(fe->device_board);
   }
   save_settings(fe);
   frontend_show_launch_settings(fe);
   fe->selected = selected;
   clamp_selection(fe);
}

enum frontend_toggle_flags {
   FRONTEND_TOGGLE_CUSTOM = 1u << 0,
   FRONTEND_TOGGLE_RELOAD_THEME = 1u << 1,
};

struct frontend_toggle {
   const char *action;
   size_t offset;
   unsigned flags;
};

#define FRONTEND_TOGGLE(action, field, flags) \
   { action, offsetof(struct frontend_state, field), flags }

static int activate_toggle(struct frontend_state *fe, const char *action,
   unsigned selected)
{
   static const struct frontend_toggle toggles[] = {
      FRONTEND_TOGGLE("sort", sort_desc, 0),
      FRONTEND_TOGGLE("hidden", show_hidden, 0),
      FRONTEND_TOGGLE("folder_counts", folder_counts, 0),
      FRONTEND_TOGGLE("empty_folder", display_empty_folder, 0),
      FRONTEND_TOGGLE("counter_folder", menu_counter_folder, 0),
      FRONTEND_TOGGLE("counter_file", menu_counter_file, 0),
      FRONTEND_TOGGLE("content_collect", content_collect, 0),
      FRONTEND_TOGGLE("content_history", content_history, 0),
      FRONTEND_TOGGLE("mixed_content", mixed_content, 0),
      FRONTEND_TOGGLE("clock", clock_enabled, 0),
      FRONTEND_TOGGLE("title_root", title_include_root, 0),
      FRONTEND_TOGGLE("theme_alternate", theme_alternate,
         FRONTEND_TOGGLE_CUSTOM | FRONTEND_TOGGLE_RELOAD_THEME),
      FRONTEND_TOGGLE("boxart_hide", boxart_hidden, FRONTEND_TOGGLE_CUSTOM),
      FRONTEND_TOGGLE("launch_splash", launch_splash, FRONTEND_TOGGLE_CUSTOM),
      FRONTEND_TOGGLE("sound", sound_enabled, FRONTEND_TOGGLE_CUSTOM),
   };

   for (unsigned i = 0; i < ARRAY_SIZE(toggles); i++) {
      const struct frontend_toggle *toggle = &toggles[i];
      int *value;

      if (strcmp(action, toggle->action) != 0)
         continue;
      value = (int *)((unsigned char *)fe + toggle->offset);
      *value = !*value;
      if (toggle->flags & FRONTEND_TOGGLE_RELOAD_THEME)
         load_theme(fe);
      save_settings(fe);
      if (toggle->flags & FRONTEND_TOGGLE_CUSTOM)
         frontend_show_custom(fe);
      else
         frontend_show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
      return 1;
   }
   return 0;
}

int frontend_preferences_activate(struct frontend_state *fe,
   const struct frontend_item *item, unsigned selected)
{
   if (item->action == UNIFROG_FRONTEND_ACTION_LAUNCH_SETTINGS) {
      frontend_parent_view_push(fe);
      frontend_show_launch_settings(fe);
      return 1;
   }
   if (fe->view == FRONTEND_VIEW_LAUNCH_SETTINGS) {
      if (strcmp(item->path, "rom_systems") == 0) {
         frontend_parent_view_clear(fe);
         frontend_show_rom_system_mappings(fe);
         return 1;
      }
      if (strcmp(item->path, "rom_roots") == 0) {
         frontend_parent_view_clear(fe);
         frontend_show_rom_roots(fe);
         return 1;
      }
      if (strcmp(item->path, "back_config") == 0) {
         restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
         return 1;
      }
      frontend_change_config(fe, 1);
      return 1;
   }
   return activate_toggle(fe, item->path, selected);
}
