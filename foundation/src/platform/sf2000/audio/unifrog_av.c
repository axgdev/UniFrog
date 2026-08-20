#include <unifrog/av.h>

#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>

#include <hcuapi/dis.h>
#include <hcuapi/tvtype.h>

#include <unifrog/log.h>

static int cached_mode = UNIFROG_AV_OFF;
static int cached_mode_valid;

static int sanitize_mode(int mode)
{
   if (mode == UNIFROG_AV_NTSC || mode == UNIFROG_AV_PAL)
      return mode;
   return UNIFROG_AV_OFF;
}

int unifrog_av_get_mode(int *mode)
{
   if (!mode)
      return -1;
   *mode = cached_mode_valid ? cached_mode : UNIFROG_AV_OFF;
   return 0;
}

int unifrog_av_set_mode(int mode)
{
   struct dis_dac_param dac;
   int fd;
   int ret = 0;

   mode = sanitize_mode(mode);
   if (cached_mode_valid && cached_mode == mode) {
      unifrog_log("unifrog av mode=%d ret=0 cached=1\n", mode);
      return 0;
   }
   if (!cached_mode_valid && mode == UNIFROG_AV_OFF) {
      cached_mode = mode;
      cached_mode_valid = 1;
      unifrog_log("unifrog av mode=%d ret=0 assume_off=1\n", mode);
      return 0;
   }

   fd = open("/dev/dis", O_RDWR);
   if (fd < 0) {
      unifrog_log("unifrog av mode=%d open_dis=fail\n", mode);
      return -1;
   }

   memset(&dac, 0, sizeof(dac));
   dac.distype = DIS_TYPE_SD;
   dac.info.type = DIS_DAC_CVBS;
   if (ioctl(fd, DIS_UNREGISTER_DAC, &dac) != 0)
      ret = -1;

   if (mode != UNIFROG_AV_OFF) {
      struct dis_tvsys tvsys;

      memset(&tvsys, 0, sizeof(tvsys));
      tvsys.distype = DIS_TYPE_SD;
      tvsys.layer = DIS_LAYER_MAIN;
      tvsys.tvtype = mode == UNIFROG_AV_PAL ? TV_PAL : TV_NTSC;
      tvsys.progressive = false;
      if (ioctl(fd, DIS_SET_TVSYS, &tvsys) != 0)
         ret = -1;

      memset(&dac, 0, sizeof(dac));
      dac.distype = DIS_TYPE_SD;
      dac.info.type = DIS_DAC_CVBS;
      dac.info.dac.enable = 1;
      dac.info.dac.progressive = false;
      dac.info.dac.dacidx.cvbs.cv = DIS_DAC_0;
      if (ioctl(fd, DIS_REGISTER_DAC, &dac) != 0)
         ret = -1;
   }

   close(fd);
   cached_mode = mode;
   cached_mode_valid = 1;
   unifrog_log("unifrog av mode=%d ret=%d\n", mode, ret);
   return ret;
}
