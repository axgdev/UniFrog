#include <unifrog/backlight.h>

#include <stdbool.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>

#include <hcuapi/gpio.h>
#include <hcuapi/pinmux.h>
#include <hcuapi/pinpad.h>
#include <hcuapi/pwm.h>

#include <unifrog/log.h>

#define BACKLIGHT_PWM_DEV "/dev/pwm2"
#define BACKLIGHT_PWM_FREQ_HZ 10000u
#define BACKLIGHT_PWM_PERIOD_NS (1000000000u / BACKLIGHT_PWM_FREQ_HZ)

static unsigned cached_level = 100;
static int cached_level_valid;

int unifrog_backlight_get(unsigned *level)
{
   unsigned char value = 0;
   int fd;
   int ret;

   if (!level)
      return -1;

   if (cached_level_valid) {
      *level = cached_level;
      return 0;
   }

   fd = open("/dev/backlight", O_RDONLY);
   if (fd < 0)
      return -1;

   ret = read(fd, &value, sizeof(value));
   close(fd);
   if (ret != (int)sizeof(value))
      return -1;

   *level = value > 100 ? 100 : value;
   cached_level = *level;
   cached_level_valid = 1;
   return 0;
}

static int set_direct_pwm(unsigned level)
{
   struct pwm_info_s info;
   int fd;
   int ret_set;
   int ret_start;

   if (level > 100)
      level = 100;

   if (level == 0) {
      pinmux_configure(PINPAD_R05, PINMUX_R05_GPIO);
      gpio_configure(PINPAD_R05, GPIO_DIR_OUTPUT);
      gpio_set_output(PINPAD_R05, true);
      cached_level = 0;
      cached_level_valid = 1;
      unifrog_log("unifrog backlight direct level=0 gpio_off=1\n");
      return 0;
   }

   memset(&info, 0, sizeof(info));
   info.period_ns = BACKLIGHT_PWM_PERIOD_NS;
   info.duty_ns = (BACKLIGHT_PWM_PERIOD_NS * level) / 100u;
   info.polarity = true;

   pinmux_configure(PINPAD_R05, PINMUX_R05_PWM_2);
   fd = open(BACKLIGHT_PWM_DEV, O_RDWR);
   if (fd < 0) {
      unifrog_log("unifrog backlight direct open_fail dev=%s level=%u\n",
         BACKLIGHT_PWM_DEV, level);
      return -1;
   }

   ret_set = ioctl(fd, PWMIOC_SETCHARACTERISTICS, &info);
   ret_start = ret_set == 0 ? ioctl(fd, PWMIOC_START, 0) : ret_set;
   close(fd);

   unifrog_log("unifrog backlight direct level=%u duty_ns=%lu period_ns=%lu polarity=%d ret_set=%d ret_start=%d\n",
      level, (unsigned long)info.duty_ns, (unsigned long)info.period_ns,
      info.polarity ? 1 : 0, ret_set, ret_start);
   if (ret_set != 0 || ret_start != 0)
      return -1;

   cached_level = level;
   cached_level_valid = 1;
   return 0;
}

int unifrog_backlight_set(unsigned level)
{
   int value;
   int fd;
   int ret;

   if (level > 100)
      level = 100;

   if (cached_level_valid && cached_level == level) {
      unifrog_log("unifrog backlight direct level=%u cached=1\n", level);
      return 0;
   }

   if (set_direct_pwm(level) == 0)
      return 0;

   value = (int)level;
   fd = open("/dev/backlight", O_RDWR);
   if (fd < 0)
      return -1;

   ret = write(fd, &value, sizeof(value));
   close(fd);
   if (ret != (int)sizeof(value))
      return -1;

   cached_level = level;
   cached_level_valid = 1;
   return 0;
}
