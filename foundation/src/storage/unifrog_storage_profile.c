#include <unifrog/storage_profile.h>

#include <string.h>

static const struct unifrog_storage_profile_info profiles[] = {
   { "boot", "boot profile", "build setting", "build setting", "build setting" },
   { "auto", "automatic fallback", "automatic", "normal/fallback", "automatic" },
   { "safe", "1-bit safe 25 MHz", "1-bit", "legacy 25 MHz", "3.3 V" },
   { "wide1", "4-bit 1 MHz", "4-bit", "legacy 1 MHz", "3.3 V" },
   { "wide2", "4-bit 2 MHz", "4-bit", "legacy 2 MHz", "3.3 V" },
   { "wide4", "4-bit 4 MHz", "4-bit", "legacy 4 MHz", "3.3 V" },
   { "wide8", "4-bit 8 MHz", "4-bit", "legacy 8 MHz", "3.3 V" },
   { "wide10", "4-bit 10 MHz", "4-bit", "legacy 10 MHz", "3.3 V" },
   { "wide12", "4-bit 12 MHz", "4-bit", "legacy 12 MHz", "3.3 V" },
   { "wide14", "4-bit 14 MHz", "4-bit", "legacy 14 MHz", "3.3 V" },
   { "wide16", "4-bit 16 MHz", "4-bit", "legacy 16 MHz", "3.3 V" },
   { "wide18", "4-bit 18 MHz", "4-bit", "legacy 18 MHz", "3.3 V" },
   { "wide20", "4-bit 20 MHz", "4-bit", "legacy 20 MHz", "3.3 V" },
   { "wide22", "4-bit 22 MHz", "4-bit", "legacy 22 MHz", "3.3 V" },
   { "wide24", "4-bit 24 MHz", "4-bit", "legacy 24 MHz", "3.3 V" },
   { "wide25", "4-bit 25 MHz", "4-bit", "legacy 25 MHz", "3.3 V" },
   { "wide37", "4-bit HS 37 MHz", "4-bit", "high speed 37 MHz", "3.3 V" },
   { "hs1", "1-bit HS max", "1-bit", "high speed maximum", "3.3 V" },
   { "wide50", "4-bit HS 50 MHz", "4-bit", "high speed 50 MHz", "3.3 V" },
   { "wide", "4-bit HS max", "4-bit", "high speed maximum", "3.3 V" },
   { "uhs12", "UHS SDR12", "4-bit", "UHS SDR12", "1.8 V" },
   { "uhs25", "UHS SDR25", "4-bit", "UHS SDR25", "1.8 V" },
   { "uhs", "UHS max", "4-bit", "UHS maximum", "1.8 V" },
};

const struct unifrog_storage_profile_info *unifrog_storage_profile_info(
   const char *profile)
{
   const char *name = profile && profile[0] ? profile : "boot";

   for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
      if (strcmp(name, profiles[i].name) == 0)
         return &profiles[i];
   }
   return &profiles[0];
}

unsigned unifrog_storage_profile_count(void)
{
   return sizeof(profiles) / sizeof(profiles[0]);
}

const char *unifrog_storage_profile_name(unsigned index)
{
   return index < unifrog_storage_profile_count() ?
      profiles[index].name : NULL;
}
