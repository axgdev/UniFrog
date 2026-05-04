#include <unifrog/text.h>

#include <string.h>

static char ascii_lower(char c)
{
   if (c >= 'A' && c <= 'Z')
      return (char)(c - 'A' + 'a');
   return c;
}

void unifrog_text_copy(char *dst, size_t dst_size, const char *src)
{
   if (!dst || dst_size == 0)
      return;
   if (!src)
      src = "";

   while (dst_size > 1 && *src) {
      *dst++ = *src++;
      dst_size--;
   }
   *dst = 0;
}

int unifrog_text_ends_with_ci(const char *text, const char *suffix)
{
   size_t text_len;
   size_t suffix_len;

   if (!text || !suffix)
      return 0;

   text_len = strlen(text);
   suffix_len = strlen(suffix);
   if (suffix_len > text_len)
      return 0;

   text += text_len - suffix_len;
   while (*suffix) {
      if (ascii_lower(*text++) != ascii_lower(*suffix++))
         return 0;
   }

   return 1;
}
