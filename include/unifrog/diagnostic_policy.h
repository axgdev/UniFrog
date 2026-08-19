#ifndef UNIFROG_DIAGNOSTIC_POLICY_H
#define UNIFROG_DIAGNOSTIC_POLICY_H

#include <stddef.h>
#include <stdint.h>

void unifrog_diagnostic_fill_pattern(void *buffer, size_t size,
   unsigned phase);
uint32_t unifrog_diagnostic_checksum_update(uint32_t checksum,
   const void *data, size_t size);
unsigned long unifrog_diagnostic_kib_per_second(unsigned long bytes,
   unsigned long milliseconds);
uint16_t unifrog_diagnostic_rgb565_pixel(unsigned x, unsigned y,
   unsigned width, unsigned height);
uint32_t unifrog_diagnostic_xrgb8888_pixel(unsigned x, unsigned y,
   unsigned width, unsigned height);

#endif
