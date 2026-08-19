#ifndef UNIFROG_LIBRETRO_POLICY_H
#define UNIFROG_LIBRETRO_POLICY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned unifrog_libretro_policy_fast_forward(unsigned current, int delta);
unsigned unifrog_libretro_policy_audio_gain(unsigned configured);
size_t unifrog_libretro_policy_cpu_count(int include_default);
unsigned unifrog_libretro_policy_cpu_at(size_t index, int include_default);
int unifrog_libretro_policy_cpu_valid(unsigned mhz);
unsigned unifrog_libretro_policy_cpu(unsigned current, int delta,
   int include_default);
int unifrog_libretro_policy_frameskip(int current, int delta);
int unifrog_libretro_policy_display(int current, int delta);
int unifrog_libretro_policy_input_profile(int current, int delta);
int unifrog_libretro_policy_ge_clock(int current, int delta);
unsigned unifrog_libretro_policy_level(const unsigned *levels, size_t count,
   unsigned current, int delta);
const char *unifrog_libretro_policy_frameskip_label(int frameskip);
const char *unifrog_libretro_policy_ge_label(int ge_clock);
int unifrog_libretro_policy_native_archive(int need_fullpath,
   const char *valid_extensions, const char *path);
const char *unifrog_libretro_policy_system_core(const char *system);

#ifdef __cplusplus
}
#endif

#endif
