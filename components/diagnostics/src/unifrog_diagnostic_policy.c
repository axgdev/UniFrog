#include <unifrog/diagnostic_policy.h>

#include <limits.h>

static void diagnostic_rgb(unsigned x, unsigned y, unsigned width,
   unsigned height, unsigned *red, unsigned *green, unsigned *blue)
{
   *red = (x * 255u) / (width ? width : 1u);
   *green = (y * 255u) / (height ? height : 1u);
   *blue = ((x ^ y) * 255u) /
      ((width + height) ? (width + height) : 1u);
}

void unifrog_diagnostic_fill_pattern(void *buffer, size_t size,
   unsigned phase)
{
   unsigned char *bytes = buffer;

   if (!bytes)
      return;
   for (size_t i = 0; i < size; i++)
      bytes[i] = (unsigned char)((i * 33u + phase * 17u) & 0xffu);
}

uint32_t unifrog_diagnostic_checksum_update(uint32_t checksum,
   const void *data, size_t size)
{
   const unsigned char *bytes = data;

   if (!bytes)
      return checksum;
   for (size_t i = 0; i < size; i++) {
      checksum ^= bytes[i];
      checksum *= 16777619u;
   }
   return checksum;
}

unsigned long unifrog_diagnostic_kib_per_second(unsigned long bytes,
   unsigned long milliseconds)
{
   uint64_t rate;

   if (!milliseconds)
      return 0;
   rate = ((uint64_t)bytes * 1000ull) /
      ((uint64_t)milliseconds * 1024ull);
   return rate > (uint64_t)ULONG_MAX ? ULONG_MAX : (unsigned long)rate;
}

uint16_t unifrog_diagnostic_rgb565_pixel(unsigned x, unsigned y,
   unsigned width, unsigned height)
{
   unsigned red;
   unsigned green;
   unsigned blue;

   diagnostic_rgb(x, y, width, height, &red, &green, &blue);
   return (uint16_t)(((red & 0xf8u) << 8) |
      ((green & 0xfcu) << 3) | (blue >> 3));
}

uint32_t unifrog_diagnostic_xrgb8888_pixel(unsigned x, unsigned y,
   unsigned width, unsigned height)
{
   unsigned red;
   unsigned green;
   unsigned blue;

   diagnostic_rgb(x, y, width, height, &red, &green, &blue);
   return 0xff000000u | (red << 16) | (green << 8) | blue;
}
