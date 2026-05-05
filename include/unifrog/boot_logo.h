#ifndef UNIFROG_BOOT_LOGO_H
#define UNIFROG_BOOT_LOGO_H

#include <unifrog/fb.h>

#ifdef __cplusplus
extern "C" {
#endif

int unifrog_boot_logo_present(struct unifrog_fb *fb, const char *tag);
int unifrog_boot_logo_present_early(void);
int unifrog_boot_logo_is_active(void);
void unifrog_boot_logo_mark_replaced(void);
void unifrog_boot_logo_release_early(void);

#ifdef __cplusplus
}
#endif

#endif
