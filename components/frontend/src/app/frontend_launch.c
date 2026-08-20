#include "frontend_internal.h"

void frontend_launch_game(struct frontend_state *fe, struct frontend_item *item)
{
   const char *core;
   struct unifrog_libretro_run_options options;
   struct stat st;
   int ret;

   if (!item || !item->path[0])
      return;
   if (item->core[0]) {
      const char *safe_core = safe_core_for_path(fe, item->path, item->core);

      if (strcmp(safe_core, item->core) != 0) {
         unifrog_log("frontend launch core sanitized path=%s requested=%s safe=%s\n",
            item->path, item->core, safe_core);
         unifrog_text_copy(item->core, sizeof(item->core), safe_core);
      }
   }
   if (fe->view != FRONTEND_VIEW_OPEN_WITH &&
       fe->view != FRONTEND_VIEW_OPEN_WITH_OTHER && !item->core[0]) {
      frontend_parent_view_push(fe);
      frontend_show_open_with(fe, item);
      return;
   }
   if (unifrog_storage_stat_resilient(item->path, &st, "launch_game",
       16, 250) != 0) {
      frontend_set_status(fe, "missing: %s", item->name);
      unifrog_log("frontend launch missing path=%s errno=%d\n", item->path,
         errno);
      return;
   }
   if (!S_ISREG(st.st_mode) || st.st_size <= 0) {
      frontend_set_status(fe, "invalid game file");
      unifrog_log("frontend launch invalid_file path=%s mode=0x%lx size=%ld\n",
         item->path, (unsigned long)st.st_mode, (long)st.st_size);
      return;
   }
   unifrog_log("frontend launch game path=%s core=%s\n", item->path,
      item->core);
   options = fe->run_options;
   if (item->core[0])
      unifrog_text_copy(options.core_id, sizeof(options.core_id), item->core);
   else
      options.core_id[0] = '\0';
   unifrog_frontend_config_apply(&fe->scoped_config, options.core_id,
      item->path, &options);
   if (options.core_id[0]) {
      const char *safe_core = safe_core_for_path(fe, item->path,
         options.core_id);

      if (strcmp(safe_core, options.core_id) != 0)
         unifrog_text_copy(options.core_id, sizeof(options.core_id),
            safe_core);
   }
   options.core_path[0] = '\0';
   if (options.core_id[0]) {
      const struct unifrog_core_registry_entry *entry =
         unifrog_core_registry_find(&fe->core_registry, options.core_id);

      if (entry && entry->format == UNIFROG_CORE_REGISTRY_NATIVE)
         unifrog_text_copy(options.core_path, sizeof(options.core_path),
            entry->path);
   }
   core = options.core_id[0] ? options.core_id : "";
   if (fe->launch_splash)
      frontend_loading_show(fe, "Launching", item->name,
         core[0] ? core : "auto core", 8);
   unifrog_diag_memory_snapshot("frontend.launch");
   frontend_sound_shutdown();
   ret = frontend_services_run_game(fe, item->path, &options);
   {
      unsigned config_errors = 0;

      if (unifrog_frontend_config_load(&fe->scoped_config,
          UNIFROG_CONFIG_PATH, &config_errors) == 0 && config_errors)
         unifrog_log("frontend scoped reload parse_errors=%u path=%s\n",
            config_errors, UNIFROG_CONFIG_PATH);
   }
   frontend_history_record(fe, item->path, core);
   frontend_set_status(fe, "returned %d", ret);
   (void)unifrog_ui_open(&fe->ui, 0);
   unifrog_input_clear();
   frontend_request_return_redraw(fe, "game");
}

struct frontend_launch_progress {
   struct frontend_state *fe;
   char name[96];
   int handoff_done;
};

static void frontend_launch_progress_update(void *userdata, const char *stage,
   unsigned done, unsigned total)
{
   struct frontend_launch_progress *progress = userdata;
   unsigned percent = done;

   if (!progress || !progress->fe)
      return;
   if (total > 0)
      percent = (unsigned)(((uint64_t)done * 100ull) / (uint64_t)total);
   if (percent > 100u)
      percent = 100u;
   if (percent >= 100u) {
      if (!progress->handoff_done) {
         progress->handoff_done = 1;
         frontend_loading_handoff_black(progress->fe,
            stage && stage[0] ? stage : "media");
      }
      return;
   }
   if (progress->handoff_done)
      return;
   frontend_loading_show(progress->fe, "Media", progress->name,
      stage && stage[0] ? stage : "loading", percent);
}

void frontend_launch_media(struct frontend_state *fe,
   struct frontend_item *item)
{
   struct unifrog_media_video_options options;
   struct frontend_launch_progress progress;
   struct stat st;
   enum unifrog_media_route route = UNIFROG_MEDIA_ROUTE_AUTO;
   int route_valid;
   int ret = -1;

   if (!item || !item->path[0])
      return;
   route_valid = unifrog_media_route_parse(item->core, &route) == 0;
   if (fe->view != FRONTEND_VIEW_OPEN_WITH &&
       ((!item->core[0] && media_path_has_open_with_choices(item->path)) ||
        (item->core[0] && (!route_valid ||
         !unifrog_media_route_available(item->path, route,
            UNIFROG_HCRTOS_MEDIA_FIRMWARE))))) {
      frontend_parent_view_push(fe);
      frontend_show_open_with(fe, item);
      return;
   }
   if (unifrog_storage_stat_resilient(item->path, &st, "launch_media",
       16, 250) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
      frontend_set_status(fe, "missing: %s", item->name);
      unifrog_log("frontend launch media missing path=%s errno=%d\n",
         item->path, errno);
      return;
   }
   unifrog_log("frontend launch media path=%s\n", item->path);
   memset(&progress, 0, sizeof(progress));
   progress.fe = fe;
   unifrog_text_copy(progress.name, sizeof(progress.name), item->name);
   if (fe->launch_splash && !media_path_is_audio(item->path))
      frontend_loading_show(fe, "Media", item->name, "loading", 20);
   memset(&options, 0, sizeof(options));
   options.preset = -1;
   options.tuning = &fe->media_tuning;
   if (fe->launch_splash && !media_path_is_audio(item->path)) {
      options.progress = frontend_launch_progress_update;
      options.progress_userdata = &progress;
   }
   options.route = route_valid ? route : UNIFROG_MEDIA_ROUTE_AUTO;
   frontend_sound_shutdown();
   ret = frontend_services_play_media(fe, item->path, &options);
   frontend_set_status(fe, "media returned %d", ret);
   if (fe->ui.fb.pixels)
      unifrog_ui_close(&fe->ui);
   (void)unifrog_ui_open(&fe->ui, 0);
   unifrog_input_clear();
   if (fe->view == FRONTEND_VIEW_OPEN_WITH ||
       fe->view == FRONTEND_VIEW_OPEN_WITH_OTHER)
      frontend_return_from_open_with(fe);
   frontend_request_return_redraw(fe, "media");
}

void frontend_launch_reader(struct frontend_state *fe,
   struct frontend_item *item)
{
   struct stat st;
   int ret;

   if (!item || !item->path[0])
      return;
   if (stat(item->path, &st) != 0 ||
       (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
      frontend_set_status(fe, "missing: %s", item->name);
      return;
   }
   unifrog_log("frontend launch reader path=%s\n", item->path);
   if (fe->launch_splash)
      frontend_loading_show(fe, "Reader", item->name, "opening", 15);
   frontend_sound_shutdown();
   unifrog_ui_close(&fe->ui);
   ret = frontend_services_run_reader(fe, item->path);
   frontend_set_status(fe, "reader returned %d", ret);
   (void)unifrog_ui_open(&fe->ui, 0);
   unifrog_input_clear();
   if (fe->view == FRONTEND_VIEW_OPEN_WITH ||
       fe->view == FRONTEND_VIEW_OPEN_WITH_OTHER)
      frontend_return_from_open_with(fe);
   frontend_request_return_redraw(fe, "reader");
}

void frontend_launch_with_handler(struct frontend_state *fe,
   struct frontend_item *item, const char *handler)
{
   enum unifrog_media_route route;

   if (!fe || !item)
      return;
   unifrog_text_copy(item->core, sizeof(item->core), handler ? handler : "");
   if (handler && strcmp(handler, "reader") == 0) {
      item->kind = FRONTEND_ITEM_READER;
      frontend_launch_reader(fe, item);
   } else if (media_handler_route(handler, &route)) {
      item->kind = FRONTEND_ITEM_MEDIA;
      frontend_launch_media(fe, item);
   } else {
      item->kind = FRONTEND_ITEM_GAME;
      frontend_launch_game(fe, item);
   }
}

void frontend_launch_script(struct frontend_state *fe,
   struct frontend_item *item)
{
   struct stat st;
   int ret;
   int stat_ret;

   if (!item || !item->path[0])
      return;
   stat_ret = stat(item->path, &st);
   if (stat_ret != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
      frontend_set_status(fe, "invalid script");
      unifrog_log("frontend script invalid path=%s errno=%d size=%ld\n",
         item->path, errno, stat_ret == 0 ? (long)st.st_size : -1l);
      return;
   }
   unifrog_log("frontend script launch path=%s size=%ld\n",
      item->path, (long)st.st_size);
   if (fe->launch_splash)
      frontend_loading_show(fe, "Script", item->name, "running", 8);
   frontend_sound_shutdown();
   (void)unifrog_log_flush();
   unifrog_ui_close(&fe->ui);
   ret = frontend_services_run_script(fe, item->path);
   frontend_set_status(fe, "script returned %d", ret);
   (void)unifrog_ui_open(&fe->ui, 0);
   unifrog_input_clear();
   frontend_request_return_redraw(fe, "script");
}

void frontend_launch_last_game(struct frontend_state *fe)
{
   struct frontend_item last;

   if (!fe->last_path[0]) {
      frontend_set_status(fe, "no recent game");
      return;
   }
   memset(&last, 0, sizeof(last));
   unifrog_text_copy(last.name, sizeof(last.name),
      frontend_basename(fe->last_path));
   unifrog_text_copy(last.path, sizeof(last.path), fe->last_path);
   unifrog_text_copy(last.core, sizeof(last.core), fe->last_core);
   last.kind = FRONTEND_ITEM_GAME;
   frontend_launch_game(fe, &last);
}

void frontend_launch_audio_diagnostics(struct frontend_state *fe)
{
   char summary[96];
   struct frontend_launch_progress progress;
   int ret;

   frontend_loading_show(fe, "Audio", "GB300 diagnostics",
      "listen for route tones", 8);
   frontend_sound_shutdown();
   (void)unifrog_log_flush();
   summary[0] = '\0';
   memset(&progress, 0, sizeof(progress));
   progress.fe = fe;
   unifrog_text_copy(progress.name, sizeof(progress.name),
      "GB300 diagnostics");
   ret = frontend_services_run_audio_diagnostics(fe, summary,
      sizeof(summary), frontend_launch_progress_update, &progress);
   unifrog_input_clear();
   frontend_set_status(fe, "audio diag %d %s", ret, summary);
}
