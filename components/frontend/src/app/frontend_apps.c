#include "frontend_internal.h"

void frontend_show_apps(struct frontend_state *fe)
{
   reset_items(fe, "Apps");
   fe->view = FRONTEND_VIEW_APPS;
   add_item(fe, "Media Player", "videos and music", FRONTEND_ITEM_ACTION,
      "media_player", NULL);
   add_item(fe, "Image Viewer", "photos and folders", FRONTEND_ITEM_ACTION,
      "reader", NULL);
   add_item(fe, "Comic Reader", "CBZ and EPUB", FRONTEND_ITEM_ACTION,
      "reader", NULL);
   add_item(fe, "File Browser", "SD root", FRONTEND_ITEM_ACTION,
      "explore_sd", NULL);
   if (frontend_file_exists(UNIFROG_CORE_ROOT "/frogui.bin"))
      add_item(fe, "FrogUI", "alternative game browser",
         FRONTEND_ITEM_ACTION, "frogui", NULL);
   add_item(fe, "Updates", "versions", FRONTEND_ITEM_ACTION,
      "updates", NULL);
   add_item(fe, "Core Manager", "ABI status", FRONTEND_ITEM_ACTION,
      "cores", NULL);
   add_item(fe, "Package Check", "layout", FRONTEND_ITEM_ACTION,
      "package_check", NULL);
   add_item(fe, "Create Bug Report", "logs and diagnostics ZIP",
      FRONTEND_ITEM_ACTION, "bug_report", NULL);
   add_item(fe, "Runtime Settings", "core options", FRONTEND_ITEM_ACTION,
      "launch_settings", NULL);
   add_item(fe, "Audio Diagnostics", "GB300 route test",
      FRONTEND_ITEM_ACTION, "audio_test", NULL);
   add_item(fe, "JavaScript Scripts", "/unifrog/scripts", FRONTEND_ITEM_ACTION,
      "scripts", NULL);
   add_item(fe, "Storage Recover", "remount SD", FRONTEND_ITEM_ACTION,
      "storage_recover", NULL);
   add_item(fe, "Storage Probe", "speed/stability", FRONTEND_ITEM_ACTION,
      "storage_fast_probe", NULL);
   add_item(fe, "Flush Log", "write log.txt", FRONTEND_ITEM_ACTION,
      "flush_log", NULL);
   add_item(fe, "Firmware Boot", "handoff", FRONTEND_ITEM_ACTION, "firmware",
      NULL);
   add_item(fe, "Power", "battery", FRONTEND_ITEM_ACTION, "power", NULL);
   add_item(fe, "SysInfo", "device", FRONTEND_ITEM_ACTION, "sysinfo", NULL);
   add_item(fe, "Back", "launcher", FRONTEND_ITEM_ACTION, "back", NULL);
}

void frontend_show_media_player(struct frontend_state *fe, const char *path)
{
   DIR *dir;
   struct dirent *entry;
   struct frontend_item *info;
   char label[64];

   reset_items(fe, "Media Player");
   fe->view = FRONTEND_VIEW_MEDIA_PLAYER;
   if (!path || !path[0])
      path = FRONTEND_ROOT;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), path);
   unifrog_text_copy(label, sizeof(label), frontend_basename(path));
   if (!label[0])
      snprintf(label, sizeof(label), "SD Card");
   info = add_info(fe, label, "current folder");
   if (info)
      info->name[sizeof(info->name) - 1u] = '\0';
   if (fe->nav.count > 0 || strcmp(path, FRONTEND_ROOT) != 0)
      add_item(fe, "Back", "folder", FRONTEND_ITEM_DIR, "", NULL);
   dir = opendir(path);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];
         struct stat st;

         if (entry->d_name[0] == '.' && !fe->show_hidden)
            continue;
         if (strcmp(entry->d_name, ".") == 0 ||
             strcmp(entry->d_name, "..") == 0)
            continue;
         if (frontend_path_join(full, sizeof(full), path, entry->d_name) != 0)
            continue;
         if (stat(full, &st) != 0)
            continue;
         if (S_ISDIR(st.st_mode)) {
            add_item(fe, entry->d_name, "folder", FRONTEND_ITEM_DIR, full,
               NULL);
         } else if (S_ISREG(st.st_mode) && is_media_file(full) &&
               !is_reader_file(full)) {
            add_item(fe, entry->d_name, media_path_is_audio(full) ?
               "audio" : "media", FRONTEND_ITEM_MEDIA, full, "media");
         }
      }
      closedir(dir);
   }
   sort_items(fe);
   frontend_set_status(fe, "A play  X open with  B back");
}

void frontend_show_reader_browser(struct frontend_state *fe, const char *path)
{
   DIR *dir;
   struct dirent *entry;
   struct frontend_item *info;
   char label[64];

   reset_items(fe, "Reader");
   fe->view = FRONTEND_VIEW_READER;
   if (!path || !path[0])
      path = FRONTEND_ROOT;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), path);
   unifrog_text_copy(label, sizeof(label), frontend_basename(path));
   if (!label[0])
      snprintf(label, sizeof(label), "SD Card");
   info = add_info(fe, label, "current folder");
   if (info)
      info->name[sizeof(info->name) - 1u] = '\0';
   if (fe->nav.count > 0 || strcmp(path, FRONTEND_ROOT) != 0)
      add_item(fe, "Back", "folder", FRONTEND_ITEM_DIR, "", NULL);
   dir = opendir(path);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];
         struct stat st;

         if (entry->d_name[0] == '.' && !fe->show_hidden)
            continue;
         if (strcmp(entry->d_name, ".") == 0 ||
             strcmp(entry->d_name, "..") == 0)
            continue;
         if (frontend_path_join(full, sizeof(full), path, entry->d_name) != 0)
            continue;
         if (stat(full, &st) != 0)
            continue;
         if (S_ISDIR(st.st_mode))
            add_item(fe, entry->d_name, "folder", FRONTEND_ITEM_DIR, full,
               NULL);
         else if (S_ISREG(st.st_mode) && is_reader_file(full))
            add_item(fe, entry->d_name, "reader", FRONTEND_ITEM_READER, full,
               "reader");
      }
      closedir(dir);
   }
   sort_items(fe);
   frontend_set_status(fe, "A open  L/R page in reader  B back");
}

int frontend_apps_activate(struct frontend_state *fe,
   const struct frontend_item *item)
{
   if (item->action == UNIFROG_FRONTEND_ACTION_APPS) {
      frontend_parent_view_push(fe);
      frontend_show_apps(fe);
      return 1;
   }
   if (strcmp(item->path, "scripts") == 0) {
      frontend_parent_view_push(fe);
      frontend_nav_reset(fe);
      frontend_show_script_browser(fe, FRONTEND_SCRIPT_ROOT);
      return 1;
   }
   if (strcmp(item->path, "media_player") == 0) {
      const char *root = frontend_file_exists(FRONTEND_ROOT "/VIDEOS") ?
         FRONTEND_ROOT "/VIDEOS" : FRONTEND_ROOT;

      frontend_parent_view_push(fe);
      frontend_nav_reset(fe);
      frontend_show_media_player(fe, root);
      return 1;
   }
   if (strcmp(item->path, "reader") == 0) {
      const char *root = frontend_file_exists(FRONTEND_ROOT "/BOOKS") ?
         FRONTEND_ROOT "/BOOKS" : FRONTEND_ROOT;

      frontend_parent_view_push(fe);
      frontend_nav_reset(fe);
      frontend_show_reader_browser(fe, root);
      return 1;
   }
   if (strcmp(item->path, "audio_test") == 0) {
      frontend_launch_audio_diagnostics(fe);
      return 1;
   }
   if (strcmp(item->path, "explore_sd") == 0) {
      frontend_parent_view_clear(fe);
      frontend_nav_reset(fe);
      frontend_show_explore(fe, FRONTEND_ROOT);
      return 1;
   }
   if (strcmp(item->path, "frogui") == 0) {
      struct frontend_item launcher;

      memset(&launcher, 0, sizeof(launcher));
      unifrog_text_copy(launcher.name, sizeof(launcher.name), "FrogUI");
      unifrog_text_copy(launcher.path, sizeof(launcher.path),
         UNIFROG_DATA_ROOT "/frogui/launcher.frogui");
      frontend_launch_with_handler(fe, &launcher, "frogui");
      return 1;
   }
   return 0;
}
