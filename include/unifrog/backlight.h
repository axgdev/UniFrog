#ifndef UNIFROG_BACKLIGHT_H
#define UNIFROG_BACKLIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

int unifrog_backlight_get(unsigned *level);
int unifrog_backlight_set(unsigned level);

#ifdef __cplusplus
}
#endif

#endif
