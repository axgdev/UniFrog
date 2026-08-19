#ifndef UNIFROG_ARTWORK_H
#define UNIFROG_ARTWORK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_ARTWORK_TEMPLATE_MAX 512u
#define UNIFROG_ARTWORK_PATH_MAX 256u

struct unifrog_artwork_paths {
   char box[UNIFROG_ARTWORK_PATH_MAX];
   char preview[UNIFROG_ARTWORK_PATH_MAX];
   char text[UNIFROG_ARTWORK_PATH_MAX];
};

int unifrog_artwork_resolve(const char *rom_path, const char *system,
   const char *box_templates, const char *preview_templates,
   const char *text_templates, struct unifrog_artwork_paths *paths);

#ifdef __cplusplus
}
#endif

#endif
