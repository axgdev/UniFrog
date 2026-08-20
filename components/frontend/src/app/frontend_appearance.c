#include "frontend_internal.h"

void frontend_show_custom(struct frontend_state *fe)
{
   reset_items(fe, "Custom");
   fe->view = FRONTEND_VIEW_CUSTOM;
   add_item(fe, "Theme", active_theme_label(fe), FRONTEND_ITEM_ACTION, "theme",
      NULL);
   add_item(fe, "Theme Font", fe->theme_font[0] ? fe->theme_font : "auto",
      FRONTEND_ITEM_ACTION, "font", NULL);
   add_item(fe, "Alternate Theme", on_off_label(fe->theme_alternate),
      FRONTEND_ITEM_ACTION, "theme_alternate", NULL);
   add_item(fe, "Launch Splash", on_off_label(fe->launch_splash),
      FRONTEND_ITEM_ACTION, "launch_splash", NULL);
   add_item(fe, "Box Art Hide", on_off_label(fe->boxart_hidden),
      FRONTEND_ITEM_ACTION, "boxart_hide", NULL);
   add_item(fe, "ROM Artwork", fe->artwork_layout, FRONTEND_ITEM_ACTION,
      "artwork", NULL);
   add_item(fe, "Navigation Sound", on_off_label(fe->sound_enabled),
      FRONTEND_ITEM_ACTION, "sound", NULL);
   add_item(fe, "Log Detail", fe->log_level, FRONTEND_ITEM_ACTION,
      "log_level", NULL);
}

void frontend_show_artwork(struct frontend_state *fe)
{
   reset_items(fe, "ROM Artwork");
   fe->view = FRONTEND_VIEW_ARTWORK;
   add_item(fe, "Layout", fe->artwork_layout, FRONTEND_ITEM_ACTION,
      "artwork_layout", NULL);
   add_info(fe, "Box Art", fe->artwork_box_templates);
   add_info(fe, "Game Preview", fe->artwork_preview_templates);
   add_info(fe, "Game Text", fe->artwork_text_templates);
   add_info(fe, "Templates", "Edit unifrog.ini for custom paths");
}

static int font_filename(const char *name)
{
   return name && (unifrog_text_ends_with_ci(name, ".ttf") ||
      unifrog_text_ends_with_ci(name, ".otf") ||
      unifrog_text_ends_with_ci(name, ".bin") ||
      unifrog_text_ends_with_ci(name, ".font"));
}

static void add_font_directory(struct frontend_state *fe, const char *dir,
   const char *prefix, const char *label)
{
   DIR *fonts = opendir(dir);
   struct dirent *entry;

   if (!fonts)
      return;
   while ((entry = readdir(fonts)) != NULL) {
      char preference[96];

      if (!font_filename(entry->d_name))
         continue;
      if (snprintf(preference, sizeof(preference), "%s%s", prefix,
          entry->d_name) >= (int)sizeof(preference))
         continue;
      add_item(fe, entry->d_name,
         strcmp(fe->theme_font, preference) == 0 ? "active" : label,
         FRONTEND_ITEM_ACTION, "font_select", preference);
   }
   closedir(fonts);
}

void frontend_show_font_list(struct frontend_state *fe)
{
   char theme_dir[FRONTEND_MAX_PATH];
   char font_dir[FRONTEND_MAX_PATH];

   reset_items(fe, "Theme Font");
   fe->view = FRONTEND_VIEW_FONT;
   add_item(fe, "Automatic", strcmp(fe->theme_font, "auto") == 0 ?
      "active" : "theme/language default", FRONTEND_ITEM_ACTION,
      "font_select", "auto");
   if (frontend_path_join(theme_dir, sizeof(theme_dir), FRONTEND_THEME_ROOT,
       active_theme_label(fe)) == 0 &&
       frontend_path_join(font_dir, sizeof(font_dir), theme_dir, "font") == 0)
      add_font_directory(fe, font_dir, "theme:font/", "theme font");
   add_font_directory(fe, UNIFROG_FONT_ROOT, "user:", "user font");
   add_info(fe, "Add Fonts", "/unifrog_data/fonts (TTF, OTF, BIN)");
   frontend_set_status(fe, "A sets font for this theme");
}

void frontend_set_artwork_layout(struct frontend_state *fe, const char *layout)
{
   if (!fe || !layout)
      return;
   unifrog_text_copy(fe->artwork_layout, sizeof(fe->artwork_layout), layout);
   if (strcmp(layout, "skraper") == 0) {
      unifrog_text_copy(fe->artwork_box_templates,
         sizeof(fe->artwork_box_templates),
         "{rom_dir}/media/images/{name}.png|{rom_dir}/media/box2dfront/"
         "{name}.png");
      unifrog_text_copy(fe->artwork_preview_templates,
         sizeof(fe->artwork_preview_templates),
         "{rom_dir}/media/screenshots/{name}.png|{rom_dir}/media/miximages/"
         "{name}.png|{rom_dir}/media/images/{name}.png");
      unifrog_text_copy(fe->artwork_text_templates,
         sizeof(fe->artwork_text_templates),
         "{rom_dir}/media/text/{name}.txt|{rom_dir}/media/manuals/"
         "{name}.txt");
   } else if (strcmp(layout, "beside") == 0) {
      unifrog_text_copy(fe->artwork_box_templates,
         sizeof(fe->artwork_box_templates), "{rom_dir}/{name}.box.png");
      unifrog_text_copy(fe->artwork_preview_templates,
         sizeof(fe->artwork_preview_templates),
         "{rom_dir}/{name}.preview.png");
      unifrog_text_copy(fe->artwork_text_templates,
         sizeof(fe->artwork_text_templates), "{rom_dir}/{name}.txt");
   } else if (strcmp(layout, "muos") == 0) {
      unifrog_text_copy(fe->artwork_layout, sizeof(fe->artwork_layout),
         "muos");
      unifrog_text_copy(fe->artwork_box_templates,
         sizeof(fe->artwork_box_templates),
         "unifrog_data/artwork/{system}/box/{name}.png|MUOS/info/catalogue/"
         "{system}/box/{name}.png|muos/info/catalogue/{system}/box/"
         "{name}.png");
      unifrog_text_copy(fe->artwork_preview_templates,
         sizeof(fe->artwork_preview_templates),
         "unifrog_data/artwork/{system}/preview/{name}.png|MUOS/info/"
         "catalogue/{system}/preview/{name}.png|muos/info/catalogue/"
         "{system}/preview/{name}.png");
      unifrog_text_copy(fe->artwork_text_templates,
         sizeof(fe->artwork_text_templates),
         "unifrog_data/artwork/{system}/text/{name}.txt|MUOS/info/catalogue/"
         "{system}/text/{name}.txt|muos/info/catalogue/{system}/text/"
         "{name}.txt");
   }
   fe->artwork_cache_item[0] = '\0';
}

static void cycle_artwork_layout(struct frontend_state *fe)
{
   static const char *const layouts[] = { "muos", "skraper", "beside" };
   unsigned index = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(layouts); i++) {
      if (strcmp(fe->artwork_layout, layouts[i]) == 0) {
         index = i;
         break;
      }
   }
   frontend_set_artwork_layout(fe, layouts[(index + 1u) % ARRAY_SIZE(layouts)]);
}

struct font_preference_write {
   const char *font;
};

static int write_font_preference(FILE *file, void *userdata)
{
   const struct font_preference_write *preference = userdata;

   fprintf(file,
      "# Per-theme font selected on-device. Values: auto, theme:PATH relative\n"
      "# to the theme directory, or user:FILENAME under /unifrog_data/fonts.\n"
      "font=%s\n", preference->font);
   return ferror(file) ? -1 : 0;
}

static int save_theme_font(struct frontend_state *fe, const char *font)
{
   struct font_preference_write preference = { font };
   char section[80];

   if (!fe || !font || !font[0] || snprintf(section, sizeof(section),
       "theme.%s", active_theme_label(fe)) >= (int)sizeof(section))
      return -1;
   return unifrog_config_replace_section(UNIFROG_CONFIG_PATH, section,
      write_font_preference, &preference);
}

void frontend_show_visual(struct frontend_state *fe)
{
   struct unifrog_frontend_model model;
   struct unifrog_frontend_model_settings settings;

   reset_items(fe, "Visual");
   fe->view = FRONTEND_VIEW_VISUAL;
   frontend_model_settings(fe, &settings);
   unifrog_frontend_model_build(&model, UNIFROG_FRONTEND_MODEL_VISUAL,
      &settings);
   add_model_items(fe, &model);
}

void frontend_show_theme_list(struct frontend_state *fe)
{
   DIR *dir;
   struct dirent *entry;

   reset_items(fe, "Theme");
   fe->view = FRONTEND_VIEW_THEME;
   add_item(fe, "muOS", strcmp(active_theme_label(fe), "muos") == 0 ?
      "active" : "built-in", FRONTEND_ITEM_ACTION, "theme_select", "muos");
   dir = opendir(FRONTEND_THEME_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char name[48];
         char meta[32];
         char version_path[FRONTEND_MAX_PATH];
         char full[FRONTEND_MAX_PATH];

         unifrog_text_copy(name, sizeof(name), entry->d_name);
         if (!name[0] || name[0] == '.' || strcmp(name, "muos") == 0)
            continue;
         if (unifrog_text_ends_with_ci(name, ".ini")) {
            frontend_strip_ini_suffix(name);
         } else {
            if (frontend_path_join(full, sizeof(full), FRONTEND_THEME_ROOT,
                entry->d_name) != 0 ||
                frontend_path_join(version_path, sizeof(version_path), full,
                "version.txt") != 0 ||
                access(version_path, F_OK) != 0)
               continue;
         }
         snprintf(meta, sizeof(meta), "%s",
            strcmp(active_theme_label(fe), name) == 0 ? "active" : "theme");
         add_item(fe, name, meta, FRONTEND_ITEM_ACTION, "theme_select", name);
      }
      closedir(dir);
   }
   dir = opendir(FRONTEND_ARCHIVE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];

         if (!unifrog_text_ends_with_ci(entry->d_name, ".muxthm"))
            continue;
         if (frontend_path_join(full, sizeof(full), FRONTEND_ARCHIVE_ROOT,
             entry->d_name) != 0)
            continue;
         add_item(fe, entry->d_name, "install archive",
            FRONTEND_ITEM_THEME_ARCHIVE, full, NULL);
      }
      closedir(dir);
   }
   dir = opendir(FRONTEND_STOCK_ARCHIVE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];

         if (!unifrog_text_ends_with_ci(entry->d_name, ".muxthm"))
            continue;
         if (frontend_path_join(full, sizeof(full), FRONTEND_STOCK_ARCHIVE_ROOT,
             entry->d_name) != 0)
            continue;
         add_item(fe, entry->d_name, "install archive",
            FRONTEND_ITEM_THEME_ARCHIVE, full, NULL);
      }
      closedir(dir);
   }
   add_item(fe, "Back", "custom", FRONTEND_ITEM_ACTION, "custom", NULL);
   frontend_set_status(fe, "A apply/install theme");
}

void frontend_show_language_list(struct frontend_state *fe)
{
   DIR *dir;
   struct dirent *entry;

   reset_items(fe, "Language");
   fe->view = FRONTEND_VIEW_LANGUAGE;
   add_item(fe, "english", strcmp(active_language_label(fe), "english") == 0 ?
      "active" : "built-in", FRONTEND_ITEM_ACTION, "language_select",
      "english");
   dir = opendir(FRONTEND_LANGUAGE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char name[48];
         char meta[32];

         if (!unifrog_text_ends_with_ci(entry->d_name, ".ini"))
            continue;
         unifrog_text_copy(name, sizeof(name), entry->d_name);
         frontend_strip_ini_suffix(name);
         if (!name[0] || strcmp(name, "english") == 0)
            continue;
         snprintf(meta, sizeof(meta), "%s",
            strcmp(active_language_label(fe), name) == 0 ? "active" :
            "language");
         add_item(fe, name, meta, FRONTEND_ITEM_ACTION, "language_select", name);
      }
      closedir(dir);
   }
   add_item(fe, "Back", "config", FRONTEND_ITEM_ACTION, "config", NULL);
   frontend_set_status(fe, "A apply language");
}

static int activate_theme_archive(struct frontend_state *fe,
   const struct frontend_item *item)
{
   char installed[96];
   struct frontend_install_progress progress;
   size_t old_auto_flush;
   int ret;

   memset(&progress, 0, sizeof(progress));
   progress.fe = fe;
   unifrog_text_copy(progress.title, sizeof(progress.title), "Theme");
   unifrog_text_copy(progress.name, sizeof(progress.name), item->name);
   frontend_install_progress_update(&progress, "installing", 0, 100);
   unifrog_log("frontend theme archive activate path=%s name=%s\n",
      item->path, item->name);
   unifrog_frontend_lvgl_clear_resource_cache();
   fe->resource_cache_key[0] = '\0';
   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_log_defer_begin();
   ret = install_theme_archive(item->path, installed, sizeof(installed),
      frontend_install_progress_update, &progress);
   unifrog_log_defer_end();
   unifrog_log_set_auto_flush_bytes(old_auto_flush);
   unifrog_log_note_storage_quiet(500u);
   unifrog_log("frontend theme archive activate done path=%s ret=%d installed=%s\n",
      item->path, ret, ret == 0 ? installed : "");
   (void)unifrog_log_flush();
   if (ret == 0) {
      unifrog_text_copy(fe->theme_name, sizeof(fe->theme_name), installed);
      fe->resource_cache_key[0] = '\0';
      fe->theme_loaded = 0;
      load_theme(fe);
      save_settings(fe);
      frontend_set_status(fe, "installed %s", installed);
   } else {
      frontend_set_status(fe, "theme install failed %d", ret);
   }
   frontend_show_theme_list(fe);
   return 1;
}

int frontend_appearance_activate(struct frontend_state *fe,
   const struct frontend_item *item, unsigned selected)
{
   if (item->kind == FRONTEND_ITEM_THEME_ARCHIVE)
      return activate_theme_archive(fe, item);

   switch (item->action) {
   case UNIFROG_FRONTEND_ACTION_CUSTOM:
      frontend_parent_view_push(fe);
      frontend_show_custom(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_THEME:
      frontend_parent_view_push(fe);
      frontend_show_theme_list(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_LANGUAGE:
      frontend_parent_view_push(fe);
      frontend_show_language_list(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_INTERFACE:
      frontend_parent_view_push(fe);
      frontend_show_visual(fe);
      return 1;
   default:
      break;
   }

   if (strcmp(item->path, "log_level") == 0) {
      static const char *const levels[] = {
         "trace", "debug", "info", "warn", "error", "off",
      };

      frontend_cycle_string_choice(fe->log_level, sizeof(fe->log_level), levels,
         ARRAY_SIZE(levels));
      unifrog_log_set_level(unifrog_log_level_from_name(fe->log_level,
         UNIFROG_LOG_TRACE));
      save_settings(fe);
      frontend_show_custom(fe);
      restore_view_selection(fe, selected, fe->scroll);
      return 1;
   }
   if (strcmp(item->path, "artwork") == 0) {
      frontend_parent_view_push(fe);
      frontend_show_artwork(fe);
      return 1;
   }
   if (strcmp(item->path, "artwork_layout") == 0) {
      cycle_artwork_layout(fe);
      save_settings(fe);
      frontend_show_artwork(fe);
      restore_view_selection(fe, selected, fe->scroll);
      return 1;
   }
   if (strcmp(item->path, "font") == 0) {
      frontend_parent_view_push(fe);
      frontend_show_font_list(fe);
      return 1;
   }
   if (strcmp(item->path, "font_select") == 0) {
      if (save_theme_font(fe, item->core[0] ? item->core : "auto") == 0) {
         unifrog_text_copy(fe->theme_font, sizeof(fe->theme_font),
            item->core[0] ? item->core : "auto");
         fe->theme_loaded = 0;
         load_theme(fe);
         frontend_set_status(fe, "font saved for %s", active_theme_label(fe));
      } else {
         frontend_set_status(fe, "font selection save failed");
      }
      frontend_show_font_list(fe);
      restore_view_selection(fe, selected, fe->scroll);
      return 1;
   }
   if (strcmp(item->path, "theme_select") == 0) {
      unifrog_text_copy(fe->theme_name, sizeof(fe->theme_name),
         item->core[0] ? item->core : "muos");
      load_theme(fe);
      save_settings(fe);
      frontend_set_status(fe, "theme %s", active_theme_label(fe));
      frontend_show_theme_list(fe);
      restore_view_selection(fe, selected, fe->scroll);
      return 1;
   }
   if (strcmp(item->path, "language_select") == 0) {
      unifrog_text_copy(fe->language_name, sizeof(fe->language_name),
         item->core[0] ? item->core : "english");
      load_language(fe);
      load_theme(fe);
      save_settings(fe);
      frontend_set_status(fe, "language %s", active_language_label(fe));
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
      return 1;
   }
   return 0;
}
