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
      unifrog_log_flush();

      ret = js2300_runtime_create(&config, &host, &runtime);
      printf("unifrog boot_time stage=js_runtime_created total_ms=%lu ret=%d relaunch=%u\n",
         (unsigned long)(unifrog_perf_time_ms() - frontend_start_ms),
         ret, relaunch);
      if (ret == 0)
         ret = js2300_runtime_run(runtime);
      js2300_runtime_destroy(runtime);
      unifrog_log_set_auto_flush_bytes(old_auto_flush);
      printf("unifrog js done ret=%d action=%s path=%s\n",
         ret, frontend.action, frontend.path);

      if (ret == 0 && frontend.action[0])
         ret = run_requested_action(&frontend);
      else
         unifrog_log_flush();
   } while (ret == 0 && frontend.relaunch);

   frontend_icon_cache_clear(&frontend);
   unifrog_fb_close(&frontend.fb);
   return ret;
}
