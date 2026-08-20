#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <libretro.h>
#include <unifrog/libretro_extension.h>
#include <unifrog/paths.h>

static unsigned pressed_button = (unsigned)-1;
static unsigned video_frames;
static struct unifrog_libretro_launch_content launch_request;

static int make_dir(const char *path)
{
   return mkdir(path, 0777) == 0 || errno == EEXIST ? 0 : -1;
}

static int write_file(const char *path, const char *text)
{
   FILE *file = fopen(path, "wb");
   size_t size = strlen(text);
   int ok = file && fwrite(text, 1, size, file) == size;

   if (file)
      fclose(file);
   return ok ? 0 : -1;
}

static bool environment_cb(unsigned command, void *data)
{
   if (command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT)
      return data && *(enum retro_pixel_format *)data ==
         RETRO_PIXEL_FORMAT_RGB565;
   if (command == UNIFROG_ENVIRONMENT_LAUNCH_CONTENT) {
      const struct unifrog_libretro_launch_content *request = data;

      if (!request || request->size != sizeof(*request))
         return false;
      launch_request = *request;
      return true;
   }
   if (command == RETRO_ENVIRONMENT_SHUTDOWN)
      return true;
   return false;
}

static void video_cb(const void *data, unsigned width, unsigned height,
   size_t pitch)
{
   if (data && width == 320u && height == 240u && pitch == 640u)
      video_frames++;
}

static void input_poll_cb(void)
{
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index,
   unsigned id)
{
   return port == 0 && device == RETRO_DEVICE_JOYPAD && index == 0 &&
      id == pressed_button;
}

static void press(unsigned id)
{
   pressed_button = id;
   retro_run();
   pressed_button = (unsigned)-1;
   retro_run();
}

int main(void)
{
   struct retro_game_info game;
   char path[512];

   snprintf(path, sizeof(path), "%s", UNIFROG_SD_ROOT);
   if (make_dir(path) != 0)
      return 1;
   snprintf(path, sizeof(path), "%s/unifrog_data", UNIFROG_SD_ROOT);
   if (make_dir(path) != 0)
      return 2;
   snprintf(path, sizeof(path), "%s/ROMS", UNIFROG_SD_ROOT);
   if (make_dir(path) != 0)
      return 3;
   snprintf(path, sizeof(path), "%s/ROMS/TEST", UNIFROG_SD_ROOT);
   if (make_dir(path) != 0)
      return 4;
   snprintf(path, sizeof(path), "%s/ROMS/TEST/game.rom", UNIFROG_SD_ROOT);
   if (write_file(path, "test rom\n") != 0)
      return 5;
   snprintf(path, sizeof(path), "%s/unifrog_data/unifrog.ini",
      UNIFROG_SD_ROOT);
   if (write_file(path,
       "# FrogUI host integration test.\n"
       "rom_root=/ROMS\nrom_system=TEST:dummy\n"
       "[frogui]\ntheme=MinUI Style\nfont=Monogram\n") != 0)
      return 6;

   retro_set_environment(environment_cb);
   retro_set_video_refresh(video_cb);
   retro_set_input_poll(input_poll_cb);
   retro_set_input_state(input_state_cb);
   retro_set_audio_sample(NULL);
   retro_set_audio_sample_batch(NULL);
   retro_init();
   memset(&game, 0, sizeof(game));
   if (!retro_load_game(&game))
      return 7;
   retro_run();
   press(RETRO_DEVICE_ID_JOYPAD_A);
   press(RETRO_DEVICE_ID_JOYPAD_A);
   retro_unload_game();
   retro_deinit();

   snprintf(path, sizeof(path), "%s/ROMS/TEST/game.rom", UNIFROG_SD_ROOT);
   if (video_frames < 2u ||
       launch_request.version != UNIFROG_LIBRETRO_LAUNCH_VERSION ||
       strcmp(launch_request.core_id, "dummy") != 0 ||
       strcmp(launch_request.path, path) != 0) {
      fprintf(stderr, "FrogUI host check failed frames=%u core=%s path=%s\n",
         video_frames, launch_request.core_id, launch_request.path);
      return 8;
   }
   puts("OK FrogUI host integration");
   return 0;
}
