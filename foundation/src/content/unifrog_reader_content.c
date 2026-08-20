#include <unifrog/reader.h>

#include <stddef.h>
#include <string.h>
#include <strings.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int path_has_suffix(const char *path, const char *suffix)
{
   size_t path_len;
   size_t suffix_len;

   if (!path || !suffix)
      return 0;
   path_len = strlen(path);
   suffix_len = strlen(suffix);
   return path_len >= suffix_len &&
      strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

static int path_has_any_suffix(const char *path,
   const char *const *suffixes, unsigned count)
{
   for (unsigned i = 0; i < count; i++) {
      if (path_has_suffix(path, suffixes[i]))
         return 1;
   }
   return 0;
}

int unifrog_reader_path_is_image(const char *path)
{
   static const char *const suffixes[] = {
      ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".tif", ".tiff",
      ".tga", ".svg",
   };

   return path_has_any_suffix(path, suffixes, ARRAY_SIZE(suffixes));
}

int unifrog_reader_path_is_text(const char *path)
{
   static const char *const suffixes[] = {
      ".txt", ".md", ".html", ".htm", ".xhtml",
   };

   return path_has_any_suffix(path, suffixes, ARRAY_SIZE(suffixes));
}

int unifrog_reader_path_is_archive(const char *path)
{
   return path_has_suffix(path, ".cbz") || path_has_suffix(path, ".epub");
}

int unifrog_reader_path_supported(const char *path)
{
   return unifrog_reader_path_is_image(path) ||
      unifrog_reader_path_is_text(path) ||
      unifrog_reader_path_is_archive(path);
}
