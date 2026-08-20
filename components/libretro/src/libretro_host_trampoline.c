#include "unifrog_libretro_internal.h"

bool unifrog_libretro_environment_trampoline(unsigned cmd, void *data)
{
   return unifrog_libretro_environment_cb(cmd, data);
}

void unifrog_libretro_video_refresh_trampoline(const void *data,
   unsigned width, unsigned height, size_t pitch)
{
   unifrog_libretro_video_refresh_cb(data, width, height, pitch);
}

void unifrog_libretro_audio_sample_trampoline(int16_t left, int16_t right)
{
   unifrog_libretro_audio_sample_cb(left, right);
}

size_t unifrog_libretro_audio_batch_trampoline(const int16_t *data,
   size_t frames)
{
   return unifrog_libretro_audio_batch_cb(data, frames);
}

void unifrog_libretro_input_poll_trampoline(void)
{
   unifrog_libretro_input_poll_cb();
}

int16_t unifrog_libretro_input_state_trampoline(unsigned port,
   unsigned device, unsigned index, unsigned id)
{
   return unifrog_libretro_input_state_cb(port, device, index, id);
}

bool unifrog_libretro_rumble_trampoline(unsigned port,
   enum retro_rumble_effect effect, uint16_t strength)
{
   return unifrog_libretro_rumble_cb(port, effect, strength);
}
