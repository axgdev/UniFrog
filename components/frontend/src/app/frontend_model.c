#include <unifrog/frontend_model.h>

#include <stdio.h>
#include <string.h>

#include <unifrog/storage_profile.h>

struct action_id {
   enum unifrog_frontend_action action;
   const char *id;
};

static const struct action_id action_ids[] = {
   { UNIFROG_FRONTEND_ACTION_EXPLORE, "explore" },
   { UNIFROG_FRONTEND_ACTION_EXPLORE_SD, "explore_sd" },
   { UNIFROG_FRONTEND_ACTION_FAVORITES, "favorites" },
   { UNIFROG_FRONTEND_ACTION_HISTORY, "history" },
   { UNIFROG_FRONTEND_ACTION_APPS, "apps" },
   { UNIFROG_FRONTEND_ACTION_INFO, "info" },
   { UNIFROG_FRONTEND_ACTION_CONFIG, "config" },
   { UNIFROG_FRONTEND_ACTION_REBOOT, "reboot" },
   { UNIFROG_FRONTEND_ACTION_SHUTDOWN, "shutdown" },
   { UNIFROG_FRONTEND_ACTION_LAUNCH_SETTINGS, "launch_settings" },
   { UNIFROG_FRONTEND_ACTION_CUSTOM, "custom" },
   { UNIFROG_FRONTEND_ACTION_INTERFACE, "interface" },
   { UNIFROG_FRONTEND_ACTION_THEME, "theme" },
   { UNIFROG_FRONTEND_ACTION_LANGUAGE, "language" },
   { UNIFROG_FRONTEND_ACTION_POWER, "power" },
   { UNIFROG_FRONTEND_ACTION_STORAGE, "storage" },
   { UNIFROG_FRONTEND_ACTION_STORAGE_MODE, "storage_mode" },
   { UNIFROG_FRONTEND_ACTION_SORT, "sort" },
   { UNIFROG_FRONTEND_ACTION_CLOCK, "clock" },
   { UNIFROG_FRONTEND_ACTION_TITLE_ROOT, "title_root" },
   { UNIFROG_FRONTEND_ACTION_COUNTER_FOLDER, "counter_folder" },
   { UNIFROG_FRONTEND_ACTION_COUNTER_FILE, "counter_file" },
   { UNIFROG_FRONTEND_ACTION_HIDDEN, "hidden" },
   { UNIFROG_FRONTEND_ACTION_CONTENT_COLLECT, "content_collect" },
   { UNIFROG_FRONTEND_ACTION_CONTENT_HISTORY, "content_history" },
   { UNIFROG_FRONTEND_ACTION_MIXED_CONTENT, "mixed_content" },
   { UNIFROG_FRONTEND_ACTION_EXPLORE_UNIFROG, "explore_unifrog" },
   { UNIFROG_FRONTEND_ACTION_EXPLORE_BIOS, "explore_bios" },
   { UNIFROG_FRONTEND_ACTION_EXPLORE_DATA, "explore_data" },
   { UNIFROG_FRONTEND_ACTION_FIRMWARE, "firmware" },
   { UNIFROG_FRONTEND_ACTION_UPDATES, "updates" },
   { UNIFROG_FRONTEND_ACTION_CORES, "cores" },
   { UNIFROG_FRONTEND_ACTION_PACKAGE_CHECK, "package_check" },
   { UNIFROG_FRONTEND_ACTION_EXPLORE_SAVES, "explore_saves" },
   { UNIFROG_FRONTEND_ACTION_STORAGE_FAST_PROBE, "storage_fast_probe" },
   { UNIFROG_FRONTEND_ACTION_FLUSH_LOG, "flush_log" },
   { UNIFROG_FRONTEND_ACTION_STORAGE_PROFILE, "storage_profile" },
   { UNIFROG_FRONTEND_ACTION_BACK_STORAGE, "back_storage" },
};

static const char *on_off(int value)
{
   return value ? "on" : "off";
}

static void reset(struct unifrog_frontend_model *model,
   enum unifrog_frontend_model_screen screen, const char *title)
{
   memset(model, 0, sizeof(*model));
   model->screen = screen;
   snprintf(model->title, sizeof(model->title), "%s", title);
}

static void add(struct unifrog_frontend_model *model, const char *label,
   const char *detail, enum unifrog_frontend_action action,
   const char *payload)
{
   struct unifrog_frontend_model_item *item;

   if (model->count >= UNIFROG_FRONTEND_MODEL_MAX_ITEMS)
      return;
   item = &model->items[model->count++];
   snprintf(item->label, sizeof(item->label), "%s", label ? label : "");
   snprintf(item->detail, sizeof(item->detail), "%s", detail ? detail : "");
   item->action = action;
   snprintf(item->payload, sizeof(item->payload), "%s", payload ? payload : "");
}

const char *unifrog_frontend_action_id(enum unifrog_frontend_action action)
{
   for (unsigned i = 0; i < sizeof(action_ids) / sizeof(action_ids[0]); i++) {
      if (action_ids[i].action == action)
         return action_ids[i].id;
   }
   return "";
}

enum unifrog_frontend_action unifrog_frontend_action_from_id(const char *id)
{
   if (!id || !id[0])
      return UNIFROG_FRONTEND_ACTION_NONE;
   for (unsigned i = 0; i < sizeof(action_ids) / sizeof(action_ids[0]); i++) {
      if (strcmp(action_ids[i].id, id) == 0)
         return action_ids[i].action;
   }
   return UNIFROG_FRONTEND_ACTION_NONE;
}

static void build_launch(struct unifrog_frontend_model *model)
{
   reset(model, UNIFROG_FRONTEND_MODEL_LAUNCH, "muOS");
   add(model, "Explore", "SD root", UNIFROG_FRONTEND_ACTION_EXPLORE_SD, NULL);
   add(model, "Collection", "favorites", UNIFROG_FRONTEND_ACTION_FAVORITES, NULL);
   add(model, "History", "recent", UNIFROG_FRONTEND_ACTION_HISTORY, NULL);
   add(model, "Apps", "native", UNIFROG_FRONTEND_ACTION_APPS, NULL);
   add(model, "Info", "device", UNIFROG_FRONTEND_ACTION_INFO, NULL);
   add(model, "Config", "settings", UNIFROG_FRONTEND_ACTION_CONFIG, NULL);
   add(model, "Reboot", "system", UNIFROG_FRONTEND_ACTION_REBOOT, NULL);
   add(model, "Safe Shutdown", "flush writes, then power off",
      UNIFROG_FRONTEND_ACTION_SHUTDOWN, NULL);
}

static void build_config(struct unifrog_frontend_model *model,
   const struct unifrog_frontend_model_settings *settings)
{
   reset(model, UNIFROG_FRONTEND_MODEL_CONFIG, "Config");
   add(model, "Gameplay", "launch defaults",
      UNIFROG_FRONTEND_ACTION_LAUNCH_SETTINGS, NULL);
   add(model, "Browser", "listing and history",
      UNIFROG_FRONTEND_ACTION_INTERFACE, NULL);
   add(model, "Appearance", settings->theme, UNIFROG_FRONTEND_ACTION_CUSTOM, NULL);
   add(model, "Storage", "SD tools", UNIFROG_FRONTEND_ACTION_STORAGE, NULL);
   add(model, "Power", "battery and reboot", UNIFROG_FRONTEND_ACTION_POWER, NULL);
   add(model, "Language", settings->language, UNIFROG_FRONTEND_ACTION_LANGUAGE, NULL);
}

static void build_visual(struct unifrog_frontend_model *model,
   const struct unifrog_frontend_model_settings *settings)
{
   reset(model, UNIFROG_FRONTEND_MODEL_VISUAL, "Visual");
   add(model, "Sort", settings->sort_desc ? "name desc" : "name asc",
      UNIFROG_FRONTEND_ACTION_SORT, NULL);
   add(model, "Clock", on_off(settings->clock_enabled),
      UNIFROG_FRONTEND_ACTION_CLOCK, NULL);
   add(model, "Title Root Drive", on_off(settings->title_include_root),
      UNIFROG_FRONTEND_ACTION_TITLE_ROOT, NULL);
   add(model, "Menu Counter Folder", on_off(settings->menu_counter_folder),
      UNIFROG_FRONTEND_ACTION_COUNTER_FOLDER, NULL);
   add(model, "Menu Counter File", on_off(settings->menu_counter_file),
      UNIFROG_FRONTEND_ACTION_COUNTER_FILE, NULL);
   add(model, "Hidden", settings->show_hidden ? "shown" : "hidden",
      UNIFROG_FRONTEND_ACTION_HIDDEN, NULL);
   add(model, "Content Collect", on_off(settings->content_collect),
      UNIFROG_FRONTEND_ACTION_CONTENT_COLLECT, NULL);
   add(model, "Content History", on_off(settings->content_history),
      UNIFROG_FRONTEND_ACTION_CONTENT_HISTORY, NULL);
   add(model, "Mixed Content", on_off(settings->mixed_content),
      UNIFROG_FRONTEND_ACTION_MIXED_CONTENT, NULL);
}

static void build_storage(struct unifrog_frontend_model *model,
   const struct unifrog_frontend_model_settings *settings)
{
   char policy[UNIFROG_FRONTEND_MODEL_TEXT_MAX];

   reset(model, UNIFROG_FRONTEND_MODEL_STORAGE, "Storage");
   if (strcmp(settings->configured_storage_profile, "auto") == 0) {
      snprintf(policy, sizeof(policy), "normal %s; fallback %s; battery unchanged",
         settings->storage_normal_profile,
         settings->storage_fallback_profile);
   } else {
      snprintf(policy, sizeof(policy), "manual %s",
         settings->configured_storage_profile);
   }
   add(model, "SD Mode",
      unifrog_storage_profile_info(settings->active_storage_profile)->label,
      UNIFROG_FRONTEND_ACTION_STORAGE_MODE, NULL);
   add(model, "Policy", policy,
      UNIFROG_FRONTEND_ACTION_STORAGE_MODE, NULL);
   add(model, "UniFrog Files", "/unifrog",
      UNIFROG_FRONTEND_ACTION_EXPLORE_UNIFROG, NULL);
   add(model, "Bios", "/bios", UNIFROG_FRONTEND_ACTION_EXPLORE_BIOS, NULL);
   add(model, settings->rom_root_label, settings->rom_root,
      UNIFROG_FRONTEND_ACTION_EXPLORE, NULL);
   add(model, "Collection", "favorites", UNIFROG_FRONTEND_ACTION_FAVORITES, NULL);
   add(model, "History", "recent", UNIFROG_FRONTEND_ACTION_HISTORY, NULL);
   add(model, "Data", "/unifrog_data", UNIFROG_FRONTEND_ACTION_EXPLORE_DATA, NULL);
   add(model, "Firmware Boot", "boot .asd", UNIFROG_FRONTEND_ACTION_FIRMWARE, NULL);
   add(model, "Updates", "version slots", UNIFROG_FRONTEND_ACTION_UPDATES, NULL);
   add(model, "Core Manager", "ABI status", UNIFROG_FRONTEND_ACTION_CORES, NULL);
   add(model, "Package Check", "layout", UNIFROG_FRONTEND_ACTION_PACKAGE_CHECK, NULL);
   add(model, "Save Data", "/unifrog_data/saves",
      UNIFROG_FRONTEND_ACTION_EXPLORE_SAVES, NULL);
   add(model, "Storage Probe", "1-25 MHz stress",
      UNIFROG_FRONTEND_ACTION_STORAGE_FAST_PROBE, NULL);
   add(model, "Theme Files", settings->theme, UNIFROG_FRONTEND_ACTION_THEME, NULL);
   add(model, "Log Flush", "logs", UNIFROG_FRONTEND_ACTION_FLUSH_LOG, NULL);
}

static void build_storage_mode(struct unifrog_frontend_model *model,
   const struct unifrog_frontend_model_settings *settings)
{
   const struct unifrog_storage_profile_info *active =
      unifrog_storage_profile_info(settings->active_storage_profile);

   reset(model, UNIFROG_FRONTEND_MODEL_STORAGE_MODE, "SD Mode");
   add(model, "Active", active->label, UNIFROG_FRONTEND_ACTION_NONE, NULL);
   add(model, "Configured",
      unifrog_storage_profile_info(settings->configured_storage_profile)->label,
      UNIFROG_FRONTEND_ACTION_NONE, NULL);
   add(model, "Bus Width", active->bus_width, UNIFROG_FRONTEND_ACTION_NONE, NULL);
   add(model, "Timing", active->timing, UNIFROG_FRONTEND_ACTION_NONE, NULL);
   add(model, "Signal", active->signal, UNIFROG_FRONTEND_ACTION_NONE, NULL);
   for (unsigned i = 0; i < unifrog_storage_profile_count(); i++) {
      const char *name = unifrog_storage_profile_name(i);
      const struct unifrog_storage_profile_info *info =
         unifrog_storage_profile_info(name);
      char detail[UNIFROG_FRONTEND_MODEL_TEXT_MAX];

      snprintf(detail, sizeof(detail), "%s%s", strcmp(name, "boot") == 0 ?
         settings->boot_storage_profile : info->label,
         strcmp(name, settings->configured_storage_profile) == 0 ? " *" : "");
      add(model, strcmp(name, "boot") == 0 ? "Boot Profile" : name, detail,
         UNIFROG_FRONTEND_ACTION_STORAGE_PROFILE, name);
   }
   add(model, "Back", "storage", UNIFROG_FRONTEND_ACTION_BACK_STORAGE, NULL);
   snprintf(model->status, sizeof(model->status), "A choose  B back");
}

void unifrog_frontend_model_build(struct unifrog_frontend_model *model,
   enum unifrog_frontend_model_screen screen,
   const struct unifrog_frontend_model_settings *settings)
{
   struct unifrog_frontend_model_settings empty;

   if (!model)
      return;
   if (!settings) {
      memset(&empty, 0, sizeof(empty));
      empty.theme = "muos";
      empty.language = "english";
      empty.rom_root_label = "ROMs";
      empty.rom_root = "/ROMS";
      empty.active_storage_profile = "boot";
      empty.configured_storage_profile = "boot";
      empty.boot_storage_profile = "boot";
      settings = &empty;
   }
   switch (screen) {
   case UNIFROG_FRONTEND_MODEL_LAUNCH:
      build_launch(model);
      break;
   case UNIFROG_FRONTEND_MODEL_CONFIG:
      build_config(model, settings);
      break;
   case UNIFROG_FRONTEND_MODEL_VISUAL:
      build_visual(model, settings);
      break;
   case UNIFROG_FRONTEND_MODEL_STORAGE:
      build_storage(model, settings);
      break;
   case UNIFROG_FRONTEND_MODEL_STORAGE_MODE:
      build_storage_mode(model, settings);
      break;
   }
}

void unifrog_frontend_model_move(struct unifrog_frontend_model *model,
   int direction)
{
   if (!model || !model->count || !direction)
      return;
   if (direction < 0)
      model->selected = model->selected == 0 ? model->count - 1u :
         model->selected - 1u;
   else
      model->selected = (model->selected + 1u) % model->count;
}
