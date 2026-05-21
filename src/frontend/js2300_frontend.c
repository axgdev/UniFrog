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

void js2300_frontend_default_run_options(
   struct unifrog_libretro_run_options *options)
{
   unifrog_libretro_run_options_init(options);
   if (!options)
      return;
   options->audio_enabled = 1;
   options->audio_gain = 1;
   options->scpu_mhz = 702;
   options->ge_clock = -1;
   options->backlight_level = -1;
   options->frameskip = UNIFROG_LIBRETRO_FRAMESKIP_AUTO;
   options->display_mode = UNIFROG_LIBRETRO_DISPLAY_FIT;
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
   frontend.frontend_start_ms = start_ms;
   frontend.extension_mode = mode == JS2300_SCRIPT_MODE_EXTENSION;
   unifrog_battery_status_init(&frontend.battery);
   js2300_frontend_default_run_options(&frontend.run_options);

   if (!frontend.extension_mode && frontend_fb_open(&frontend) != 0)
      return -1;
   frontend.owns_framebuffer = frontend.extension_mode ? 0 : 1;
   ret = run_js_script_file(&frontend, path);
   if (ret == 0 && frontend.action[0])
      ret = run_requested_action(&frontend);
   if (frontend.ge_ready)
      unifrog_ge_close(&frontend.ge);
   if (frontend.owns_framebuffer)
      unifrog_fb_close(&frontend.fb);
   printf("js2300 script native_return ret=%d ms=%lu path=%s mode=%s\n",
      ret, (unsigned long)(unifrog_perf_time_ms() - start_ms), path,
      frontend.extension_mode ? "extension" : "standalone");
   return ret;
}

int js2300_run_script_file(const char *path)
{
   return js2300_run_script_file_ex(path, JS2300_SCRIPT_MODE_STANDALONE);
}
