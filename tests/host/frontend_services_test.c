#include "test.h"

#include "frontend_internal.h"

#include <js2300/unifrog_host.h>

#include <stdio.h>

enum call_kind {
   CALL_NONE = 0,
   CALL_GAME,
   CALL_MEDIA,
   CALL_READER,
   CALL_SCRIPT,
   CALL_AUDIO_DIAG,
   CALL_BUG_REPORT,
};

static enum call_kind last_call;
static char last_path[64];

static void note_call(enum call_kind call, const char *path)
{
   last_call = call;
   snprintf(last_path, sizeof(last_path), "%s", path ? path : "");
}

int unifrog_libretro_run_path_ex(const char *path,
   const struct unifrog_libretro_run_options *options)
{
   (void)options;
   note_call(CALL_GAME, path);
   return 11;
}

int unifrog_media_play_video_ex(const char *path,
   const struct unifrog_media_video_options *options)
{
   (void)options;
   note_call(CALL_MEDIA, path);
   return 22;
}

int unifrog_reader_run(const char *path)
{
   note_call(CALL_READER, path);
   return 33;
}

int js2300_run_script_file(const char *path)
{
   note_call(CALL_SCRIPT, path);
   return 44;
}

int unifrog_media_run_audio_diagnostics_ex(char *summary, size_t summary_size,
   unifrog_media_progress_cb progress, void *userdata)
{
   note_call(CALL_AUDIO_DIAG, "audio");
   if (progress)
      progress(userdata, "default-audio", 1, 2);
   if (summary && summary_size)
      snprintf(summary, summary_size, "default-audio");
   return 55;
}

int unifrog_bug_report_create(char *output_path, size_t output_path_size,
   char *summary, size_t summary_size)
{
   note_call(CALL_BUG_REPORT, "bug");
   if (output_path && output_path_size)
      snprintf(output_path, output_path_size, "default.zip");
   if (summary && summary_size)
      snprintf(summary, summary_size, "default-bug");
   return 66;
}

static int fake_run_game(const char *path,
   const struct unifrog_libretro_run_options *options)
{
   (void)options;
   note_call(CALL_GAME, path);
   return 101;
}

static int fake_run_script(const char *path)
{
   note_call(CALL_SCRIPT, path);
   return 104;
}

static void progress_note(void *userdata, const char *stage, unsigned done,
   unsigned total)
{
   char *buffer = userdata;

   snprintf(buffer, 32, "%s:%u/%u", stage ? stage : "", done, total);
}

static void test_default_services(void)
{
   static struct frontend_state fe;
   struct unifrog_libretro_run_options game_options;
   struct unifrog_media_video_options media_options;
   char summary[32];
   char report_path[32];
   char progress[32];

   memset(&fe, 0, sizeof(fe));
   memset(&game_options, 0, sizeof(game_options));
   memset(&media_options, 0, sizeof(media_options));
   memset(summary, 0, sizeof(summary));
   memset(report_path, 0, sizeof(report_path));
   memset(progress, 0, sizeof(progress));

   TEST_CHECK(unifrog_frontend_launch_services_default() != NULL);
   TEST_EQ_INT(11, frontend_services_run_game(&fe, "game.gb",
      &game_options));
   TEST_EQ_INT(CALL_GAME, last_call);
   TEST_EQ_STR("game.gb", last_path);

   TEST_EQ_INT(22, frontend_services_play_media(&fe, "movie.mp4",
      &media_options));
   TEST_EQ_INT(CALL_MEDIA, last_call);
   TEST_EQ_STR("movie.mp4", last_path);

   TEST_EQ_INT(33, frontend_services_run_reader(&fe, "manual.txt"));
   TEST_EQ_INT(CALL_READER, last_call);
   TEST_EQ_STR("manual.txt", last_path);

   TEST_EQ_INT(44, frontend_services_run_script(&fe, "tool.js"));
   TEST_EQ_INT(CALL_SCRIPT, last_call);
   TEST_EQ_STR("tool.js", last_path);

   TEST_EQ_INT(55, frontend_services_run_audio_diagnostics(&fe, summary,
      sizeof(summary), progress_note, progress));
   TEST_EQ_INT(CALL_AUDIO_DIAG, last_call);
   TEST_EQ_STR("default-audio", summary);
   TEST_EQ_STR("default-audio:1/2", progress);

   TEST_EQ_INT(66, frontend_services_create_bug_report(&fe, report_path,
      sizeof(report_path), summary, sizeof(summary)));
   TEST_EQ_INT(CALL_BUG_REPORT, last_call);
   TEST_EQ_STR("default.zip", report_path);
   TEST_EQ_STR("default-bug", summary);
}

static void test_partial_override_falls_back(void)
{
   static struct frontend_state fe;
   struct unifrog_frontend_launch_services services;
   struct unifrog_libretro_run_options game_options;
   struct unifrog_media_video_options media_options;

   memset(&fe, 0, sizeof(fe));
   memset(&services, 0, sizeof(services));
   memset(&game_options, 0, sizeof(game_options));
   memset(&media_options, 0, sizeof(media_options));
   services.run_game = fake_run_game;
   services.run_script = fake_run_script;
   fe.launch_services = &services;

   TEST_EQ_INT(101, frontend_services_run_game(&fe, "override.gb",
      &game_options));
   TEST_EQ_INT(CALL_GAME, last_call);
   TEST_EQ_STR("override.gb", last_path);

   TEST_EQ_INT(22, frontend_services_play_media(&fe, "fallback.mp4",
      &media_options));
   TEST_EQ_INT(CALL_MEDIA, last_call);
   TEST_EQ_STR("fallback.mp4", last_path);

   TEST_EQ_INT(104, frontend_services_run_script(&fe, "override.js"));
   TEST_EQ_INT(CALL_SCRIPT, last_call);
   TEST_EQ_STR("override.js", last_path);
}

int main(void)
{
   test_default_services();
   test_partial_override_falls_back();
   return test_finish("frontend services");
}
