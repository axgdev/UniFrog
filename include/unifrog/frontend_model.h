#ifndef UNIFROG_FRONTEND_MODEL_H
#define UNIFROG_FRONTEND_MODEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_FRONTEND_MODEL_MAX_ITEMS 32u
#define UNIFROG_FRONTEND_MODEL_TEXT_MAX 96u

enum unifrog_frontend_model_screen {
   UNIFROG_FRONTEND_MODEL_LAUNCH = 0,
   UNIFROG_FRONTEND_MODEL_CONFIG,
   UNIFROG_FRONTEND_MODEL_VISUAL,
   UNIFROG_FRONTEND_MODEL_STORAGE,
   UNIFROG_FRONTEND_MODEL_STORAGE_MODE,
   UNIFROG_FRONTEND_MODEL_SERVICE,
};

enum unifrog_frontend_action {
   UNIFROG_FRONTEND_ACTION_NONE = 0,
   UNIFROG_FRONTEND_ACTION_EXPLORE,
   UNIFROG_FRONTEND_ACTION_FAVORITES,
   UNIFROG_FRONTEND_ACTION_HISTORY,
   UNIFROG_FRONTEND_ACTION_APPS,
   UNIFROG_FRONTEND_ACTION_INFO,
   UNIFROG_FRONTEND_ACTION_CONFIG,
   UNIFROG_FRONTEND_ACTION_REBOOT,
   UNIFROG_FRONTEND_ACTION_SHUTDOWN,
   UNIFROG_FRONTEND_ACTION_LAUNCH_SETTINGS,
   UNIFROG_FRONTEND_ACTION_CUSTOM,
   UNIFROG_FRONTEND_ACTION_INTERFACE,
   UNIFROG_FRONTEND_ACTION_THEME,
   UNIFROG_FRONTEND_ACTION_LANGUAGE,
   UNIFROG_FRONTEND_ACTION_POWER,
   UNIFROG_FRONTEND_ACTION_STORAGE,
   UNIFROG_FRONTEND_ACTION_STORAGE_MODE,
   UNIFROG_FRONTEND_ACTION_SORT,
   UNIFROG_FRONTEND_ACTION_CLOCK,
   UNIFROG_FRONTEND_ACTION_TITLE_ROOT,
   UNIFROG_FRONTEND_ACTION_COUNTER_FOLDER,
   UNIFROG_FRONTEND_ACTION_COUNTER_FILE,
   UNIFROG_FRONTEND_ACTION_HIDDEN,
   UNIFROG_FRONTEND_ACTION_CONTENT_COLLECT,
   UNIFROG_FRONTEND_ACTION_CONTENT_HISTORY,
   UNIFROG_FRONTEND_ACTION_MIXED_CONTENT,
   UNIFROG_FRONTEND_ACTION_EXPLORE_UNIFROG,
   UNIFROG_FRONTEND_ACTION_EXPLORE_BIOS,
   UNIFROG_FRONTEND_ACTION_EXPLORE_DATA,
   UNIFROG_FRONTEND_ACTION_FIRMWARE,
   UNIFROG_FRONTEND_ACTION_UPDATES,
   UNIFROG_FRONTEND_ACTION_CORES,
   UNIFROG_FRONTEND_ACTION_PACKAGE_CHECK,
   UNIFROG_FRONTEND_ACTION_EXPLORE_SAVES,
   UNIFROG_FRONTEND_ACTION_STORAGE_FAST_PROBE,
   UNIFROG_FRONTEND_ACTION_FLUSH_LOG,
   UNIFROG_FRONTEND_ACTION_STORAGE_PROFILE,
   UNIFROG_FRONTEND_ACTION_BACK_STORAGE,
};

struct unifrog_frontend_model_settings {
   int sort_desc;
   int clock_enabled;
   int title_include_root;
   int menu_counter_folder;
   int menu_counter_file;
   int show_hidden;
   int content_collect;
   int content_history;
   int mixed_content;
   const char *theme;
   const char *language;
   const char *rom_root_label;
   const char *rom_root;
   const char *active_storage_profile;
   const char *configured_storage_profile;
   const char *boot_storage_profile;
   const char *storage_normal_profile;
   const char *storage_fallback_profile;
};

struct unifrog_frontend_model_item {
   char label[UNIFROG_FRONTEND_MODEL_TEXT_MAX];
   char detail[UNIFROG_FRONTEND_MODEL_TEXT_MAX];
   enum unifrog_frontend_action action;
   char payload[UNIFROG_FRONTEND_MODEL_TEXT_MAX];
};

struct unifrog_frontend_model {
   enum unifrog_frontend_model_screen screen;
   char title[UNIFROG_FRONTEND_MODEL_TEXT_MAX];
   char status[UNIFROG_FRONTEND_MODEL_TEXT_MAX];
   struct unifrog_frontend_model_item items[UNIFROG_FRONTEND_MODEL_MAX_ITEMS];
   unsigned count;
   unsigned selected;
};

const char *unifrog_frontend_action_id(enum unifrog_frontend_action action);
enum unifrog_frontend_action unifrog_frontend_action_from_id(const char *id);
void unifrog_frontend_model_build(struct unifrog_frontend_model *model,
   enum unifrog_frontend_model_screen screen,
   const struct unifrog_frontend_model_settings *settings);
void unifrog_frontend_model_move(struct unifrog_frontend_model *model,
   int direction);

#ifdef __cplusplus
}
#endif

#endif
