#ifndef UNIFROG_AV_H
#define UNIFROG_AV_H

#ifdef __cplusplus
extern "C" {
#endif

enum unifrog_av_mode {
   UNIFROG_AV_OFF = 0,
   UNIFROG_AV_NTSC = 1,
   UNIFROG_AV_PAL = 2,
};

int unifrog_av_get_mode(int *mode);
int unifrog_av_set_mode(int mode);

#ifdef __cplusplus
}
#endif

#endif
