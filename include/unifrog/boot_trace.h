#ifndef UNIFROG_BOOT_TRACE_H
#define UNIFROG_BOOT_TRACE_H

#include <fastboot/handoff.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t unifrog_boot_trace_r05_state(void);
void unifrog_boot_trace_mark(uint32_t event, uint32_t arg0,
   uint32_t arg1, uint32_t arg2);
void unifrog_boot_trace_log(const char *tag);

#ifdef __cplusplus
}
#endif

#endif
