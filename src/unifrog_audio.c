#include <unifrog/audio.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/unistd.h>

#include <hcuapi/avsync.h>
#include <hcuapi/audsink.h>
#include <hcuapi/gpio.h>
#include <hcuapi/pinpad.h>
#include <hcuapi/snd.h>
#include <kernel/module.h>

#include <unifrog/log.h>

#define printf unifrog_log

#define LCD_ID_SF2000 0x858552ul
#define LCD_ID_GB300 0x009306ul
#define LCD_ID_DY14 0x009307ul

#define GPIO_L_OUTPUT ((volatile uint32_t *)0xb8800054u)
#define GPIO_L_DIR ((volatile uint32_t *)0xb8800058u)
#define GPIO_R_OUTPUT ((volatile uint32_t *)0xb88000f4u)
#define GPIO_R_DIR ((volatile uint32_t *)0xb88000f8u)
#define GPIO_L15_MASK (1u << 15)
#define GPIO_R07_MASK (1u << 7)
#define SYSTEM_AUDIO_VOLUME 75u

extern unsigned long sf2000_lcd_panel_id(void) __attribute__((weak));
extern unsigned long PINMUXL;
extern unsigned long PINMUXB;
extern unsigned long PINMUXR;
extern unsigned long PINMUXT;
extern unsigned long SND0;
extern unsigned long SND0_DAC;

enum audio_gate {
   AUDIO_GATE_SF2000_R07,
   AUDIO_GATE_GB300_L15,
};

static void clear_audio(struct unifrog_audio *audio)
{
   if (audio) {
      memset(audio, 0, sizeof(*audio));
      audio->fd = -1;
      audio->backend = UNIFROG_AUDIO_BACKEND_AUTO;
   }
}

static enum audio_gate current_audio_gate(void)
{
   unsigned long lcd_id = sf2000_lcd_panel_id ? sf2000_lcd_panel_id() : 0;

   if (lcd_id == LCD_ID_GB300 || lcd_id == LCD_ID_DY14)
      return AUDIO_GATE_GB300_L15;

   (void)LCD_ID_SF2000;
   return AUDIO_GATE_SF2000_R07;
}

static void set_reg_gate(volatile uint32_t *dir, volatile uint32_t *out,
   uint32_t mask, int enabled)
{
   uint32_t value;

   *dir = *dir | mask;
   value = *out;
   if (enabled)
      value &= ~mask;
   else
      value |= mask;
   *out = value;
}

static void set_stock_audio_output_gate(int enabled)
{
   enum audio_gate gate = current_audio_gate();

   if (gate == AUDIO_GATE_GB300_L15) {
      gpio_configure(PINPAD_L15, GPIO_DIR_OUTPUT);
      gpio_set_output(PINPAD_L15, enabled ? false : true);
      set_reg_gate(GPIO_L_DIR, GPIO_L_OUTPUT, GPIO_L15_MASK, enabled);
   } else {
      gpio_configure(PINPAD_R07, GPIO_DIR_OUTPUT);
      gpio_set_output(PINPAD_R07, enabled ? false : true);
      set_reg_gate(GPIO_R_DIR, GPIO_R_OUTPUT, GPIO_R07_MASK, enabled);
   }
}

static int ensure_audio_drivers(void)
{
   static int initialized;
   static int result;
   static char *modules[] = {
      "apll_dai",
      "avsync",
      "cjc8990_dai",
      "cjc8988_dai",
      "cs4344_dai",
      "pwm_dac_dai",
      "i2s_dai",
      "i2si_platform",
      "i2so_platform",
      "spo_dai",
      "spo_platform",
      "spin_platform",
      "hc16xx_link",
      "auddec",
      "audsink",
   };

   if (initialized)
      return result;

   initialized = 1;
   for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
      int ret = module_init(modules[i]);

      printf("unifrog audio lazy_module name=%s ret=%d\n", modules[i], ret);
      if (ret != 0 && result == 0)
         result = ret;
   }
   return result;
}

static int open_system_snd(void)
{
   return open("/dev/sndC0i2so", O_WRONLY);
}

int unifrog_audio_set_system_volume(unsigned volume)
{
   uint8_t value;
   int ret;
   int fd;

   if (volume > 100)
      volume = 100;
   value = (uint8_t)volume;
   fd = open_system_snd();
   if (fd < 0)
      return -1;
   ret = ioctl(fd, SND_IOCTL_SET_VOLUME, &value);
   close(fd);
   printf("unifrog audio system_volume volume=%u ret=%d\n", volume, ret);
   return ret;
}

int unifrog_audio_set_system_mute(int mute)
{
   int value = mute ? 1 : 0;
   int ret;
   int fd;

   fd = open_system_snd();
   if (fd < 0)
      return -1;
   ret = ioctl(fd, SND_IOCTL_SET_MUTE, value);
   close(fd);
   printf("unifrog audio system_mute mute=%d ret=%d\n", value, ret);
   return ret;
}

static int open_audsink(struct unifrog_audio *audio,
   unsigned rate, unsigned channels,
   unsigned period_bytes, unsigned periods)
{
   struct audsink_init_params params;
   int duplicate;
   int fd;

   fd = open("/dev/audsink", O_WRONLY);
   if (fd < 0)
      return -1;

   memset(&params, 0, sizeof(params));
   params.buf_size = period_bytes * periods;
   params.snd_devs = AUDSINK_SND_DEVBIT_I2SO;
   params.sync_type = AVSYNC_TYPE_FREERUN;
   params.audio_flush_thres = 0;
   params.pcm.access = SND_PCM_ACCESS_RW_INTERLEAVED;
   params.pcm.format = SND_PCM_FORMAT_S16_LE;
   params.pcm.sync_mode = AVSYNC_TYPE_FREERUN;
   params.pcm.align = SND_PCM_ALIGN_LEFT;
   params.pcm.rate = rate;
   params.pcm.channels = channels;
   params.pcm.period_size = period_bytes;
   params.pcm.periods = periods;
   params.pcm.bitdepth = 16;
   params.pcm.start_threshold = 0;
   params.pcm.pcm_dest = SND_PCM_DEST_DMA;
   if (ioctl(fd, AUDSINK_IOCTL_INIT, &params) != 0)
      goto fail;

   duplicate = AUDSINK_PCM_DUPLICATE_LEFT;
   ioctl(fd, AUDSINK_IOCTL_SETDUPLICATE, duplicate);

   audio->fd = fd;
   audio->backend = UNIFROG_AUDIO_BACKEND_AUDSINK;
   audio->rate = rate;
   audio->channels = channels;
   audio->period_bytes = period_bytes;
   audio->periods = periods;
   audio->frame_bytes = channels * sizeof(int16_t);
   printf("unifrog audio open backend=audsink fd=%d rate=%u ch=%u period=%u periods=%u\n",
      fd, rate, channels, period_bytes, periods);
   return 0;

fail:
   close(fd);
   return -1;
}

static int open_snd(struct unifrog_audio *audio,
   unsigned rate, unsigned channels,
   unsigned period_bytes, unsigned periods)
{
   struct snd_pcm_params params;
   snd_pcm_uframes_t avail_min;
   int fd;

   fd = open("/dev/sndC0i2so", O_WRONLY);
   if (fd < 0)
      return -1;

   memset(&params, 0, sizeof(params));
   params.access = SND_PCM_ACCESS_RW_INTERLEAVED;
   params.format = SND_PCM_FORMAT_S16_LE;
   params.sync_mode = AVSYNC_TYPE_FREERUN;
   params.align = SND_PCM_ALIGN_LEFT;
   params.rate = rate;
   params.channels = channels;
   params.period_size = period_bytes;
   params.periods = periods;
   params.bitdepth = 16;
   params.start_threshold = 0;
   params.pcm_dest = SND_PCM_DEST_DMA;
   if (ioctl(fd, SND_IOCTL_HW_PARAMS, &params) != 0)
      goto fail;
   avail_min = period_bytes;
   ioctl(fd, SND_IOCTL_AVAIL_MIN, &avail_min);

   audio->fd = fd;
   audio->backend = UNIFROG_AUDIO_BACKEND_SND;
   audio->rate = rate;
   audio->channels = channels;
   audio->period_bytes = period_bytes;
   audio->periods = periods;
   audio->frame_bytes = channels * sizeof(int16_t);
   printf("unifrog audio open backend=snd fd=%d rate=%u ch=%u period=%u periods=%u\n",
      fd, rate, channels, period_bytes, periods);
   return 0;

fail:
   close(fd);
   return -1;
}

int unifrog_audio_open_backend(struct unifrog_audio *audio,
   unsigned rate, unsigned channels,
   unsigned period_bytes, unsigned periods, int backend)
{
   if (!audio || rate == 0 || channels == 0 ||
       channels > UINT32_MAX / sizeof(int16_t))
      return -1;
   clear_audio(audio);
   if (period_bytes == 0)
      period_bytes = UNIFROG_AUDIO_DEFAULT_PERIOD_BYTES;
   if (periods == 0)
      periods = UNIFROG_AUDIO_DEFAULT_PERIODS;
   if (period_bytes & 31u)
      period_bytes = (period_bytes + 31u) & ~31u;

   (void)ensure_audio_drivers();

   if (backend == UNIFROG_AUDIO_BACKEND_AUDSINK)
      return open_audsink(audio, rate, channels, period_bytes, periods);
   if (backend == UNIFROG_AUDIO_BACKEND_SND)
      return open_snd(audio, rate, channels, period_bytes, periods);

   if (open_audsink(audio, rate, channels, period_bytes, periods) == 0)
      return 0;
   return open_snd(audio, rate, channels, period_bytes, periods);
}

int unifrog_audio_open(struct unifrog_audio *audio,
   unsigned rate, unsigned channels,
   unsigned period_bytes, unsigned periods)
{
   return unifrog_audio_open_backend(audio, rate, channels, period_bytes,
      periods, UNIFROG_AUDIO_BACKEND_AUTO);
}

void unifrog_audio_close(struct unifrog_audio *audio)
{
   if (!audio)
      return;
   if (audio->fd >= 0) {
      int mute_ret = -1;
      int drop_ret = -1;
      int free_ret = -1;

      if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK) {
         drop_ret = ioctl(audio->fd, AUDSINK_IOCTL_DROP, 0);
         free_ret = ioctl(audio->fd, AUDSINK_IOCTL_FLUSH, 0);
      } else {
         mute_ret = ioctl(audio->fd, SND_IOCTL_SET_MUTE, 1);
         drop_ret = ioctl(audio->fd, SND_IOCTL_DROP, 0);
         free_ret = ioctl(audio->fd, SND_IOCTL_HW_FREE, 0);
      }

      printf("unifrog audio close backend=%d fd=%d mute_ret=%d drop_ret=%d free_ret=%d\n",
         audio->backend, audio->fd, mute_ret, drop_ret, free_ret);
      close(audio->fd);
      (void)unifrog_audio_set_system_mute(1);
      set_stock_audio_output_gate(0);
   }
   clear_audio(audio);
}

int unifrog_audio_start(struct unifrog_audio *audio)
{
   if (!audio || audio->fd < 0)
      return -1;
   if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK)
      return ioctl(audio->fd, AUDSINK_IOCTL_START, 0);
   return ioctl(audio->fd, SND_IOCTL_START, 0);
}

int unifrog_audio_drop(struct unifrog_audio *audio)
{
   if (!audio || audio->fd < 0)
      return -1;
   if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK)
      return ioctl(audio->fd, AUDSINK_IOCTL_DROP, 0);
   return ioctl(audio->fd, SND_IOCTL_DROP, 0);
}

int unifrog_audio_write_timeout(struct unifrog_audio *audio,
   const int16_t *samples, unsigned frames,
   unsigned attempts, unsigned poll_timeout_ms)
{
   struct pollfd pollfd;
   int ret = -1;
   unsigned tries = 0;

   if (!audio || audio->fd < 0 || !samples || frames == 0)
      return -1;
   if (attempts == 0)
      attempts = 1;

   pollfd.fd = audio->fd;
   pollfd.events = POLLOUT | POLLWRNORM;

   do {
      if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK) {
         struct audsink_xfer xfer;

         memset(&xfer, 0, sizeof(xfer));
         xfer.data = (void *)samples;
         xfer.frames = frames;
         ret = ioctl(audio->fd, AUDSINK_IOCTL_XFER, &xfer);
      } else {
         struct snd_xfer xfer;

         memset(&xfer, 0, sizeof(xfer));
         xfer.data = (void *)samples;
         xfer.frames = frames;
         ret = ioctl(audio->fd, SND_IOCTL_XFER, &xfer);
      }
      if (ret >= 0)
         return 0;
      if (tries + 1 >= attempts)
         break;
      poll(&pollfd, 1, (int)poll_timeout_ms);
   } while (++tries < attempts);

   return ret;
}

int unifrog_audio_write(struct unifrog_audio *audio,
   const int16_t *samples, unsigned frames)
{
   return unifrog_audio_write_timeout(audio, samples, frames, 20, 20);
}

int unifrog_audio_delay(struct unifrog_audio *audio, unsigned long *frames)
{
   snd_pcm_uframes_t delay = 0;
   int ret;

   if (!audio || audio->fd < 0)
      return -1;
   if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK)
      ret = ioctl(audio->fd, AUDSINK_IOCTL_DELAY, &delay);
   else
      ret = ioctl(audio->fd, SND_IOCTL_DELAY, &delay);
   if (frames)
      *frames = delay;
   return ret;
}

int unifrog_audio_set_volume(struct unifrog_audio *audio, unsigned volume)
{
   uint8_t value;

   if (!audio || audio->fd < 0)
      return -1;
   if (volume > 100)
      volume = 100;
   value = (uint8_t)volume;
   if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK) {
      int ret = ioctl(audio->fd, AUDSINK_IOCTL_SET_VOLUME, &value);
      int system_ret = unifrog_audio_set_system_volume(volume);

      return ret == 0 ? 0 : system_ret;
   }
   return ioctl(audio->fd, SND_IOCTL_SET_VOLUME, &value);
}

int unifrog_audio_set_mute(struct unifrog_audio *audio, int mute)
{
   if (!audio || audio->fd < 0)
      return -1;
   if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK)
      return unifrog_audio_set_system_mute(mute);
   return ioctl(audio->fd, SND_IOCTL_SET_MUTE, mute ? 1 : 0);
}

int unifrog_audio_set_output_enabled(struct unifrog_audio *audio, int enabled)
{
   if (!audio || audio->fd < 0)
      return -1;
   if (enabled) {
      (void)unifrog_audio_set_volume(audio, SYSTEM_AUDIO_VOLUME);
      (void)unifrog_audio_set_mute(audio, 0);
      set_stock_audio_output_gate(1);
   } else {
      set_stock_audio_output_gate(0);
      (void)unifrog_audio_set_mute(audio, 1);
   }
   return 0;
}

void unifrog_audio_set_system_output_enabled(int enabled)
{
   if (enabled) {
      (void)unifrog_audio_set_system_volume(SYSTEM_AUDIO_VOLUME);
      (void)unifrog_audio_set_system_mute(0);
      set_stock_audio_output_gate(1);
   } else {
      set_stock_audio_output_gate(0);
      (void)unifrog_audio_set_system_mute(1);
   }
}

void unifrog_audio_debug_gate(uint32_t *l_dir, uint32_t *l_out,
   uint32_t *r_dir, uint32_t *r_out)
{
   if (l_dir)
      *l_dir = *GPIO_L_DIR;
   if (l_out)
      *l_out = *GPIO_L_OUTPUT;
   if (r_dir)
      *r_dir = *GPIO_R_DIR;
   if (r_out)
      *r_out = *GPIO_R_OUTPUT;
}

static unsigned read_pinmux(pinpad_e pin)
{
   volatile unsigned char *base;

   if (pin < 32)
      base = (volatile unsigned char *)&PINMUXL;
   else if (pin < 64)
      base = (volatile unsigned char *)&PINMUXB - 32;
   else if (pin < 96)
      base = (volatile unsigned char *)&PINMUXR - 64;
   else
      base = (volatile unsigned char *)&PINMUXT - 96;

   return base[pin];
}

void unifrog_audio_debug_dump(struct unifrog_audio *audio, const char *tag)
{
   struct snd_hw_info hw;
   uint32_t l_dir;
   uint32_t l_out;
   uint32_t r_dir;
   uint32_t r_out;
   unsigned long lcd_id = sf2000_lcd_panel_id ? sf2000_lcd_panel_id() : 0;
   int hw_ret = -1;

   memset(&hw, 0, sizeof(hw));
   if (audio && audio->fd >= 0 &&
       audio->backend == UNIFROG_AUDIO_BACKEND_SND)
      hw_ret = ioctl(audio->fd, SND_IOCTL_GET_HW_INFO, &hw);

   unifrog_audio_debug_gate(&l_dir, &l_out, &r_dir, &r_out);
   printf("unifrog audio diag tag=%s lcd=0x%06lx gate=%s l=0x%08lx/0x%08lx r=0x%08lx/0x%08lx\n",
      tag ? tag : "?",
      lcd_id,
      current_audio_gate() == AUDIO_GATE_GB300_L15 ? "gb300_l15" : "sf2000_r07",
      (unsigned long)l_dir, (unsigned long)l_out,
      (unsigned long)r_dir, (unsigned long)r_out);
   printf("unifrog audio mux tag=%s l22=%u l23=%u l24=%u l25=%u l26=%u l27=%u l28=%u l29=%u r07=%u\n",
      tag ? tag : "?",
      read_pinmux(PINPAD_L22), read_pinmux(PINPAD_L23),
      read_pinmux(PINPAD_L24), read_pinmux(PINPAD_L25),
      read_pinmux(PINPAD_L26), read_pinmux(PINPAD_L27),
      read_pinmux(PINPAD_L28), read_pinmux(PINPAD_L29),
      read_pinmux(PINPAD_R07));
   printf("unifrog audio hw tag=%s backend=%d snd0=0x%08lx dac=0x%08lx hw_ret=%d dma=0x%08lx/%lu hw_rate=%u hw_ch=%u hw_fmt=%u hw_period=%lu hw_periods=%lu\n",
      tag ? tag : "?",
      audio ? audio->backend : 0,
      (unsigned long)*(volatile uint32_t *)&SND0,
      (unsigned long)*(volatile uint32_t *)&SND0_DAC,
      hw_ret,
      (unsigned long)hw.dma_addr, (unsigned long)hw.dma_size,
      hw.pcm_params.rate, hw.pcm_params.channels,
      (unsigned)hw.pcm_params.format,
      (unsigned long)hw.pcm_params.period_size,
      (unsigned long)hw.pcm_params.periods);
}
