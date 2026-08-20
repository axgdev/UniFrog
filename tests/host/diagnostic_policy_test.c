#include "test.h"

#include <unifrog/diagnostic_policy.h>

static void test_pattern_and_checksum(void)
{
   unsigned char bytes[4];
   uint32_t checksum;

   unifrog_diagnostic_fill_pattern(bytes, sizeof(bytes), 1);
   TEST_EQ_INT(17, bytes[0]);
   TEST_EQ_INT(50, bytes[1]);
   TEST_EQ_INT(83, bytes[2]);
   TEST_EQ_INT(116, bytes[3]);
   checksum = unifrog_diagnostic_checksum_update(2166136261u, "hello", 5);
   TEST_EQ_INT(0x4f9f2cabu, checksum);
   TEST_EQ_INT(2000, unifrog_diagnostic_kib_per_second(2048, 1));
   TEST_EQ_INT(500, unifrog_diagnostic_kib_per_second(512, 1));
   TEST_EQ_INT(0, unifrog_diagnostic_kib_per_second(2048, 0));
}

static void test_display_pixels(void)
{
   TEST_EQ_INT(0, unifrog_diagnostic_rgb565_pixel(0, 0, 320, 240));
   TEST_EQ_INT(0xff000000u,
      unifrog_diagnostic_xrgb8888_pixel(0, 0, 320, 240));
   TEST_CHECK(unifrog_diagnostic_rgb565_pixel(319, 239, 320, 240) != 0);
   TEST_CHECK(unifrog_diagnostic_xrgb8888_pixel(319, 239, 320, 240) !=
      0xff000000u);
}

int main(void)
{
   test_pattern_and_checksum();
   test_display_pixels();
   return test_finish("diagnostic policy");
}
