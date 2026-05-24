#include <unifrog/audio.h>

#include <errno.h>
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
#include <hcuapi/pinmux.h>
#include <hcuapi/snd.h>
#include <kernel/module.h>

#include <unifrog/backlight.h>
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
#define SYSTEM_AUDIO_VOLUME 65u
#define GB300_SYSTEM_AUDIO_VOLUME 90u
#define UNIFROG_AUDIO_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#ifndef UNIFROG_AUDIO_GB300_ROUTE_PROBE_ONCE
#define UNIFROG_AUDIO_GB300_ROUTE_PROBE_ONCE 0
#endif
#define GB300_ROUTE_PROBE_RATE 44100u
#define GB300_ROUTE_PROBE_CHANNELS 2u
#define GB300_ROUTE_PROBE_PERIOD_BYTES 4096u
#define GB300_ROUTE_PROBE_PERIODS 8u
#define GB300_ROUTE_PROBE_FRAMES 8192u
#define GB300_ROUTE_PROBE_XFERS 4u
#define GB300_ROUTE_PROBE_ROUTE_PAUSE_US 420000u
#define GB300_SND_PERIOD_BYTES 3072u
#define GB300_SND_PERIODS 40u
#define GB300_SND_START_THRESHOLD 2u
#define GB300_SND_MIN_WRITE_ATTEMPTS 16u
#define GB300_GATE_PROBE_VOLUME 100u
#define GB300_GATE_PROBE_STAGE_XFERS 4u
#define GB300_GATE_PROBE_STAGE_PAUSE_US 450000u
#define GB300_CONTROL_SWEEP_XFERS 5u
#define GB300_CONTROL_SWEEP_PAUSE_US 250000u
#define PWMCTRL_BASE ((volatile uint32_t *)0xb8818410u)
#define SND0_BASE ((volatile uint32_t *)0xb880a000u)
#define SND0_DAC_BASE ((volatile uint32_t *)0xb880b000u)
#define SND0_UNDERRUN_FADE_REG (0x3cu / sizeof(uint32_t))
#define SND0_UNDERRUN_FADE_BIT 0x40u
#define SND0_FADE_REG (0x90u / sizeof(uint32_t))
extern unsigned long sf2000_lcd_panel_id(void) __attribute__((weak));
extern unsigned long PINMUXL;
extern unsigned long PINMUXB;
extern unsigned long PINMUXR;
extern unsigned long PINMUXT;
extern unsigned long SND0;
extern unsigned long SND0_DAC;
extern int unifrog_input_uses_stock_bits(void) __attribute__((weak));
extern unsigned char i2so_platform_dev[] __attribute__((weak));

/* Offsets mirror the SDK i2so_platform_device layout; the app include path
 * intentionally does not expose the kernel-private header. */
#define I2SO_PLATFORM_STATUS_OFF 4u
#define I2SO_PLATFORM_REMAINS_OFF 40u
#define I2SO_PLATFORM_WR_OFF 52u
#define I2SO_PLATFORM_RD_OFF 56u
#define I2SO_PLATFORM_AVAIL_OFF 60u
#define I2SO_PLATFORM_REF_CNT_OFF 168u
#define I2SO_PLATFORM_PINMUX_DATA_OFF 172u
#define I2SO_PLATFORM_PINMUX_MUTE_OFF 176u
#define I2SO_PLATFORM_MUTE_POLAR_OFF 180u
#define I2SO_PLATFORM_VOLUME_OFF 181u
#define I2SO_PLATFORM_FADE_EN_OFF 182u
#define I2SO_PLATFORM_FADE_STEP_OFF 183u
#define I2SO_PLATFORM_MUTE_OFF 264u
#define I2SO_PLATFORM_U8(off) (*(volatile uint8_t *)(void *)(i2so_platform_dev + (off)))
#define I2SO_PLATFORM_INT(off) (*(volatile int *)(void *)(i2so_platform_dev + (off)))
#define I2SO_PLATFORM_SIZE(off) (*(volatile size_t *)(void *)(i2so_platform_dev + (off)))
#define I2SO_PLATFORM_PINMUX_DATA (*(struct pinmux_setting **)(void *)(i2so_platform_dev + I2SO_PLATFORM_PINMUX_DATA_OFF))
#define I2SO_PLATFORM_PINMUX_MUTE (*(struct pinmux_setting **)(void *)(i2so_platform_dev + I2SO_PLATFORM_PINMUX_MUTE_OFF))

enum audio_gate {
   AUDIO_GATE_SF2000_R07,
   AUDIO_GATE_GB300_L15,
};

static int stock_audio_output_gate_enabled;

static int current_audio_uses_gb300_gate(void);
static unsigned read_pinmux(pinpad_e pin);

static void clear_audio(struct unifrog_audio *audio)
{
   if (audio) {
      memset(audio, 0, sizeof(*audio));
      audio->fd = -1;
      audio->backend = UNIFROG_AUDIO_BACKEND_AUTO;
      audio->muted = 1;
      audio->output_gate_enabled = 0;
      audio->output_gate_pending_signal = 0;
   }
}

static enum audio_gate current_audio_gate(void)
{
   unsigned long lcd_id = sf2000_lcd_panel_id ? sf2000_lcd_panel_id() : 0;

   if (lcd_id == LCD_ID_GB300 || lcd_id == LCD_ID_DY14)
      return AUDIO_GATE_GB300_L15;
   if (unifrog_input_uses_stock_bits && unifrog_input_uses_stock_bits())
      return AUDIO_GATE_GB300_L15;

   (void)LCD_ID_SF2000;
   return AUDIO_GATE_SF2000_R07;
}

static const char *audio_gate_name(enum audio_gate gate)
{
   return gate == AUDIO_GATE_GB300_L15 ? "gb300_l15" : "sf2000_r07";
}

static const char *audio_backend_name(int backend)
{
   switch (backend) {
   case UNIFROG_AUDIO_BACKEND_SND:
      return "snd";
   case UNIFROG_AUDIO_BACKEND_AUDSINK:
      return "audsink";
   case UNIFROG_AUDIO_BACKEND_AUTO:
      return "auto";
   default:
      return "unknown";
   }
}

static void gb300_i2so_platform_log(const char *tag)
{
   struct pinmux_setting *data;
   struct pinmux_setting *mute;
   int mute_pin = -1;
   unsigned mute_func = 0;
   int data_pin = -1;
   unsigned data_func = 0;

   if (!current_audio_uses_gb300_gate() || !i2so_platform_dev)
      return;
   data = I2SO_PLATFORM_PINMUX_DATA;
   mute = I2SO_PLATFORM_PINMUX_MUTE;
   if (mute && mute->num_pins > 0) {
      mute_pin = (int)mute->settings[0].pin;
      mute_func = mute->settings[0].func;
   }
   if (data && data->num_pins > 0) {
      data_pin = (int)data->settings[0].pin;
      data_func = data->settings[0].func;
   }
   printf("unifrog audio i2so_platform tag=%s status=%d ref=%d mute=%d vol=%u fade_en=%u fade_step=%u pin_data=0x%08lx data0=%d/%u pin_mute=0x%08lx mute0=%d/%u mute_polar=%u wr=%lu rd=%lu avail=%lu remains=%lu\n",
      tag ? tag : "?",
      I2SO_PLATFORM_INT(I2SO_PLATFORM_STATUS_OFF),
      I2SO_PLATFORM_INT(I2SO_PLATFORM_REF_CNT_OFF),
      I2SO_PLATFORM_INT(I2SO_PLATFORM_MUTE_OFF),
      (unsigned)I2SO_PLATFORM_U8(I2SO_PLATFORM_VOLUME_OFF),
      (unsigned)I2SO_PLATFORM_U8(I2SO_PLATFORM_FADE_EN_OFF),
      (unsigned)I2SO_PLATFORM_U8(I2SO_PLATFORM_FADE_STEP_OFF),
      (unsigned long)data, data_pin, data_func,
      (unsigned long)mute, mute_pin, mute_func,
      I2SO_PLATFORM_U8(I2SO_PLATFORM_MUTE_POLAR_OFF) ? 1u : 0u,
      (unsigned long)I2SO_PLATFORM_SIZE(I2SO_PLATFORM_WR_OFF),
      (unsigned long)I2SO_PLATFORM_SIZE(I2SO_PLATFORM_RD_OFF),
      (unsigned long)I2SO_PLATFORM_SIZE(I2SO_PLATFORM_AVAIL_OFF),
      (unsigned long)I2SO_PLATFORM_SIZE(I2SO_PLATFORM_REMAINS_OFF));
}

static int current_audio_uses_gb300_gate(void)
{
   return current_audio_gate() == AUDIO_GATE_GB300_L15;
}

int unifrog_audio_prefers_stereo_output(void)
{
   return current_audio_uses_gb300_gate();
}

unsigned unifrog_audio_output_channels(void)
{
   /*
    * The stock GB300 sound task is fed stereo S16 frames even though the
    * handheld has one speaker. Mix content to mono above this layer and feed a
    * duplicated stereo frame stream to the I2SO route.
    */
   return current_audio_uses_gb300_gate() ? 2u : 1u;
}

static uint32_t current_audio_snd_devs(void)
{
   if (current_audio_gate() == AUDIO_GATE_GB300_L15)
      return AUDSINK_SND_DEVBIT_I2SO;
   return AUDSINK_SND_DEVBIT_I2SO;
}

static unsigned system_audio_volume(void)
{
   return current_audio_uses_gb300_gate() ?
      GB300_SYSTEM_AUDIO_VOLUME : SYSTEM_AUDIO_VOLUME;
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

static void set_reg_level(volatile uint32_t *dir, volatile uint32_t *out,
   uint32_t mask, int high)
{
   uint32_t value;

   *dir = *dir | mask;
   value = *out;
   if (high)
      value |= mask;
   else
      value &= ~mask;
   *out = value;
}

static void apply_stock_audio_output_gate(int enabled)
{
   enum audio_gate gate = current_audio_gate();

   if (gate == AUDIO_GATE_GB300_L15) {
      gpio_configure(PINPAD_L15, GPIO_DIR_OUTPUT);
      gpio_set_output(PINPAD_L15, enabled ? false : true);
      set_reg_gate(GPIO_L_DIR, GPIO_L_OUTPUT, GPIO_L15_MASK, enabled);
   } else {
      pinmux_configure(PINPAD_R07, PINMUX_R07_GPIO);
      gpio_configure(PINPAD_R07, GPIO_DIR_OUTPUT);
      gpio_set_output(PINPAD_R07, enabled ? false : true);
      set_reg_gate(GPIO_R_DIR, GPIO_R_OUTPUT, GPIO_R07_MASK, enabled);
   }
}

static void set_stock_audio_output_gate(int enabled)
{
   enum audio_gate gate = current_audio_gate();
   int old_enabled = stock_audio_output_gate_enabled;
   uint32_t l_dir;
   uint32_t l_out;
   uint32_t r_dir;
   uint32_t r_out;

   stock_audio_output_gate_enabled = enabled ? 1 : 0;
   apply_stock_audio_output_gate(enabled);
   unifrog_audio_debug_gate(&l_dir, &l_out, &r_dir, &r_out);
   printf("unifrog audio output_gate enabled=%d old=%d preferred=%s l=0x%08lx/0x%08lx r=0x%08lx/0x%08lx mux_l15=%u mux_r07=%u\n",
      stock_audio_output_gate_enabled, old_enabled, audio_gate_name(gate),
      (unsigned long)l_dir, (unsigned long)l_out,
      (unsigned long)r_dir, (unsigned long)r_out,
      read_pinmux(PINPAD_L15), read_pinmux(PINPAD_R07));
}

void unifrog_audio_set_output_gate_enabled(int enabled)
{
   set_stock_audio_output_gate(enabled);
}

void unifrog_audio_restore_output_gate(void)
{
   if (stock_audio_output_gate_enabled)
      apply_stock_audio_output_gate(1);
}

static void apply_stock_audio_silence_policy(const char *tag)
{
   uint32_t before;
   uint32_t after;
   static unsigned gb300_log_count;

   before = SND0_BASE[SND0_UNDERRUN_FADE_REG];
   if (current_audio_uses_gb300_gate()) {
      if (gb300_log_count < 6) {
         gb300_log_count++;
         printf("unifrog audio silence_policy tag=%s action=gb300_skip underrun_fade=%08lx fade90=%08lx dac=%08lx\n",
            tag ? tag : "?",
            (unsigned long)before,
            (unsigned long)SND0_BASE[SND0_FADE_REG],
            (unsigned long)SND0_DAC_BASE[0]);
      }
      return;
   }

   after = before | SND0_UNDERRUN_FADE_BIT;
   if (after != before) {
      SND0_BASE[SND0_UNDERRUN_FADE_REG] = after;
      printf("unifrog audio silence_policy tag=%s underrun_fade=%08lx->%08lx fade90=%08lx dac=%08lx\n",
         tag ? tag : "?",
         (unsigned long)before, (unsigned long)after,
         (unsigned long)SND0_BASE[SND0_FADE_REG],
         (unsigned long)SND0_DAC_BASE[0]);
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
   apply_stock_audio_silence_policy("drivers");
   return result;
}

static int open_system_snd(void)
{
   /*
    * System volume/mute only need control ioctls. Keep bidirectional DMA
    * ownership on the playback paths that intentionally open the SND node.
    */
   return open("/dev/sndC0i2so", O_WRONLY);
}

static int configure_neutral_audio_controls_fd(int fd, const char *tag)
{
   struct snd_twotone twotone;
   struct snd_audio_eq6 eq6;
   struct snd_lr_balance balance;
   int twotone_ret;
   int eq_ret;
   int balance_ret;
   static unsigned log_count;
   static unsigned gb300_log_count;
   int gb300_route = current_audio_uses_gb300_gate();

   if (fd < 0)
      return -1;
   memset(&twotone, 0, sizeof(twotone));
   memset(&eq6, 0, sizeof(eq6));
   memset(&balance, 0, sizeof(balance));
   twotone.tt_mode = SND_TWOTONE_MODE_USER;
   balance.lr_balance_index = 0;
   eq6.mode = SND_EQ6_MODE_OFF;
   twotone_ret = ioctl(fd, SND_IOCTL_SET_TWOTONE, &twotone);
   eq_ret = ioctl(fd, SND_IOCTL_SET_EQ6, &eq6);
   balance_ret = ioctl(fd, SND_IOCTL_SET_LR_BALANCE, &balance);
   if ((gb300_route && gb300_log_count < 32) ||
       (!gb300_route && log_count < 12) || twotone_ret != 0 ||
       eq_ret != 0 || balance_ret != 0) {
      if (gb300_route)
         gb300_log_count++;
      else
         log_count++;
      printf("unifrog audio neutral_controls tag=%s fd=%d gb300=%d twotone=%d eq6=%d balance=%d\n",
         tag ? tag : "?", fd, gb300_route, twotone_ret, eq_ret,
         balance_ret);
   }
   return twotone_ret == 0 && eq_ret == 0 && balance_ret == 0 ? 0 : -1;
}

int unifrog_audio_set_system_volume(unsigned volume)
{
   uint8_t value;
   int ret;
   int ret_errno;
   int fd;

   if (volume > 100)
      volume = 100;
   value = (uint8_t)volume;
   (void)ensure_audio_drivers();
   fd = open_system_snd();
   if (fd < 0) {
      printf("unifrog audio system_volume open_fail volume=%u\n", volume);
      return -1;
   }
   configure_neutral_audio_controls_fd(fd, "system_volume");
   errno = 0;
   ret = ioctl(fd, SND_IOCTL_SET_VOLUME, &value);
   ret_errno = errno;
   close(fd);
   printf("unifrog audio system_volume volume=%u ret=%d errno=%d dac0=%08lx dac1=%08lx fade90=%08lx\n",
      volume, ret, ret_errno,
      (unsigned long)SND0_DAC_BASE[0],
      (unsigned long)SND0_DAC_BASE[1],
      (unsigned long)SND0_BASE[SND0_FADE_REG]);
   gb300_i2so_platform_log("system_volume");
   return ret;
}

int unifrog_audio_set_system_mute(int mute)
{
   int value = mute ? 1 : 0;
   int ret;
   int ret_errno;
   int fd;

   (void)ensure_audio_drivers();
   fd = open_system_snd();
   if (fd < 0) {
      static int logged_open_fail;

      if (!logged_open_fail) {
         logged_open_fail = 1;
         printf("unifrog audio system_mute open_fail mute=%d\n", value);
      }
      return -1;
   }
   configure_neutral_audio_controls_fd(fd, "system_mute");
   errno = 0;
   ret = ioctl(fd, SND_IOCTL_SET_MUTE, value);
   ret_errno = errno;
   close(fd);
   printf("unifrog audio system_mute mute=%d ret=%d errno=%d dac0=%08lx dac1=%08lx fade90=%08lx\n",
      value, ret, ret_errno,
      (unsigned long)SND0_DAC_BASE[0],
      (unsigned long)SND0_DAC_BASE[1],
      (unsigned long)SND0_BASE[SND0_FADE_REG]);
   gb300_i2so_platform_log("system_mute");
   return ret;
}

static int open_audsink(struct unifrog_audio *audio,
   unsigned rate, unsigned channels,
   unsigned period_bytes, unsigned periods)
{
   struct audsink_init_params params;
   int duplicate;
   int fd;
   int init_ret;
   int init_errno;

   fd = open("/dev/audsink", O_WRONLY);
   if (fd < 0)
      return -1;

   memset(&params, 0, sizeof(params));
   params.buf_size = period_bytes * periods;
   params.snd_devs = current_audio_snd_devs();
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
   errno = 0;
   init_ret = ioctl(fd, AUDSINK_IOCTL_INIT, &params);
   init_errno = errno;
   if (init_ret != 0) {
      printf("unifrog audio open backend=audsink failed fd=%d rate=%u ch=%u period=%u periods=%u route=%s output_ch=%u snd=0x%lx init_ret=%d errno=%d\n",
         fd, rate, channels, period_bytes, periods,
         audio_gate_name(current_audio_gate()),
         unifrog_audio_output_channels(),
         (unsigned long)params.snd_devs, init_ret, init_errno);
      goto fail;
   }

   duplicate = unifrog_audio_output_channels() > 1u && channels > 1 ?
      AUDSINK_PCM_DUPLICATE_STEREO : AUDSINK_PCM_DUPLICATE_LEFT;
   ioctl(fd, AUDSINK_IOCTL_SETDUPLICATE, duplicate);

   audio->fd = fd;
   audio->backend = UNIFROG_AUDIO_BACKEND_AUDSINK;
   audio->rate = rate;
   audio->channels = channels;
   audio->period_bytes = period_bytes;
   audio->periods = periods;
   audio->frame_bytes = channels * sizeof(int16_t);
   audio->muted = 1;
   printf("unifrog audio open backend=audsink fd=%d rate=%u ch=%u period=%u periods=%u route=%s output_ch=%u snd=0x%lx duplicate=%d\n",
      fd, rate, channels, period_bytes, periods,
      audio_gate_name(current_audio_gate()),
      unifrog_audio_output_channels(),
      (unsigned long)params.snd_devs, duplicate);
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
   struct snd_hw_info hw;
   unsigned requested_period = period_bytes;
   unsigned requested_periods = periods;
   unsigned start_threshold = 0;
   int gb300_route = current_audio_gate() == AUDIO_GATE_GB300_L15;
   int hw_ret;
   int hw_errno = 0;
   int avail_ret = -1;
   int neutral_ret = -1;
   int fd;

   if (gb300_route) {
      if (period_bytes < GB300_SND_PERIOD_BYTES)
         period_bytes = GB300_SND_PERIOD_BYTES;
      if (periods < GB300_SND_PERIODS)
         periods = GB300_SND_PERIODS;
      start_threshold = GB300_SND_START_THRESHOLD;
   }

   fd = open("/dev/sndC0i2so", O_RDWR);
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
   params.start_threshold = start_threshold;
   params.pcm_source = SND_PCM_SOURCE_AUDPAD;
   params.pcm_dest = SND_PCM_DEST_DMA;
   errno = 0;
   hw_ret = ioctl(fd, SND_IOCTL_HW_PARAMS, &params);
   hw_errno = errno;
   if (hw_ret != 0)
      goto fail;
   avail_min = period_bytes;
   avail_ret = ioctl(fd, SND_IOCTL_AVAIL_MIN, &avail_min);
   neutral_ret = configure_neutral_audio_controls_fd(fd, "open_snd");

   audio->fd = fd;
   audio->backend = UNIFROG_AUDIO_BACKEND_SND;
   audio->rate = rate;
   audio->channels = channels;
   audio->period_bytes = period_bytes;
   audio->periods = periods;
   audio->frame_bytes = channels * sizeof(int16_t);
   audio->muted = 1;
   memset(&hw, 0, sizeof(hw));
   (void)ioctl(fd, SND_IOCTL_GET_HW_INFO, &hw);
   printf("unifrog audio open backend=snd fd=%d rate=%u ch=%u req_period=%u req_periods=%u period=%u periods=%u start_threshold=%u avail_min=%lu avail_ret=%d neutral=%d route=%s output_ch=%u mode=%s dma=0x%08lx/%lu hw_period=%lu hw_periods=%lu\n",
      fd, rate, channels, requested_period, requested_periods,
      period_bytes, periods, start_threshold, (unsigned long)avail_min,
      avail_ret, neutral_ret,
      audio_gate_name(current_audio_gate()),
      unifrog_audio_output_channels(),
      gb300_route ? "gb300_audpad" : "audpad",
      (unsigned long)hw.dma_addr, (unsigned long)hw.dma_size,
      (unsigned long)hw.pcm_params.period_size,
      (unsigned long)hw.pcm_params.periods);
   gb300_i2so_platform_log("open_snd");
   return 0;

fail:
   printf("unifrog audio open backend=snd failed fd=%d rate=%u ch=%u req_period=%u req_periods=%u period=%u periods=%u start_threshold=%u hw_ret=%d hw_errno=%d route=%s mode=%s\n",
      fd, rate, channels, requested_period, requested_periods,
      period_bytes, periods, start_threshold, hw_ret, hw_errno,
      audio_gate_name(current_audio_gate()),
      gb300_route ? "gb300_audpad" : "audpad");
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
   if (unifrog_audio_prefers_stereo_output())
      unifrog_audio_run_gb300_route_probe_once("open_backend");

   if (backend == UNIFROG_AUDIO_BACKEND_AUDSINK)
      return open_audsink(audio, rate, channels, period_bytes, periods);
   if (backend == UNIFROG_AUDIO_BACKEND_SND)
      return open_snd(audio, rate, channels, period_bytes, periods);

   if (unifrog_audio_prefers_stereo_output()) {
      if (open_snd(audio, rate, channels, period_bytes, periods) == 0)
         return 0;
      return open_audsink(audio, rate, channels, period_bytes, periods);
   }

   if (open_snd(audio, rate, channels, period_bytes, periods) == 0)
      return 0;
   return open_audsink(audio, rate, channels, period_bytes, periods);
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
   apply_stock_audio_silence_policy("start");
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
   int last_errno = 0;
   static unsigned failure_log_count;
   static unsigned success_log_count;
   static unsigned mute_transition_log_count;
   static unsigned signal_log_count;
   int gb300_snd = audio && audio->backend == UNIFROG_AUDIO_BACKEND_SND &&
      unifrog_audio_prefers_stereo_output();
   int has_signal = 0;
   unsigned signal_nonzero = 0;
   unsigned signal_abs_max = 0;
   int first_sample = 0;
   int last_sample = 0;

   if (!audio || audio->fd < 0 || !samples || frames == 0)
      return -1;
   if (attempts == 0)
      attempts = 1;
   if (gb300_snd && attempts < GB300_SND_MIN_WRITE_ATTEMPTS)
      attempts = GB300_SND_MIN_WRITE_ATTEMPTS;
   if (audio->backend == UNIFROG_AUDIO_BACKEND_SND) {
      unsigned channels = audio->channels ? audio->channels : 1u;
      unsigned total = frames * channels;

      if (total > 0) {
         first_sample = samples[0];
         last_sample = samples[total - 1u];
      }
      for (unsigned i = 0; i < total; i++) {
         int sample = samples[i];
         unsigned abs_value;

         if (sample < 0)
            sample = -sample;
         abs_value = (unsigned)sample;
         if (abs_value > 4u) {
            has_signal = 1;
            signal_nonzero++;
         }
         if (abs_value > signal_abs_max)
            signal_abs_max = abs_value;
      }
      if (gb300_snd) {
         int mute_ret = audio->muted ?
            unifrog_audio_set_mute(audio, 0) : 0;

         if (!audio->output_gate_enabled) {
            set_stock_audio_output_gate(1);
            audio->output_gate_enabled = 1;
            audio->output_gate_pending_signal = 0;
            if (mute_transition_log_count < 12) {
               mute_transition_log_count++;
               printf("unifrog audio signal_gate backend=snd fd=%d action=gb300_gate_on_write signal=%d nonzero=%u abs_max=%u mute_ret=%d frames=%u ch=%u\n",
                  audio->fd, has_signal, signal_nonzero, signal_abs_max,
                  mute_ret, frames, channels);
            }
         }
      } else if (has_signal && audio->muted) {
         int mute_ret = unifrog_audio_set_mute(audio, 0);

         if (mute_transition_log_count < 12) {
            mute_transition_log_count++;
            printf("unifrog audio signal_gate backend=snd fd=%d action=unmute ret=%d frames=%u ch=%u\n",
               audio->fd, mute_ret, frames, channels);
         }
      } else if (!has_signal && !audio->muted) {
         int mute_ret = unifrog_audio_set_mute(audio, 1);

         if (mute_transition_log_count < 12) {
            mute_transition_log_count++;
            printf("unifrog audio signal_gate backend=snd fd=%d action=mute ret=%d frames=%u ch=%u\n",
               audio->fd, mute_ret, frames, channels);
         }
      }
   }

   pollfd.fd = audio->fd;
   pollfd.events = POLLOUT | POLLWRNORM;

   do {
      if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK) {
         struct audsink_xfer xfer;

         memset(&xfer, 0, sizeof(xfer));
         xfer.data = (void *)samples;
         xfer.frames = frames;
         errno = 0;
         ret = ioctl(audio->fd, AUDSINK_IOCTL_XFER, &xfer);
         last_errno = errno;
      } else {
         struct snd_xfer xfer;

         memset(&xfer, 0, sizeof(xfer));
         xfer.data = (void *)samples;
         xfer.frames = frames;
         errno = 0;
         ret = ioctl(audio->fd, SND_IOCTL_XFER, &xfer);
         last_errno = errno;
      }
      if (ret >= 0) {
         if (gb300_snd && has_signal && signal_log_count < 12) {
            signal_log_count++;
            printf("unifrog audio gb300_first_signal idx=%u fd=%d frames=%u ch=%u nonzero=%u abs_max=%u first=%d last=%d muted=%d gate=%d dac0=%08lx dac1=%08lx fade90=%08lx\n",
               signal_log_count, audio->fd, frames, audio->channels,
               signal_nonzero, signal_abs_max, first_sample, last_sample,
               audio->muted, audio->output_gate_enabled,
               (unsigned long)SND0_DAC_BASE[0],
               (unsigned long)SND0_DAC_BASE[1],
               (unsigned long)SND0_BASE[SND0_FADE_REG]);
            if (signal_log_count <= 4)
               unifrog_audio_debug_dump(audio, "gb300_first_signal");
         }
         if (gb300_snd && success_log_count < 8) {
            snd_pcm_uframes_t delay = 0;
            int delay_ret = ioctl(audio->fd, SND_IOCTL_DELAY, &delay);

            success_log_count++;
            printf("unifrog audio write ok backend=snd fd=%d frames=%u tries=%u attempts=%u delay_ret=%d delay=%lu muted=%d gate=%d pending=%d\n",
               audio->fd, frames, tries + 1, attempts, delay_ret,
               (unsigned long)delay, audio->muted,
               audio->output_gate_enabled,
               audio->output_gate_pending_signal);
         }
         return 0;
      }
      if (tries + 1 >= attempts)
         break;
      poll(&pollfd, 1, (int)poll_timeout_ms);
   } while (++tries < attempts);

   if (failure_log_count < 20) {
      snd_pcm_uframes_t delay = 0;
      int delay_ret = -1;

      failure_log_count++;
      if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK)
         delay_ret = ioctl(audio->fd, AUDSINK_IOCTL_DELAY, &delay);
      else
         delay_ret = ioctl(audio->fd, SND_IOCTL_DELAY, &delay);
      printf("unifrog audio write fail backend=%s fd=%d ret=%d errno=%d frames=%u tries=%u attempts=%u poll_ms=%u rate=%u ch=%u muted=%d gate=%d pending=%d delay_ret=%d delay=%lu\n",
         audio_backend_name(audio->backend), audio->fd, ret, last_errno,
         frames, tries + 1, attempts, poll_timeout_ms, audio->rate,
         audio->channels, audio->muted, audio->output_gate_enabled,
         audio->output_gate_pending_signal, delay_ret, (unsigned long)delay);
   }
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
   int ret;
   int ret_errno = 0;
   int system_ret = 0;
   static unsigned gb300_log_count;

   if (!audio || audio->fd < 0)
      return -1;
   if (volume > 100)
      volume = 100;
   value = (uint8_t)volume;
   if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK) {
      errno = 0;
      ret = ioctl(audio->fd, AUDSINK_IOCTL_SET_VOLUME, &value);
      ret_errno = errno;
      system_ret = unifrog_audio_set_system_volume(volume);
   } else {
      errno = 0;
      ret = ioctl(audio->fd, SND_IOCTL_SET_VOLUME, &value);
      ret_errno = errno;
   }
   if (current_audio_uses_gb300_gate() &&
       (gb300_log_count < 40 || ret != 0 || system_ret != 0)) {
      gb300_log_count++;
      printf("unifrog audio set_volume backend=%s fd=%d volume=%u ret=%d errno=%d system_ret=%d dac0=%08lx dac1=%08lx fade90=%08lx\n",
         audio_backend_name(audio->backend), audio->fd, volume, ret,
         ret_errno, system_ret,
         (unsigned long)SND0_DAC_BASE[0],
         (unsigned long)SND0_DAC_BASE[1],
         (unsigned long)SND0_BASE[SND0_FADE_REG]);
      gb300_i2so_platform_log("set_volume");
   }
   if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK)
      return ret == 0 ? 0 : system_ret;
   return ret;
}

int unifrog_audio_set_mute(struct unifrog_audio *audio, int mute)
{
   int ret;
   int ret_errno;
   static unsigned gb300_log_count;

   if (!audio || audio->fd < 0)
      return -1;
   if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK) {
      errno = 0;
      ret = unifrog_audio_set_system_mute(mute);
      ret_errno = errno;
   } else {
      errno = 0;
      ret = ioctl(audio->fd, SND_IOCTL_SET_MUTE, mute ? 1 : 0);
      ret_errno = errno;
   }
   if (current_audio_uses_gb300_gate() &&
       (gb300_log_count < 48 || ret != 0)) {
      gb300_log_count++;
      printf("unifrog audio set_mute backend=%s fd=%d mute=%d ret=%d errno=%d dac0=%08lx dac1=%08lx fade90=%08lx\n",
         audio_backend_name(audio->backend), audio->fd, mute ? 1 : 0, ret,
         ret_errno,
         (unsigned long)SND0_DAC_BASE[0],
         (unsigned long)SND0_DAC_BASE[1],
         (unsigned long)SND0_BASE[SND0_FADE_REG]);
      gb300_i2so_platform_log("set_mute");
   }
   if (ret != 0)
      return -1;
   audio->muted = mute ? 1 : 0;
   return 0;
}

int unifrog_audio_set_output_enabled(struct unifrog_audio *audio, int enabled)
{
   if (!audio || audio->fd < 0)
      return -1;
   if (current_audio_uses_gb300_gate()) {
      if (enabled) {
         unsigned volume = system_audio_volume();
         int system_volume_ret;
         int system_mute_ret;
         int volume_ret;
         int mute_ret;

         apply_stock_audio_silence_policy("gb300_enable");
         system_volume_ret = unifrog_audio_set_system_volume(volume);
         system_mute_ret = unifrog_audio_set_system_mute(0);
         volume_ret = unifrog_audio_set_volume(audio, volume);
         mute_ret = unifrog_audio_set_mute(audio, 0);
         set_stock_audio_output_gate(1);
         audio->output_gate_enabled = 1;
         audio->output_gate_pending_signal = 0;
         printf("unifrog audio output_enable action=gb300_system_unmute backend=%s fd=%d volume=%u sys_volume=%d sys_mute=%d volume_ret=%d mute_ret=%d muted=%d gate=%d\n",
            audio_backend_name(audio->backend), audio->fd, volume,
            system_volume_ret, system_mute_ret, volume_ret, mute_ret,
            audio->muted, audio->output_gate_enabled);
      } else {
         int mute_ret;
         int system_mute_ret;

         set_stock_audio_output_gate(0);
         audio->output_gate_enabled = 0;
         audio->output_gate_pending_signal = 0;
         mute_ret = unifrog_audio_set_mute(audio, 1);
         system_mute_ret = unifrog_audio_set_system_mute(1);
         printf("unifrog audio output_enable action=gb300_system_mute backend=%s fd=%d mute_ret=%d sys_mute=%d muted=%d gate=%d\n",
            audio_backend_name(audio->backend), audio->fd, mute_ret,
            system_mute_ret, audio->muted, audio->output_gate_enabled);
      }
      return 0;
   }
   if (enabled) {
      apply_stock_audio_silence_policy("enable");
      (void)unifrog_audio_set_volume(audio, system_audio_volume());
      if (audio->backend == UNIFROG_AUDIO_BACKEND_SND &&
          unifrog_audio_prefers_stereo_output()) {
         (void)unifrog_audio_set_system_mute(0);
         (void)unifrog_audio_set_mute(audio, 0);
         set_stock_audio_output_gate(1);
         audio->output_gate_enabled = 1;
         audio->output_gate_pending_signal = 0;
         printf("unifrog audio signal_gate backend=snd fd=%d action=gb300_gate_on_enable\n",
            audio->fd);
      } else {
         set_stock_audio_output_gate(1);
         audio->output_gate_enabled = 1;
         audio->output_gate_pending_signal = 0;
         if (audio->backend == UNIFROG_AUDIO_BACKEND_SND)
            (void)unifrog_audio_set_mute(audio, 1);
         else
            (void)unifrog_audio_set_mute(audio, 0);
      }
   } else {
      set_stock_audio_output_gate(0);
      audio->output_gate_enabled = 0;
      audio->output_gate_pending_signal = 0;
      (void)unifrog_audio_set_mute(audio, 1);
   }
   return 0;
}

void unifrog_audio_set_system_output_enabled(int enabled)
{
   if (enabled) {
      (void)ensure_audio_drivers();
      apply_stock_audio_silence_policy("system_enable");
      (void)unifrog_audio_set_system_volume(system_audio_volume());
      (void)unifrog_audio_set_system_mute(0);
      set_stock_audio_output_gate(1);
   } else {
      set_stock_audio_output_gate(0);
      (void)unifrog_audio_set_system_mute(1);
   }
}

static int16_t gb300_route_probe_pcm[GB300_ROUTE_PROBE_FRAMES *
   GB300_ROUTE_PROBE_CHANNELS];
static int16_t gb300_route_probe_mono_pcm[GB300_ROUTE_PROBE_FRAMES];

static void fill_gb300_route_probe_pcm(unsigned route)
{
   unsigned period = 44u + (route % 7u) * 8u;
   unsigned amplitude = 9000u + (route % 5u) * 1200u;

   for (unsigned i = 0; i < GB300_ROUTE_PROBE_FRAMES; i++) {
      int16_t sample = ((i / period) & 1u) ?
         (int16_t)amplitude : -(int16_t)amplitude;

      gb300_route_probe_pcm[i * 2u] = sample;
      gb300_route_probe_pcm[i * 2u + 1u] = sample;
      gb300_route_probe_mono_pcm[i] = sample;
   }
}

static void gb300_gate_probe_set_level(const char *stage, int l15_high,
   int r07_high)
{
   uint32_t l_dir;
   uint32_t l_out;
   uint32_t r_dir;
   uint32_t r_out;

   pinmux_configure(PINPAD_L15, PINMUX_L15_GPIO);
   gpio_configure(PINPAD_L15, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_L15, l15_high ? true : false);
   set_reg_level(GPIO_L_DIR, GPIO_L_OUTPUT, GPIO_L15_MASK, l15_high);

   pinmux_configure(PINPAD_R07, PINMUX_R07_GPIO);
   gpio_configure(PINPAD_R07, GPIO_DIR_OUTPUT);
   gpio_set_output(PINPAD_R07, r07_high ? true : false);
   set_reg_level(GPIO_R_DIR, GPIO_R_OUTPUT, GPIO_R07_MASK, r07_high);

   unifrog_audio_debug_gate(&l_dir, &l_out, &r_dir, &r_out);
   printf("unifrog audio gb300_gate_probe gate stage=%s l15_high=%d r07_high=%d l=0x%08lx/0x%08lx r=0x%08lx/0x%08lx mux_l15=%u mux_r07=%u\n",
      stage ? stage : "?", l15_high, r07_high,
      (unsigned long)l_dir, (unsigned long)l_out,
      (unsigned long)r_dir, (unsigned long)r_out,
      read_pinmux(PINPAD_L15), read_pinmux(PINPAD_R07));
}

static void init_gb300_route_probe_pcm_params(struct snd_pcm_params *params,
   snd_pcm_source_t source)
{
   memset(params, 0, sizeof(*params));
   params->access = SND_PCM_ACCESS_RW_INTERLEAVED;
   params->format = SND_PCM_FORMAT_S16_LE;
   params->sync_mode = AVSYNC_TYPE_FREERUN;
   params->align = SND_PCM_ALIGN_LEFT;
   params->rate = GB300_ROUTE_PROBE_RATE;
   params->channels = GB300_ROUTE_PROBE_CHANNELS;
   params->period_size = GB300_ROUTE_PROBE_PERIOD_BYTES;
   params->periods = GB300_ROUTE_PROBE_PERIODS;
   params->bitdepth = 16;
   params->start_threshold = GB300_SND_START_THRESHOLD;
   params->pcm_source = source;
   params->pcm_dest = SND_PCM_DEST_DMA;
}

static int gb300_route_probe_audsink(unsigned route, const char *name,
   uint32_t snd_devs, int duplicate)
{
   struct audsink_init_params params;
   struct audsink_xfer xfer;
   snd_pcm_uframes_t delay = 0;
   uint8_t volume = (uint8_t)system_audio_volume();
   int fd;
   int init_ret = -1;
   int init_errno = 0;
   int dup_ret = -1;
   int volume_ret = -1;
   int volume_errno = 0;
   int start_ret = -1;
   int start_errno = 0;
   int xfer_ret = -1;
   int xfer_errno = 0;
   unsigned xfer_count = 0;
   int delay_ret = -1;
   int delay_errno = 0;
   int drain_ret = -1;
   int drain_errno = 0;

   fd = open("/dev/audsink", O_WRONLY);
   if (fd < 0) {
      printf("unifrog audio gb300_probe route=%u kind=audsink name=%s open=-1 errno=%d snd=0x%lx\n",
         route, name, errno, (unsigned long)snd_devs);
      return -1;
   }

   memset(&params, 0, sizeof(params));
   params.buf_size = GB300_ROUTE_PROBE_PERIOD_BYTES *
      GB300_ROUTE_PROBE_PERIODS;
   params.snd_devs = snd_devs;
   params.sync_type = AVSYNC_TYPE_FREERUN;
   init_gb300_route_probe_pcm_params(&params.pcm, SND_PCM_SOURCE_AUDPAD);
   errno = 0;
   init_ret = ioctl(fd, AUDSINK_IOCTL_INIT, &params);
   init_errno = errno;
   if (init_ret == 0) {
      dup_ret = ioctl(fd, AUDSINK_IOCTL_SETDUPLICATE, duplicate);
      errno = 0;
      volume_ret = ioctl(fd, AUDSINK_IOCTL_SET_VOLUME, &volume);
      volume_errno = errno;
      errno = 0;
      start_ret = ioctl(fd, AUDSINK_IOCTL_START, 0);
      start_errno = errno;
      if (start_ret == 0) {
         for (unsigned i = 0; i < GB300_ROUTE_PROBE_XFERS; i++) {
            memset(&xfer, 0, sizeof(xfer));
            xfer.data = gb300_route_probe_pcm;
            xfer.frames = GB300_ROUTE_PROBE_FRAMES;
            errno = 0;
            xfer_ret = ioctl(fd, AUDSINK_IOCTL_XFER, &xfer);
            xfer_errno = errno;
            if (xfer_ret < 0)
               break;
            xfer_count++;
         }
         errno = 0;
         delay_ret = ioctl(fd, AUDSINK_IOCTL_DELAY, &delay);
         delay_errno = errno;
         usleep(GB300_ROUTE_PROBE_ROUTE_PAUSE_US);
         errno = 0;
         drain_ret = ioctl(fd, AUDSINK_IOCTL_DRAIN, 0);
         drain_errno = errno;
      }
   }
   printf("unifrog audio gb300_probe route=%u kind=audsink name=%s fd=%d snd=0x%lx dup=%d init=%d init_errno=%d duplicate=%d volume=%d volume_errno=%d start=%d start_errno=%d xfer=%d xfer_errno=%d xfers=%u delay_ret=%d delay_errno=%d delay=%lu drain=%d drain_errno=%d\n",
      route, name, fd, (unsigned long)snd_devs, duplicate, init_ret,
      init_errno, dup_ret, volume_ret, volume_errno, start_ret, start_errno,
      xfer_ret, xfer_errno, xfer_count, delay_ret, delay_errno,
      (unsigned long)delay, drain_ret, drain_errno);
   (void)ioctl(fd, AUDSINK_IOCTL_DROP, 0);
   (void)ioctl(fd, AUDSINK_IOCTL_FLUSH, 0);
   close(fd);
   return init_ret == 0 && start_ret == 0 && xfer_count > 0 ? 0 : -1;
}

static int gb300_route_probe_snd(unsigned route, const char *name,
   const char *dev, snd_pcm_source_t source, snd_pcm_dest_t dest,
   unsigned channels, int open_flags, unsigned period_bytes, unsigned periods,
   unsigned start_threshold)
{
   struct snd_pcm_params params;
   struct snd_xfer xfer;
   struct snd_hw_info hw;
   snd_pcm_uframes_t avail_min;
   snd_pcm_uframes_t delay = 0;
   const int16_t *pcm;
   uint8_t volume = (uint8_t)system_audio_volume();
   int fd;
   int hw_ret = -1;
   int hw_errno = 0;
   int avail_ret = -1;
   int avail_errno = 0;
   int neutral_ret = 0;
   int volume_ret = -1;
   int volume_errno = 0;
   int mute_ret = -1;
   int mute_errno = 0;
   int start_ret = -1;
   int start_errno = 0;
   int xfer_ret = -1;
   int xfer_errno = 0;
   unsigned xfer_count = 0;
   int delay_ret = -1;
   int delay_errno = 0;
   int drain_ret = -1;
   int drain_errno = 0;
   int info_ret = -1;

   if (channels == 0)
      channels = GB300_ROUTE_PROBE_CHANNELS;
   if (period_bytes == 0)
      period_bytes = GB300_ROUTE_PROBE_PERIOD_BYTES;
   if (periods == 0)
      periods = GB300_ROUTE_PROBE_PERIODS;
   if (open_flags == 0)
      open_flags = O_RDWR;
   avail_min = period_bytes;
   pcm = channels == 1 ? gb300_route_probe_mono_pcm : gb300_route_probe_pcm;

   fd = open(dev, open_flags);
   if (fd < 0) {
      printf("unifrog audio gb300_probe route=%u kind=snd name=%s dev=%s open=-1 errno=%d source=%lu dest=%lu ch=%u flags=0x%x\n",
         route, name, dev, errno, (unsigned long)source,
         (unsigned long)dest, channels, open_flags);
      return -1;
   }

   init_gb300_route_probe_pcm_params(&params, source);
   params.channels = channels;
   params.period_size = period_bytes;
   params.periods = periods;
   params.start_threshold = start_threshold;
   params.pcm_dest = dest;
   errno = 0;
   hw_ret = ioctl(fd, SND_IOCTL_HW_PARAMS, &params);
   hw_errno = errno;
   if (hw_ret == 0) {
      errno = 0;
      avail_ret = ioctl(fd, SND_IOCTL_AVAIL_MIN, &avail_min);
      avail_errno = errno;
      neutral_ret = configure_neutral_audio_controls_fd(fd, name);
      errno = 0;
      volume_ret = ioctl(fd, SND_IOCTL_SET_VOLUME, &volume);
      volume_errno = errno;
      errno = 0;
      mute_ret = ioctl(fd, SND_IOCTL_SET_MUTE, 0);
      mute_errno = errno;
      errno = 0;
      start_ret = ioctl(fd, SND_IOCTL_START, 0);
      start_errno = errno;
      if (start_ret == 0) {
         for (unsigned i = 0; i < GB300_ROUTE_PROBE_XFERS; i++) {
            memset(&xfer, 0, sizeof(xfer));
            xfer.data = (void *)pcm;
            xfer.frames = GB300_ROUTE_PROBE_FRAMES;
            errno = 0;
            xfer_ret = ioctl(fd, SND_IOCTL_XFER, &xfer);
            xfer_errno = errno;
            if (xfer_ret < 0)
               break;
            xfer_count++;
         }
         errno = 0;
         delay_ret = ioctl(fd, SND_IOCTL_DELAY, &delay);
         delay_errno = errno;
         usleep(GB300_ROUTE_PROBE_ROUTE_PAUSE_US);
         errno = 0;
         drain_ret = ioctl(fd, SND_IOCTL_DRAIN, 0);
         drain_errno = errno;
      }
   }
   memset(&hw, 0, sizeof(hw));
   info_ret = ioctl(fd, SND_IOCTL_GET_HW_INFO, &hw);
   printf("unifrog audio gb300_probe route=%u kind=snd name=%s dev=%s fd=%d source=%lu dest=%lu flags=0x%x req_ch=%u req_period=%u req_periods=%u req_start=%u hw=%d hw_errno=%d avail=%d avail_errno=%d neutral=%d volume=%d volume_errno=%d mute=%d mute_errno=%d start=%d start_errno=%d xfer=%d xfer_errno=%d xfers=%u delay_ret=%d delay_errno=%d delay=%lu drain=%d drain_errno=%d info=%d dma=0x%08lx/%lu rate=%u ch=%u period=%lu periods=%lu\n",
      route, name, dev, fd, (unsigned long)source, (unsigned long)dest,
      open_flags, channels, period_bytes, periods, start_threshold,
      hw_ret, hw_errno, avail_ret, avail_errno, neutral_ret, volume_ret,
      volume_errno, mute_ret, mute_errno, start_ret, start_errno, xfer_ret,
      xfer_errno, xfer_count, delay_ret, delay_errno, (unsigned long)delay,
      drain_ret, drain_errno, info_ret,
      (unsigned long)hw.dma_addr, (unsigned long)hw.dma_size,
      hw.pcm_params.rate, hw.pcm_params.channels,
      (unsigned long)hw.pcm_params.period_size,
      (unsigned long)hw.pcm_params.periods);
   (void)ioctl(fd, SND_IOCTL_SET_MUTE, 1);
   (void)ioctl(fd, SND_IOCTL_DROP, 0);
   (void)ioctl(fd, SND_IOCTL_HW_FREE, 0);
   close(fd);
   return hw_ret == 0 && start_ret == 0 && xfer_count > 0 ? 0 : -1;
}

struct gb300_control_sweep_profile {
   const char *name;
   unsigned channels;
   unsigned volume;
   int neutral_controls;
   int system_unmute;
   int disable_driver_mute_pin;
   int fade_action;
   int l15_high;
   int r07_high;
};

static int gb300_route_probe_control_profile(unsigned route, unsigned index,
   const struct gb300_control_sweep_profile *profile)
{
   struct snd_pcm_params params;
   struct snd_xfer xfer;
   struct snd_hw_info hw;
   struct unifrog_audio diag_audio;
   struct pinmux_setting *saved_mute_pin;
   int saved_mute_polar;
   uint32_t saved_underrun_fade;
   snd_pcm_uframes_t avail_min = GB300_SND_PERIOD_BYTES;
   snd_pcm_uframes_t delay = 0;
   const int16_t *pcm;
   uint8_t volume;
   unsigned channels;
   int fd = -1;
   int open_errno = 0;
   int hw_ret = -1;
   int hw_errno = 0;
   int avail_ret = -1;
   int avail_errno = 0;
   int neutral_ret = -2;
   int sys_volume_ret = -2;
   int sys_mute_ret = -2;
   int volume_ret = -1;
   int volume_errno = 0;
   int mute_ret = -1;
   int mute_errno = 0;
   int start_ret = -1;
   int start_errno = 0;
   int xfer_ret = -1;
   int xfer_errno = 0;
   unsigned xfer_count = 0;
   int delay_ret = -1;
   int delay_errno = 0;
   int drain_ret = -1;
   int drain_errno = 0;
   int info_ret = -1;

   if (!profile || !i2so_platform_dev)
      return -1;
   channels = profile->channels;
   if (channels == 0 || channels > 2u)
      channels = GB300_ROUTE_PROBE_CHANNELS;
   volume = (uint8_t)(profile->volume > 100u ? 100u : profile->volume);
   pcm = channels == 1u ? gb300_route_probe_mono_pcm : gb300_route_probe_pcm;
   saved_mute_pin = I2SO_PLATFORM_PINMUX_MUTE;
   saved_mute_polar = I2SO_PLATFORM_U8(I2SO_PLATFORM_MUTE_POLAR_OFF) ? 1 : 0;
   saved_underrun_fade = SND0_BASE[SND0_UNDERRUN_FADE_REG];
   fill_gb300_route_probe_pcm(route);

   printf("unifrog audio gb300_sweep profile=%u route=%u name=%s begin ch=%u volume=%u neutral=%d system_unmute=%d disable_driver_mute_pin=%d fade_action=%d l15_high=%d r07_high=%d\n",
      index, route, profile->name, channels, (unsigned)volume,
      profile->neutral_controls, profile->system_unmute,
      profile->disable_driver_mute_pin, profile->fade_action,
      profile->l15_high, profile->r07_high);
   if (profile->disable_driver_mute_pin)
      I2SO_PLATFORM_PINMUX_MUTE = NULL;
   if (profile->fade_action == 0)
      SND0_BASE[SND0_UNDERRUN_FADE_REG] =
         saved_underrun_fade & ~SND0_UNDERRUN_FADE_BIT;
   else if (profile->fade_action > 0)
      SND0_BASE[SND0_UNDERRUN_FADE_REG] =
         saved_underrun_fade | SND0_UNDERRUN_FADE_BIT;
   gb300_i2so_platform_log("gb300_sweep_before_open");

   errno = 0;
   fd = open("/dev/sndC0i2so", O_RDWR);
   open_errno = errno;
   if (fd < 0)
      goto done;

   init_gb300_route_probe_pcm_params(&params, SND_PCM_SOURCE_AUDPAD);
   params.channels = channels;
   params.period_size = GB300_SND_PERIOD_BYTES;
   params.periods = GB300_SND_PERIODS;
   params.start_threshold = GB300_SND_START_THRESHOLD;
   params.pcm_dest = SND_PCM_DEST_DMA;
   errno = 0;
   hw_ret = ioctl(fd, SND_IOCTL_HW_PARAMS, &params);
   hw_errno = errno;
   if (hw_ret == 0) {
      errno = 0;
      avail_ret = ioctl(fd, SND_IOCTL_AVAIL_MIN, &avail_min);
      avail_errno = errno;
      if (profile->neutral_controls)
         neutral_ret = configure_neutral_audio_controls_fd(fd, profile->name);
      else
         printf("unifrog audio neutral_controls tag=%s fd=%d gb300=1 action=sweep_skip\n",
            profile->name, fd);
      if (profile->system_unmute) {
         sys_volume_ret = unifrog_audio_set_system_volume(volume);
         sys_mute_ret = unifrog_audio_set_system_mute(0);
      }
      errno = 0;
      volume_ret = ioctl(fd, SND_IOCTL_SET_VOLUME, &volume);
      volume_errno = errno;
      errno = 0;
      mute_ret = ioctl(fd, SND_IOCTL_SET_MUTE, 0);
      mute_errno = errno;
      errno = 0;
      start_ret = ioctl(fd, SND_IOCTL_START, 0);
      start_errno = errno;
      if (start_ret == 0) {
         memset(&diag_audio, 0, sizeof(diag_audio));
         diag_audio.fd = fd;
         diag_audio.backend = UNIFROG_AUDIO_BACKEND_SND;
         diag_audio.rate = GB300_ROUTE_PROBE_RATE;
         diag_audio.channels = channels;
         diag_audio.period_bytes = GB300_SND_PERIOD_BYTES;
         diag_audio.periods = GB300_SND_PERIODS;
         diag_audio.frame_bytes = channels * sizeof(int16_t);
         diag_audio.muted = 0;
         diag_audio.output_gate_enabled = 1;
         gb300_gate_probe_set_level(profile->name, profile->l15_high,
            profile->r07_high);
         unifrog_audio_debug_dump(&diag_audio, "gb300_sweep_after_start");
         for (unsigned i = 0; i < GB300_CONTROL_SWEEP_XFERS; i++) {
            memset(&xfer, 0, sizeof(xfer));
            xfer.data = (void *)pcm;
            xfer.frames = GB300_ROUTE_PROBE_FRAMES;
            errno = 0;
            xfer_ret = ioctl(fd, SND_IOCTL_XFER, &xfer);
            xfer_errno = errno;
            if (xfer_ret < 0)
               break;
            xfer_count++;
         }
         errno = 0;
         delay_ret = ioctl(fd, SND_IOCTL_DELAY, &delay);
         delay_errno = errno;
         usleep(GB300_CONTROL_SWEEP_PAUSE_US);
         errno = 0;
         drain_ret = ioctl(fd, SND_IOCTL_DRAIN, 0);
         drain_errno = errno;
      }
   }

done:
   memset(&hw, 0, sizeof(hw));
   if (fd >= 0)
      info_ret = ioctl(fd, SND_IOCTL_GET_HW_INFO, &hw);
   printf("unifrog audio gb300_sweep profile=%u route=%u name=%s fd=%d open_errno=%d hw=%d hw_errno=%d avail=%d avail_errno=%d neutral=%d sys_volume=%d sys_mute=%d volume=%d volume_errno=%d mute=%d mute_errno=%d start=%d start_errno=%d xfer=%d xfer_errno=%d xfers=%u delay_ret=%d delay_errno=%d delay=%lu drain=%d drain_errno=%d info=%d dma=0x%08lx/%lu rate=%u ch=%u period=%lu periods=%lu saved_fade=%08lx now_fade=%08lx saved_mute_pin=0x%08lx now_mute_pin=0x%08lx saved_polar=%u now_polar=%u\n",
      index, route, profile->name, fd, open_errno, hw_ret, hw_errno,
      avail_ret, avail_errno, neutral_ret, sys_volume_ret, sys_mute_ret,
      volume_ret, volume_errno, mute_ret, mute_errno, start_ret, start_errno,
      xfer_ret, xfer_errno, xfer_count, delay_ret, delay_errno,
      (unsigned long)delay, drain_ret, drain_errno, info_ret,
      (unsigned long)hw.dma_addr, (unsigned long)hw.dma_size,
      hw.pcm_params.rate, hw.pcm_params.channels,
      (unsigned long)hw.pcm_params.period_size,
      (unsigned long)hw.pcm_params.periods,
      (unsigned long)saved_underrun_fade,
      (unsigned long)SND0_BASE[SND0_UNDERRUN_FADE_REG],
      (unsigned long)saved_mute_pin,
      (unsigned long)I2SO_PLATFORM_PINMUX_MUTE,
      saved_mute_polar ? 1u : 0u,
      I2SO_PLATFORM_U8(I2SO_PLATFORM_MUTE_POLAR_OFF) ? 1u : 0u);
   if (fd >= 0) {
      (void)ioctl(fd, SND_IOCTL_SET_MUTE, 1);
      (void)ioctl(fd, SND_IOCTL_DROP, 0);
      (void)ioctl(fd, SND_IOCTL_HW_FREE, 0);
      close(fd);
   }
   I2SO_PLATFORM_PINMUX_MUTE = saved_mute_pin;
   I2SO_PLATFORM_U8(I2SO_PLATFORM_MUTE_POLAR_OFF) =
      (uint8_t)(saved_mute_polar ? 1 : 0);
   SND0_BASE[SND0_UNDERRUN_FADE_REG] = saved_underrun_fade;
   gb300_gate_probe_set_level("gb300_sweep_between", 1, 1);
   gb300_i2so_platform_log("gb300_sweep_restored");
   (void)unifrog_audio_set_system_mute(1);
   usleep(100000);
   return hw_ret == 0 && start_ret == 0 && xfer_count > 0 ? 0 : -1;
}

static unsigned gb300_route_probe_control_sweep(unsigned *route_io)
{
   static const struct gb300_control_sweep_profile profiles[] = {
      { "release_system90_neutral", 2, 90, 1, 1, 0, -1, 0, 0 },
      { "no_neutral_system90", 2, 90, 0, 1, 0, -1, 0, 0 },
      { "volume100_system_unmute", 2, 100, 1, 1, 0, -1, 0, 0 },
      { "mono_system90", 1, 90, 1, 1, 0, -1, 0, 0 },
      { "fade_clear_system90", 2, 90, 1, 1, 0, 0, 0, 0 },
      { "driver_mute_disabled_r07_low", 2, 90, 1, 1, 1, -1, 0, 0 },
      { "driver_mute_disabled_r07_high", 2, 90, 1, 1, 1, -1, 0, 1 },
   };
   unsigned route;
   unsigned ok = 0;

   if (!route_io)
      return 0;
   route = *route_io;
   printf("unifrog audio gb300_sweep begin profiles=%lu route_base=%u\n",
      (unsigned long)UNIFROG_AUDIO_ARRAY_SIZE(profiles), route);
   for (unsigned i = 0; i < UNIFROG_AUDIO_ARRAY_SIZE(profiles); i++) {
      if (gb300_route_probe_control_profile(route, i, &profiles[i]) == 0)
         ok++;
      route++;
   }
   *route_io = route;
   printf("unifrog audio gb300_sweep end ok=%u profiles=%lu route_next=%u\n",
      ok, (unsigned long)UNIFROG_AUDIO_ARRAY_SIZE(profiles), route);
   return ok;
}

static void gb300_route_probe_gate_matrix(unsigned route)
{
   struct snd_pcm_params params;
   struct snd_hw_info hw;
   snd_pcm_uframes_t avail_min = GB300_ROUTE_PROBE_PERIOD_BYTES;
   uint8_t volume = GB300_GATE_PROBE_VOLUME;
   int fd;
   int hw_ret = -1;
   int avail_ret = -1;
   int volume_ret = -1;
   int mute_ret = -1;
   int start_ret = -1;
   static const struct {
      const char *name;
      int l15_high;
      int r07_high;
   } stages[] = {
      { "both_low", 0, 0 },
      { "l15_low_r07_high", 0, 1 },
      { "l15_high_r07_low", 1, 0 },
      { "both_high", 1, 1 },
   };

   fd = open("/dev/sndC0i2so", O_RDWR);
   if (fd < 0) {
      printf("unifrog audio gb300_gate_probe open=-1 errno=%d route=%u\n",
         errno, route);
      return;
   }

   init_gb300_route_probe_pcm_params(&params, SND_PCM_SOURCE_AUDPAD);
   params.period_size = GB300_SND_PERIOD_BYTES;
   params.periods = GB300_SND_PERIODS;
   params.start_threshold = GB300_SND_START_THRESHOLD;
   errno = 0;
   hw_ret = ioctl(fd, SND_IOCTL_HW_PARAMS, &params);
   if (hw_ret == 0) {
      avail_ret = ioctl(fd, SND_IOCTL_AVAIL_MIN, &avail_min);
      configure_neutral_audio_controls_fd(fd, "gb300_gate_probe");
      volume_ret = ioctl(fd, SND_IOCTL_SET_VOLUME, &volume);
      mute_ret = ioctl(fd, SND_IOCTL_SET_MUTE, 0);
      start_ret = ioctl(fd, SND_IOCTL_START, 0);
   }
   memset(&hw, 0, sizeof(hw));
   (void)ioctl(fd, SND_IOCTL_GET_HW_INFO, &hw);
   printf("unifrog audio gb300_gate_probe begin route=%u fd=%d hw=%d avail=%d volume=%d mute=%d start=%d dma=0x%08lx/%lu rate=%u ch=%u period=%lu periods=%lu\n",
      route, fd, hw_ret, avail_ret, volume_ret, mute_ret, start_ret,
      (unsigned long)hw.dma_addr, (unsigned long)hw.dma_size,
      hw.pcm_params.rate, hw.pcm_params.channels,
      (unsigned long)hw.pcm_params.period_size,
      (unsigned long)hw.pcm_params.periods);
   if (hw_ret == 0 && start_ret == 0) {
      for (unsigned i = 0; i < UNIFROG_AUDIO_ARRAY_SIZE(stages); i++) {
         int last_ret = -1;
         int last_errno = 0;
         snd_pcm_uframes_t delay = 0;
         int delay_ret = -1;

         fill_gb300_route_probe_pcm(route + i);
         gb300_gate_probe_set_level(stages[i].name, stages[i].l15_high,
            stages[i].r07_high);
         for (unsigned j = 0; j < GB300_GATE_PROBE_STAGE_XFERS; j++) {
            struct snd_xfer xfer;

            memset(&xfer, 0, sizeof(xfer));
            xfer.data = gb300_route_probe_pcm;
            xfer.frames = GB300_ROUTE_PROBE_FRAMES;
            errno = 0;
            last_ret = ioctl(fd, SND_IOCTL_XFER, &xfer);
            last_errno = errno;
            if (last_ret < 0)
               usleep(2000);
         }
         delay_ret = ioctl(fd, SND_IOCTL_DELAY, &delay);
         printf("unifrog audio gb300_gate_probe stage=%u name=%s l15_high=%d r07_high=%d xfers=%u last=%d last_errno=%d delay_ret=%d delay=%lu\n",
            i, stages[i].name, stages[i].l15_high, stages[i].r07_high,
            GB300_GATE_PROBE_STAGE_XFERS, last_ret, last_errno, delay_ret,
            (unsigned long)delay);
         usleep(GB300_GATE_PROBE_STAGE_PAUSE_US);
         (void)ioctl(fd, SND_IOCTL_DRAIN, 0);
         gb300_gate_probe_set_level("between", 1, 1);
         usleep(80000);
      }
   }
   (void)ioctl(fd, SND_IOCTL_SET_MUTE, 1);
   (void)ioctl(fd, SND_IOCTL_DROP, 0);
   (void)ioctl(fd, SND_IOCTL_HW_FREE, 0);
   close(fd);
   set_stock_audio_output_gate(0);
   printf("unifrog audio gb300_gate_probe end route=%u\n", route);
}

int unifrog_audio_run_gb300_route_probe(const char *tag)
{
   static int running;
   static const struct {
      const char *name;
      uint32_t snd_devs;
      int duplicate;
   } audsink_routes[] = {
      { "audsink_i2so_spo", AUDSINK_SND_DEVBIT_I2SO | AUDSINK_SND_DEVBIT_SPO,
         AUDSINK_PCM_DUPLICATE_STEREO },
      { "audsink_spo", AUDSINK_SND_DEVBIT_SPO, AUDSINK_PCM_DUPLICATE_STEREO },
      { "audsink_i2so", AUDSINK_SND_DEVBIT_I2SO,
         AUDSINK_PCM_DUPLICATE_STEREO },
      { "audsink_pcmo", AUDSINK_SND_DEVBIT_PCMO,
         AUDSINK_PCM_DUPLICATE_STEREO },
      { "audsink_i2so_pcmo", AUDSINK_SND_DEVBIT_I2SO |
         AUDSINK_SND_DEVBIT_PCMO, AUDSINK_PCM_DUPLICATE_STEREO },
   };
   static const struct {
      const char *name;
      const char *dev;
      snd_pcm_source_t source;
      snd_pcm_dest_t dest;
      unsigned channels;
      int open_flags;
      unsigned period_bytes;
      unsigned periods;
      unsigned start_threshold;
   } snd_routes[] = {
      { "snd_i2so_audpad_dma", "/dev/sndC0i2so", SND_PCM_SOURCE_AUDPAD,
         SND_PCM_DEST_DMA, 2, O_RDWR, GB300_ROUTE_PROBE_PERIOD_BYTES,
         GB300_ROUTE_PROBE_PERIODS, 0 },
      { "snd_i2so_audpad_bypass", "/dev/sndC0i2so", SND_PCM_SOURCE_AUDPAD,
         SND_PCM_DEST_BYPASS, 2, O_RDWR, GB300_ROUTE_PROBE_PERIOD_BYTES,
         GB300_ROUTE_PROBE_PERIODS, 0 },
      { "snd_i2so_legacy_stereo", "/dev/sndC0i2so", SND_PCM_SOURCE_AUDPAD,
         SND_PCM_DEST_DMA, 2, O_WRONLY, GB300_ROUTE_PROBE_PERIOD_BYTES,
         GB300_ROUTE_PROBE_PERIODS, 0 },
      { "snd_i2so_legacy_mono", "/dev/sndC0i2so", SND_PCM_SOURCE_AUDPAD,
         SND_PCM_DEST_DMA, 1, O_WRONLY, GB300_ROUTE_PROBE_PERIOD_BYTES,
         GB300_ROUTE_PROBE_PERIODS, 0 },
      { "snd_i2so_runtime_mono", "/dev/sndC0i2so", SND_PCM_SOURCE_AUDPAD,
         SND_PCM_DEST_DMA, 1, O_RDWR, GB300_SND_PERIOD_BYTES,
         GB300_SND_PERIODS, GB300_SND_START_THRESHOLD },
      { "snd_spo_i2sodma_dma", "/dev/sndC0spo", SND_SPO_SOURCE_I2SODMA,
         SND_PCM_DEST_DMA, 2, O_RDWR, GB300_ROUTE_PROBE_PERIOD_BYTES,
         GB300_ROUTE_PROBE_PERIODS, 0 },
      { "snd_spo_i2sodma_bypass", "/dev/sndC0spo", SND_SPO_SOURCE_I2SODMA,
         SND_PCM_DEST_BYPASS, 2, O_RDWR, GB300_ROUTE_PROBE_PERIOD_BYTES,
         GB300_ROUTE_PROBE_PERIODS, 0 },
      { "snd_spo_spodma_dma", "/dev/sndC0spo", SND_SPO_SOURCE_SPODMA,
         SND_PCM_DEST_DMA, 2, O_RDWR, GB300_ROUTE_PROBE_PERIOD_BYTES,
         GB300_ROUTE_PROBE_PERIODS, 0 },
      { "snd_pcmo_audpad_dma", "/dev/sndC0pcmo", SND_PCM_SOURCE_AUDPAD,
         SND_PCM_DEST_DMA, 2, O_RDWR, GB300_ROUTE_PROBE_PERIOD_BYTES,
         GB300_ROUTE_PROBE_PERIODS, 0 },
      { "snd_pcmo_audpad_bypass", "/dev/sndC0pcmo", SND_PCM_SOURCE_AUDPAD,
         SND_PCM_DEST_BYPASS, 2, O_RDWR, GB300_ROUTE_PROBE_PERIOD_BYTES,
         GB300_ROUTE_PROBE_PERIODS, 0 },
   };
   unsigned route = 0;
   unsigned ok = 0;

   if (!unifrog_audio_prefers_stereo_output() || running)
      return -1;
   running = 1;
   (void)ensure_audio_drivers();
   printf("unifrog audio gb300_probe begin tag=%s audsink_routes=%lu snd_routes=%lu rate=%u frames=%u xfers=%u gate=%s\n",
      tag ? tag : "?", (unsigned long)UNIFROG_AUDIO_ARRAY_SIZE(audsink_routes),
      (unsigned long)UNIFROG_AUDIO_ARRAY_SIZE(snd_routes),
      GB300_ROUTE_PROBE_RATE, GB300_ROUTE_PROBE_FRAMES,
      GB300_ROUTE_PROBE_XFERS,
      audio_gate_name(current_audio_gate()));
   unifrog_audio_debug_dump(NULL, "gb300_probe_begin");
   ok += gb300_route_probe_control_sweep(&route);

   for (unsigned i = 0; i < UNIFROG_AUDIO_ARRAY_SIZE(audsink_routes); i++) {
      fill_gb300_route_probe_pcm(route);
      unifrog_audio_set_system_output_enabled(1);
      if (gb300_route_probe_audsink(route, audsink_routes[i].name,
          audsink_routes[i].snd_devs, audsink_routes[i].duplicate) == 0)
         ok++;
      unifrog_audio_set_system_output_enabled(0);
      usleep(80000);
      route++;
   }

   for (unsigned i = 0; i < UNIFROG_AUDIO_ARRAY_SIZE(snd_routes); i++) {
      fill_gb300_route_probe_pcm(route);
      unifrog_audio_set_system_output_enabled(1);
      if (gb300_route_probe_snd(route, snd_routes[i].name, snd_routes[i].dev,
          snd_routes[i].source, snd_routes[i].dest, snd_routes[i].channels,
          snd_routes[i].open_flags, snd_routes[i].period_bytes,
          snd_routes[i].periods, snd_routes[i].start_threshold) == 0)
         ok++;
      unifrog_audio_set_system_output_enabled(0);
      usleep(80000);
      route++;
   }

   gb300_route_probe_gate_matrix(route);
   route++;

   unifrog_audio_debug_dump(NULL, "gb300_probe_end");
   printf("unifrog audio gb300_probe end tag=%s ok=%u routes=%u\n",
      tag ? tag : "?", ok, route);
   running = 0;
   return ok > 0 ? 0 : -1;
}

void unifrog_audio_run_gb300_route_probe_once(const char *tag)
{
   static int done;

   if (!UNIFROG_AUDIO_GB300_ROUTE_PROBE_ONCE || done)
      return;
   done = 1;
   (void)unifrog_audio_run_gb300_route_probe(tag);
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
   int stock_bits = unifrog_input_uses_stock_bits ?
      unifrog_input_uses_stock_bits() : 0;
   int hw_ret = -1;

   memset(&hw, 0, sizeof(hw));
   if (audio && audio->fd >= 0 &&
       audio->backend == UNIFROG_AUDIO_BACKEND_SND)
      hw_ret = ioctl(audio->fd, SND_IOCTL_GET_HW_INFO, &hw);

   unifrog_audio_debug_gate(&l_dir, &l_out, &r_dir, &r_out);
   printf("unifrog audio diag tag=%s lcd=0x%06lx stock_bits=%d preferred_gate=%s output_ch=%u gate_enabled=%d audio_muted=%d audio_gate=%d audio_pending=%d l=0x%08lx/0x%08lx r=0x%08lx/0x%08lx\n",
      tag ? tag : "?",
      lcd_id, stock_bits, audio_gate_name(current_audio_gate()),
      unifrog_audio_output_channels(),
      stock_audio_output_gate_enabled, audio ? audio->muted : -1,
      audio ? audio->output_gate_enabled : -1,
      audio ? audio->output_gate_pending_signal : -1,
      (unsigned long)l_dir, (unsigned long)l_out,
      (unsigned long)r_dir, (unsigned long)r_out);
   printf("unifrog audio mux tag=%s l15=%u l22=%u l23=%u l24=%u l25=%u l26=%u l27=%u l28=%u l29=%u r07=%u\n",
      tag ? tag : "?",
      read_pinmux(PINPAD_L15),
      read_pinmux(PINPAD_L22), read_pinmux(PINPAD_L23),
      read_pinmux(PINPAD_L24), read_pinmux(PINPAD_L25),
      read_pinmux(PINPAD_L26), read_pinmux(PINPAD_L27),
      read_pinmux(PINPAD_L28), read_pinmux(PINPAD_L29),
      read_pinmux(PINPAD_R07));
   {
      unsigned backlight = 0;
      int backlight_ret = unifrog_backlight_get(&backlight);

      printf("unifrog audio pwm tag=%s r05_mux=%u backlight_ret=%d backlight=%u pwmctrl=%08lx %08lx %08lx %08lx %08lx %08lx\n",
         tag ? tag : "?",
         read_pinmux(PINPAD_R05), backlight_ret, backlight,
         (unsigned long)PWMCTRL_BASE[0], (unsigned long)PWMCTRL_BASE[1],
         (unsigned long)PWMCTRL_BASE[2], (unsigned long)PWMCTRL_BASE[3],
         (unsigned long)PWMCTRL_BASE[4], (unsigned long)PWMCTRL_BASE[5]);
   }
   printf("unifrog audio hw tag=%s backend=%d snd0=0x%08lx dac=0x%08lx hw_ret=%d dma=0x%08lx/%lu hw_rate=%u hw_ch=%u hw_fmt=%u hw_period=%lu hw_periods=%lu\n",
      tag ? tag : "?",
      audio ? audio->backend : 0,
      (unsigned long)SND0_BASE[0],
      (unsigned long)SND0_DAC_BASE[0],
      hw_ret,
      (unsigned long)hw.dma_addr, (unsigned long)hw.dma_size,
      hw.pcm_params.rate, hw.pcm_params.channels,
      (unsigned)hw.pcm_params.format,
      (unsigned long)hw.pcm_params.period_size,
      (unsigned long)hw.pcm_params.periods);
   printf("unifrog audio i2s tag=%s ctrl3c=0x%08lx underrun_fade=%u fade90=0x%08lx dac0=0x%08lx dac1=0x%08lx\n",
      tag ? tag : "?",
      (unsigned long)SND0_BASE[SND0_UNDERRUN_FADE_REG],
      (SND0_BASE[SND0_UNDERRUN_FADE_REG] & SND0_UNDERRUN_FADE_BIT) ? 1u : 0u,
      (unsigned long)SND0_BASE[SND0_FADE_REG],
      (unsigned long)SND0_DAC_BASE[0],
      (unsigned long)SND0_DAC_BASE[1]);
   gb300_i2so_platform_log(tag ? tag : "debug_dump");
}
