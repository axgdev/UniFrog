#ifndef UNIFROG_TEXT_H
#define UNIFROG_TEXT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void unifrog_text_copy(char *dst, size_t dst_size, const char *src);
int unifrog_text_ends_with_ci(const char *text, const char *suffix);

#ifdef __cplusplus
}
#endif

#endif
