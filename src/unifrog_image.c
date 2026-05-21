#include <unifrog/image.h>

#include <string.h>

#include <unifrog/log.h>
#include <unifrog/perf.h>

static int has_ext(const char *path, const char *ext)
{
   size_t path_len;
   size_t ext_len;

   if (!path || !ext)
      return 0;
   path_len = strlen(path);
   ext_len = strlen(ext);
   if (path_len < ext_len)
      return 0;
   path += path_len - ext_len;
   for (size_t i = 0; i < ext_len; i++) {
      char a = path[i];
      char b = ext[i];

      if (a >= 'A' && a <= 'Z')
         a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z')
         b = (char)(b - 'A' + 'a');
      if (a != b)
         return 0;
   }
   return 1;
}

int unifrog_image_load_file(const char *path, unifrog_image *image)
{
   uint32_t start;
   int ret;

   if (!path || !image)
      return -1;
   start = unifrog_perf_time_ms();

   /*
    * This wrapper is intentionally the single frontend image decode entry point.
    * HCRTOS image decode libraries are linked, but the stable public API exposed
    * in this SDK is player-oriented. Keep the fallback small and swap the backend
    * here once a direct decode-to-surface API is confirmed.
    */
   if (has_ext(path, ".png"))
      ret = unifrog_png_load_file(path, image);
   else
      ret = -1;

   unifrog_log("unifrog image load backend=%s ret=%d ms=%u path=%s\n",
      ret == 0 ? "png_cpu_mmz" : "unsupported",
      ret, (unsigned)(unifrog_perf_time_ms() - start), path);
   return ret;
}

void unifrog_image_free(unifrog_image *image)
{
   unifrog_png_free(image);
}
