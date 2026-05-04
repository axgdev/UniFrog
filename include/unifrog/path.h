#ifndef UNIFROG_PATH_H
#define UNIFROG_PATH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void unifrog_path_join(char *dst, size_t dst_size,
   const char *base, const char *name);

#ifdef __cplusplus
}
#endif

#endif
