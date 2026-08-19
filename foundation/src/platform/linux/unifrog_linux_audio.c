#include <unifrog/audio.h>

#include <errno.h>
#include <string.h>

static int linux_system_output_enabled = 1;
static int linux_output_gate_enabled = 1;
static unsigned linux_system_volume = 65;
static int linux_system_muted;

static void linux_audio_clear(struct unifrog_audio *audio)
{
   if (!audio)
      return;
   memset(audio, 0, sizeof(*audio));
   audio->fd = -1;
   audio->backend = UNIFROG_AUDIO_BACKEND_AUTO;
   audio->muted = 1;
}

int unifrog_audio_open(struct unifrog_audio *audio,
   unsigned rate, unsigned channels,
   unsigned period_bytes, unsigned periods)
{
   return unifrog_audio_open_backend(audio, rate, channels, period_bytes,
      periods, UNIFROG_AUDIO_BACKEND_AUTO);
}

int unifrog_audio_open_backend(struct unifrog_audio *audio,
   unsigned rate, unsigned channels,
   unsigned period_bytes, unsigned periods, int backend)
{
   if (!audio || rate == 0 || channels == 0 || channels > 2 ||
       period_bytes == 0 || periods == 0)
      return -EINVAL;
   linux_audio_clear(audio);
   audio->fd = -2;
   audio->backend = backend;
   audio->rate = rate;
   audio->channels = channels;
   audio->period_bytes = period_bytes;
   audio->periods = periods;
   audio->frame_bytes = channels * sizeof(int16_t);
   audio->muted = 1;
   audio->output_gate_enabled = linux_output_gate_enabled;
   return 0;
}

void unifrog_audio_close(struct unifrog_audio *audio)
{
   linux_audio_clear(audio);
}

int unifrog_audio_start(struct unifrog_audio *audio)
{
   return audio && audio->fd != -1 ? 0 : -EINVAL;
}

int unifrog_audio_drop(struct unifrog_audio *audio)
{
   return audio && audio->fd != -1 ? 0 : -EINVAL;
}

int unifrog_audio_write(struct unifrog_audio *audio,
   const int16_t *samples, unsigned frames)
{
   if (!audio || audio->fd == -1 || (!samples && frames))
      return -EINVAL;
   return 0;
}

int unifrog_audio_write_timeout(struct unifrog_audio *audio,
   const int16_t *samples, unsigned frames,
   unsigned attempts, unsigned poll_timeout_ms)
{
   (void)attempts;
   (void)poll_timeout_ms;
   return unifrog_audio_write(audio, samples, frames);
}

int unifrog_audio_delay(struct unifrog_audio *audio, unsigned long *frames)
{
   if (!audio || audio->fd == -1 || !frames)
      return -EINVAL;
   *frames = 0;
   return 0;
}

int unifrog_audio_set_volume(struct unifrog_audio *audio, unsigned volume)
{
   if (!audio || audio->fd == -1)
      return -EINVAL;
   (void)volume;
   return 0;
}

int unifrog_audio_set_mute(struct unifrog_audio *audio, int mute)
{
   if (!audio || audio->fd == -1)
      return -EINVAL;
   audio->muted = mute ? 1 : 0;
   return 0;
}

int unifrog_audio_set_output_enabled(struct unifrog_audio *audio, int enabled)
{
   if (!audio || audio->fd == -1)
      return -EINVAL;
   audio->output_gate_enabled = enabled ? 1 : 0;
   return 0;
}

int unifrog_audio_set_system_volume(unsigned volume)
{
   linux_system_volume = volume > 100u ? 100u : volume;
   return 0;
}

int unifrog_audio_set_system_mute(int mute)
{
   linux_system_muted = mute ? 1 : 0;
   return 0;
}

void unifrog_audio_prepare_output_route(void)
{
}

void unifrog_audio_set_system_output_enabled(int enabled)
{
   linux_system_output_enabled = enabled ? 1 : 0;
}

void unifrog_audio_set_output_gate_enabled(int enabled)
{
   linux_output_gate_enabled = enabled ? 1 : 0;
}

void unifrog_audio_restore_output_gate(void)
{
   linux_output_gate_enabled = 1;
}

int unifrog_audio_prefers_stereo_output(void)
{
   return 1;
}

unsigned unifrog_audio_output_channels(void)
{
   return 2;
}

int unifrog_audio_run_gb300_route_probe(const char *tag)
{
   (void)tag;
   return 0;
}

void unifrog_audio_run_gb300_route_probe_once(const char *tag)
{
   (void)tag;
}

void unifrog_audio_debug_gate(uint32_t *l_dir, uint32_t *l_out,
   uint32_t *r_dir, uint32_t *r_out)
{
   if (l_dir)
      *l_dir = 0;
   if (l_out)
      *l_out = (uint32_t)(linux_system_output_enabled &&
         !linux_system_muted && linux_system_volume > 0);
   if (r_dir)
      *r_dir = 0;
   if (r_out)
      *r_out = (uint32_t)linux_output_gate_enabled;
}

void unifrog_audio_debug_dump(struct unifrog_audio *audio, const char *tag)
{
   (void)audio;
   (void)tag;
}
