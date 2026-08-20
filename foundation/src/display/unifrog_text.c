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

static size_t utf8_char_bytes(const char *text)
{
   const unsigned char *p = (const unsigned char *)text;
   size_t count;

   if (!p || !p[0])
      return 0;
   if (p[0] < 0x80u)
      return 1;
   if ((p[0] & 0xe0u) == 0xc0u)
      count = 2;
   else if ((p[0] & 0xf0u) == 0xe0u)
      count = 3;
   else if ((p[0] & 0xf8u) == 0xf0u)
      count = 4;
   else
      return 1;
   for (size_t i = 1; i < count; i++) {
      if ((p[i] & 0xc0u) != 0x80u)
         return 1;
   }
   return count;
}

size_t unifrog_text_utf8_length(const char *text)
{
   size_t count = 0;

   if (!text)
      return 0;
   while (*text) {
      text += utf8_char_bytes(text);
      count++;
   }
   return count;
}

size_t unifrog_text_utf8_copy_chars(char *dst, size_t dst_size,
   const char *src, size_t max_chars)
{
   size_t used = 0;
   size_t chars = 0;

   if (!dst || dst_size == 0)
      return 0;
   if (!src)
      src = "";
   while (*src && chars < max_chars) {
      size_t bytes = utf8_char_bytes(src);

      if (used + bytes >= dst_size)
         break;
      memcpy(dst + used, src, bytes);
      used += bytes;
      src += bytes;
      chars++;
   }
   dst[used] = '\0';
   return chars;
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

int unifrog_text_marquee(char *dst, size_t dst_size, const char *text,
   size_t max_chars, uint32_t elapsed_ms)
{
   static const char gap[] = "   ";
   const uint32_t hold_ms = 900u;
   const uint32_t step_ms = 180u;
   size_t len;
   size_t cycle_chars;
   size_t offset;

   if (!dst || dst_size == 0)
      return 0;
   if (!text)
      text = "";
   if (max_chars >= dst_size)
      max_chars = dst_size - 1u;
   len = unifrog_text_utf8_length(text);
   if (len <= max_chars || max_chars == 0) {
      unifrog_text_copy(dst, dst_size, max_chars ? text : "");
      return 0;
   }

   /*
    * Hold the start long enough to read it, then scroll through a short gap
    * before wrapping. Copy complete UTF-8 sequences so scrolling cannot split
    * a translated glyph into invalid bytes.
    */
   cycle_chars = len + sizeof(gap) - 1u;
   offset = elapsed_ms < hold_ms ? 0u :
      ((elapsed_ms - hold_ms) / step_ms) % cycle_chars;
   dst[0] = '\0';
   for (size_t i = 0; i < max_chars; i++) {
      size_t pos = (offset + i) % cycle_chars;
      size_t used = strlen(dst);

      if (pos < len) {
         const char *source = text;

         for (size_t n = 0; n < pos; n++)
            source += utf8_char_bytes(source);
         if (used + utf8_char_bytes(source) >= dst_size)
            break;
         memcpy(dst + used, source, utf8_char_bytes(source));
         dst[used + utf8_char_bytes(source)] = '\0';
      } else if (used + 1u < dst_size) {
         dst[used] = gap[pos - len];
         dst[used + 1u] = '\0';
      }
   }
   return 1;
}
