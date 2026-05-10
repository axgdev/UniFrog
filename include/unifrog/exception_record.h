#ifndef UNIFROG_EXCEPTION_RECORD_H
#define UNIFROG_EXCEPTION_RECORD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void unifrog_exception_record_store(uint32_t cause, uint32_t epc,
   uint32_t badvaddr, uint32_t ra);
void unifrog_exception_record_log_and_clear(const char *tag);

#ifdef __cplusplus
}
#endif

#endif
