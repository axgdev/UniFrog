#include "unifrog_audio_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>

/* Private GB300 audio route diagnostics and gate sweep helpers. */
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
