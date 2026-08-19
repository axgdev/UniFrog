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
#include <unifrog/device.h>
#include <unifrog/log.h>

#define printf unifrog_log

#define GPIO_L_OUTPUT ((volatile uint32_t *)0xb8800054u)
#define GPIO_L_DIR ((volatile uint32_t *)0xb8800058u)
#define GPIO_R_OUTPUT ((volatile uint32_t *)0xb88000f4u)
#define GPIO_R_DIR ((volatile uint32_t *)0xb88000f8u)
#define GPIO_L15_MASK (1u << 15)
#define GPIO_R07_MASK (1u << 7)
#define SYSTEM_AUDIO_VOLUME 65u
#define GB300_SYSTEM_AUDIO_VOLUME 75u
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
#define GB300_SND_MIN_WRITE_ATTEMPTS 100u
#define GB300_SND_TRANSIENT_LOG_LIMIT 12u
/* GB300 diagnostics produce audible output with multi-period bursts, while
 * media and cores feed 512-frame chunks. Coalesce small runtime writes to the
 * burst size that the direct SND path is known to accept. */
#define GB300_SND_COALESCE_FLUSH_FRAMES 2048u
#define GB300_SND_COALESCE_CAP_FRAMES 8192u
#define GB300_SND_COALESCE_MAX_CHANNELS 2u
/* Keep the mute path open across about 100ms of silence so short PCM gaps do
 * not cause repeated mute/unmute transitions. Bounds cover the supported
 * 8-48kHz range while keeping malformed rates from extending the hold. */
#define UNIFROG_AUDIO_SILENCE_HOLD_MIN_FRAMES 800u
#define UNIFROG_AUDIO_SILENCE_HOLD_MAX_FRAMES 4800u
#define UNIFROG_AUDIO_SILENCE_HOLD_RATE_DIVISOR 10u
/* A reopened SND path also needs a one-shot settle window so the first buffer
 * does not race mute/GPIO/DAC unwind. Keep it short and board-specific. */
#define UNIFROG_AUDIO_SF2000_REOPEN_SETTLE_US 8000u
#define UNIFROG_AUDIO_GB300_REOPEN_SETTLE_US 12000u
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
extern unsigned long PINMUXL;
extern unsigned long PINMUXB;
extern unsigned long PINMUXR;
extern unsigned long PINMUXT;
extern unsigned long SND0;
extern unsigned long SND0_DAC;
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
static int gb300_snd_coalesce_fd = -1;
static unsigned gb300_snd_coalesce_channels;
static unsigned gb300_snd_coalesce_frames;
static int gb300_snd_coalesce_seen_signal;
static int gb300_snd_coalesce_has_signal;
static int16_t gb300_snd_coalesce_pcm[
   GB300_SND_COALESCE_CAP_FRAMES * GB300_SND_COALESCE_MAX_CHANNELS];
static unsigned gb300_snd_coalesce_log_count;

int current_audio_uses_gb300_gate(void);
unsigned read_pinmux(pinpad_e pin);

static void clear_audio(struct unifrog_audio *audio)
{
   if (audio) {
      memset(audio, 0, sizeof(*audio));
      audio->fd = -1;
      audio->backend = UNIFROG_AUDIO_BACKEND_AUTO;
      audio->muted = 1;
      audio->output_gate_enabled = 0;
      audio->output_gate_pending_signal = 0;
      audio->output_gate_silence_frames = 0;
      audio->output_gate_settle_pending = 0;
   }
}

enum audio_gate current_audio_gate(void)
{
   if (unifrog_device_uses_gb300_quirks())
      return AUDIO_GATE_GB300_L15;

   return AUDIO_GATE_SF2000_R07;
}

const char *audio_gate_name(enum audio_gate gate)
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

void gb300_i2so_platform_log(const char *tag)
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

static void apply_gb300_i2so_platform_quirks(const char *tag)
{
   static int applied;
   struct pinmux_setting *old_mute;
   unsigned old_fade_en;
   unsigned old_fade_step;
   unsigned old_volume;

   if (!current_audio_uses_gb300_gate() || !i2so_platform_dev)
      return;

   old_mute = I2SO_PLATFORM_PINMUX_MUTE;
   old_fade_en = (unsigned)I2SO_PLATFORM_U8(I2SO_PLATFORM_FADE_EN_OFF);
   old_fade_step = (unsigned)I2SO_PLATFORM_U8(I2SO_PLATFORM_FADE_STEP_OFF);
   old_volume = (unsigned)I2SO_PLATFORM_U8(I2SO_PLATFORM_VOLUME_OFF);

   /*
    * The shared DTS currently describes the SF2000 R07 mute pin and fade
    * policy. Known-working GB300 builds predate those i2so_platform fields,
    * and GB300 has its own L15 physical gate instead.
    */
   I2SO_PLATFORM_PINMUX_MUTE = NULL;
   I2SO_PLATFORM_U8(I2SO_PLATFORM_FADE_EN_OFF) = 0;
   I2SO_PLATFORM_U8(I2SO_PLATFORM_FADE_STEP_OFF) = 0xff;

   if (!applied || old_mute || old_fade_en || old_fade_step != 0xffu) {
      printf("unifrog audio gb300_i2so_quirk tag=%s mute=0x%08lx->0x%08lx fade_en=%u->%u fade_step=%u->%u vol=%u\n",
         tag ? tag : "?",
         (unsigned long)old_mute,
         (unsigned long)I2SO_PLATFORM_PINMUX_MUTE,
         old_fade_en,
         (unsigned)I2SO_PLATFORM_U8(I2SO_PLATFORM_FADE_EN_OFF),
         old_fade_step,
         (unsigned)I2SO_PLATFORM_U8(I2SO_PLATFORM_FADE_STEP_OFF),
         old_volume);
   }
   applied = 1;
}

int current_audio_uses_gb300_gate(void)
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
    * GB300 has one speaker, but the stock/HCRTOS path initializes the hardware
    * for stereo s16 frames. Callers mono-mix the content, then duplicate that
    * mono sample into both hardware slots.
    */
   if (current_audio_uses_gb300_gate())
      return 2u;
   return 1u;
}

static uint32_t current_audio_snd_devs(void)
{
   if (current_audio_gate() == AUDIO_GATE_GB300_L15)
      return AUDSINK_SND_DEVBIT_I2SO;
   return AUDSINK_SND_DEVBIT_I2SO;
}

unsigned system_audio_volume(void)
{
   return current_audio_uses_gb300_gate() ?
      GB300_SYSTEM_AUDIO_VOLUME : SYSTEM_AUDIO_VOLUME;
}

static void gb300_snd_coalesce_begin(int fd, unsigned channels)
{
   gb300_snd_coalesce_fd = fd;
   gb300_snd_coalesce_channels = channels;
   gb300_snd_coalesce_frames = 0;
   gb300_snd_coalesce_seen_signal = 0;
   gb300_snd_coalesce_has_signal = 0;
}

static void gb300_snd_coalesce_reset_fd(int fd)
{
   if (fd >= 0 && fd != gb300_snd_coalesce_fd)
      return;
   gb300_snd_coalesce_fd = -1;
   gb300_snd_coalesce_channels = 0;
   gb300_snd_coalesce_frames = 0;
   gb300_snd_coalesce_seen_signal = 0;
   gb300_snd_coalesce_has_signal = 0;
}

static unsigned audio_silence_hold_frames(const struct unifrog_audio *audio)
{
   unsigned hold_frames = UNIFROG_AUDIO_SILENCE_HOLD_MIN_FRAMES;

   /* This layer only holds the mute state open across brief PCM gaps.
    * Higher layers can still close the physical output gate after sustained
    * silence; this driver never rewrites or zeros caller PCM. */
   if (audio && audio->rate) {
      unsigned rate_frames = audio->rate /
         UNIFROG_AUDIO_SILENCE_HOLD_RATE_DIVISOR;

      if (rate_frames > hold_frames)
         hold_frames = rate_frames;
   }
   if (hold_frames > UNIFROG_AUDIO_SILENCE_HOLD_MAX_FRAMES)
      hold_frames = UNIFROG_AUDIO_SILENCE_HOLD_MAX_FRAMES;
   return hold_frames;
}

static void audio_reset_silence_state(struct unifrog_audio *audio)
{
   if (!audio)
      return;
   audio->output_gate_pending_signal = 0;
   audio->output_gate_silence_frames = 0;
   audio->output_gate_settle_pending = 0;
}

static void audio_apply_reopen_settle_delay(struct unifrog_audio *audio)
{
   unsigned settle_delay_us;

   if (!audio || audio->backend != UNIFROG_AUDIO_BACKEND_SND ||
       !audio->output_gate_settle_pending)
      return;

   /* Only the first post-unmute buffer waits. Once the DAC and GPIO have had
    * a short board-specific settle window, steady-state writes flow normally. */
   settle_delay_us = current_audio_uses_gb300_gate() ?
      UNIFROG_AUDIO_GB300_REOPEN_SETTLE_US :
      UNIFROG_AUDIO_SF2000_REOPEN_SETTLE_US;
   audio->output_gate_settle_pending = 0;
   if (settle_delay_us > 0)
      usleep(settle_delay_us);
}

static void audio_scan_signal(const int16_t *samples, unsigned frames,
   unsigned channels, int *has_signal, unsigned *signal_nonzero,
   unsigned *signal_abs_max, int *first_sample, int *last_sample)
{
   unsigned total;

   if (has_signal)
      *has_signal = 0;
   if (signal_nonzero)
      *signal_nonzero = 0;
   if (signal_abs_max)
      *signal_abs_max = 0;
   if (first_sample)
      *first_sample = 0;
   if (last_sample)
      *last_sample = 0;
   if (!samples || frames == 0 || channels == 0 ||
       frames > UINT32_MAX / channels)
      return;

   total = frames * channels;
   if (first_sample)
      *first_sample = samples[0];
   if (last_sample)
      *last_sample = samples[total - 1u];
   for (unsigned i = 0; i < total; i++) {
      int sample = samples[i];
      unsigned abs_value;

      if (sample < 0)
         sample = -sample;
      abs_value = (unsigned)sample;
      if (abs_value > 4u) {
         if (has_signal)
            *has_signal = 1;
         if (signal_nonzero)
            (*signal_nonzero)++;
      }
      if (signal_abs_max && abs_value > *signal_abs_max)
         *signal_abs_max = abs_value;
   }
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

void set_reg_level(volatile uint32_t *dir, volatile uint32_t *out,
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

void set_stock_audio_output_gate(int enabled)
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

int ensure_audio_drivers(void)
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

   if (initialized) {
      apply_gb300_i2so_platform_quirks("drivers_cached");
      return result;
   }

   initialized = 1;
   for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
      int ret = module_init(modules[i]);

      printf("unifrog audio lazy_module name=%s ret=%d\n", modules[i], ret);
      if (ret != 0 && result == 0)
         result = ret;
   }
   apply_gb300_i2so_platform_quirks("drivers");
   apply_stock_audio_silence_policy("drivers");
   return result;
}

void unifrog_audio_prepare_output_route(void)
{
   (void)ensure_audio_drivers();
}

static int open_system_snd(void)
{
   /*
    * System volume/mute only need control ioctls. Keep bidirectional DMA
    * ownership on the playback paths that intentionally open the SND node.
    */
   return open("/dev/sndC0i2so", O_WRONLY);
}

int configure_neutral_audio_controls_fd(int fd, const char *tag)
{
   struct snd_twotone twotone;
   struct snd_audio_eq6 eq6;
   struct snd_lr_balance balance;
   int twotone_ret;
   int eq_ret;
   int balance_ret;
   static unsigned log_count;
   static unsigned gb300_log_count;

   if (fd < 0)
      return -1;
   if (current_audio_uses_gb300_gate()) {
      if (gb300_log_count < 12) {
         gb300_log_count++;
         printf("unifrog audio neutral_controls tag=%s action=gb300_skip fd=%d\n",
            tag ? tag : "?", fd);
      }
      return 0;
   }
   memset(&twotone, 0, sizeof(twotone));
   memset(&eq6, 0, sizeof(eq6));
   memset(&balance, 0, sizeof(balance));
   twotone.tt_mode = SND_TWOTONE_MODE_USER;
   balance.lr_balance_index = 0;
   eq6.mode = SND_EQ6_MODE_OFF;
   twotone_ret = ioctl(fd, SND_IOCTL_SET_TWOTONE, &twotone);
   eq_ret = ioctl(fd, SND_IOCTL_SET_EQ6, &eq6);
   balance_ret = ioctl(fd, SND_IOCTL_SET_LR_BALANCE, &balance);
   if (log_count < 12 || twotone_ret != 0 || eq_ret != 0 ||
       balance_ret != 0) {
      log_count++;
      printf("unifrog audio neutral_controls tag=%s fd=%d twotone=%d eq6=%d balance=%d\n",
         tag ? tag : "?", fd, twotone_ret, eq_ret, balance_ret);
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
   audio_reset_silence_state(audio);
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

   /*
    * 0142 and 0147 both showed the newer GB300 AUDPAD/O_RDWR direct route can
    * accept nonzero DMA writes while staying inaudible. Keep direct SND as a
    * fallback, but use the simpler v0.4.4-style parameters when release audio
    * lands here.
    */

   fd = open("/dev/sndC0i2so", gb300_route ? O_WRONLY : O_RDWR);
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
   if (!gb300_route)
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
   audio_reset_silence_state(audio);
   if (gb300_route)
      gb300_snd_coalesce_begin(fd, channels);
   memset(&hw, 0, sizeof(hw));
   (void)ioctl(fd, SND_IOCTL_GET_HW_INFO, &hw);
   printf("unifrog audio open backend=snd fd=%d rate=%u ch=%u req_period=%u req_periods=%u period=%u periods=%u start_threshold=%u avail_min=%lu avail_ret=%d neutral=%d route=%s output_ch=%u mode=%s dma=0x%08lx/%lu hw_period=%lu hw_periods=%lu\n",
      fd, rate, channels, requested_period, requested_periods,
      period_bytes, periods, start_threshold, (unsigned long)avail_min,
      avail_ret, neutral_ret,
      audio_gate_name(current_audio_gate()),
      unifrog_audio_output_channels(),
      gb300_route ? "gb300_legacy" : "audpad",
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
      gb300_route ? "gb300_legacy" : "audpad");
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
      if (open_audsink(audio, rate, channels, period_bytes, periods) == 0)
         return 0;
      return open_snd(audio, rate, channels, period_bytes, periods);
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
      gb300_snd_coalesce_reset_fd(audio->fd);
      close(audio->fd);
      (void)unifrog_audio_set_system_mute(1);
      set_stock_audio_output_gate(0);
   }
   audio_reset_silence_state(audio);
   clear_audio(audio);
}

int unifrog_audio_start(struct unifrog_audio *audio)
{
   if (!audio || audio->fd < 0)
      return -1;
   audio_reset_silence_state(audio);
   apply_stock_audio_silence_policy("start");
   if (audio->backend == UNIFROG_AUDIO_BACKEND_AUDSINK)
      return ioctl(audio->fd, AUDSINK_IOCTL_START, 0);
   return ioctl(audio->fd, SND_IOCTL_START, 0);
}

int unifrog_audio_drop(struct unifrog_audio *audio)
{
   if (!audio || audio->fd < 0)
      return -1;
   gb300_snd_coalesce_reset_fd(audio->fd);
   audio_reset_silence_state(audio);
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
   static unsigned transient_log_count;
   int gb300_snd = audio && audio->backend == UNIFROG_AUDIO_BACKEND_SND &&
      unifrog_audio_prefers_stereo_output();
   int gb300_coalesced_flush = 0;
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
      unsigned input_frames = frames;

      audio_scan_signal(samples, frames, channels, &has_signal,
         &signal_nonzero, &signal_abs_max, &first_sample, &last_sample);
      if (gb300_snd) {
         if (channels <= GB300_SND_COALESCE_MAX_CHANNELS) {
            if (gb300_snd_coalesce_fd != audio->fd ||
                gb300_snd_coalesce_channels != channels)
               gb300_snd_coalesce_begin(audio->fd, channels);
            if (!has_signal && !gb300_snd_coalesce_seen_signal) {
               if (gb300_snd_coalesce_log_count < 16) {
                  gb300_snd_coalesce_log_count++;
                  printf("unifrog audio gb300_coalesce action=skip_initial_silence fd=%d frames=%u ch=%u abs_max=%u buffered=%u\n",
                     audio->fd, frames, channels, signal_abs_max,
                     gb300_snd_coalesce_frames);
               }
               return 0;
            }
            if (has_signal)
               gb300_snd_coalesce_seen_signal = 1;
            if (gb300_snd_coalesce_seen_signal &&
                (frames < GB300_SND_COALESCE_FLUSH_FRAMES ||
                 gb300_snd_coalesce_frames > 0)) {
               unsigned total_frames = gb300_snd_coalesce_frames + frames;

               if (frames <= GB300_SND_COALESCE_CAP_FRAMES &&
                   total_frames <= GB300_SND_COALESCE_CAP_FRAMES) {
                  unsigned copy_samples = frames * channels;

                  memcpy(gb300_snd_coalesce_pcm +
                        gb300_snd_coalesce_frames * channels,
                     samples, copy_samples * sizeof(samples[0]));
                  gb300_snd_coalesce_frames = total_frames;
                  if (has_signal)
                     gb300_snd_coalesce_has_signal = 1;
                  if (gb300_snd_coalesce_frames <
                      GB300_SND_COALESCE_FLUSH_FRAMES) {
                     if (gb300_snd_coalesce_log_count < 16) {
                        gb300_snd_coalesce_log_count++;
                        printf("unifrog audio gb300_coalesce action=buffer fd=%d in_frames=%u buffered=%u ch=%u signal=%d nonzero=%u abs_max=%u\n",
                           audio->fd, input_frames,
                           gb300_snd_coalesce_frames, channels, has_signal,
                           signal_nonzero, signal_abs_max);
                     }
                     return 0;
                  }
                  samples = gb300_snd_coalesce_pcm;
                  frames = gb300_snd_coalesce_frames;
                  has_signal = gb300_snd_coalesce_has_signal;
                  audio_scan_signal(samples, frames, channels, &has_signal,
                     &signal_nonzero, &signal_abs_max, &first_sample,
                     &last_sample);
                  gb300_coalesced_flush = 1;
                  if (gb300_snd_coalesce_log_count < 16) {
                     gb300_snd_coalesce_log_count++;
                     printf("unifrog audio gb300_coalesce action=flush fd=%d in_frames=%u out_frames=%u ch=%u signal=%d nonzero=%u abs_max=%u\n",
                        audio->fd, input_frames, frames, channels,
                        has_signal, signal_nonzero, signal_abs_max);
                  }
               } else {
                  if (gb300_snd_coalesce_log_count < 16) {
                     gb300_snd_coalesce_log_count++;
                     printf("unifrog audio gb300_coalesce action=overflow_reset fd=%d in_frames=%u buffered=%u ch=%u\n",
                        audio->fd, input_frames, gb300_snd_coalesce_frames,
                        channels);
                  }
                  gb300_snd_coalesce_frames = 0;
                  gb300_snd_coalesce_has_signal = 0;
               }
            }
         }
         if (has_signal) {
            audio_reset_silence_state(audio);
            if (gb300_snd && (!audio->output_gate_enabled || audio->muted)) {
               int enable_ret = unifrog_audio_set_output_enabled(audio, 1);

               if (mute_transition_log_count < 12) {
                  mute_transition_log_count++;
                  printf("unifrog audio signal_gate backend=snd fd=%d action=gb300_enable_on_signal ret=%d nonzero=%u abs_max=%u frames=%u ch=%u\n",
                     audio->fd, enable_ret, signal_nonzero, signal_abs_max,
                     frames, channels);
               }
               if (enable_ret == 0)
                  audio->output_gate_settle_pending = 1;
            } else if (audio->muted) {
               int mute_ret = unifrog_audio_set_mute(audio, 0);

               if (mute_transition_log_count < 12) {
                  mute_transition_log_count++;
                  printf("unifrog audio signal_gate backend=snd fd=%d action=unmute ret=%d frames=%u ch=%u\n",
                     audio->fd, mute_ret, frames, channels);
               }
               if (mute_ret == 0)
                  audio->output_gate_settle_pending = 1;
            }
         } else if (!audio->muted) {
            unsigned silence_hold_frames = audio_silence_hold_frames(audio);
            unsigned silence_frames = audio->output_gate_silence_frames;

            if (UINT32_MAX - silence_frames < frames)
               silence_frames = UINT32_MAX;
            else
               silence_frames += frames;

            if (silence_frames < silence_hold_frames) {
               audio->output_gate_silence_frames = silence_frames;
               audio->output_gate_pending_signal = 1;
               if (mute_transition_log_count < 12) {
                  mute_transition_log_count++;
                  printf("unifrog audio signal_gate backend=snd fd=%d action=hold_silence frames=%u ch=%u silence=%u hold=%u\n",
                     audio->fd, frames, channels, silence_frames,
                     silence_hold_frames);
               }
            } else {
               int mute_ret = unifrog_audio_set_mute(audio, 1);

               audio_reset_silence_state(audio);
               if (mute_transition_log_count < 12) {
                  mute_transition_log_count++;
                  printf("unifrog audio signal_gate backend=snd fd=%d action=mute_after_hold ret=%d frames=%u ch=%u hold=%u\n",
                     audio->fd, mute_ret, frames, channels,
                     silence_hold_frames);
               }
            }
         } else {
            audio_reset_silence_state(audio);
         }
      }
   }

   audio_apply_reopen_settle_delay(audio);

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
         if (gb300_coalesced_flush) {
            gb300_snd_coalesce_frames = 0;
            gb300_snd_coalesce_has_signal = 0;
         }
         return 0;
      }
      if (tries + 1 >= attempts)
         break;
      if (gb300_snd && last_errno == EPERM) {
         snd_pcm_uframes_t delay = 0;
         int delay_ret = ioctl(audio->fd, SND_IOCTL_DELAY, &delay);

         if (transient_log_count < GB300_SND_TRANSIENT_LOG_LIMIT) {
            transient_log_count++;
            printf("unifrog audio gb300_write_wait reason=not_enough_avail fd=%d frames=%u try=%u attempts=%u sleep_ms=%u delay_ret=%d delay=%lu\n",
               audio->fd, frames, tries + 1, attempts, poll_timeout_ms,
               delay_ret, (unsigned long)delay);
         }
         usleep((poll_timeout_ms ? poll_timeout_ms : 1u) * 1000u);
      } else {
         poll(&pollfd, 1, (int)poll_timeout_ms);
      }
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
   if (gb300_coalesced_flush) {
      gb300_snd_coalesce_frames = 0;
      gb300_snd_coalesce_has_signal = 0;
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
   audio_reset_silence_state(audio);
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
         audio->output_gate_silence_frames = 0;
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
         audio->output_gate_silence_frames = 0;
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
         audio->output_gate_silence_frames = 0;
         printf("unifrog audio signal_gate backend=snd fd=%d action=gb300_gate_on_enable\n",
            audio->fd);
      } else {
         set_stock_audio_output_gate(1);
         audio->output_gate_enabled = 1;
         audio->output_gate_pending_signal = 0;
         audio->output_gate_silence_frames = 0;
         if (audio->backend == UNIFROG_AUDIO_BACKEND_SND)
            (void)unifrog_audio_set_mute(audio, 1);
         else
            (void)unifrog_audio_set_mute(audio, 0);
      }
   } else {
      set_stock_audio_output_gate(0);
      audio->output_gate_enabled = 0;
      audio->output_gate_pending_signal = 0;
      audio->output_gate_silence_frames = 0;
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

unsigned read_pinmux(pinpad_e pin)
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
   int hw_ret = -1;

   memset(&hw, 0, sizeof(hw));
   if (audio && audio->fd >= 0 &&
       audio->backend == UNIFROG_AUDIO_BACKEND_SND)
      hw_ret = ioctl(audio->fd, SND_IOCTL_GET_HW_INFO, &hw);

   unifrog_audio_debug_gate(&l_dir, &l_out, &r_dir, &r_out);
   printf("unifrog audio diag tag=%s board=%s board_override=%s panel=%s lcd=0x%06lx preferred_gate=%s output_ch=%u gate_enabled=%d audio_muted=%d audio_gate=%d audio_pending=%d l=0x%08lx/0x%08lx r=0x%08lx/0x%08lx\n",
      tag ? tag : "?",
      unifrog_device_board_name(unifrog_device_board()),
      unifrog_device_board_override_name(),
      unifrog_device_panel_name(unifrog_device_panel()),
      unifrog_device_lcd_panel_id(),
      audio_gate_name(current_audio_gate()),
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
