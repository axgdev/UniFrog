#include <libretro.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static unsigned frame_counter;
static bool game_loaded;

void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_variable variables[] = {
      { "unifrog_dummy_speed", "Dummy speed; normal|slow|fast" },
      { NULL, NULL },
   };

   env_cb = cb;
   if (env_cb)
      (void)env_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)variables);
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   (void)cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_init(void)
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
   struct retro_variable speed = { "unifrog_dummy_speed", NULL };

   frame_counter = 0;
   game_loaded = false;
   if (env_cb) {
      (void)env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
      (void)env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &speed);
   }
   fprintf(stderr, "dummy core option unifrog_dummy_speed=%s\n",
      speed.value ? speed.value : "(null)");
}

void retro_deinit(void)
{
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name = "unifrog-linux-dummy";
   info->library_version = "1";
#ifdef UNIFROG_DUMMY_FULLPATH
   info->valid_extensions = "zip|ZIP";
   info->need_fullpath = true;
#else
   info->valid_extensions = "gb";
   info->need_fullpath = false;
#endif
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   if (!game_loaded) {
      fprintf(stderr, "dummy lifecycle error: AV info requested before game\n");
      abort();
   }
   memset(info, 0, sizeof(*info));
   info->geometry.base_width = 64;
   info->geometry.base_height = 48;
   info->geometry.max_width = 64;
   info->geometry.max_height = 48;
   info->geometry.aspect_ratio = 4.0f / 3.0f;
   info->timing.fps = 60.0;
   info->timing.sample_rate = 44100.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_run(void)
{
   static uint16_t frame[64 * 48];
   int16_t audio[64 * 2];

   if (input_poll_cb)
      input_poll_cb();
   if (input_state_cb)
      (void)input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
         RETRO_DEVICE_ID_JOYPAD_A);
   for (unsigned y = 0; y < 48; y++) {
      for (unsigned x = 0; x < 64; x++) {
         unsigned r = (x + frame_counter) & 31u;
         unsigned g = (y + frame_counter) & 63u;
         unsigned b = (x + y + frame_counter) & 31u;

         frame[y * 64 + x] = (uint16_t)((r << 11) | (g << 5) | b);
      }
   }
   for (unsigned i = 0; i < 64; i++) {
      int16_t sample = (int16_t)((int)((i + frame_counter) & 31u) - 16);

      audio[i * 2] = sample;
      audio[i * 2 + 1] = sample;
   }
   if (video_cb)
      video_cb(frame, 64, 48, 64 * sizeof(uint16_t));
   if (audio_batch_cb)
      (void)audio_batch_cb(audio, 64);
   frame_counter++;
}

void retro_unload_game(void)
{
   game_loaded = false;
}

bool retro_load_game(const struct retro_game_info *game)
{
#ifdef UNIFROG_DUMMY_FULLPATH
   const char *ext = game && game->path ? strrchr(game->path, '.') : NULL;

   game_loaded = ext && strcmp(ext, ".zip") == 0 &&
      game->data == NULL && game->size == 0;
#else
   game_loaded = game && game->data && game->size > 0;
#endif
   return game_loaded;
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}
