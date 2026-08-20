#include <unifrog/boot.h>

#include <string.h>

#include <unifrog/text.h>

int unifrog_boot_asd_path_supported(const char *path)
{
   size_t len;
   int component_start = 1;

   if (!path || path[0] == '\0' || path[0] == '/' || path[0] == '.')
      return 0;
   if (!unifrog_text_ends_with_ci(path, ".asd"))
      return 0;
   len = strlen(path);
   if (len >= UNIFROG_BOOT_PATH_MAX)
      return 0;

   for (size_t i = 0; i < len; i++) {
      char c = path[i];

      if (c == '\\' || c == ':' || c == '\r' || c == '\n' ||
          c == '\t' || c == ' ')
         return 0;
      if (c == '/') {
         if (component_start)
            return 0;
         component_start = 1;
         continue;
      }
      if (component_start && c == '.')
         return 0;
      component_start = 0;
   }
   return !component_start;
}
