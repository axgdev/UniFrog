#include <unifrog/battery.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>

#include <kernel/io.h>

#include <unifrog/log.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define ADCCTRL_BASE 0xb8818400u
#define ADC_CTRL0 (ADCCTRL_BASE + 0x0)
#define ADC_CTRL1 (ADCCTRL_BASE + 0x4)
#define ADC_CTRL2 (ADCCTRL_BASE + 0x8)
#define ADC_CTRL3 (ADCCTRL_BASE + 0xc)

static const unsigned battery_thresholds[] = { 183, 186, 190, 194, 200 };
static int battery_direct_initialized;
static int queryadc_open_failed_logged;

static void copy_source(char *dst, size_t dst_size, const char *src)
{
   if (dst_size == 0)
      return;
   if (!src)
      src = "?";
   snprintf(dst, dst_size, "%s", src);
}

void unifrog_battery_status_init(struct unifrog_battery_status *status)
{
   if (!status)
      return;

   memset(status, 0, sizeof(*status));
   status->available = -1;
   copy_source(status->source, sizeof(status->source), "?");
}

unsigned unifrog_battery_bars_for_raw(unsigned raw)
{
   unsigned bars = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(battery_thresholds); i++) {
      if (raw >= battery_thresholds[i])
         bars = i + 1;
   }

   return bars;
}

static void battery_direct_adc_init(void)
{
   if (battery_direct_initialized)
      return;

   REG32_WRITE(ADC_CTRL3, 0x00);
   REG32_SET_FIELD2(ADC_CTRL3, 16, 2, 1);
   REG32_SET_FIELD2(ADC_CTRL2, 0, 8, 0xff);
   REG32_SET_FIELD2(ADC_CTRL2, 8, 8, 0x00);
   REG32_SET_FIELD2(ADC_CTRL1, 24, 8, 0x01);
   REG32_SET_FIELD2(ADC_CTRL1, 8, 8, 0x0f);
   REG32_SET_FIELD2(ADC_CTRL1, 0, 1, 1);
   REG32_SET_FIELD2(ADC_CTRL1, 8, 8, 0xff);
   REG32_SET_FIELD2(ADC_CTRL1, 0, 1, 1);
   usleep(1000);

   battery_direct_initialized = 1;
   unifrog_log("unifrog battery direct_adc init ctrl0=0x%08lx ctrl1=0x%08lx ctrl2=0x%08lx ctrl3=0x%08lx\n",
      (unsigned long)REG32_READ(ADC_CTRL0),
      (unsigned long)REG32_READ(ADC_CTRL1),
      (unsigned long)REG32_READ(ADC_CTRL2),
      (unsigned long)REG32_READ(ADC_CTRL3));
}

int unifrog_battery_read_raw(unsigned char *raw, const char **source, int force_log)
{
   int fd;
   int ret;

   if (!raw)
      return -1;

   if (source)
      *source = "?";

   fd = open("/dev/queryadc0", O_RDONLY);
   if (fd >= 0) {
      ret = read(fd, raw, sizeof(*raw));
      close(fd);
      if (ret == (int)sizeof(*raw)) {
         queryadc_open_failed_logged = 0;
         if (source)
            *source = "queryadc0";
         return 0;
      }
      if (force_log)
         unifrog_log("unifrog battery queryadc0=read_fail ret=%d fallback=direct\n", ret);
   } else if (force_log || !queryadc_open_failed_logged) {
      unifrog_log("unifrog battery queryadc0=open_fail fallback=direct\n");
      queryadc_open_failed_logged = 1;
   }

   battery_direct_adc_init();
   *raw = (unsigned char)REG32_GET_FIELD2(ADC_CTRL0, 16, 8);
   if (source)
      *source = "direct";
   return 0;
}

int unifrog_battery_update(struct unifrog_battery_status *status, int force_log)
{
   unsigned char raw = 0;
   const char *source = "?";
   unsigned bars;
   unsigned millivolts;
   int low;
   int changed;

   if (!status)
      return -1;

   if (unifrog_battery_read_raw(&raw, &source, force_log) != 0) {
      changed = status->available != 0;
      status->available = 0;
      status->raw = 0;
      status->millivolts = 0;
      status->bars = 0;
      status->low = 0;
      copy_source(status->source, sizeof(status->source), "?");
      return changed;
   }

   /*
    * Stock firmware thresholds use raw units of battery volts * 50.
    * The patched stock-battery curve is 4.00, 3.88, 3.80, 3.72, 3.66 V.
    */
   millivolts = (unsigned)raw * 20u;
   bars = unifrog_battery_bars_for_raw(raw);
   low = raw < battery_thresholds[0];
   changed = status->available != 1 || status->raw != raw ||
      status->bars != bars || status->low != low ||
      strcmp(status->source, source) != 0;

   status->available = 1;
   status->raw = raw;
   status->millivolts = millivolts;
   status->bars = bars;
   status->low = low;
   copy_source(status->source, sizeof(status->source), source);

   if (changed || force_log) {
      unifrog_log("unifrog battery source=%s raw=%u mv=%u bars=%u low=%d ctrl0=0x%08lx thresholds=%u,%u,%u,%u,%u\n",
         status->source, status->raw, status->millivolts, status->bars,
         status->low, (unsigned long)REG32_READ(ADC_CTRL0),
         battery_thresholds[4], battery_thresholds[3], battery_thresholds[2],
         battery_thresholds[1], battery_thresholds[0]);
   }

   return changed;
}
