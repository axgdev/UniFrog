#ifndef UNIFROG_AUDIO_H
#define UNIFROG_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_AUDIO_DEFAULT_PERIOD_BYTES 512u
#define UNIFROG_AUDIO_DEFAULT_PERIODS 8u
#define UNIFROG_AUDIO_BACKEND_AUTO 0
#define UNIFROG_AUDIO_BACKEND_SND 1
#define UNIFROG_AUDIO_BACKEND_AUDSINK 2

struct unifrog_audio {
   int fd;
   int backend;
   unsigned rate;
   unsigned channels;
   unsigned period_bytes;
   unsigned periods;
   unsigned frame_bytes;
   int muted;
   int output_gate_enabled;
   int output_gate_pending_signal;
};

int unifrog_audio_open(struct unifrog_audio *audio,
   unsigned rate, unsigned channels,
   unsigned period_bytes, unsigned periods);
int unifrog_audio_open_backend(struct unifrog_audio *audio,
   unsigned rate, unsigned channels,
   unsigned period_bytes, unsigned periods, int backend);
void unifrog_audio_close(struct unifrog_audio *audio);
int unifrog_audio_start(struct unifrog_audio *audio);
int unifrog_audio_drop(struct unifrog_audio *audio);
int unifrog_audio_write(struct unifrog_audio *audio,
   const int16_t *samples, unsigned frames);
int unifrog_audio_write_timeout(struct unifrog_audio *audio,
   const int16_t *samples, unsigned frames,
   unsigned attempts, unsigned poll_timeout_ms);
int unifrog_audio_delay(struct unifrog_audio *audio, unsigned long *frames);
int unifrog_audio_set_volume(struct unifrog_audio *audio, unsigned volume);
int unifrog_audio_set_mute(struct unifrog_audio *audio, int mute);
int unifrog_audio_set_output_enabled(struct unifrog_audio *audio, int enabled);
int unifrog_audio_set_system_volume(unsigned volume);
int unifrog_audio_set_system_mute(int mute);
void unifrog_audio_set_system_output_enabled(int enabled);
void unifrog_audio_set_output_gate_enabled(int enabled);
void unifrog_audio_restore_output_gate(void);
int unifrog_audio_prefers_stereo_output(void);
unsigned unifrog_audio_output_channels(void);
int unifrog_audio_run_gb300_route_probe(const char *tag);
void unifrog_audio_run_gb300_route_probe_once(const char *tag);
void unifrog_audio_debug_gate(uint32_t *l_dir, uint32_t *l_out,
   uint32_t *r_dir, uint32_t *r_out);
void unifrog_audio_debug_dump(struct unifrog_audio *audio, const char *tag);

#ifdef __cplusplus
}
#endif

#endif
