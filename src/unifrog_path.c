#include <unifrog/path.h>

#include <string.h>

#include <unifrog/text.h>

void unifrog_path_join(char *dst, size_t dst_size,
   const char *base, const char *name)
{
   size_t len;

   if (!dst || dst_size == 0)
      return;

   unifrog_text_copy(dst, dst_size, base ? base : "");
   len = strlen(dst);
   if (len + 1 < dst_size && len > 0 && dst[len - 1] != '/') {
      dst[len++] = '/';
      dst[len] = 0;
   }
   unifrog_text_copy(dst + len, dst_size - len, name ? name : "");
}
