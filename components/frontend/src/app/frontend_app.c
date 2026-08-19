#include "frontend_internal.h"

#include <unifrog/frontend_app.h>

static void show_launch(struct frontend_state *fe)
{
   struct unifrog_frontend_model model;

   reset_items(fe, "muOS");
   fe->view = FRONTEND_VIEW_LAUNCH;
   frontend_nav_reset(fe);
   unifrog_frontend_model_build(&model, UNIFROG_FRONTEND_MODEL_LAUNCH, NULL);
   add_model_items(fe, &model);
   fe->status[0] = '\0';
   fe->needs_draw = 1;
}

static void show_config(struct frontend_state *fe)
{
   struct unifrog_frontend_model model;
   struct unifrog_frontend_model_settings settings;

   reset_items(fe, "Config");
   fe->view = FRONTEND_VIEW_CONFIG;
   frontend_model_settings(fe, &settings);
   unifrog_frontend_model_build(&model, UNIFROG_FRONTEND_MODEL_CONFIG,
      &settings);
   add_model_items(fe, &model);
}

static void show_connect(struct frontend_state *fe)
{
   reset_items(fe, "Connect");
   fe->view = FRONTEND_VIEW_CONNECT;
   add_info(fe, "Network", "unsupported");
   add_info(fe, "NetAdv", "unsupported");
   add_info(fe, "Services", "unavailable");
   add_info(fe, "Bluetooth", "unsupported");
   add_info(fe, "UsbFunction", "unsupported");
}

static void show_view(struct frontend_state *fe, enum frontend_view view)
{
   switch (view) {
   case FRONTEND_VIEW_LAUNCH:
      show_launch(fe);
      break;
   case FRONTEND_VIEW_CONFIG:
      show_config(fe);
      break;
   case FRONTEND_VIEW_CONNECT:
      show_connect(fe);
      break;
   case FRONTEND_VIEW_CUSTOM:
      frontend_show_custom(fe);
      break;
   case FRONTEND_VIEW_VISUAL:
      frontend_show_visual(fe);
      break;
   case FRONTEND_VIEW_POWER:
      frontend_show_power(fe);
      break;
   case FRONTEND_VIEW_CLOCK:
      frontend_show_clock(fe);
      break;
   case FRONTEND_VIEW_ARTWORK:
      frontend_show_artwork(fe);
      break;
   case FRONTEND_VIEW_FONT:
      frontend_show_font_list(fe);
      break;
   case FRONTEND_VIEW_STORAGE:
      frontend_show_storage(fe);
      break;
   case FRONTEND_VIEW_STORAGE_MODE:
      frontend_show_storage_mode(fe);
      break;
   case FRONTEND_VIEW_INFO:
      frontend_show_info(fe);
      break;
   case FRONTEND_VIEW_APPS:
      frontend_show_apps(fe);
      break;
   case FRONTEND_VIEW_UPDATES:
      frontend_show_updates(fe);
      break;
   case FRONTEND_VIEW_MEDIA_PLAYER:
      frontend_show_media_player(fe, fe->current_dir[0] ? fe->current_dir :
         FRONTEND_ROOT);
      break;
   case FRONTEND_VIEW_READER:
      frontend_show_reader_browser(fe, fe->current_dir[0] ? fe->current_dir :
         FRONTEND_ROOT);
      break;
   case FRONTEND_VIEW_CORES:
      frontend_show_core_manager(fe);
      break;
   case FRONTEND_VIEW_PACKAGE_CHECK:
      frontend_show_package_check(fe);
      break;
   case FRONTEND_VIEW_SYSINFO:
      frontend_show_sysinfo(fe);
      break;
   case FRONTEND_VIEW_THEME:
      frontend_show_theme_list(fe);
      break;
   case FRONTEND_VIEW_LANGUAGE:
      frontend_show_language_list(fe);
      break;
   case FRONTEND_VIEW_LAUNCH_SETTINGS:
      frontend_show_launch_settings(fe);
      break;
   default:
      show_launch(fe);
      break;
   }
}

int restore_parent_view(struct frontend_state *fe,
   enum frontend_view fallback)
{
   enum frontend_view view = fallback;
   unsigned selected = 0;
   unsigned scroll = 0;
   int had_parent = 0;

   if (fe->nav.view_count > 0) {
      fe->nav.view_count--;
      view = fe->nav.view_stack[fe->nav.view_count];
      selected = fe->nav.view_selected[fe->nav.view_count];
      scroll = fe->nav.view_scroll[fe->nav.view_count];
      had_parent = 1;
   }
   if (fe->nav.view_count > 0) {
      fe->nav.parent_view = fe->nav.view_stack[fe->nav.view_count - 1u];
      fe->nav.has_parent_view = 1;
   } else {
      fe->nav.parent_view = FRONTEND_VIEW_LAUNCH;
      fe->nav.has_parent_view = 0;
   }
   show_view(fe, view);
   if (had_parent)
      restore_view_selection(fe, selected, scroll);
   return had_parent;
}

static void browser_back(struct frontend_state *fe)
{
   char parent[FRONTEND_MAX_PATH];
   char *slash;
   unsigned selected;

   if (fe->view == FRONTEND_VIEW_LAUNCH)
      return;
   if (fe->view == FRONTEND_VIEW_OPEN_WITH ||
       fe->view == FRONTEND_VIEW_OPEN_WITH_OTHER) {
      frontend_return_from_open_with(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_ROM_ROOTS) {
      frontend_show_launch_settings(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_ROM_ROOT_PICKER) {
      char parent_dir[FRONTEND_MAX_PATH];

      unifrog_text_copy(parent_dir, sizeof(parent_dir), fe->current_dir);
      slash = strrchr(parent_dir, '/');
      if (slash && slash > parent_dir)
         *slash = '\0';
      else
         unifrog_text_copy(parent_dir, sizeof(parent_dir), FRONTEND_ROOT);
      frontend_show_rom_root_picker(fe, parent_dir);
      return;
   }
   if (fe->view == FRONTEND_VIEW_SYSINFO || fe->view == FRONTEND_VIEW_CORES ||
       fe->view == FRONTEND_VIEW_PACKAGE_CHECK) {
      restore_parent_view(fe, FRONTEND_VIEW_INFO);
      return;
   }
   if (fe->view == FRONTEND_VIEW_CORE_INFO) {
      frontend_show_core_manager(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_HISTORY || fe->view == FRONTEND_VIEW_FAVORITES) {
      restore_parent_view(fe, FRONTEND_VIEW_LAUNCH);
      return;
   }
   if (fe->view == FRONTEND_VIEW_CONFIG || fe->view == FRONTEND_VIEW_INFO ||
       fe->view == FRONTEND_VIEW_APPS) {
      restore_parent_view(fe, FRONTEND_VIEW_LAUNCH);
      return;
   }
   if (fe->view == FRONTEND_VIEW_CONNECT || fe->view == FRONTEND_VIEW_CUSTOM ||
       fe->view == FRONTEND_VIEW_VISUAL) {
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
      return;
   }
   if (fe->view == FRONTEND_VIEW_THEME) {
      restore_parent_view(fe, FRONTEND_VIEW_CUSTOM);
      return;
   }
   if (fe->view == FRONTEND_VIEW_LANGUAGE) {
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
      return;
   }
   if (fe->view == FRONTEND_VIEW_STORAGE_CONFIRM) {
      frontend_show_storage_mode(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_STORAGE_MODE) {
      frontend_show_storage(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_ROM_SYSTEMS) {
      show_launch(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_ROM_SYSTEM_MAPPINGS) {
      frontend_show_launch_settings(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_POWER || fe->view == FRONTEND_VIEW_STORAGE ||
       fe->view == FRONTEND_VIEW_LAUNCH_SETTINGS || fe->view == FRONTEND_VIEW_UPDATES) {
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
      return;
   }
   if (fe->view == FRONTEND_VIEW_CLOCK) {
      restore_parent_view(fe, FRONTEND_VIEW_POWER);
      return;
   }
   if (fe->view == FRONTEND_VIEW_ARTWORK) {
      restore_parent_view(fe, FRONTEND_VIEW_CUSTOM);
      return;
   }
   if (fe->view == FRONTEND_VIEW_FONT) {
      restore_parent_view(fe, FRONTEND_VIEW_CUSTOM);
      return;
   }
   if (fe->view == FRONTEND_VIEW_MEDIA_PLAYER ||
       fe->view == FRONTEND_VIEW_READER) {
      char path[FRONTEND_MAX_PATH];

      if (frontend_nav_pop(fe, path, sizeof(path), &selected)) {
         if (fe->view == FRONTEND_VIEW_READER)
            frontend_show_reader_browser(fe, path);
         else
            frontend_show_media_player(fe, path);
         restore_view_selection(fe, selected, fe->scroll);
         return;
      }
      restore_parent_view(fe, FRONTEND_VIEW_APPS);
      return;
   }
   if (fe->view == FRONTEND_VIEW_FIRMWARE) {
      char path[FRONTEND_MAX_PATH];

      if (frontend_nav_pop(fe, path, sizeof(path), &selected)) {
         frontend_show_firmware_browser(fe, path);
         restore_view_selection(fe, selected, fe->scroll);
         return;
      }
      if (strcmp(fe->current_dir, FRONTEND_FIRMWARE_ROOT) == 0) {
         unifrog_log("frontend nav back firmware root -> apps\n");
         frontend_show_apps(fe);
         return;
      }
      unifrog_text_copy(parent, sizeof(parent), fe->current_dir);
      slash = strrchr(parent, '/');
      if (slash && slash > parent)
         *slash = '\0';
      else
         unifrog_text_copy(parent, sizeof(parent), FRONTEND_FIRMWARE_ROOT);
      if (strncmp(parent, FRONTEND_FIRMWARE_ROOT,
          strlen(FRONTEND_FIRMWARE_ROOT)) != 0)
         unifrog_text_copy(parent, sizeof(parent), FRONTEND_FIRMWARE_ROOT);
      unifrog_log("frontend nav back firmware parent=%s\n", parent);
      frontend_show_firmware_browser(fe, parent);
      return;
   }
   if (fe->view != FRONTEND_VIEW_EXPLORE) {
      show_launch(fe);
      return;
   }
   if (frontend_nav_pop(fe, parent, sizeof(parent), &selected)) {
      frontend_show_explore(fe, parent);
      restore_view_selection(fe, selected, fe->scroll);
      return;
   }
   if (frontend_path_is_rom_root(fe, fe->current_dir)) {
      frontend_show_rom_systems(fe);
      return;
   }
   if (strcmp(fe->current_dir, FRONTEND_ROOT) == 0) {
      show_launch(fe);
      return;
   }
   unifrog_text_copy(parent, sizeof(parent), fe->current_dir);
   slash = strrchr(parent, '/');
   if (slash && slash > parent)
      *slash = '\0';
   else
      unifrog_text_copy(parent, sizeof(parent), FRONTEND_ROOT);
   if (frontend_path_is_inside_rom_root(fe, fe->current_dir) &&
       !frontend_path_is_inside_rom_root(fe, parent)) {
      frontend_show_rom_systems(fe);
      return;
   }
   unifrog_log("frontend nav back explore parent=%s\n", parent);
   frontend_show_explore(fe, parent);
}

/* Shared-model menus use typed actions; runtime-created items use string keys. */
static int activate_typed_action(struct frontend_state *fe,
   const struct frontend_item *item)
{
   switch (item->action) {
   case UNIFROG_FRONTEND_ACTION_EXPLORE:
      frontend_parent_view_clear(fe);
      frontend_nav_reset(fe);
      frontend_show_rom_systems(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_HISTORY:
      frontend_parent_view_push(fe);
      frontend_nav_reset(fe);
      frontend_show_file_list(fe, "History", FRONTEND_HISTORY_PATH, FRONTEND_VIEW_HISTORY);
      return 1;
   case UNIFROG_FRONTEND_ACTION_FAVORITES:
      frontend_parent_view_push(fe);
      frontend_nav_reset(fe);
      frontend_show_file_list(fe, "Collection", FRONTEND_FAVORITES_PATH,
         FRONTEND_VIEW_FAVORITES);
      return 1;
   case UNIFROG_FRONTEND_ACTION_CONFIG:
      frontend_parent_view_push(fe);
      show_config(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_POWER:
      frontend_parent_view_push(fe);
      frontend_show_power(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_STORAGE:
      frontend_parent_view_push(fe);
      frontend_show_storage(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_STORAGE_MODE:
      frontend_show_storage_mode(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_EXPLORE_UNIFROG:
      frontend_parent_view_clear(fe);
      frontend_nav_reset(fe);
      frontend_show_explore(fe, FRONTEND_DIST_ROOT);
      return 1;
   case UNIFROG_FRONTEND_ACTION_EXPLORE_BIOS:
      frontend_parent_view_clear(fe);
      frontend_nav_reset(fe);
      frontend_show_explore(fe, FRONTEND_ROOT "/bios");
      return 1;
   case UNIFROG_FRONTEND_ACTION_EXPLORE_DATA:
      frontend_parent_view_clear(fe);
      frontend_nav_reset(fe);
      frontend_show_explore(fe, FRONTEND_DATA_ROOT);
      return 1;
   case UNIFROG_FRONTEND_ACTION_EXPLORE_SAVES:
      frontend_parent_view_clear(fe);
      frontend_nav_reset(fe);
      frontend_show_explore(fe, FRONTEND_DATA_ROOT "/saves");
      return 1;
   case UNIFROG_FRONTEND_ACTION_STORAGE_PROFILE:
      frontend_show_storage_confirm(fe, item->core[0] ? item->core : "boot");
      return 1;
   case UNIFROG_FRONTEND_ACTION_BACK_STORAGE:
      frontend_show_storage(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_REBOOT:
      frontend_loading_show(fe, "Reboot", "system", "rebooting", 90);
      (void)unifrog_log_flush();
      unifrog_boot_reboot();
      return 1;
   case UNIFROG_FRONTEND_ACTION_SHUTDOWN:
      frontend_prepare_safe_shutdown(fe);
      return 1;
   default:
      return 0;
   }
}

static void activate(struct frontend_state *fe)
{
   struct frontend_item item;
   unsigned selected;

   if (fe->selected >= fe->item_count)
      return;
   selected = fe->selected;
   item = fe->items[selected];
   unifrog_log("frontend activate view=%d selected=%u name=%s path=%s kind=%d\n",
      fe->view, selected, item.name, item.path, item.kind);
   if (fe->view == FRONTEND_VIEW_CLOCK &&
       frontend_adjust_clock_item(fe, item.path, 1))
      return;
   if (frontend_preferences_activate(fe, &item, selected))
      return;
   if (fe->view == FRONTEND_VIEW_ROM_ROOTS) {
      if (strcmp(item.path, "rom_root_add") == 0 && item.core[0]) {
         if (frontend_rom_root_add(fe, item.core) != 0)
            frontend_set_status(fe, "root list full");
         save_settings(fe);
         frontend_show_rom_roots(fe);
         fe->selected = selected;
         clamp_selection(fe);
         return;
      }
      if (strcmp(item.path, "rom_root_remove") == 0 && item.core[0]) {
         if (frontend_rom_root_remove(fe, item.core) != 0)
            frontend_set_status(fe, "keep at least one root");
         save_settings(fe);
         frontend_show_rom_roots(fe);
         if (selected >= fe->item_count)
            selected = fe->item_count ? fe->item_count - 1u : 0;
         fe->selected = selected;
         clamp_selection(fe);
         return;
      }
      if (strcmp(item.path, "rom_root_browse") == 0) {
         frontend_show_rom_root_picker(fe, FRONTEND_ROOT);
         return;
      }
      if (strcmp(item.path, "rom_root_hint") == 0) {
         frontend_set_status(fe, "select an active root to remove it");
         return;
      }
      if (strcmp(item.path, "back_config") == 0) {
         frontend_show_launch_settings(fe);
         return;
      }
   }
   if (fe->view == FRONTEND_VIEW_ROM_ROOT_PICKER) {
      if (strcmp(item.path, "rom_root_use_current") == 0) {
         if (frontend_rom_root_add(fe, fe->current_dir) != 0)
            frontend_set_status(fe, "root list full");
         save_settings(fe);
         frontend_show_rom_roots(fe);
         return;
      }
      if (item.kind == FRONTEND_ITEM_DIR) {
         if (item.path[0])
            frontend_show_rom_root_picker(fe, item.path);
         else
            browser_back(fe);
         return;
      }
   }
   if (fe->view == FRONTEND_VIEW_ROM_SYSTEM_MAPPINGS) {
      if (strcmp(item.path, "back_config") == 0) {
         frontend_show_launch_settings(fe);
         return;
      }
      if (item.kind == FRONTEND_ITEM_DIR && item.name[0]) {
         frontend_cycle_rom_system_core(fe, item.name, 1);
         save_settings(fe);
         frontend_show_rom_system_mappings(fe);
         fe->selected = selected;
         clamp_selection(fe);
         return;
      }
   }
   if (fe->view == FRONTEND_VIEW_OPEN_WITH ||
       fe->view == FRONTEND_VIEW_OPEN_WITH_OTHER) {
      struct frontend_item pending = fe->pending_open_item;

      if (strcmp(item.path, "open_with_handler") == 0 &&
          fe->pending_open_valid) {
         frontend_launch_with_handler(fe, &pending, item.core);
         return;
      }
      if (strcmp(item.path, "open_with_other") == 0 &&
          fe->pending_open_valid) {
         frontend_show_open_with_other(fe);
         return;
      }
      if (strcmp(item.path, "open_with_back") == 0 &&
          fe->pending_open_valid) {
         frontend_show_open_with(fe, &fe->pending_open_item);
         return;
      }
      if (strcmp(item.path, "back") == 0) {
         browser_back(fe);
         return;
      }
   }
   if (item.kind == FRONTEND_ITEM_GAME) {
      frontend_launch_game(fe, &item);
      return;
   }
   if (item.kind == FRONTEND_ITEM_MEDIA) {
      frontend_launch_media(fe, &item);
      return;
   }
   if (item.kind == FRONTEND_ITEM_READER) {
      frontend_launch_reader(fe, &item);
      return;
   }
   if (item.kind == FRONTEND_ITEM_SCRIPT) {
      frontend_launch_script(fe, &item);
      return;
   }
   if (item.kind == FRONTEND_ITEM_FIRMWARE) {
      char rel[FRONTEND_MAX_PATH];
      int supported;
      int ret;

      if (frontend_sd_relative_path(rel, sizeof(rel), item.path) != 0) {
         frontend_set_status(fe, "not on SD root");
         unifrog_log("frontend firmware invalid_full path=%s\n", item.path);
         return;
      }
      supported = unifrog_boot_asd_path_supported(rel);
      unifrog_log("frontend firmware boot full=%s relative=%s supported=%d\n",
         item.path, rel, supported);
      if (!supported) {
         frontend_set_status(fe, "unsupported .asd name");
         return;
      }
      frontend_loading_show(fe, "Firmware", item.name, "rebooting", 90);
      ret = unifrog_boot_asd_path(rel);
      frontend_set_status(fe, "firmware boot failed %d", ret);
      return;
   }
   if (frontend_appearance_activate(fe, &item, selected) ||
       frontend_maintenance_activate(fe, &item) ||
       frontend_apps_activate(fe, &item))
      return;
   if (item.kind == FRONTEND_ITEM_INFO) {
      frontend_set_status(fe, "%s: %s", item.name, item.meta);
      return;
   }
   if (item.kind == FRONTEND_ITEM_DIR) {
      if (item.path[0] && is_content_file(fe, item.path)) {
         frontend_launch_with_handler(fe, &item,
            default_handler_for_path(fe, item.path));
      } else if (item.path[0]) {
         frontend_nav_push(fe);
         if (fe->view == FRONTEND_VIEW_FIRMWARE)
            frontend_show_firmware_browser(fe, item.path);
         else if (fe->view == FRONTEND_VIEW_SCRIPTS)
            frontend_show_script_browser(fe, item.path);
         else if (fe->view == FRONTEND_VIEW_MEDIA_PLAYER)
            frontend_show_media_player(fe, item.path);
         else if (fe->view == FRONTEND_VIEW_READER)
            frontend_show_reader_browser(fe, item.path);
         else
            frontend_show_explore(fe, item.path);
      } else {
         browser_back(fe);
      }
      return;
   }
   if (activate_typed_action(fe, &item) ||
       frontend_system_activate(fe, &item, selected))
      return;
   if (strcmp(item.path, "connect") == 0) {
      frontend_parent_view_push(fe);
      show_connect(fe);
   } else if (strcmp(item.path, "resume") == 0) {
      frontend_launch_last_game(fe);
   } else if (strcmp(item.path, "back_config") == 0) {
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
   } else if (strcmp(item.path, "colour") == 0 ||
              strcmp(item.path, "overlay") == 0 ||
              strcmp(item.path, "backup") == 0 ||
              strcmp(item.path, "noop") == 0) {
      frontend_set_status(fe, "%s unavailable on SF2000", item.name);
   } else if (strcmp(item.path, "back") == 0) {
      if (fe->view == FRONTEND_VIEW_APPS || fe->view == FRONTEND_VIEW_CONFIG ||
          fe->view == FRONTEND_VIEW_INFO)
         restore_parent_view(fe, FRONTEND_VIEW_LAUNCH);
      else
         show_launch(fe);
   }
}

static void loop_once(struct frontend_state *fe)
{
   uint32_t now = unifrog_perf_time_ms();
   int select_down;
   int combo_handled = 0;

   unifrog_ui_poll(&fe->ui);
   if (fe->shutdown_safe) {
      if (fe->needs_draw)
         frontend_draw(fe);
      unifrog_perf_delay_us(50000u);
      return;
   }
   if (unifrog_frontend_lvgl_animation_active() &&
       now - fe->animation_draw_ms >= 120u) {
      /*
       * Marquees are the only idle animation. Redraw them at a low fixed rate
       * to avoid turning an otherwise static menu into a 60 Hz render loop.
       */
      frontend_invalidate_draw(fe);
      fe->needs_draw = 1;
      fe->animation_draw_ms = now;
   }
   select_down = unifrog_ui_down(&fe->ui, UNIFROG_UI_SELECT);
   if (now - fe->battery_ms > 5000u) {
      int battery_ret = unifrog_battery_update(&fe->battery, 0);
      int battery_low = battery_ret == 0 && fe->battery.available &&
         fe->battery.low;

      fe->battery_ms = now;
      fe->needs_draw = 1;
      if (battery_low != fe->battery_low_warning) {
         fe->battery_low_warning = battery_low;
         if (battery_low) {
            fe->battery_notice_input_ms = now + 750u;
            fe->battery_notice_until_ms = now + 8000u;
         }
      }
   }
   if (now - fe->log_flush_ms >= 30000u) {
      fe->log_flush_ms = now;
      if (unifrog_log_pending() > 0)
         (void)unifrog_log_flush();
   }
   if (fe->battery_notice_until_ms &&
       (int32_t)(fe->battery_notice_until_ms - now) > 0) {
      if ((int32_t)(now - fe->battery_notice_input_ms) >= 0 &&
          fe->ui.buttons != 0) {
         fe->battery_notice_until_ms = 0;
         fe->needs_draw = 1;
      } else {
         if (fe->needs_draw)
            frontend_draw(fe);
         unifrog_perf_delay_us(16000u);
         return;
      }
   }
   if (select_down &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_A) ||
        (unifrog_ui_down(&fe->ui, UNIFROG_UI_A) &&
         unifrog_ui_pressed(&fe->ui, UNIFROG_UI_SELECT)))) {
      unifrog_log("frontend shortcut action=last_game combo=SELECT+A\n");
      frontend_launch_last_game(fe);
      combo_handled = 1;
   } else if (select_down &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_Y) ||
        (unifrog_ui_down(&fe->ui, UNIFROG_UI_Y) &&
         unifrog_ui_pressed(&fe->ui, UNIFROG_UI_SELECT)))) {
      int ret;

      unifrog_log("frontend shortcut action=flush_logs combo=SELECT+Y\n");
      ret = unifrog_log_flush_force();
      frontend_set_status(fe, "log flush ret %d", ret);
      combo_handled = 1;
   } else if (select_down &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_X) ||
        (unifrog_ui_down(&fe->ui, UNIFROG_UI_X) &&
         unifrog_ui_pressed(&fe->ui, UNIFROG_UI_SELECT)))) {
      unifrog_frontend_lvgl_request_screenshot();
      frontend_set_status(fe, "screenshot log queued");
      combo_handled = 1;
   }

   (void)frontend_input_handle_menu_navigation(fe, now, combo_handled);
   if (!combo_handled && unifrog_ui_pressed(&fe->ui, UNIFROG_UI_Y)) {
      if (fe->selected < fe->item_count &&
          fe->items[fe->selected].kind == FRONTEND_ITEM_GAME)
         frontend_favorite_toggle(fe, &fe->items[fe->selected]);
      else if (fe->view == FRONTEND_VIEW_EXPLORE || fe->view == FRONTEND_VIEW_FIRMWARE ||
          fe->view == FRONTEND_VIEW_HISTORY || fe->view == FRONTEND_VIEW_FAVORITES)
         frontend_input_jump_selection_group(fe, 1);
   }
   if (!combo_handled && unifrog_ui_pressed(&fe->ui, UNIFROG_UI_B))
      browser_back(fe);
   if (!combo_handled && fe->view == FRONTEND_VIEW_LAUNCH_SETTINGS &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_LEFT)) {
      frontend_change_config(fe, -1);
      fe->needs_draw = 1;
   }
   if (!combo_handled && fe->view == FRONTEND_VIEW_CLOCK &&
       fe->selected < fe->item_count &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_LEFT) ||
        unifrog_ui_pressed(&fe->ui, UNIFROG_UI_RIGHT))) {
      (void)frontend_adjust_clock_item(fe, fe->items[fe->selected].path,
         unifrog_ui_pressed(&fe->ui, UNIFROG_UI_LEFT) ? -1 : 1);
      combo_handled = 1;
   }
   if (!combo_handled && fe->view == FRONTEND_VIEW_LAUNCH_SETTINGS &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_RIGHT)) {
      frontend_change_config(fe, 1);
      fe->needs_draw = 1;
   }
   if (!combo_handled && fe->view == FRONTEND_VIEW_ROM_SYSTEM_MAPPINGS &&
       fe->selected < fe->item_count &&
       fe->items[fe->selected].kind == FRONTEND_ITEM_DIR &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_LEFT) ||
        unifrog_ui_pressed(&fe->ui, UNIFROG_UI_RIGHT))) {
      frontend_cycle_rom_system_core(fe, fe->items[fe->selected].name,
         unifrog_ui_pressed(&fe->ui, UNIFROG_UI_LEFT) ? -1 : 1);
      save_settings(fe);
      frontend_show_rom_system_mappings(fe);
      fe->needs_draw = 1;
   }
   if (!combo_handled && !select_down &&
       fe->view == FRONTEND_VIEW_FIRMWARE &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_X)) {
      if (fe->selected < fe->item_count &&
          fe->items[fe->selected].kind == FRONTEND_ITEM_FIRMWARE) {
         char rel[UNIFROG_BOOT_PATH_MAX];

         if (frontend_sd_relative_path(rel, sizeof(rel),
             fe->items[fe->selected].path) == 0 &&
             unifrog_boot_asd_path_supported(rel)) {
            unifrog_text_copy(fe->default_boot, sizeof(fe->default_boot), rel);
            if (sync_default_boot(fe) == 0 && save_settings(fe) == 0)
               frontend_set_status(fe, "default boot: %s", rel);
            else
               frontend_set_status(fe, "default boot save failed");
         } else {
            frontend_set_status(fe, "unsupported default boot path");
         }
      } else {
         frontend_set_status(fe, "highlight an .asd file first");
      }
      combo_handled = 1;
   } else if (!combo_handled && !select_down &&
       (fe->view == FRONTEND_VIEW_OPEN_WITH ||
        fe->view == FRONTEND_VIEW_OPEN_WITH_OTHER) &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_X)) {
      if (fe->pending_open_valid && fe->selected < fe->item_count &&
          strcmp(fe->items[fe->selected].path, "open_with_handler") == 0 &&
          fe->items[fe->selected].core[0]) {
         int ret = frontend_association_set_default(fe,
            fe->pending_open_item.path, fe->items[fe->selected].core);

         frontend_set_status(fe, ret == 0 ? "default saved: %s" :
            "default save failed: %s", fe->items[fe->selected].core);
      }
   } else if (!combo_handled && !select_down &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_X) &&
       fe->selected < fe->item_count &&
       (fe->items[fe->selected].kind == FRONTEND_ITEM_GAME ||
        fe->items[fe->selected].kind == FRONTEND_ITEM_MEDIA ||
        fe->items[fe->selected].kind == FRONTEND_ITEM_READER)) {
         frontend_parent_view_push(fe);
         frontend_show_open_with(fe, &fe->items[fe->selected]);
   } else if (!combo_handled && !select_down &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_X)) {
      frontend_parent_view_clear(fe);
      show_config(fe);
   }
   if (!combo_handled &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_A) ||
        unifrog_ui_pressed(&fe->ui, UNIFROG_UI_START)))
      activate(fe);
   if (fe->needs_draw)
      frontend_draw(fe);
   unifrog_perf_delay_us(16000u);
}

static struct frontend_state frontend_app;
static int frontend_app_initialized;
static const struct unifrog_frontend_launch_services
   *frontend_app_launch_services;

void unifrog_frontend_app_set_launch_services(
   const struct unifrog_frontend_launch_services *services)
{
   frontend_app_launch_services = services;
   if (frontend_app_initialized)
      frontend_app.launch_services = services ? services :
         unifrog_frontend_launch_services_default();
}

int unifrog_frontend_app_init(void)
{
   struct frontend_state *fe = &frontend_app;
   int ret;

   if (frontend_app_initialized)
      return 0;
   memset(fe, 0, sizeof(*fe));
   fe->launch_services = frontend_app_launch_services ?
      frontend_app_launch_services : unifrog_frontend_launch_services_default();
   fe->theme = &frontend_theme;
   frontend_ensure_data_dirs();
   (void)unifrog_clock_init();
   unifrog_battery_status_init(&fe->battery);
   unifrog_battery_calibration_defaults(&fe->battery_calibration);
   unifrog_libretro_run_options_init(&fe->run_options);
   unifrog_media_tuning_defaults(&fe->media_tuning);
   fe->run_options.audio_enabled = UNIFROG_DEFAULT_AUDIO;
   fe->run_options.audio_gain = UNIFROG_DEFAULT_GAIN;
   fe->run_options.scpu_mhz = UNIFROG_DEFAULT_CPU;
   fe->run_options.ge_clock = UNIFROG_DEFAULT_GE_CLOCK;
   fe->run_options.backlight_level = UNIFROG_DEFAULT_BACKLIGHT;
   fe->run_options.frameskip = UNIFROG_DEFAULT_FRAMESKIP;
   fe->run_options.display_mode = UNIFROG_DEFAULT_DISPLAY;
   fe->run_options.framebuffer_format = UNIFROG_DEFAULT_FRAMEBUFFER;
   fe->run_options.input_profile = UNIFROG_DEFAULT_KEYMAP;
   fe->run_options.state_slot = UNIFROG_DEFAULT_STATE_SLOT;
   fe->run_options.state_auto_load = UNIFROG_DEFAULT_STATE_AUTO_LOAD;
   fe->run_options.state_auto_save = UNIFROG_DEFAULT_STATE_AUTO_SAVE;
   fe->sort_desc = UNIFROG_DEFAULT_SORT_DESC;
   fe->show_hidden = UNIFROG_DEFAULT_SHOW_HIDDEN;
   fe->folder_counts = UNIFROG_DEFAULT_FOLDER_COUNTS;
   fe->mixed_content = UNIFROG_DEFAULT_MIXED_CONTENT;
   fe->display_empty_folder = UNIFROG_DEFAULT_DISPLAY_EMPTY_FOLDER;
   fe->menu_counter_folder = UNIFROG_DEFAULT_MENU_COUNTER_FOLDER;
   fe->menu_counter_file = UNIFROG_DEFAULT_MENU_COUNTER_FILE;
   fe->content_collect = UNIFROG_DEFAULT_CONTENT_COLLECT;
   fe->content_history = UNIFROG_DEFAULT_CONTENT_HISTORY;
   fe->clock_enabled = UNIFROG_DEFAULT_CLOCK_ENABLED;
   fe->title_include_root = UNIFROG_DEFAULT_TITLE_INCLUDE_ROOT;
   fe->theme_alternate = UNIFROG_DEFAULT_THEME_ALTERNATE;
   fe->boxart_hidden = UNIFROG_DEFAULT_BOXART_HIDDEN;
   fe->launch_splash = UNIFROG_DEFAULT_LAUNCH_SPLASH;
   fe->sound_enabled = UNIFROG_DEFAULT_SOUND_ENABLED;
   unifrog_text_copy(fe->log_level, sizeof(fe->log_level),
      UNIFROG_DEFAULT_LOG_LEVEL);
   unifrog_text_copy(fe->theme_name, sizeof(fe->theme_name),
      UNIFROG_DEFAULT_THEME_NAME);
   frontend_set_artwork_layout(fe, "muos");
   unifrog_text_copy(fe->language_name, sizeof(fe->language_name),
      UNIFROG_DEFAULT_LANGUAGE_NAME);
   unifrog_text_copy(fe->device_board, sizeof(fe->device_board),
      UNIFROG_DEFAULT_DEVICE_BOARD);
   unifrog_text_copy(fe->storage_profile, sizeof(fe->storage_profile),
      UNIFROG_DEFAULT_STORAGE_PROFILE);
   unifrog_text_copy(fe->storage_normal_profile,
      sizeof(fe->storage_normal_profile), "wide25");
   unifrog_text_copy(fe->storage_fallback_profile,
      sizeof(fe->storage_fallback_profile), "safe");
   unifrog_text_copy(fe->default_boot, sizeof(fe->default_boot), "unifrog");
   unifrog_text_copy(fe->rom_root, sizeof(fe->rom_root),
      UNIFROG_DEFAULT_ROM_ROOT);
   unifrog_text_copy(fe->rom_root_label, sizeof(fe->rom_root_label),
      UNIFROG_DEFAULT_ROM_ROOT_LABEL);
   (void)frontend_rom_root_add(fe, UNIFROG_DEFAULT_ROM_ROOT);
   (void)unifrog_device_set_board_override(fe->device_board);
   load_settings(fe);
   if (sync_default_boot(fe) != 0) {
      UF_LOG_ERROR("boot", "event=default_sync_failed target=%s",
         fe->default_boot);
      unifrog_text_copy(fe->default_boot, sizeof(fe->default_boot), "unifrog");
      (void)sync_default_boot(fe);
   }
   frontend_associations_load(fe);
   if (!frontend_file_exists(UNIFROG_CONFIG_PATH))
      save_settings(fe);
   unifrog_media_set_tuning(&fe->media_tuning);
   frontend_rom_root_sync_primary(fe);
   if (unifrog_core_registry_scan(&fe->core_registry, UNIFROG_CORE_ROOT) != 0)
      unifrog_log("frontend core_registry scan_failed root=%s\n",
         UNIFROG_CORE_ROOT);
   else
      unifrog_log("frontend core_registry count=%u\n",
         fe->core_registry.count);
   unifrog_log_set_level(unifrog_log_level_from_name(fe->log_level,
      UNIFROG_LOG_TRACE));
   unifrog_text_copy(fe->log_level, sizeof(fe->log_level),
      unifrog_log_level_name(unifrog_log_get_level()));
   unifrog_log_set_auto_flush_bytes((size_t)UNIFROG_LOG_AUTO_FLUSH_BYTES);
   load_language(fe);
   load_theme(fe);
   unifrog_input_init();
   if (fe->run_options.backlight_level >= 0)
      (void)unifrog_backlight_set((unsigned)fe->run_options.backlight_level);
   mark_boot_ok();

   ret = unifrog_ui_open(&fe->ui, unifrog_boot_logo_is_active());
   if (ret != 0) {
      printf("unifrog frontend fb_open failed ret=%d\n", ret);
      return ret;
   }
   printf("unifrog frontend start commit=%s dirty=%d theme=%s sdk=%s cores=%s media=%s\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY,
      UNIFROG_FRONTEND_GIT_COMMIT, UNIFROG_SDK_GIT_COMMIT,
      UNIFROG_CORES_GIT_COMMIT, UNIFROG_HCRTOS_MEDIA);
   if (strcmp(fe->storage_profile, "boot") != 0) {
      frontend_loading_show(fe, "Storage", fe->storage_profile,
         storage_profile_label(fe->storage_profile), 20);
      (void)apply_storage_profile(fe, "startup");
   }
   show_launch(fe);
   fe->running = 1;
   frontend_app_initialized = 1;
   return 0;
}

int unifrog_frontend_app_step(void)
{
   if (!frontend_app_initialized || !frontend_app.running)
      return 0;
   loop_once(&frontend_app);
   return frontend_app.running ? 1 : 0;
}

int unifrog_frontend_app_running(void)
{
   return frontend_app_initialized && frontend_app.running;
}

void unifrog_frontend_app_request_stop(void)
{
   if (frontend_app_initialized)
      frontend_app.running = 0;
}

struct unifrog_surface unifrog_frontend_app_surface(void)
{
   if (!frontend_app_initialized)
      return (struct unifrog_surface){0};
   return unifrog_fb_surface(&frontend_app.ui.fb);
}

void unifrog_frontend_app_shutdown(void)
{
   if (!frontend_app_initialized)
      return;
   unifrog_ui_close(&frontend_app.ui);
   memset(&frontend_app, 0, sizeof(frontend_app));
   frontend_app_initialized = 0;
}

int unifrog_frontend_main(void)
{
   int ret = unifrog_frontend_app_init();

   if (ret != 0)
      return ret;
   while (unifrog_frontend_app_step())
      ;
   unifrog_frontend_app_shutdown();
   return 0;
}
