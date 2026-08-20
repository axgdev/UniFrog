#include "frontend_internal.h"

static void fast_probe_progress_cb(void *userdata, const char *line1,
   const char *line2)
{
   struct frontend_state *fe = userdata;

   if (!fe)
      return;
   frontend_set_status(fe, "%s%s%s", line1 ? line1 : "fast SD probe",
      line2 && line2[0] ? ": " : "", line2 && line2[0] ? line2 : "");
   frontend_draw(fe);
}

void frontend_show_firmware(struct frontend_state *fe)
{
   frontend_nav_reset(fe);
   frontend_show_firmware_browser(fe, FRONTEND_ROOT);
}

void frontend_show_info(struct frontend_state *fe)
{
   char detail[64];
   struct stat st;

   reset_items(fe, "Information");
   fe->view = FRONTEND_VIEW_INFO;
   snprintf(detail, sizeof(detail), "%s%s", UNIFROG_GIT_COMMIT,
      UNIFROG_GIT_DIRTY ? " dirty" : "");
   add_item(fe, "Activity Log", "mark log", FRONTEND_ITEM_ACTION,
      "flush_log", NULL);
   add_item(fe, "Screenshot", "diagnose", FRONTEND_ITEM_ACTION,
      "screenshot", NULL);
   add_item(fe, "Diagnostics", "runtime", FRONTEND_ITEM_ACTION, "sysinfo", NULL);
   add_item(fe, "Core Manager", "ABI status", FRONTEND_ITEM_ACTION, "cores",
      NULL);
   add_item(fe, "Package Check", "layout", FRONTEND_ITEM_ACTION, "package_check",
      NULL);
   add_item(fe, "Storage", "status and tools", FRONTEND_ITEM_ACTION, "storage",
      NULL);
   add_item(fe, "Uptime", fe->clock_enabled ? "local tick" : "off",
      FRONTEND_ITEM_ACTION, "chrony", NULL);
   add_info(fe, "Credits", UNIFROG_FRONTEND_GIT_COMMIT);
   if (stat(UNIFROG_CONFIG_PATH, &st) == 0) {
      snprintf(detail, sizeof(detail), "%lu bytes",
         (unsigned long)st.st_size);
      frontend_set_status(fe, "settings %s", detail);
   }
}

void frontend_show_updates(struct frontend_state *fe)
{
   DIR *dir;
   struct dirent *entry;

   frontend_ensure_data_dirs();
   reset_items(fe, "Updates");
   fe->view = FRONTEND_VIEW_UPDATES;
   add_info(fe, "Current", UNIFROG_GIT_COMMIT);
   dir = opendir(FRONTEND_UPDATE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];

         if (!is_zip_file(entry->d_name))
            continue;
         if (frontend_path_join(full, sizeof(full), FRONTEND_UPDATE_ROOT,
             entry->d_name) != 0)
            continue;
         add_item(fe, entry->d_name, "install zip",
            FRONTEND_ITEM_UPDATE_ARCHIVE, full, NULL);
      }
      closedir(dir);
   }
   dir = opendir(FRONTEND_VERSION_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];
         char marker[FRONTEND_MAX_PATH];
         struct stat st;

         if (entry->d_name[0] == '.')
            continue;
         if (frontend_path_join(full, sizeof(full), FRONTEND_VERSION_ROOT,
             entry->d_name) != 0 ||
             stat(full, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
         if (frontend_path_join(marker, sizeof(marker), full,
             "unifrog/firmware/unifrog.bin") != 0 || !frontend_file_exists(marker))
            continue;
         add_item(fe, entry->d_name, "activate version",
            FRONTEND_ITEM_VERSION, full, entry->d_name);
      }
      closedir(dir);
   }
   add_item(fe, "Back", "apps", FRONTEND_ITEM_ACTION, "back_apps", NULL);
   frontend_set_status(fe, "put update zips in /unifrog_data/updates");
}

static void add_check_item(struct frontend_state *fe, unsigned *pass,
   unsigned *fail, const char *name, int ok, const char *detail)
{
   add_info(fe, name, ok ? "ok" : detail ? detail : "bad");
   if (ok)
      (*pass)++;
   else
      (*fail)++;
}

void frontend_show_core_info(struct frontend_state *fe, const char *path)
{
   const struct unifrog_core_registry_entry *entry =
      unifrog_core_registry_find_path(&fe->core_registry, path);
   struct unifrog_core_module_header h;
   char detail[96];
   int valid;
   int compat;

   reset_items(fe, "Core Info");
   fe->view = FRONTEND_VIEW_CORE_INFO;
   if (entry && entry->format == UNIFROG_CORE_REGISTRY_NATIVE) {
      add_info(fe, "Core", entry->header.core_id);
      add_info(fe, "Status", "native Linux core");
      add_info(fe, "Extensions", entry->header.extensions[0] ?
         entry->header.extensions : "none");
      add_info(fe, "Path", frontend_basename(entry->path));
      add_item(fe, "Back", "cores", FRONTEND_ITEM_ACTION, "cores", NULL);
      return;
   }
   if (unifrog_core_registry_read_header(path, &h) != 0) {
      add_info(fe, frontend_basename(path), "read failed");
      add_item(fe, "Back", "cores", FRONTEND_ITEM_ACTION, "cores", NULL);
      return;
   }
   valid = unifrog_core_registry_header_valid(&h);
   compat = core_module_header_compatible(&h);
   add_info(fe, "Core", h.core_id[0] ? h.core_id : frontend_basename(path));
   add_info(fe, "Status", valid ? (compat ? "compatible" : "unsupported") :
      "invalid");
   snprintf(detail, sizeof(detail), "%u.%u.%u size %lu",
      (unsigned)UNIFROG_ABI_VERSION_GET_MAJOR(h.required_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_MINOR(h.required_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_PATCH(h.required_abi_version),
      (unsigned long)(h.required_abi_size ? h.required_abi_size :
      (unsigned)UNIFROG_ABI_CORE_MIN_SIZE));
   add_info(fe, "Requires ABI", detail);
   snprintf(detail, sizeof(detail), "%u.%u.%u size %lu",
      (unsigned)UNIFROG_ABI_VERSION_GET_MAJOR(h.built_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_MINOR(h.built_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_PATCH(h.built_abi_version),
      (unsigned long)h.built_abi_size);
   add_info(fe, "Built ABI", detail);
   snprintf(detail, sizeof(detail), "0x%08lx + %u",
      (unsigned long)h.load_addr,
      (unsigned)(h.memory_end_addr - h.load_addr));
   add_info(fe, "Memory", detail);
   snprintf(detail, sizeof(detail), "%lu bytes",
      (unsigned long)h.exports_size);
   add_info(fe, "Exports", detail);
   add_info(fe, "Extensions", h.extensions[0] ? h.extensions : "none");
   add_info(fe, "Path", frontend_basename(path));
   add_item(fe, "Back", "cores", FRONTEND_ITEM_ACTION, "cores", NULL);
}

void frontend_show_core_manager(struct frontend_state *fe)
{
   unsigned total = 0;
   unsigned bad = 0;

   reset_items(fe, "Cores");
   fe->view = FRONTEND_VIEW_CORES;
   for (unsigned i = 0; i < fe->core_registry.count; i++) {
      const struct unifrog_core_registry_entry *entry =
         &fe->core_registry.entries[i];
      char meta[64];

      total++;
      if (entry->format == UNIFROG_CORE_REGISTRY_NATIVE) {
         snprintf(meta, sizeof(meta), "native: %.48s",
            entry->header.extensions[0] ? entry->header.extensions : "any");
      } else {
         core_module_meta(meta, sizeof(meta), &entry->header);
         if (!core_module_header_compatible(&entry->header))
            bad++;
      }
      add_item(fe, entry->header.core_id, meta, FRONTEND_ITEM_CORE_MODULE,
         entry->path, NULL);
   }
   add_item(fe, "Back", "apps", FRONTEND_ITEM_ACTION, "back_apps", NULL);
   frontend_set_status(fe, "%u cores, %u issues", total, bad);
}

static int dist_has_user_state_dir(const char *name)
{
   return strcmp(name, "saves") == 0 || strcmp(name, "cache") == 0 ||
      strcmp(name, "logs") == 0 || strcmp(name, "themes") == 0 ||
      strcmp(name, "languages") == 0 || strcmp(name, "archive") == 0 ||
      strcmp(name, "scripts") == 0 || strcmp(name, "updates") == 0 ||
      strcmp(name, "versions") == 0 || strcmp(name, "user") == 0;
}

void frontend_show_package_check(struct frontend_state *fe)
{
   DIR *dir;
   struct dirent *entry;
   unsigned pass = 0;
   unsigned fail = 0;
   unsigned cores = 0;
   unsigned core_bad = 0;
   char summary[96];

   reset_items(fe, "Package Check");
   fe->view = FRONTEND_VIEW_PACKAGE_CHECK;
   add_check_item(fe, &pass, &fail, "bios/bisrv.asd",
      frontend_file_exists(UNIFROG_BIOS_ROOT "/bisrv.asd"), "missing");
   add_check_item(fe, &pass, &fail, "firmware",
      frontend_file_exists(UNIFROG_DIST_FIRMWARE_PATH), "missing");
   add_check_item(fe, &pass, &fail, "manifest",
      frontend_file_exists(UNIFROG_DIST_MANIFEST_PATH), "missing");
   add_check_item(fe, &pass, &fail, "data root",
      frontend_file_exists(UNIFROG_DATA_ROOT), "missing");
   add_check_item(fe, &pass, &fail, "logs root",
      frontend_file_exists(UNIFROG_LOG_ROOT), "missing");
   dir = opendir(FRONTEND_DIST_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         if (entry->d_name[0] == '.' || !dist_has_user_state_dir(entry->d_name))
            continue;
         snprintf(summary, sizeof(summary), "state in dist: %.72s",
            entry->d_name);
         add_check_item(fe, &pass, &fail, "dist clean", 0, summary);
      }
      closedir(dir);
   }
   dir = opendir(UNIFROG_CORE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];
         struct unifrog_core_module_header h;

         if (!is_core_module_file(entry->d_name))
            continue;
         if (frontend_path_join(full, sizeof(full), UNIFROG_CORE_ROOT,
             entry->d_name) != 0)
            continue;
         cores++;
         if (unifrog_core_registry_read_header(full, &h) != 0 ||
             !core_module_header_compatible(&h))
            core_bad++;
      }
      closedir(dir);
   }
   snprintf(summary, sizeof(summary), "%u cores, %u bad", cores, core_bad);
   add_check_item(fe, &pass, &fail, "core headers", core_bad == 0 && cores > 0,
      summary);
   dir = opendir(FRONTEND_UPDATE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];

         if (!is_zip_file(entry->d_name))
            continue;
         if (frontend_path_join(full, sizeof(full), FRONTEND_UPDATE_ROOT,
             entry->d_name) != 0)
            continue;
         validate_update_archive(full, summary, sizeof(summary));
         add_check_item(fe, &pass, &fail, entry->d_name,
            strncmp(summary, "ok ", 3) == 0, summary);
      }
      closedir(dir);
   }
   snprintf(summary, sizeof(summary), "pass=%u fail=%u\ncommit=%s\n",
      pass, fail, UNIFROG_GIT_COMMIT);
   (void)frontend_write_text_file(UNIFROG_PACKAGE_CHECK_PATH, summary);
   add_item(fe, "Back", "apps", FRONTEND_ITEM_ACTION, "back_apps", NULL);
   frontend_set_status(fe, "%u pass, %u fail", pass, fail);
   unifrog_log("frontend package check pass=%u fail=%u cores=%u bad=%u\n",
      pass, fail, cores, core_bad);
}

void frontend_show_sysinfo(struct frontend_state *fe)
{
   char detail[64];
   char slot[64];
   uint32_t now = unifrog_perf_time_ms();

   reset_items(fe, "SysInfo");
   fe->view = FRONTEND_VIEW_SYSINFO;
   add_info(fe, "Version", UNIFROG_GIT_COMMIT);
   add_info(fe, "Layout", "current");
   snprintf(detail, sizeof(detail), "%u.%u.%u size %u",
      UNIFROG_ABI_VERSION_MAJOR_VALUE, UNIFROG_ABI_VERSION_MINOR_VALUE,
      UNIFROG_ABI_VERSION_PATCH_VALUE, (unsigned)unifrog_abi_get()->size);
   add_info(fe, "ABI", detail);
   if (frontend_read_file_key(slot, sizeof(slot), FRONTEND_ACTIVE_VERSION_PATH,
       "slot") != 0)
      unifrog_text_copy(slot, sizeof(slot), "live");
   add_info(fe, "Slot", slot);
   add_info(fe, "Boot OK", frontend_file_exists(UNIFROG_BOOT_OK_PATH) ? "yes" : "no");
   add_info(fe, "Pending", frontend_file_exists(UNIFROG_PENDING_VERSION_PATH) ? "yes" :
      "no");
   add_info(fe, "Theme", active_theme_label(fe));
   add_info(fe, "Language", active_language_label(fe));
   add_info(fe, "SD Mode", unifrog_platform_sd_active_profile());
   add_info(fe, "Log Path", unifrog_log_last_path() ?
      unifrog_log_last_path() : UNIFROG_LOG_ROOT);
   snprintf(detail, sizeof(detail), "%s%s", UNIFROG_FRONTEND_GIT_COMMIT,
      UNIFROG_GIT_DIRTY ? " dirty" : "");
   add_info(fe, "Build", detail);
   add_info(fe, "Device", "SF2000");
   add_info(fe, "Kernel", "HCRTOS");
   snprintf(detail, sizeof(detail), "%lus", (unsigned long)(now / 1000u));
   add_info(fe, "Uptime", detail);
   add_info(fe, "Cpu", "MIPS");
   snprintf(detail, sizeof(detail), "%u MHz", fe->run_options.scpu_mhz);
   add_info(fe, "Speed", detail);
   add_info(fe, "Governor", "fixed");
   add_info(fe, "Memory", "external");
   add_info(fe, "Swap", "none");
   add_info(fe, "Temp", "n/a");
   snprintf(detail, sizeof(detail), "%u bars", fe->battery.bars);
   add_info(fe, "Capacity", fe->battery.available ? detail : "n/a");
   snprintf(detail, sizeof(detail), "%u mV", fe->battery.millivolts);
   add_info(fe, "Voltage", fe->battery.available ? detail : "n/a");
   add_info(fe, "Charger", "unknown");
   add_item(fe, "Reload", "refresh", FRONTEND_ITEM_ACTION, "sysinfo", NULL);
}

static int activate_update_archive(struct frontend_state *fe,
   const struct frontend_item *item)
{
   char slot[64];
   int ret;

   frontend_loading_show(fe, "Update", item->name, "installing", 5);
   ret = install_update_archive(item->path, slot, sizeof(slot));
   if (ret == 0)
      frontend_set_status(fe, "installed %s", slot);
   else
      frontend_set_status(fe, "update install failed %d", ret);
   frontend_show_updates(fe);
   return 1;
}

static int activate_version(struct frontend_state *fe,
   const struct frontend_item *item)
{
   int ret;

   frontend_loading_show(fe, "Update", item->name, "activating", 50);
   ret = activate_installed_version(item->core[0] ? item->core : item->name);
   if (ret == 0) {
      frontend_loading_show(fe, "Update", item->name, "rebooting", 95);
      (void)unifrog_log_flush();
      unifrog_boot_reboot();
   }
   frontend_set_status(fe, "activation failed %d", ret);
   frontend_show_updates(fe);
   return 1;
}

static int activate_storage_fast_probe(struct frontend_state *fe)
{
   char summary[64];
   int ret;

   frontend_loading_show(fe, "Fast SD Probe", "Testing profiles",
      "Report in /unifrog", 5);
   ret = unifrog_storage_fast_probe_run(fast_probe_progress_cb, fe,
      summary, sizeof(summary));
   frontend_set_status(fe, "fast SD probe %d  %s", ret, summary);
   return 1;
}

static int activate_flush_log(struct frontend_state *fe)
{
   size_t pending = unifrog_log_pending();

   unifrog_log("frontend log marker view=%d title=%s pending_before=%lu\n",
      fe->view, fe->title, (unsigned long)pending);
   frontend_set_status(fe, "log marked %lu bytes", (unsigned long)pending);
   return 1;
}

static int activate_bug_report(struct frontend_state *fe)
{
   char path[FRONTEND_MAX_PATH];
   char summary[96];
   int ret;

   frontend_loading_show(fe, "Bug Report", "collecting diagnostics",
      "writing ZIP", 30);
   ret = frontend_services_create_bug_report(fe, path, sizeof(path),
      summary, sizeof(summary));
   frontend_show_apps(fe);
   frontend_set_status(fe, ret == 0 ? "Created %s" : "Bug report failed: %s",
      ret == 0 ? frontend_basename(path) : summary);
   return 1;
}

int frontend_maintenance_activate(struct frontend_state *fe,
   const struct frontend_item *item)
{
   if (item->kind == FRONTEND_ITEM_UPDATE_ARCHIVE)
      return activate_update_archive(fe, item);
   if (item->kind == FRONTEND_ITEM_VERSION)
      return activate_version(fe, item);
   if (item->kind == FRONTEND_ITEM_CORE_MODULE) {
      frontend_show_core_info(fe, item->path);
      return 1;
   }

   switch (item->action) {
   case UNIFROG_FRONTEND_ACTION_INFO:
      frontend_parent_view_push(fe);
      frontend_show_info(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_UPDATES:
      frontend_parent_view_push(fe);
      frontend_show_updates(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_CORES:
      frontend_parent_view_push(fe);
      frontend_show_core_manager(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_PACKAGE_CHECK:
      frontend_parent_view_push(fe);
      frontend_show_package_check(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_FIRMWARE:
      frontend_show_firmware(fe);
      return 1;
   case UNIFROG_FRONTEND_ACTION_STORAGE_FAST_PROBE:
      return activate_storage_fast_probe(fe);
   case UNIFROG_FRONTEND_ACTION_FLUSH_LOG:
      return activate_flush_log(fe);
   default:
      break;
   }

   if (strcmp(item->path, "sysinfo") == 0) {
      frontend_parent_view_push(fe);
      frontend_show_sysinfo(fe);
      return 1;
   }
   if (strcmp(item->path, "bug_report") == 0)
      return activate_bug_report(fe);
   if (strcmp(item->path, "screenshot") == 0) {
      unifrog_diag_memory_snapshot("muos.info.screenshot");
      frontend_set_status(fe, "snapshot logged");
      return 1;
   }
   if (strcmp(item->path, "news") == 0) {
      frontend_set_status(fe, "news unavailable offline");
      return 1;
   }
   if (strcmp(item->path, "netinfo") == 0) {
      frontend_set_status(fe, "network unsupported on SF2000");
      return 1;
   }
   if (strcmp(item->path, "chrony") == 0) {
      frontend_set_status(fe, "uptime %lus",
         (unsigned long)(unifrog_perf_time_ms() / 1000u));
      return 1;
   }
   if (strcmp(item->path, "back_apps") == 0) {
      frontend_show_apps(fe);
      return 1;
   }
   return 0;
}
