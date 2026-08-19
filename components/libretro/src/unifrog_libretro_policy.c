#include <unifrog/libretro_policy.h>

#include <unifrog/ge.h>
#include <unifrog/libretro_host.h>

#include <string.h>
#include <strings.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const int cpu_mhz_values[] = {
   0, 198, 297, 396, 594, 702, 756, 808, 810, 864, 918,
};

struct system_core {
   const char *system;
   const char *core;
};

/*
 * Common SD-card folder names. Configuration entries take precedence over
 * this table; these defaults keep existing cards useful after new systems are
 * installed and avoid treating every .zip file as an arcade ROM.
 */
static const struct system_core system_cores[] = {
   { "ARCADE", "fbalpha2012" },
   { "NEOGEO", "fbalpha2012" },
   { "ATARI", "stella2014" },
   { "ATARI2600", "stella2014" },
   { "A2600", "stella2014" },
   { "ATARI5200", "a5200" },
   { "A5200", "a5200" },
   { "ATARI7800", "prosystem" },
   { "A7800", "prosystem" },
   { "GB", "gambatte" },
   { "GBC", "gambatte" },
   { "GBA", "gpsp" },
   { "FC", "quicknes" },
   { "NES", "quicknes" },
   { "SFC", "snes9x2005" },
   { "SNES", "snes9x2005" },
   { "GG", "picodrive" },
   { "GAMEGEAR", "picodrive" },
   { "SMS", "picodrive" },
   { "MD", "picodrive" },
   { "GENESIS", "picodrive" },
   { "MEGADRIVE", "picodrive" },
   { "PCE", "pce-fast" },
   { "TG16", "pce-fast" },
   { "TURBOGRAFX-16", "pce-fast" },
   { "PS", "qpsx" },
   { "PS1", "qpsx" },
   { "PSX", "qpsx" },
   { "LYNX", "handy" },
   { "NGP", "race" },
   { "NGPC", "race" },
   { "WS", "beetle-cygne" },
   { "WSC", "beetle-cygne" },
   { "COLECO", "gearcoleco" },
   { "COLECOVISION", "gearcoleco" },
   { "MSX", "bluemsx-prosty" },
   { "C64", "frodo-prosty" },
   { "PICO8", "fake08-prosty" },
};

static size_t cycle_index(const int *values, size_t count, int current,
   int delta, size_t fallback)
{
   size_t index = fallback < count ? fallback : 0;

   for (size_t i = 0; i < count; i++) {
      if (values[i] == current) {
         index = i;
         break;
      }
   }
   if (delta < 0)
      return index == 0 ? count - 1u : index - 1u;
   return (index + 1u) % count;
}

unsigned unifrog_libretro_policy_fast_forward(unsigned current, int delta)
{
   static const int values[] = { 0, 2, 4, 8, 16 };
   size_t index = cycle_index(values, ARRAY_SIZE(values), (int)current,
      delta, 0);

   return (unsigned)values[index];
}

unsigned unifrog_libretro_policy_audio_gain(unsigned configured)
{
   return configured <= 4u ? configured : 1u;
}

size_t unifrog_libretro_policy_cpu_count(int include_default)
{
   return ARRAY_SIZE(cpu_mhz_values) - (include_default ? 0u : 1u);
}

unsigned unifrog_libretro_policy_cpu_at(size_t index, int include_default)
{
   size_t offset = include_default ? 0u : 1u;
   size_t count = unifrog_libretro_policy_cpu_count(include_default);

   return index < count ? (unsigned)cpu_mhz_values[index + offset] : 0u;
}

int unifrog_libretro_policy_cpu_valid(unsigned mhz)
{
   for (size_t i = 0; i < ARRAY_SIZE(cpu_mhz_values); i++) {
      if ((unsigned)cpu_mhz_values[i] == mhz)
         return 1;
   }
   return 0;
}

unsigned unifrog_libretro_policy_cpu(unsigned current, int delta,
   int include_default)
{
   size_t offset = include_default ? 0u : 1u;
   size_t count = unifrog_libretro_policy_cpu_count(include_default);

   return (unsigned)cpu_mhz_values[offset +
      cycle_index(cpu_mhz_values + offset, count, (int)current, delta, 0)];
}

int unifrog_libretro_policy_frameskip(int current, int delta)
{
   static const int values[] = {
      UNIFROG_LIBRETRO_FRAMESKIP_OFF,
      UNIFROG_LIBRETRO_FRAMESKIP_AUTO,
      UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1,
      UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2,
   };

   return values[cycle_index(values, ARRAY_SIZE(values), current, delta, 0)];
}

int unifrog_libretro_policy_display(int current, int delta)
{
   static const int values[] = {
      UNIFROG_LIBRETRO_DISPLAY_FIT,
      UNIFROG_LIBRETRO_DISPLAY_STRETCH,
      UNIFROG_LIBRETRO_DISPLAY_ORIGINAL,
   };

   return values[cycle_index(values, ARRAY_SIZE(values), current, delta, 0)];
}

int unifrog_libretro_policy_input_profile(int current, int delta)
{
   static const int values[] = {
      UNIFROG_LIBRETRO_INPUT_DEFAULT,
      UNIFROG_LIBRETRO_INPUT_RETROARCH,
      UNIFROG_LIBRETRO_INPUT_GENESIS,
      UNIFROG_LIBRETRO_INPUT_SWAP_AB,
      UNIFROG_LIBRETRO_INPUT_SWAP_XY,
   };

   return values[cycle_index(values, ARRAY_SIZE(values), current, delta, 0)];
}

int unifrog_libretro_policy_ge_clock(int current, int delta)
{
   static const int values[] = {
      UNIFROG_GE_CLOCK_148MHZ,
      UNIFROG_GE_CLOCK_198MHZ,
      UNIFROG_GE_CLOCK_225MHZ,
      UNIFROG_GE_CLOCK_238MHZ,
   };

   return values[cycle_index(values, ARRAY_SIZE(values), current, delta, 1)];
}

unsigned unifrog_libretro_policy_level(const unsigned *levels, size_t count,
   unsigned current, int delta)
{
   if (!levels || count == 0)
      return current;
   if (delta < 0) {
      for (size_t i = count; i > 0; i--) {
         if (levels[i - 1u] < current)
            return levels[i - 1u];
      }
      return levels[count - 1u];
   }
   for (size_t i = 0; i < count; i++) {
      if (levels[i] > current)
         return levels[i];
   }
   return levels[0];
}

const char *unifrog_libretro_policy_frameskip_label(int frameskip)
{
   switch (frameskip) {
   case UNIFROG_LIBRETRO_FRAMESKIP_AUTO:
      return "Auto";
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1:
      return "1";
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2:
      return "2";
   default:
      return "Off";
   }
}

const char *unifrog_libretro_policy_ge_label(int ge_clock)
{
   switch (ge_clock) {
   case UNIFROG_GE_CLOCK_148MHZ:
      return "148";
   case UNIFROG_GE_CLOCK_225MHZ:
      return "225";
   case UNIFROG_GE_CLOCK_238MHZ:
      return "238";
   default:
      return "198";
   }
}

int unifrog_libretro_policy_native_archive(int need_fullpath,
   const char *valid_extensions, const char *path)
{
   const char *name;
   const char *ext;
   const char *cursor;
   size_t ext_len;

   if (!need_fullpath || !valid_extensions || !path)
      return 0;
   name = strrchr(path, '/');
   name = name ? name + 1 : path;
   ext = strrchr(name, '.');
   if (!ext || !ext[1])
      return 0;
   ext++;
   if (strcasecmp(ext, "zip") != 0 && strcasecmp(ext, "7z") != 0)
      return 0;
   ext_len = strlen(ext);
   cursor = valid_extensions;
   while (*cursor) {
      const char *begin = cursor;
      size_t len;

      while (*cursor && *cursor != '|')
         cursor++;
      while (*begin == '.')
         begin++;
      len = (size_t)(cursor - begin);
      if (len == ext_len && strncasecmp(begin, ext, len) == 0)
         return 1;
      if (*cursor == '|')
         cursor++;
   }
   return 0;
}

const char *unifrog_libretro_policy_system_core(const char *system)
{
   if (!system || !system[0])
      return NULL;
   for (size_t i = 0; i < ARRAY_SIZE(system_cores); i++) {
      if (strcasecmp(system, system_cores[i].system) == 0)
         return system_cores[i].core;
   }
   return NULL;
}
