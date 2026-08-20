/*
 * The SDK kernel keeps initcall and filesystem-map references to optional
 * media/NTFS plugins. In the module media build those plugins live outside the
 * boot image, so the base firmware only needs inert placeholders.
 *
 * Keep libviddrv linked in the base firmware so /dev/dis and normal display
 * controls still exist before the media module is loaded. Only the heavier
 * image sink and NTFS hooks remain stubbed here.
 */

#include <stddef.h>
#include <stdio.h>

#include <unifrog/media.h>

struct mountpt_operations {
   void *reserved[19];
};

const struct mountpt_operations ntfs_operations = { { 0 } };

int vidsink_init(void)
{
   return 0;
}

int unifrog_media_play_video_ex(const char *path,
   const struct unifrog_media_video_options *options)
{
   (void)path;
   (void)options;
   return -1;
}

int unifrog_media_play_video(const char *path)
{
   return unifrog_media_play_video_ex(path, NULL);
}

int unifrog_media_run_audio_diagnostics_ex(char *summary, size_t summary_size,
   unifrog_media_progress_cb progress, void *userdata)
{
   if (progress)
      progress(userdata, "module media unavailable", 100, 100);
   if (summary && summary_size)
      snprintf(summary, summary_size, "module media unavailable");
   return -1;
}

int unifrog_media_run_audio_diagnostics(char *summary, size_t summary_size)
{
   return unifrog_media_run_audio_diagnostics_ex(summary, summary_size, NULL,
      NULL);
}
