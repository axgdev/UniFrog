#include <unifrog/media.h>

#include "unifrog_media_tuning_defaults.h"

static const struct unifrog_media_tuning media_default_tuning =
   UNIFROG_MEDIA_TUNING_DEFAULT_INITIALIZER;
struct unifrog_media_tuning media_runtime_tuning =
   UNIFROG_MEDIA_TUNING_DEFAULT_INITIALIZER;

void unifrog_media_tuning_defaults(struct unifrog_media_tuning *tuning)
{
   if (tuning)
      *tuning = media_default_tuning;
}

void unifrog_media_set_tuning(const struct unifrog_media_tuning *tuning)
{
   media_runtime_tuning = tuning ? *tuning : media_default_tuning;
}
