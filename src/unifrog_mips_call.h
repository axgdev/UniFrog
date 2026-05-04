#ifndef UNIFROG_MIPS_CALL_H
#define UNIFROG_MIPS_CALL_H

#include <stdint.h>

uintptr_t unifrog_mips_call0(uintptr_t gp, uintptr_t fn);
uintptr_t unifrog_mips_call1(uintptr_t gp, uintptr_t fn, uintptr_t a0);
uintptr_t unifrog_mips_call2(uintptr_t gp, uintptr_t fn, uintptr_t a0,
   uintptr_t a1);
uintptr_t unifrog_mips_call3(uintptr_t gp, uintptr_t fn, uintptr_t a0,
   uintptr_t a1, uintptr_t a2);

#endif
