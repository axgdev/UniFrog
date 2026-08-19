#ifndef UNIFROG_LIBRETRO_CORE_CALL_H
#define UNIFROG_LIBRETRO_CORE_CALL_H

#include <stdint.h>

#ifndef UNIFROG_LIBRETRO_NATIVE_DLOPEN
#include "abi/unifrog_mips_call.h"
#endif

#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN

#define CORE_CALL0_VOID(core, fn) do { \
   (void)(core); \
   (fn)(); \
} while (0)

#define CORE_CALL0_RET(core, fn) ({ \
   (void)(core); \
   (fn)(); \
})

#define CORE_CALL1_VOID(core, fn, a0) do { \
   (void)(core); \
   (fn)(a0); \
} while (0)

#define CORE_CALL1_RET(core, fn, a0) ({ \
   (void)(core); \
   (fn)(a0); \
})

#define CORE_CALL2_VOID(core, fn, a0, a1) do { \
   (void)(core); \
   (fn)(a0, a1); \
} while (0)

#define CORE_CALL2_RET(core, fn, a0, a1) ({ \
   (void)(core); \
   (fn)(a0, a1); \
})

#define CORE_CALL3_VOID(core, fn, a0, a1, a2) do { \
   (void)(core); \
   (fn)(a0, a1, a2); \
} while (0)

#else

#ifdef UNIFROG_LIBRETRO_CORE_CALL_RESTORE_HOST_GP
#define CORE_CALL_AFTER() host_force_expected_gp()
#else
#define CORE_CALL_AFTER() ((void)0)
#endif

#define CORE_CALL0_VOID(core, fn) do { \
   (void)unifrog_mips_call0(core_call_gp(core), (uintptr_t)(fn)); \
   CORE_CALL_AFTER(); \
} while (0)

#define CORE_CALL0_RET(core, fn) ({ \
   __typeof__((fn)()) ret__ = (__typeof__((fn)())) \
      unifrog_mips_call0(core_call_gp(core), (uintptr_t)(fn)); \
   CORE_CALL_AFTER(); \
   ret__; \
})

#define CORE_CALL1_VOID(core, fn, a0) do { \
   (void)unifrog_mips_call1(core_call_gp(core), (uintptr_t)(fn), \
      (uintptr_t)(a0)); \
   CORE_CALL_AFTER(); \
} while (0)

#define CORE_CALL1_RET(core, fn, a0) ({ \
   __typeof__((fn)(a0)) ret__ = (__typeof__((fn)(a0))) \
      unifrog_mips_call1(core_call_gp(core), (uintptr_t)(fn), \
         (uintptr_t)(a0)); \
   CORE_CALL_AFTER(); \
   ret__; \
})

#define CORE_CALL2_VOID(core, fn, a0, a1) do { \
   (void)unifrog_mips_call2(core_call_gp(core), (uintptr_t)(fn), \
      (uintptr_t)(a0), (uintptr_t)(a1)); \
   CORE_CALL_AFTER(); \
} while (0)

#define CORE_CALL2_RET(core, fn, a0, a1) ({ \
   __typeof__((fn)(a0, a1)) ret__ = (__typeof__((fn)(a0, a1))) \
      unifrog_mips_call2(core_call_gp(core), (uintptr_t)(fn), \
         (uintptr_t)(a0), (uintptr_t)(a1)); \
   CORE_CALL_AFTER(); \
   ret__; \
})

#define CORE_CALL3_VOID(core, fn, a0, a1, a2) do { \
   (void)unifrog_mips_call3(core_call_gp(core), (uintptr_t)(fn), \
      (uintptr_t)(a0), (uintptr_t)(a1), (uintptr_t)(a2)); \
   CORE_CALL_AFTER(); \
} while (0)

#endif

#endif
