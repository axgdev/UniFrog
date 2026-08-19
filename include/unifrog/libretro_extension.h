#ifndef UNIFROG_LIBRETRO_EXTENSION_H
#define UNIFROG_LIBRETRO_EXTENSION_H

#include <stdint.h>

/*
 * UniFrog-private libretro environment commands.  Requests are copied by the
 * host before the callback returns, so cores may keep the structure on their
 * stack.  Keep this ABI pointer-free for external MIPS modules.
 */
#define UNIFROG_ENVIRONMENT_LAUNCH_CONTENT 0x15546u
#define UNIFROG_LIBRETRO_LAUNCH_VERSION 1u
#define UNIFROG_LIBRETRO_CORE_ID_MAX 24u
#define UNIFROG_LIBRETRO_CONTENT_PATH_MAX 256u

struct unifrog_libretro_launch_content {
   uint32_t size;
   uint32_t version;
   char core_id[UNIFROG_LIBRETRO_CORE_ID_MAX];
   char path[UNIFROG_LIBRETRO_CONTENT_PATH_MAX];
};

#endif
