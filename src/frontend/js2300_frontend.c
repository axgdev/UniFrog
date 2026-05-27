#include "js2300_frontend_internal.h"

static int split_script_path(const char *path, char *root, size_t root_size,
   char *entry, size_t entry_size)
{
   const char *slash;
   size_t root_len;

   if (!path || !path[0] || !root || !entry ||
       root_size == 0 || entry_size == 0)
      return -1;
   slash = strrchr(path, '/');
   if (!slash || slash == path)
      return -1;
   root_len = (size_t)(slash - path);
   if (root_len >= root_size || strlen(slash + 1) >= entry_size)
      return -1;
   memcpy(root, path, root_len);
   root[root_len] = '\0';
   strcpy(entry, slash + 1);
   return 0;
}

void frontend_configure_host(struct js2300_frontend *frontend,
   struct js2300_host *host)
{
   memset(host, 0, sizeof(*host));
   host->size = sizeof(*host);
   host->opaque = frontend;
   host->log = host_log;
   host->flush_log = host_flush_log;
   host->millis = host_millis;
   host->sleep_ms = host_sleep;
   host->video_clear = host_video_clear;
   host->video_rects = host_video_rects;
   host->video_text = host_video_text;
   host->video_image = host_video_image;
   host->video_present = host_video_present;
   host->font_load = host_video_font;
   host->input_poll = host_input_poll;
   host->battery = host_battery;
   host->fs_list = host_fs_list;
   host->action = host_action;
   host->exit = host_exit;
   host->backlight = host_backlight;
   host->av_output = host_av_output;
   host->fs_read_text = host_fs_read_text;
   host->fs_write_text = host_fs_write_text;
   host->fs_index = host_fs_index;
}

int run_js_script_file(struct js2300_frontend *frontend, const char *path)
{
   struct js2300_config config;
   struct js2300_host host;
   struct js2300_runtime *runtime = NULL;
   char root[JS2300_FRONTEND_MAX_PATH];
   char entry[96];
   uint32_t start_ms;
   uint32_t create_start_ms;
   uint32_t run_start_ms;
   int ret;

   if (!frontend || split_script_path(path, root, sizeof(root),
       entry, sizeof(entry)) != 0)
      return -1;

   js2300_config_init(&config);
   config.app_root = root;
   config.entry_script = entry;
   config.heap_bytes = 8u * 1024u * 1024u;
   frontend_configure_host(frontend, &host);
   start_ms = unifrog_perf_time_ms();
   printf("js2300 script launch root=%s script=%s heap=%u mode=%s\n",
      root, entry, (unsigned)config.heap_bytes,
      frontend->extension_mode ? "extension" : "standalone");
   unifrog_diag_memory_snapshot("script.launch");
   (void)unifrog_log_flush();
   create_start_ms = unifrog_perf_time_ms();
   ret = js2300_runtime_create(&config, &host, &runtime);
   printf("js2300 script phase=runtime_create ms=%lu ret=%d\n",
      (unsigned long)(unifrog_perf_time_ms() - create_start_ms), ret);
   unifrog_diag_memory_snapshot("script.created");
   if (ret == 0) {
      run_start_ms = unifrog_perf_time_ms();
      ret = js2300_runtime_run(runtime);
      printf("js2300 script phase=runtime_run ms=%lu ret=%d\n",
         (unsigned long)(unifrog_perf_time_ms() - run_start_ms), ret);
   }
   unifrog_diag_memory_snapshot("script.after_run");
   js2300_runtime_destroy(runtime);
   unifrog_diag_memory_snapshot("script.destroyed");
   printf("js2300 script done ret=%d ms=%lu path=%s action=%s\n",
      ret, (unsigned long)(unifrog_perf_time_ms() - start_ms), path,
      frontend->action);
   (void)unifrog_log_flush();
   return ret;
}

int js2300_run_script_file_ex(const char *path, enum js2300_script_mode mode)
{
   struct js2300_frontend frontend;
   uint32_t start_ms;
   int ret;

   if (!path || !path[0])
      return -1;

   memset(&frontend, 0, sizeof(frontend));
   start_ms = unifrog_perf_time_ms();
   frontend.extension_mode = mode == JS2300_SCRIPT_MODE_EXTENSION;
   unifrog_battery_status_init(&frontend.battery);

   ret = run_js_script_file(&frontend, path);
   if (ret == 0 && frontend.action[0])
      ret = run_requested_action(&frontend);
   printf("js2300 script native_return ret=%d ms=%lu path=%s mode=%s\n",
      ret, (unsigned long)(unifrog_perf_time_ms() - start_ms), path,
      frontend.extension_mode ? "extension" : "standalone");
   return ret;
}

int js2300_run_script_file(const char *path)
{
   return js2300_run_script_file_ex(path, JS2300_SCRIPT_MODE_STANDALONE);
}
