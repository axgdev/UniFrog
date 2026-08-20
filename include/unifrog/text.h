#ifndef UNIFROG_TEXT_H
#define UNIFROG_TEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void unifrog_text_copy(char *dst, size_t dst_size, const char *src);
size_t unifrog_text_utf8_length(const char *text);
size_t unifrog_text_utf8_copy_chars(char *dst, size_t dst_size,
   const char *src, size_t max_chars);
int unifrog_text_ends_with_ci(const char *text, const char *suffix);
int unifrog_text_marquee(char *dst, size_t dst_size, const char *text,
   size_t max_chars, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif
