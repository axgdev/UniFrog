#ifndef UNIFROG_LINUX_HOST_H
#define UNIFROG_LINUX_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void unifrog_linux_input_set_buttons(uint32_t buttons);
void unifrog_linux_set_stop_requested(int requested);
int unifrog_linux_stop_requested(void);
int unifrog_linux_display_copy_rgb565(uint16_t *pixels, unsigned width,
   unsigned height);
int unifrog_linux_frontend_run_script(const char *script);
int unifrog_linux_frontend_render_ppm(const char *path, const char *script);

#ifdef __cplusplus
}
#endif

#endif
