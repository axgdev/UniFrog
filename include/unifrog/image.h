#ifndef UNIFROG_IMAGE_H
#define UNIFROG_IMAGE_H

#include <unifrog/png.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct unifrog_png_image unifrog_image;

int unifrog_image_load_file(const char *path, unifrog_image *image);
void unifrog_image_free(unifrog_image *image);

#ifdef __cplusplus
}
#endif

#endif
