#include <unifrog/bug_report.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __HCRTOS__
#include <sys/unistd.h>
#else
#include <unistd.h>
#endif

#include <unifrog/battery.h>
#include <unifrog/build_info.h>
#include <unifrog/config.h>
#include <unifrog/device.h>
#include <unifrog/log.h>
#include <unifrog/paths.h>
#include <unifrog/perf.h>
#include <unifrog/platform.h>
#include <unifrog/zip.h>

#define BUG_REPORT_FILE_MAX (8u * 1024u * 1024u)
#define BUG_REPORT_TOTAL_MAX (32u * 1024u * 1024u)
#define BUG_REPORT_TREE_DEPTH 4u

struct bug_report_context {
   struct unifrog_zip_writer *writer;
   size_t bytes;
   unsigned files;
   unsigned skipped;
};

static int report_name_skip(const char *name)
{
   size_t len = strlen(name);

   if (!name[0] || name[0] == '.')
      return 1;
   if (len >= 4u && strcmp(name + len - 4u, ".zip") == 0)
      return 1;
   if (len >= 4u && strcmp(name + len - 4u, ".ppm") == 0)
      return 1;
   return len >= 4u && strcmp(name + len - 4u, ".tmp") == 0;
}

static int report_add_file(struct bug_report_context *context,
   const char *archive_name, const char *path)
{
   struct stat st;

   if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0)
      return 0;
   if ((uint64_t)st.st_size > BUG_REPORT_FILE_MAX ||
       (uint64_t)st.st_size > BUG_REPORT_TOTAL_MAX - context->bytes) {
      context->skipped++;
      return 0;
   }
   if (unifrog_zip_writer_add_path(context->writer, archive_name, path,
       BUG_REPORT_FILE_MAX) != 0)
      return -1;
   context->bytes += (size_t)st.st_size;
   context->files++;
   return 0;
}

static int report_add_tree(struct bug_report_context *context,
   const char *root, const char *archive_root, unsigned depth)
{
   DIR *dir;
   struct dirent *entry;

   if (depth > BUG_REPORT_TREE_DEPTH)
      return 0;
   dir = opendir(root);
   if (!dir)
      return errno == ENOENT ? 0 : -1;
   while ((entry = readdir(dir)) != NULL) {
      char path[256];
      char archive_name[256];
      struct stat st;
      int ret;

      if (report_name_skip(entry->d_name))
         continue;
      if (snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >=
          (int)sizeof(path) ||
          snprintf(archive_name, sizeof(archive_name), "%s/%s", archive_root,
             entry->d_name) >= (int)sizeof(archive_name)) {
         context->skipped++;
         continue;
      }
      if (stat(path, &st) != 0) {
         context->skipped++;
         continue;
      }
      if (S_ISDIR(st.st_mode))
         ret = report_add_tree(context, path, archive_name, depth + 1u);
      else
         ret = report_add_file(context, archive_name, path);
      if (ret != 0) {
         closedir(dir);
         return ret;
      }
   }
   closedir(dir);
   return 0;
}

static int report_add_optional(struct bug_report_context *context,
   const char *name, const char *path)
{
   return report_add_file(context, name, path);
}

int unifrog_bug_report_create(char *output_path, size_t output_path_size,
   char *summary, size_t summary_size)
{
   struct unifrog_zip_writer writer;
   struct bug_report_context context;
   struct unifrog_battery_status battery;
   char final_path[256];
   char temporary_path[264];
   char manifest[1024];
   unsigned uptime = unifrog_perf_time_ms() / 1000u;
   int manifest_len;
   int ret = -1;

   if (output_path && output_path_size)
      output_path[0] = '\0';
   if (summary && summary_size)
      summary[0] = '\0';
   memset(&context, 0, sizeof(context));
   (void)unifrog_log_flush_force();
   (void)mkdir(UNIFROG_BUG_REPORT_ROOT, 0777);
   snprintf(final_path, sizeof(final_path), "%s/bug-report-%s-%08u.zip",
      UNIFROG_BUG_REPORT_ROOT, UNIFROG_GIT_COMMIT, uptime);
   snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", final_path);
   if (unifrog_zip_writer_open_path(temporary_path, &writer) != 0)
      goto out;

   context.writer = &writer;
   unifrog_battery_status_init(&battery);
   (void)unifrog_battery_update(&battery, 0);
   manifest_len = snprintf(manifest, sizeof(manifest),
      "format=unifrog-bug-report-v1\n"
      "firmware_commit=%s\nfirmware_dirty=%d\n"
      "sdk_commit=%s\ncores_commit=%s\njs2300_commit=%s\n"
      "frontend_commit=%s\nhcrtos_media=%s\n"
      "uptime_seconds=%u\nboard=%s\nvariant=%s\npanel=%s\n"
      "battery_available=%d\nbattery_mv=%u\nbattery_percent=%u\n"
      "battery_rate_mv_per_hour=%u\nstorage_profile=%s\n"
      "log_level=%s\nlog_pending_bytes=%lu\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY, UNIFROG_SDK_GIT_COMMIT,
      UNIFROG_CORES_GIT_COMMIT, UNIFROG_JS2300_GIT_COMMIT,
      UNIFROG_FRONTEND_GIT_COMMIT, UNIFROG_HCRTOS_MEDIA, uptime,
      unifrog_device_board_name(unifrog_device_board()),
      unifrog_device_variant_name(),
      unifrog_device_panel_name(unifrog_device_panel()), battery.available,
      battery.millivolts, battery.percent, battery.discharge_mv_per_hour,
      unifrog_platform_sd_active_profile(),
      unifrog_log_level_name(unifrog_log_get_level()),
      (unsigned long)unifrog_log_pending());
   if (manifest_len <= 0 || manifest_len >= (int)sizeof(manifest) ||
       unifrog_zip_writer_add_data(&writer, "report.ini", manifest,
          (size_t)manifest_len) != 0)
      goto abort;
   context.files++;
   context.bytes += (size_t)manifest_len;

   if (report_add_optional(&context, "config/unifrog.ini",
          UNIFROG_CONFIG_PATH) != 0 ||
       report_add_optional(&context, "system/manifest.ini",
          UNIFROG_DIST_MANIFEST_PATH) != 0 ||
       report_add_optional(&context, "state/boot-ok.ini",
          UNIFROG_BOOT_OK_PATH) != 0 ||
       report_add_optional(&context, "state/last-crash.ini",
          UNIFROG_CRASH_MARKER_PATH) != 0 ||
       report_add_optional(&context, "state/package-check.txt",
          UNIFROG_PACKAGE_CHECK_PATH) != 0)
      goto abort;
   if (report_add_tree(&context, UNIFROG_LOG_ROOT, "logs", 0) != 0)
      goto abort;
   if (unifrog_zip_writer_close(&writer) != 0)
      goto out;
   if (unifrog_config_commit(temporary_path, final_path) != 0) {
      unlink(temporary_path);
      goto out;
   }
   if (output_path && output_path_size)
      snprintf(output_path, output_path_size, "%s", final_path);
   ret = 0;
   goto out;

abort:
   unifrog_zip_writer_abort(&writer);
out:
   if (summary && summary_size)
      snprintf(summary, summary_size, "%s files=%u bytes=%lu skipped=%u",
         ret == 0 ? "created" : "failed", context.files,
         (unsigned long)context.bytes, context.skipped);
   UF_LOG_INFO("bug-report", "event=create ret=%d path=%s files=%u bytes=%lu skipped=%u",
      ret, ret == 0 ? final_path : temporary_path, context.files,
      (unsigned long)context.bytes, context.skipped);
   return ret;
}
