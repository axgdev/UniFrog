#ifndef UNIFROG_SURFACE_ALLOC_H
#define UNIFROG_SURFACE_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *unifrog_surface_memalign(size_t alignment, size_t size);
void unifrog_surface_free(void *ptr);
int unifrog_surface_is_mmz(const void *ptr);

#ifdef __cplusplus
}
#endif

#endif
