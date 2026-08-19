#include "frontend_internal.h"

void frontend_show_open_with_other(struct frontend_state *fe)
{
   char ids[FRONTEND_CORE_CANDIDATE_MAX][UNIFROG_CORE_MODULE_ID_MAX];
   unsigned count;

   if (!fe || !fe->pending_open_valid)
      return;
   reset_items(fe, "Other Cores");
   fe->view = FRONTEND_VIEW_OPEN_WITH_OTHER;
   count = collect_other_core_candidates(fe, ids);
   for (unsigned i = 0; i < count; i++)
      add_item(fe, ids[i], "experimental", FRONTEND_ITEM_ACTION,
         "open_with_handler", ids[i]);
   add_item(fe, "Back", "choose handler", FRONTEND_ITEM_ACTION,
      "open_with_back", NULL);
   frontend_set_status(fe, "other cores");
}

static void add_content_item(struct frontend_state *fe, const char *name,
   const char *path)
{
   const char *handler = default_handler_for_path(fe, path);
   enum unifrog_media_route route;

   if (strcmp(handler, "reader") == 0 ||
       (!handler[0] && is_reader_file(path)))
      add_item(fe, name, "reader", FRONTEND_ITEM_READER, path, handler);
   else if ((handler[0] && media_handler_route(handler, &route)) ||
            (!handler[0] && is_media_file(path)))
      add_item(fe, name, handler[0] ? handler : "media",
         FRONTEND_ITEM_MEDIA, path, handler[0] ? handler : "media");
   else
      add_item(fe, name, handler[0] ? handler : "auto",
         FRONTEND_ITEM_GAME, path, handler);
}

static void add_dir_entry(struct frontend_state *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   struct stat st;
   int known_dir = type == DT_DIR;
   int known_file = type == DT_REG;

   if (!name || (!fe->show_hidden && name[0] == '.'))
      return;
   if (frontend_path_join(full, sizeof(full), dir, name) != 0)
      return;
   if (frontend_association_for_path(fe, full) ||
       is_reader_file(full) || (fe->mixed_content && is_media_file(full))) {
      if (known_dir) {
         if (stat(full, &st) == 0) {
            known_dir = S_ISDIR(st.st_mode);
            known_file = S_ISREG(st.st_mode);
         }
      }
      if (!known_dir) {
         add_content_item(fe, name, full);
         return;
      }
   }
   if (!known_dir && !known_file) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
      known_file = S_ISREG(st.st_mode);
   }
   if (known_dir) {
      add_item(fe, name, "folder", FRONTEND_ITEM_DIR, full, NULL);
      return;
   }
   if (!known_file)
      return;
}

static void add_rom_dir_entry(struct frontend_state *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   struct stat st;
   int known_dir = type == DT_DIR;
   int known_file = type == DT_REG;

   if (!name || (!fe->show_hidden && name[0] == '.'))
      return;
   if (frontend_path_join(full, sizeof(full), dir, name) != 0)
      return;
   if (!known_dir && !known_file) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
      known_file = S_ISREG(st.st_mode);
   }
   if (known_dir) {
      add_item(fe, name, "folder", FRONTEND_ITEM_DIR, full, NULL);
      return;
   }
   if (!known_file)
      return;
   if (is_reader_file(full) || is_media_file(full) ||
       frontend_association_for_path(fe, full))
      add_content_item(fe, name, full);
   else
      add_item(fe, name, "auto", FRONTEND_ITEM_GAME, full, NULL);
}

static void add_rom_system_entry(struct frontend_state *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   struct stat st;
   int known_dir = type == DT_DIR;

   if (!name || (!fe->show_hidden && name[0] == '.') ||
       strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      return;
   if (frontend_path_join(full, sizeof(full), dir, name) != 0)
      return;
   if (!known_dir) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
   }
   if (known_dir) {
      char meta[32];
      const char *mapped = NULL;

      for (unsigned i = 0; i < fe->rom_system_count; i++) {
         if (strcasecmp(fe->rom_system_name[i], name) == 0) {
            mapped = fe->rom_system_core[i];
            break;
         }
      }
      snprintf(meta, sizeof(meta), "%s", mapped && mapped[0] ? mapped : "auto");
      add_item(fe, name, meta, FRONTEND_ITEM_DIR, full,
         mapped && mapped[0] ? mapped : NULL);
   }
}

const char *frontend_rom_system_mapped_core(const struct frontend_state *fe,
   const char *name)
{
   if (!name)
      return NULL;
   if (fe) {
      for (unsigned i = 0; i < fe->rom_system_count; i++) {
         if (strcasecmp(fe->rom_system_name[i], name) == 0)
            return fe->rom_system_core[i];
      }
   }
   return unifrog_libretro_policy_system_core(name);
}

static unsigned rom_system_core_choices(const struct frontend_state *fe,
   const char *name,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX])
{
   unsigned count = 0;

   (void)name;
   count = unifrog_core_registry_collect_all(&fe->core_registry, ids, count,
      FRONTEND_CORE_CANDIDATE_MAX);
   if (!count)
      count = collect_other_core_candidates(fe, ids);
   return count;
}

void frontend_cycle_rom_system_core(struct frontend_state *fe,
   const char *name, int dir)
{
   char ids[FRONTEND_CORE_CANDIDATE_MAX][UNIFROG_CORE_MODULE_ID_MAX];
   const char *current;
   unsigned count;
   unsigned index = 0;
   unsigned slot;

   if (!fe || !name || !name[0])
      return;
   count = rom_system_core_choices(fe, name, ids);
   if (!count)
      return;
   current = frontend_rom_system_mapped_core(fe, name);
   for (unsigned i = 0; i < count; i++) {
      if (current && strcmp(current, ids[i]) == 0) {
         index = i;
         break;
      }
   }
   index = dir < 0 ? (index == 0 ? count - 1u : index - 1u) :
      (index + 1u) % count;
   for (slot = 0; slot < fe->rom_system_count; slot++) {
      if (strcasecmp(fe->rom_system_name[slot], name) == 0)
         break;
   }
   if (slot >= fe->rom_system_count) {
      if (fe->rom_system_count >= FRONTEND_ROM_SYSTEM_MAP_MAX)
         return;
      slot = fe->rom_system_count++;
      unifrog_text_copy(fe->rom_system_name[slot],
         sizeof(fe->rom_system_name[0]), name);
   }
   unifrog_text_copy(fe->rom_system_core[slot],
      sizeof(fe->rom_system_core[0]), ids[index]);
   frontend_set_status(fe, "%s -> %s", name, ids[index]);
}

static void add_firmware_dir_entry(struct frontend_state *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   char rel[FRONTEND_MAX_PATH];
   struct stat st;
   int known_dir = type == DT_DIR;
   int known_file = type == DT_REG;

   if (!name || name[0] == '.')
      return;
   if (frontend_path_join(full, sizeof(full), dir, name) != 0)
      return;
   if (!known_dir && !known_file) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
      known_file = S_ISREG(st.st_mode);
   }
   if (known_dir) {
      add_item(fe, name, "folder", FRONTEND_ITEM_DIR, full, NULL);
      return;
   }
   if (!known_file || !is_asd_file(full))
      return;
   if (frontend_sd_relative_path(rel, sizeof(rel), full) != 0)
      return;
   add_item(fe, name,
      strcmp(rel, fe->default_boot) == 0 ? "default boot" :
      unifrog_boot_asd_path_supported(rel) ? "boot .asd" : "unsupported name",
      FRONTEND_ITEM_FIRMWARE, full, NULL);
}

static void add_script_dir_entry(struct frontend_state *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   struct stat st;
   int known_dir = type == DT_DIR;
   int known_file = type == DT_REG;

   if (!name || name[0] == '.')
      return;
   if (frontend_path_join(full, sizeof(full), dir, name) != 0)
      return;
   if (!known_dir && !known_file) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
      known_file = S_ISREG(st.st_mode);
   }
   if (known_dir) {
      add_item(fe, name, "folder", FRONTEND_ITEM_DIR, full, NULL);
      return;
   }
   if (known_file && is_js_script_file(full) &&
       !is_js_libretro_core_script(full))
      add_item(fe, name, "JS2300", FRONTEND_ITEM_SCRIPT, full, NULL);
}

void frontend_show_rom_systems(struct frontend_state *fe)
{
   DIR *dir;
   struct dirent *entry;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned seen = 0;
   int limited = 0;

   frontend_rom_root_sync_primary(fe);
   reset_items(fe, frontend_rom_root_count(fe) > 1u ? "ROMs" :
      frontend_rom_root_label(fe));
   fe->view = FRONTEND_VIEW_ROM_SYSTEMS;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir),
      frontend_rom_root(fe));
   add_item(fe, "..", "launcher", FRONTEND_ITEM_DIR, "", NULL);
   for (unsigned r = 0; r < frontend_rom_root_count(fe); r++) {
      const char *root = frontend_rom_root_at(fe, r);

      if (!root)
         continue;
      dir = unifrog_storage_opendir_resilient(root, "rom_systems", 16, 250);
      if (!dir) {
         unifrog_log("frontend rom_systems open_failed path=%s errno=%d\n",
            root, errno);
         continue;
      }
      while ((entry = readdir(dir)) != NULL) {
         seen++;
         if (fe->item_count >= FRONTEND_MAX_ITEMS) {
            limited = 1;
            continue;
         }
         add_rom_system_entry(fe, root, entry->d_name, entry->d_type);
      }
      closedir(dir);
   }
   sort_items(fe);
   frontend_set_status(fe, limited ? "%u/%u systems" : "%u systems",
      fe->item_count ? fe->item_count - 1u : 0u, seen);
   unifrog_log("frontend rom_systems roots=%u seen=%u items=%u limited=%d ms=%lu\n",
      frontend_rom_root_count(fe), seen, fe->item_count, limited,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   log_item_sample(fe, "rom_systems");
   log_selection(fe, "enter");
}

void frontend_show_rom_system_mappings(struct frontend_state *fe)
{
   DIR *dir;
   struct dirent *entry;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned seen = 0;
   int limited = 0;

   reset_items(fe, "ROM Systems");
   fe->view = FRONTEND_VIEW_ROM_SYSTEM_MAPPINGS;
   add_item(fe, "Back", "general", FRONTEND_ITEM_ACTION, "back_config", NULL);
   for (unsigned r = 0; r < frontend_rom_root_count(fe); r++) {
      const char *root = frontend_rom_root_at(fe, r);

      if (!root)
         continue;
      dir = unifrog_storage_opendir_resilient(root, "rom_system_map", 16, 250);
      if (!dir) {
         unifrog_log("frontend rom_system_map open_failed path=%s errno=%d\n",
            root, errno);
         continue;
      }
      while ((entry = readdir(dir)) != NULL) {
         seen++;
         if (fe->item_count >= FRONTEND_MAX_ITEMS) {
            limited = 1;
            continue;
         }
         add_rom_system_entry(fe, root, entry->d_name, entry->d_type);
      }
      closedir(dir);
   }
   sort_items(fe);
   frontend_set_status(fe, limited ? "%u/%u mappings" : "%u mappings",
      fe->item_count ? fe->item_count - 1u : 0u, seen);
   unifrog_log("frontend rom_system_map roots=%u seen=%u items=%u limited=%d ms=%lu\n",
      frontend_rom_root_count(fe), seen, fe->item_count, limited,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   log_item_sample(fe, "rom_system_map");
   log_selection(fe, "enter");
}

void frontend_show_rom_roots(struct frontend_state *fe)
{
   reset_items(fe, "ROM Roots");
   fe->view = FRONTEND_VIEW_ROM_ROOTS;
   frontend_rom_root_sync_primary(fe);
   for (unsigned i = 0; i < frontend_rom_root_count(fe); i++) {
      const char *root = frontend_rom_root_at(fe, i);
      char meta[32];

      snprintf(meta, sizeof(meta), i == 0 ? "active primary" : "active");
      add_item(fe, root, meta, FRONTEND_ITEM_ACTION, "rom_root_remove",
         root);
   }
   for (unsigned i = 0; i < frontend_rom_root_preset_count(); i++) {
      const char *root = frontend_rom_root_preset_at(i);

      if (root && frontend_rom_root_index(fe, root) < 0)
         add_item(fe, root, "add preset", FRONTEND_ITEM_ACTION,
            "rom_root_add", root);
   }
   add_item(fe, "Browse for Root", "choose folder", FRONTEND_ITEM_ACTION,
      "rom_root_browse", NULL);
   add_item(fe, "Remove selected root", "A on active root",
      FRONTEND_ITEM_ACTION, "rom_root_hint", NULL);
   add_item(fe, "Back", "general", FRONTEND_ITEM_ACTION, "back_config", NULL);
   frontend_set_status(fe, "%u active roots", frontend_rom_root_count(fe));
}

void frontend_show_rom_root_picker(struct frontend_state *fe, const char *path)
{
   DIR *dir;
   struct dirent *entry;
   unsigned seen = 0;
   int limited = 0;

   if (!path || !path[0])
      path = FRONTEND_ROOT;
   reset_items(fe, fe->title_include_root ? path : frontend_rom_title(fe, path));
   fe->view = FRONTEND_VIEW_ROM_ROOT_PICKER;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), path);
   add_item(fe, "Use This Folder", path, FRONTEND_ITEM_ACTION,
      "rom_root_use_current", NULL);
   add_item(fe, "..", "up", FRONTEND_ITEM_DIR, "", NULL);
   dir = unifrog_storage_opendir_resilient(path, "rom_root_picker", 16, 250);
   if (!dir) {
      frontend_set_status(fe, "open failed: %s", path);
      return;
   }
   while ((entry = readdir(dir)) != NULL) {
      char full[FRONTEND_MAX_PATH];
      struct stat st;
      int known_dir = entry->d_type == DT_DIR;

      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0 ||
          (!fe->show_hidden && entry->d_name[0] == '.'))
         continue;
      seen++;
      if (fe->item_count >= FRONTEND_MAX_ITEMS) {
         limited = 1;
         continue;
      }
      if (frontend_path_join(full, sizeof(full), path, entry->d_name) != 0)
         continue;
      if (!known_dir) {
         if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
      }
      add_item(fe, entry->d_name, "folder", FRONTEND_ITEM_DIR, full, NULL);
   }
   closedir(dir);
   frontend_set_status(fe, limited ? "%u/%u folders" : "%u folders",
      fe->item_count > 2u ? fe->item_count - 2u : 0u, seen);
}

void frontend_show_explore(struct frontend_state *fe, const char *path)
{
   DIR *dir;
   struct dirent *entry;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned seen = 0;
   int limited = 0;

   reset_items(fe, fe->title_include_root ? path : frontend_rom_title(fe, path));
   fe->view = FRONTEND_VIEW_EXPLORE;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), path);
   add_item(fe, "..", "back", FRONTEND_ITEM_DIR, "", NULL);
   if (is_content_file(fe, path)) {
      frontend_set_status(fe, "content path; press A to launch");
      add_content_item(fe, frontend_basename(path), path);
      return;
   }
   dir = unifrog_storage_opendir_resilient(path, "explore", 16, 250);
   if (!dir) {
      frontend_set_status(fe, "open failed: %s", path);
      return;
   }
   while ((entry = readdir(dir)) != NULL) {
      seen++;
      if (fe->item_count >= FRONTEND_MAX_ITEMS) {
         limited = 1;
         continue;
      }
      if (frontend_path_is_inside_rom_root(fe, path))
         add_rom_dir_entry(fe, path, entry->d_name, entry->d_type);
      else
         add_dir_entry(fe, path, entry->d_name, entry->d_type);
   }
   closedir(dir);
   sort_items(fe);
   frontend_set_status(fe, limited ? "%u/%u entries" : "%u entries",
      fe->item_count ? fe->item_count - 1u : 0u, seen);
   unifrog_log("frontend explore path=%s seen=%u items=%u limited=%d ms=%lu\n",
      path, seen, fe->item_count, limited,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   log_item_sample(fe, "explore");
   log_selection(fe, "enter");
}

void frontend_show_firmware_browser(struct frontend_state *fe, const char *path)
{
   DIR *dir;
   struct dirent *entry;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned seen = 0;
   int limited = 0;
   char title[64];

   snprintf(title, sizeof(title), "Firmware:%s",
      strcmp(path, FRONTEND_ROOT) == 0 ? "/" : frontend_basename(path));
   reset_items(fe, title);
   fe->view = FRONTEND_VIEW_FIRMWARE;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), path);
   add_item(fe, "..", "back", FRONTEND_ITEM_DIR, "", NULL);

   dir = unifrog_storage_opendir_resilient(path, "firmware", 16, 250);
   if (!dir) {
      frontend_set_status(fe, "open failed: %s", path);
      unifrog_log("frontend firmware open_failed path=%s errno=%d\n",
         path, errno);
      return;
   }
   while ((entry = readdir(dir)) != NULL) {
      seen++;
      if (fe->item_count >= FRONTEND_MAX_ITEMS) {
         limited = 1;
         continue;
      }
      add_firmware_dir_entry(fe, path, entry->d_name, entry->d_type);
   }
   closedir(dir);
   sort_items(fe);
   add_item(fe, "Reboot UniFrog", "restart", FRONTEND_ITEM_ACTION,
      "reboot", NULL);
   frontend_set_status(fe, limited ? "A boot  X default  list limited" :
      "A boot  X set default  B back");
   unifrog_log("frontend firmware path=%s seen=%u items=%u limited=%d ms=%lu\n",
      path, seen, fe->item_count, limited,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   log_item_sample(fe, "firmware");
   log_selection(fe, "enter");
}

void frontend_show_script_browser(struct frontend_state *fe, const char *path)
{
   DIR *dir;
   struct dirent *entry;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned seen = 0;
   int limited = 0;

   reset_items(fe, strcmp(path, FRONTEND_SCRIPT_ROOT) == 0 ? "Scripts" :
      frontend_basename(path));
   fe->view = FRONTEND_VIEW_SCRIPTS;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), path);
   add_item(fe, "..", "back", FRONTEND_ITEM_DIR, "", NULL);

   dir = unifrog_storage_opendir_resilient(path, "scripts", 16, 250);
   if (!dir) {
      frontend_set_status(fe, "open failed: %s", path);
      unifrog_log("frontend scripts open_failed path=%s errno=%d\n",
         path, errno);
      return;
   }
   while ((entry = readdir(dir)) != NULL) {
      seen++;
      if (fe->item_count >= FRONTEND_MAX_ITEMS) {
         limited = 1;
         continue;
      }
      add_script_dir_entry(fe, path, entry->d_name, entry->d_type);
   }
   closedir(dir);
   sort_items(fe);
   frontend_set_status(fe, limited ? "%u/%u scripts" : "%u scripts",
      fe->item_count ? fe->item_count - 1u : 0u, seen);
   unifrog_log("frontend scripts path=%s seen=%u items=%u limited=%d ms=%lu\n",
      path, seen, fe->item_count, limited,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   log_item_sample(fe, "scripts");
   log_selection(fe, "enter");
}

void frontend_show_file_list(struct frontend_state *fe, const char *title,
   const char *path, enum frontend_view view)
{
   FILE *file;
   char line[FRONTEND_MAX_LINE];

   reset_items(fe, title);
   fe->view = view;
   file = fopen(path, "rb");
   if (!file) {
      add_item(fe, "Back", "launcher", FRONTEND_ITEM_ACTION, "back", NULL);
      frontend_set_status(fe, "no entries");
      return;
   }
   while (fgets(line, sizeof(line), file) &&
          fe->item_count < FRONTEND_MAX_ITEMS) {
      char *sep;
      char game[FRONTEND_MAX_PATH];
      char core[24];

      frontend_strip_eol(line);
      sep = strchr(line, '|');
      if (sep) {
         *sep = '\0';
         unifrog_text_copy(core, sizeof(core), sep + 1);
      } else {
         core[0] = '\0';
      }
      unifrog_text_copy(game, sizeof(game), line);
      if (game[0]) {
         const char *safe_core = safe_core_for_path(fe, game, core);
         struct stat st;
         char meta[64];

         if (stat(game, &st) == 0)
            snprintf(meta, sizeof(meta), "%s",
               safe_core[0] ? safe_core : "auto");
         else
            snprintf(meta, sizeof(meta), "missing");
         add_item(fe, frontend_basename(game), meta,
            FRONTEND_ITEM_GAME, game, safe_core[0] ? safe_core : NULL);
      }
   }
   fclose(file);
   add_item(fe, "Back", "launcher", FRONTEND_ITEM_ACTION, "back", NULL);
   frontend_set_status(fe, "%u entries",
      fe->item_count ? fe->item_count - 1u : 0u);
}

void frontend_show_open_with(struct frontend_state *fe,
   const struct frontend_item *item)
{
   const struct frontend_association *association;
   char title[96];
   char dir[FRONTEND_MAX_PATH];
   char *slash;

   if (!fe || !item)
      return;
   fe->pending_open_item = *item;
   dir[0] = '\0';
   if (item->path[0]) {
      unifrog_text_copy(dir, sizeof(dir), item->path);
      slash = strrchr(dir, '/');
      if (slash && slash > dir)
         *slash = '\0';
      else
         dir[0] = '\0';
   }
   unifrog_text_copy(fe->pending_open_dir, sizeof(fe->pending_open_dir),
      dir);
   fe->pending_open_valid = 1;
   snprintf(title, sizeof(title), "Open:%.88s", item->name);
   reset_items(fe, title);
   fe->view = FRONTEND_VIEW_OPEN_WITH;
   association = frontend_association_for_path(fe, item->path);
   if (association) {
      for (unsigned i = 0; i < association->handler_count; i++) {
         const char *handler = association->handlers[i];
         enum unifrog_media_route route;
         const char *meta = strcmp(handler, association->default_handler) == 0 ?
            "default; X saves" : "compatible; X saves";

         if (media_handler_route(handler, &route) &&
             !unifrog_media_route_available(item->path, route,
                UNIFROG_HCRTOS_MEDIA_FIRMWARE))
            continue;
         add_item(fe, handler, meta, FRONTEND_ITEM_ACTION,
            "open_with_handler", handler);
      }
   }
   if (item->kind == FRONTEND_ITEM_GAME) {
      char ids[FRONTEND_CORE_CANDIDATE_MAX][UNIFROG_CORE_MODULE_ID_MAX];
      unsigned count = collect_core_candidates(fe, item->path, ids);

      for (unsigned i = 0; i < count; i++) {
         int present = 0;

         for (unsigned j = 0; j < fe->item_count; j++)
            present |= strcmp(fe->items[j].core, ids[i]) == 0;
         if (!present)
            add_item(fe, ids[i], "compatible; X saves",
               FRONTEND_ITEM_ACTION, "open_with_handler", ids[i]);
      }
      add_item(fe, "Other Cores", "experimental", FRONTEND_ITEM_ACTION,
         "open_with_other", NULL);
      if (!count)
         add_item(fe, "Auto core", "launcher default",
            FRONTEND_ITEM_ACTION, "open_with_handler", NULL);
   } else if (item->kind == FRONTEND_ITEM_MEDIA) {
      enum unifrog_media_route route;

      if (!association)
         add_item(fe, "media", "media player; X saves",
            FRONTEND_ITEM_ACTION, "open_with_handler", "media");
      for (route = UNIFROG_MEDIA_ROUTE_NATIVE;
           route <= UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED; route++) {
         const char *handler;
         int present = 0;

         if (!unifrog_media_route_available(item->path, route,
             UNIFROG_HCRTOS_MEDIA_FIRMWARE))
            continue;
         handler = unifrog_media_route_name(route);
         for (unsigned i = 0; i < fe->item_count; i++)
            present |= strcmp(fe->items[i].core, handler) == 0;
         if (!present)
            add_item(fe, handler, "media route; X saves",
               FRONTEND_ITEM_ACTION, "open_with_handler", handler);
      }
   }
   if (item->kind != FRONTEND_ITEM_GAME)
      add_item(fe, "Other Cores", "open with any installed core",
         FRONTEND_ITEM_ACTION, "open_with_other", NULL);
   add_item(fe, "Back", "launcher", FRONTEND_ITEM_ACTION, "back", NULL);
   frontend_set_status(fe, "A open  X always use");
   log_item_sample(fe, "open_with");
}

void frontend_return_from_open_with(struct frontend_state *fe)
{
   enum frontend_view parent;
   unsigned selected = 0;
   unsigned scroll = 0;
   int had_parent = 0;

   if (!fe)
      return;
   if (fe->nav.view_count > 0) {
      parent = fe->nav.view_stack[fe->nav.view_count - 1u];
      selected = fe->nav.view_selected[fe->nav.view_count - 1u];
      scroll = fe->nav.view_scroll[fe->nav.view_count - 1u];
      had_parent = 1;
   } else if (fe->nav.has_parent_view) {
      parent = fe->nav.parent_view;
   } else {
      parent = FRONTEND_VIEW_LAUNCH;
   }
   if (parent == FRONTEND_VIEW_EXPLORE) {
      char dir[FRONTEND_MAX_PATH];

      unifrog_text_copy(dir, sizeof(dir), fe->pending_open_dir[0] ?
         fe->pending_open_dir : frontend_rom_root(fe));
      fe->pending_open_valid = 0;
      frontend_parent_view_clear(fe);
      frontend_show_explore(fe, dir);
      if (had_parent)
         restore_view_selection(fe, selected, scroll);
      return;
   }
   fe->pending_open_valid = 0;
   restore_parent_view(fe, FRONTEND_VIEW_LAUNCH);
}
