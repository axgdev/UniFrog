#include "js2300_frontend_internal.h"

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

int js2300_frontend_main(void)
{
   struct js2300_frontend frontend;
   struct js2300_config config;
   struct js2300_host host;
   uint32_t frontend_start_ms;
   unsigned launch_count = 0;
   int ret = 0;

   memset(&frontend, 0, sizeof(frontend));
   frontend_start_ms = unifrog_perf_time_ms();
   unifrog_battery_status_init(&frontend.battery);

   if (frontend_fb_open(&frontend) != 0)
      return -1;
   printf("unifrog boot_time stage=js_fb_ready total_ms=%lu\n",
      (unsigned long)(unifrog_perf_time_ms() - frontend_start_ms));

   js2300_config_init(&config);
   config.app_root = JS2300_FRONTEND_APP_ROOT;
   config.entry_script = JS2300_FRONTEND_ENTRY;
   config.heap_bytes = 8u * 1024u * 1024u;

   frontend_configure_host(&frontend, &host);

   do {
      struct js2300_runtime *runtime = NULL;
      size_t old_auto_flush;
      const char *recover_tag;
      unsigned relaunch = launch_count++;
      uint32_t launch_ms;
      uint32_t create_start_ms;
      uint32_t run_start_ms;
      char diag_tag[48];

      frontend.relaunch = 0;
      frontend.action[0] = 0;
      frontend.path[0] = 0;
      frontend.video_preset = 0;
      frontend.video_disable_audio = 0;
      recover_tag = relaunch ? "frontend_relaunch" : "frontend_launch";
      if (frontend.input_recovered) {
         printf("unifrog input recover_transition skip tag=%s reason=already_recovered\n",
            recover_tag);
         unifrog_input_clear();
         frontend.input_recovered = 0;
      } else {
         unifrog_input_recover_core_transition(recover_tag);
      }
      unifrog_libretro_run_options_init(&frontend.run_options);
      old_auto_flush = unifrog_log_auto_flush_bytes();
      unifrog_log_set_auto_flush_bytes(64u * 1024u);
      printf("unifrog js launch root=%s script=%s boot_ms=%lu relaunch=%u\n",
         config.app_root, config.entry_script,
         (unsigned long)(unifrog_perf_time_ms() - frontend_start_ms),
         relaunch);
      snprintf(diag_tag, sizeof(diag_tag), "frontend.launch.%u", relaunch);
      unifrog_diag_memory_snapshot(diag_tag);
      unifrog_log_flush();

      create_start_ms = unifrog_perf_time_ms();
      ret = js2300_runtime_create(&config, &host, &runtime);
      launch_ms = unifrog_perf_time_ms();
      printf("unifrog boot_time stage=js_runtime_created total_ms=%lu ret=%d relaunch=%u\n",
         (unsigned long)(launch_ms - frontend_start_ms),
         ret, relaunch);
      printf("unifrog js phase=runtime_create relaunch=%u ms=%lu heap=%u stack=%u bytecode_cache=%u ret=%d\n",
         relaunch, (unsigned long)(launch_ms - create_start_ms),
         (unsigned)config.heap_bytes, (unsigned)config.stack_bytes,
         (unsigned)config.bytecode_cache_bytes, ret);
      snprintf(diag_tag, sizeof(diag_tag), "frontend.created.%u", relaunch);
      unifrog_diag_memory_snapshot(diag_tag);
      if (ret == 0) {
         run_start_ms = unifrog_perf_time_ms();
         ret = js2300_runtime_run(runtime);
         printf("unifrog js phase=runtime_run relaunch=%u ms=%lu ret=%d\n",
            relaunch, (unsigned long)(unifrog_perf_time_ms() - run_start_ms),
            ret);
      }
      snprintf(diag_tag, sizeof(diag_tag), "frontend.after_run.%u", relaunch);
      unifrog_diag_memory_snapshot(diag_tag);
      js2300_runtime_destroy(runtime);
      snprintf(diag_tag, sizeof(diag_tag), "frontend.destroyed.%u", relaunch);
      unifrog_diag_memory_snapshot(diag_tag);
      unifrog_log_set_auto_flush_bytes(old_auto_flush);
      printf("unifrog js done ret=%d action=%s path=%s\n",
         ret, frontend.action, frontend.path);

      if (ret == 0 && frontend.action[0]) {
         unifrog_diag_memory_snapshot("frontend.before_action");
         ret = run_requested_action(&frontend);
         unifrog_diag_memory_snapshot("frontend.after_action");
      } else {
         unifrog_log_flush();
      }
   } while (ret == 0 && frontend.relaunch);

   frontend_icon_cache_clear(&frontend);
   unifrog_fb_close(&frontend.fb);
   return ret;
}
