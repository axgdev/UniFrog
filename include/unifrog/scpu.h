#ifndef UNIFROG_SCPU_H
#define UNIFROG_SCPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct unifrog_scpu_clock {
   int valid;
   uint32_t reg074;
   uint32_t reg07c;
   uint32_t reg380;
   unsigned selector;
   unsigned pll_enabled;
   unsigned mhz;
};

int unifrog_scpu_supported(void);
unsigned unifrog_scpu_current_mhz(void);
int unifrog_scpu_capture(struct unifrog_scpu_clock *clock);
int unifrog_scpu_apply_mhz(unsigned mhz);
int unifrog_scpu_restore(const struct unifrog_scpu_clock *clock);

#ifdef __cplusplus
}
#endif

#endif
