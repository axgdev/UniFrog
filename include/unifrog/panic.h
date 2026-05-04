#ifndef UNIFROG_PANIC_H
#define UNIFROG_PANIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void unifrog_panic_screen(const char *title, uint32_t a, uint32_t b,
   uint32_t c, uint32_t d);
void unifrog_panic_screen_labeled(const char *title, const char *label0,
   uint32_t value0, const char *label1, uint32_t value1,
   const char *label2, uint32_t value2, const char *label3,
   uint32_t value3);
void unifrog_exception_panic(uint32_t cause, uint32_t epc,
   uint32_t badvaddr, uint32_t ra);
void unifrog_panic_trigger_test_exception(void);
void unifrog_panic_trigger_cpu_exception(void);

#ifdef __cplusplus
}
#endif

#endif
