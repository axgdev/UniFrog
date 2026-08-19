#include <unifrog/media_content.h>
#include <unifrog/media.h>

#include <stddef.h>
#include <string.h>
#include <strings.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct media_route_name {
   const char *name;
   enum unifrog_media_route route;
};

static const struct media_route_name media_routes[] = {
   { "auto", UNIFROG_MEDIA_ROUTE_AUTO },
   { "native", UNIFROG_MEDIA_ROUTE_NATIVE },
   { "ffmpeg", UNIFROG_MEDIA_ROUTE_FFMPEG },
   { "wav-auddec", UNIFROG_MEDIA_ROUTE_WAV_AUDDEC },
   { "hcplayer", UNIFROG_MEDIA_ROUTE_HCPLAYER },
   { "hcplayer-audio", UNIFROG_MEDIA_ROUTE_HCPLAYER_AUDIO },
   { "hcplayer-muted", UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED },
};

int unifrog_media_route_parse(const char *name,
   enum unifrog_media_route *route)
{
   if (!name || !route)
      return -1;
   if (strcmp(name, "media") == 0) {
      *route = UNIFROG_MEDIA_ROUTE_AUTO;
      return 0;
   }
   for (unsigned i = 0; i < ARRAY_SIZE(media_routes); i++) {
      if (strcmp(name, media_routes[i].name) == 0) {
         *route = media_routes[i].route;
         return 0;
      }
   }
   return -1;
}

const char *unifrog_media_route_name(enum unifrog_media_route route)
{
   for (unsigned i = 0; i < ARRAY_SIZE(media_routes); i++) {
      if (route == media_routes[i].route)
         return media_routes[i].name;
   }
   return "auto";
}

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

int unifrog_media_path_is_audio(const char *path)
{
   static const char *const suffixes[] = {
      ".mp3", ".wav", ".flac", ".ogg", ".opus", ".aac", ".adts",
      ".m4a", ".wma", ".ra",
   };

   return path_has_any_suffix(path, suffixes, ARRAY_SIZE(suffixes));
}

int unifrog_media_path_is_image(const char *path)
{
   static const char *const suffixes[] = {
      ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp",
   };

   return path_has_any_suffix(path, suffixes, ARRAY_SIZE(suffixes));
}

int unifrog_media_path_is_supported(const char *path)
{
   static const char *const video_suffixes[] = {
      ".mp4", ".m4v", ".mov", ".mkv", ".avi", ".flv", ".webm",
      ".wmv", ".ts", ".m2ts", ".mts", ".vob", ".mpg", ".mpeg",
      ".3gp", ".3g2", ".h264", ".264", ".rm", ".rmvb",
   };

   return unifrog_media_path_is_audio(path) ||
      unifrog_media_path_is_image(path) ||
      path_has_any_suffix(path, video_suffixes, ARRAY_SIZE(video_suffixes));
}

int unifrog_media_path_is_wav(const char *path)
{
   return path_has_suffix(path, ".wav");
}

int unifrog_media_path_is_mp3(const char *path)
{
   return path_has_suffix(path, ".mp3");
}

int unifrog_media_path_is_aac(const char *path)
{
   return path_has_suffix(path, ".aac") || path_has_suffix(path, ".adts");
}

int unifrog_media_path_is_flac(const char *path)
{
   return path_has_suffix(path, ".flac");
}

int unifrog_media_path_is_ogg(const char *path)
{
   return path_has_suffix(path, ".ogg") || path_has_suffix(path, ".opus");
}

int unifrog_media_route_available(const char *path,
   enum unifrog_media_route route, int hcplayer_enabled)
{
   int audio;
   int image;

   if (!path || !path[0])
      return 0;
   audio = unifrog_media_path_is_audio(path);
   image = unifrog_media_path_is_image(path);
   switch (route) {
   case UNIFROG_MEDIA_ROUTE_AUTO:
      return 1;
   case UNIFROG_MEDIA_ROUTE_NATIVE:
   case UNIFROG_MEDIA_ROUTE_FFMPEG:
      return !image;
   case UNIFROG_MEDIA_ROUTE_WAV_AUDDEC:
      return unifrog_media_path_is_wav(path);
   case UNIFROG_MEDIA_ROUTE_HCPLAYER:
      return hcplayer_enabled;
   case UNIFROG_MEDIA_ROUTE_HCPLAYER_AUDIO:
      return hcplayer_enabled && !image;
   case UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED:
      return hcplayer_enabled && !audio;
   default:
      return 0;
   }
}
