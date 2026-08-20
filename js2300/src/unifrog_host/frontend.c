#include "internal.h"

#define JS2300_SCRIPT_HEAP_DEFAULT_BYTES (8u * 1024u * 1024u)
#define JS2300_SCRIPT_HEAP_LAUNCHER_BYTES (2u * 1024u * 1024u)
#define JS2300_SCRIPT_LAUNCHER_BYTECODE_CACHE_BYTES (256u * 1024u)

static int split_script_path(const char *path, char *root, size_t root_size,
   char *entry, size_t entry_size)
{
   const char *slash;
   size_t root_len;
   size_t entry_len;

   if (!path || !path[0] || !root || !entry ||
       root_size == 0 || entry_size == 0)
      return -1;
   slash = strrchr(path, '/');
   if (!slash || slash == path)
      return -1;
   root_len = (size_t)(slash - path);
   entry_len = strlen(slash + 1);
   if (root_len >= root_size || entry_len >= entry_size)
      return -1;
   memcpy(root, path, root_len);
   root[root_len] = '\0';
   unifrog_text_copy(entry, entry_size, slash + 1);
   return 0;
}

static size_t script_heap_bytes_for_entry(const char *entry)
{
   if (!entry)
      return JS2300_SCRIPT_HEAP_DEFAULT_BYTES;

   /*
    * libretro-benchmark.js keeps the JS runtime alive while it launches cores
    * through system.action("run+..."). Large zipped ROMs need one contiguous
    * appmem reservation at the same time, so the benchmark script intentionally
    * uses a smaller heap than general-purpose scripts.
    */
   if (strcmp(entry, "libretro-benchmark.js") == 0)
      return JS2300_SCRIPT_HEAP_LAUNCHER_BYTES;

   return JS2300_SCRIPT_HEAP_DEFAULT_BYTES;
}

static int script_uses_appmem_heap(const char *entry)
{
   if (!entry)
      return 1;

   /*
    * The appmem top allocator is deliberately single-reservation. A launcher
    * script that keeps appmem reserved while it starts libretro would block the
    * ROM loader from reserving large content buffers.
    */
   if (strcmp(entry, "libretro-benchmark.js") == 0)
      return 0;

   return 1;
}

static size_t script_bytecode_cache_bytes_for_entry(const char *entry,
   size_t fallback)
{
   if (entry && strcmp(entry, "libretro-benchmark.js") == 0)
      return JS2300_SCRIPT_LAUNCHER_BYTECODE_CACHE_BYTES;
   return fallback;
}

void frontend_configure_host(struct js2300_frontend *frontend,
   struct js2300_host *host)
{
   memset(host, 0, sizeof(*host));
   host->size = sizeof(*host);
   host->opaque = frontend;
   host->mode = frontend->extension_mode ? "extension" : "standalone";
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
   host->fs_read_bytes = host_fs_read_bytes;
   host->fs_write_text = host_fs_write_text;
   host->fs_index = host_fs_index;
   host->cpu_clock = host_cpu_clock;
   host->fs_stat = host_fs_stat;
   host->fs_mkdir = host_fs_mkdir;
   host->fs_remove = host_fs_remove;
   host->fs_rename = host_fs_rename;
   host->fs_write_bytes = host_fs_write_bytes;
}

int run_js_script_file(struct js2300_frontend *frontend, const char *path)
{
   struct js2300_config config;
   struct js2300_host host;
   struct js2300_runtime *runtime = NULL;
   char root[JS2300_FRONTEND_MAX_PATH];
   char entry[96];
   void *script_heap = NULL;
   uint32_t start_ms;
   uint32_t create_start_ms;
   uint32_t run_start_ms;
   int script_heap_reserved = 0;
   int ret;

   if (!frontend || split_script_path(path, root, sizeof(root),
       entry, sizeof(entry)) != 0)
      return -1;

   js2300_config_init(&config);
   config.app_root = root;
   config.entry_script = entry;
   config.heap_bytes = script_heap_bytes_for_entry(entry);
   config.bytecode_cache_bytes = script_bytecode_cache_bytes_for_entry(entry,
      config.bytecode_cache_bytes);
   if (script_uses_appmem_heap(entry) &&
       unifrog_abi_application_memory_reserve_top(config.heap_bytes, 32u,
          &script_heap) == 0) {
      config.heap = script_heap;
      config.heap_external = 1;
      script_heap_reserved = 1;
   }
   frontend_configure_host(frontend, &host);
   start_ms = unifrog_perf_time_ms();
   printf("js2300 script launch root=%s script=%s heap=%u heap_source=%s ptr=0x%08lx mode=%s\n",
      root, entry, (unsigned)config.heap_bytes,
      script_heap_reserved ? "appmem" : "calloc",
      (unsigned long)(uintptr_t)script_heap,
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
   if (frontend->scpu_restore_valid) {
      int restore_ret = unifrog_scpu_restore(&frontend->scpu_restore);

      printf("js2300 script cpu_restore ret=%d mhz=%u\n", restore_ret,
         unifrog_scpu_current_mhz());
      frontend->scpu_restore_valid = 0;
   }
   if (frontend->ui_open) {
      unifrog_ui_close(&frontend->ui);
      frontend->ui_open = 0;
   }
   if (script_heap_reserved)
      unifrog_abi_application_memory_release_top(script_heap);
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
