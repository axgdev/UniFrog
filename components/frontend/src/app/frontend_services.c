#include "frontend_internal.h"

#include <js2300/unifrog_host.h>

#include <unifrog/bug_report.h>
#include <unifrog/reader.h>

static const struct unifrog_frontend_launch_services default_launch_services = {
   .run_game = unifrog_libretro_run_path_ex,
   .play_media = unifrog_media_play_video_ex,
   .run_reader = unifrog_reader_run,
   .run_script = js2300_run_script_file,
   .run_audio_diagnostics = unifrog_media_run_audio_diagnostics_ex,
   .create_bug_report = unifrog_bug_report_create,
};

const struct unifrog_frontend_launch_services *
unifrog_frontend_launch_services_default(void)
{
   return &default_launch_services;
}

static const struct unifrog_frontend_launch_services *active_services(
   const struct frontend_state *fe)
{
   if (fe && fe->launch_services)
      return fe->launch_services;
   return &default_launch_services;
}

int frontend_services_run_game(const struct frontend_state *fe,
   const char *path, const struct unifrog_libretro_run_options *options)
{
   const struct unifrog_frontend_launch_services *services =
      active_services(fe);

   return (services->run_game ? services->run_game :
      default_launch_services.run_game)(path, options);
}

int frontend_services_play_media(const struct frontend_state *fe,
   const char *path, const struct unifrog_media_video_options *options)
{
   const struct unifrog_frontend_launch_services *services =
      active_services(fe);

   return (services->play_media ? services->play_media :
      default_launch_services.play_media)(path, options);
}

int frontend_services_run_reader(const struct frontend_state *fe,
   const char *path)
{
   const struct unifrog_frontend_launch_services *services =
      active_services(fe);

   return (services->run_reader ? services->run_reader :
      default_launch_services.run_reader)(path);
}

int frontend_services_run_script(const struct frontend_state *fe,
   const char *path)
{
   const struct unifrog_frontend_launch_services *services =
      active_services(fe);

   return (services->run_script ? services->run_script :
      default_launch_services.run_script)(path);
}

int frontend_services_run_audio_diagnostics(const struct frontend_state *fe,
   char *summary, size_t summary_size, unifrog_media_progress_cb progress,
   void *userdata)
{
   const struct unifrog_frontend_launch_services *services =
      active_services(fe);

   return (services->run_audio_diagnostics ?
      services->run_audio_diagnostics :
      default_launch_services.run_audio_diagnostics)(summary, summary_size,
         progress, userdata);
}

int frontend_services_create_bug_report(const struct frontend_state *fe,
   char *output_path, size_t output_path_size, char *summary,
   size_t summary_size)
{
   const struct unifrog_frontend_launch_services *services =
      active_services(fe);

   return (services->create_bug_report ? services->create_bug_report :
      default_launch_services.create_bug_report)(output_path, output_path_size,
         summary, summary_size);
}
