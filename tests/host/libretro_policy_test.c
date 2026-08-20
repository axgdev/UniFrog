#include "test.h"

#include <unifrog/ge.h>
#include <unifrog/libretro_host.h>
#include <unifrog/libretro_policy.h>

static void test_cycles(void)
{
   TEST_EQ_INT(2, unifrog_libretro_policy_fast_forward(0, 1));
   TEST_EQ_INT(0, unifrog_libretro_policy_fast_forward(16, 1));
   TEST_EQ_INT(16, unifrog_libretro_policy_fast_forward(0, -1));
   TEST_EQ_INT(2, unifrog_libretro_policy_fast_forward(99, 1));

   TEST_EQ_INT(UNIFROG_LIBRETRO_FRAMESKIP_AUTO,
      unifrog_libretro_policy_frameskip(UNIFROG_LIBRETRO_FRAMESKIP_OFF, 1));
   TEST_EQ_INT(UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2,
      unifrog_libretro_policy_frameskip(UNIFROG_LIBRETRO_FRAMESKIP_OFF, -1));

   TEST_EQ_INT(UNIFROG_LIBRETRO_DISPLAY_ORIGINAL,
      unifrog_libretro_policy_display(UNIFROG_LIBRETRO_DISPLAY_FIT, -1));
   TEST_EQ_INT(UNIFROG_LIBRETRO_INPUT_SWAP_XY,
      unifrog_libretro_policy_input_profile(UNIFROG_LIBRETRO_INPUT_DEFAULT,
         -1));
   TEST_EQ_INT(UNIFROG_GE_CLOCK_148MHZ,
      unifrog_libretro_policy_ge_clock(UNIFROG_GE_CLOCK_198MHZ, -1));
}

static void test_levels(void)
{
   static const unsigned levels[] = { 10, 20, 40, 80 };

   TEST_EQ_INT(40, unifrog_libretro_policy_level(levels, 4, 20, 1));
   TEST_EQ_INT(20, unifrog_libretro_policy_level(levels, 4, 40, -1));
   TEST_EQ_INT(10, unifrog_libretro_policy_level(levels, 4, 80, 1));
   TEST_EQ_INT(80, unifrog_libretro_policy_level(levels, 4, 10, -1));
   TEST_EQ_INT(40, unifrog_libretro_policy_level(levels, 4, 35, 1));
   TEST_EQ_INT(20, unifrog_libretro_policy_level(levels, 4, 35, -1));
}

static void test_cpu_policy(void)
{
   TEST_EQ_INT(11, unifrog_libretro_policy_cpu_count(1));
   TEST_EQ_INT(10, unifrog_libretro_policy_cpu_count(0));
   TEST_EQ_INT(0, unifrog_libretro_policy_cpu_at(0, 1));
   TEST_EQ_INT(198, unifrog_libretro_policy_cpu_at(0, 0));
   TEST_EQ_INT(918, unifrog_libretro_policy_cpu_at(9, 0));
   TEST_EQ_INT(0, unifrog_libretro_policy_cpu_at(10, 0));
   TEST_EQ_INT(1, unifrog_libretro_policy_cpu_valid(808));
   TEST_EQ_INT(0, unifrog_libretro_policy_cpu_valid(800));
   TEST_EQ_INT(198, unifrog_libretro_policy_cpu(0, 1, 1));
   TEST_EQ_INT(918, unifrog_libretro_policy_cpu(0, -1, 1));
   TEST_EQ_INT(198, unifrog_libretro_policy_cpu(918, 1, 0));
}

static void test_validation(void)
{
   TEST_EQ_INT(0, unifrog_libretro_policy_audio_gain(0));
   TEST_EQ_INT(4, unifrog_libretro_policy_audio_gain(4));
   TEST_EQ_INT(1, unifrog_libretro_policy_audio_gain(5));
}

static void test_labels(void)
{
   TEST_EQ_STR("Off", unifrog_libretro_policy_frameskip_label(
      UNIFROG_LIBRETRO_FRAMESKIP_OFF));
   TEST_EQ_STR("Auto", unifrog_libretro_policy_frameskip_label(
      UNIFROG_LIBRETRO_FRAMESKIP_AUTO));
   TEST_EQ_STR("225", unifrog_libretro_policy_ge_label(
      UNIFROG_GE_CLOCK_225MHZ));
}

static void test_content_policy(void)
{
   TEST_EQ_INT(1, unifrog_libretro_policy_native_archive(1,
      "zip|ZIP", "/ROMS/ARCADE/1943.zip"));
   TEST_EQ_INT(1, unifrog_libretro_policy_native_archive(1,
      "iso|zip|7z", "/ROMS/NEOGEO/bangbead.ZIP"));
   TEST_EQ_INT(0, unifrog_libretro_policy_native_archive(0,
      "zip", "/ROMS/FC/8 Eyes (USA).zip"));
   TEST_EQ_INT(0, unifrog_libretro_policy_native_archive(1,
      "a26|bin", "/ROMS/ATARI/Adventure (USA).zip"));
   TEST_EQ_INT(0, unifrog_libretro_policy_native_archive(1,
      "zip", "/ROMS/ARCADE/1943"));
}

static void test_system_folders(void)
{
   TEST_EQ_STR("stella2014",
      unifrog_libretro_policy_system_core("ATARI"));
   TEST_EQ_STR("fbalpha2012",
      unifrog_libretro_policy_system_core("neogeo"));
   TEST_EQ_STR("picodrive",
      unifrog_libretro_policy_system_core("GG"));
   TEST_EQ_STR("pce-fast",
      unifrog_libretro_policy_system_core("TurboGrafx-16"));
   TEST_CHECK(unifrog_libretro_policy_system_core("UNKNOWN") == NULL);
}

int main(void)
{
   test_cycles();
   test_levels();
   test_cpu_policy();
   test_validation();
   test_labels();
   test_content_policy();
   test_system_folders();
   return test_finish("libretro policy");
}
