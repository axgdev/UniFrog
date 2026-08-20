#include "unifrog_media_internal.h"

/* Private media audio diagnostics plus GB300-specific auddec route probes. */
struct media_gb300_auddec_probe_variant {
   const char *label;
   unsigned sample_rate;
   uint32_t snd_devs;
   int enable_audsink;
   int audio_flush_thres;
   int kshm_size;
   int gate_before_init;
   int controls_before_start;
   int prime_stage;
   uint32_t prime_dest;
};

static int16_t
   media_gb300_auddec_probe_pcm[MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES *
      MEDIA_GB300_AUDDEC_PROBE_CHANNELS];
static int16_t
   media_gb300_production_tone_pcm[MEDIA_GB300_PRODUCTION_TONE_FRAMES * 2u];

static void media_audio_diag_progress(unifrog_media_progress_cb progress,
   void *userdata, const char *stage, unsigned done, unsigned total)
{
   if (progress)
      progress(userdata, stage ? stage : "", done, total);
}

static void media_fill_gb300_production_tone(unsigned packet,
   unsigned channels)
{
   unsigned half_period = 22u + (packet % 4u) * 11u;
   int amplitude = 9000;

   if (channels == 0 || channels > 2u)
      channels = 2u;
   for (unsigned i = 0; i < MEDIA_GB300_PRODUCTION_TONE_FRAMES; i++) {
      int16_t sample = (((packet * MEDIA_GB300_PRODUCTION_TONE_FRAMES + i) /
         half_period) & 1u) ? (int16_t)amplitude : (int16_t)-amplitude;

      media_gb300_production_tone_pcm[i * channels] = sample;
      if (channels > 1u)
         media_gb300_production_tone_pcm[i * channels + 1u] = sample;
   }
}

static int media_run_gb300_production_tone_probe(const char *tag)
{
   struct unifrog_audio audio;
   unsigned channels = media_audio_output_channels();
   int backend = unifrog_audio_prefers_stereo_output() ?
      UNIFROG_AUDIO_BACKEND_SND : UNIFROG_AUDIO_BACKEND_AUTO;
   int ret = -1;
   unsigned writes = 0;

   if (channels == 0 || channels > 2u)
      channels = 2u;
   memset(&audio, 0, sizeof(audio));
   audio.fd = -1;
   printf("unifrog media prod_tone begin tag=%s gb300=%d rate=%u ch=%u backend=%d packets=%u frames=%u\n",
      tag ? tag : "?", unifrog_audio_prefers_stereo_output() ? 1 : 0,
      MEDIA_GB300_AUDDEC_PROBE_RATE, channels, backend,
      MEDIA_GB300_PRODUCTION_TONE_PACKETS, MEDIA_GB300_PRODUCTION_TONE_FRAMES);
   /*
    * GB300 audsink rejects this standalone 512-frame diagnostic stream after a
    * few writes, even though the SND path is stable for libretro, FFmpeg PCM,
    * and the direct route probe. Use SND for the audible production tone so
    * the first matrix sound reflects the route we actually trust for PCM.
    */
   if (unifrog_audio_open_backend(&audio, MEDIA_GB300_AUDDEC_PROBE_RATE,
       channels, 512, 8, backend) != 0) {
      printf("unifrog media prod_tone open_failed tag=%s ch=%u backend=%d\n",
         tag ? tag : "?", channels, backend);
      return -1;
   }
   (void)unifrog_audio_set_volume(&audio, media_audio_runtime_volume());
   (void)unifrog_audio_start(&audio);
   (void)unifrog_audio_set_output_enabled(&audio, 1);
   /*
    * The production tone is an audible diagnostic, not normal playback behind
    * the silence gate.  SF2000's SND gate enables the output path muted and
    * waits for later signal-triggered unmute logic; this standalone probe
    * writes fixed tone packets directly, so unmute here after routing is live.
    */
   (void)unifrog_audio_set_mute(&audio, 0);
   unifrog_audio_debug_dump(&audio, "prod_tone_after_start");
   for (unsigned packet = 0; packet < MEDIA_GB300_PRODUCTION_TONE_PACKETS;
        packet++) {
      media_fill_gb300_production_tone(packet, channels);
      media_log_pcm_stats("prod_tone", &audio,
         media_gb300_production_tone_pcm, MEDIA_GB300_PRODUCTION_TONE_FRAMES,
         tag);
      if (unifrog_audio_write(&audio, media_gb300_production_tone_pcm,
          MEDIA_GB300_PRODUCTION_TONE_FRAMES) != 0)
         break;
      writes++;
      usleep(10000);
   }
   ret = writes == MEDIA_GB300_PRODUCTION_TONE_PACKETS ? 0 : -1;
   printf("unifrog media prod_tone end tag=%s ret=%d writes=%u/%u\n",
      tag ? tag : "?", ret, writes, MEDIA_GB300_PRODUCTION_TONE_PACKETS);
   usleep(250000);
   unifrog_audio_close(&audio);
   return ret;
}

static void media_fill_gb300_auddec_probe_pcm(unsigned variant,
   unsigned packet)
{
   unsigned half_period = 18u + (variant % 7u) * 5u;
   int amplitude = 5200 + (int)(variant % 5u) * 900;
   unsigned base = packet * MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES;

   for (unsigned i = 0; i < MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES; i++) {
      int16_t sample = (((base + i) / half_period) & 1u) ?
         (int16_t)amplitude : (int16_t)-amplitude;

      for (unsigned ch = 0; ch < MEDIA_GB300_AUDDEC_PROBE_CHANNELS; ch++)
         media_gb300_auddec_probe_pcm[
            i * MEDIA_GB300_AUDDEC_PROBE_CHANNELS + ch] = sample;
   }
}

static void media_gb300_auddec_probe_controls(int fd, const char *label,
   const char *stage)
{
   audio_channel_select_t channel = media_audio_channel_select();
   uint8_t volume = 90u;
   unsigned int mute = 0;
   int channel_ret;
   int channel_errno;
   int volume_ret;
   int volume_errno;
   int mute_ret;
   int mute_errno;

   errno = 0;
   channel_ret = ioctl(fd, AUDIO_CHANNEL_SELECT, &channel);
   channel_errno = errno;
   errno = 0;
   volume_ret = ioctl(fd, AUDIO_SET_VOLUME, &volume);
   volume_errno = errno;
   errno = 0;
   mute_ret = ioctl(fd, AUDIO_SET_MUTE, &mute);
   mute_errno = errno;
   printf("unifrog media gb300_auddec_probe controls label=%s stage=%s fd=%d channel=%u channel_ret=%d channel_errno=%d volume=%u volume_ret=%d volume_errno=%d mute=%u mute_ret=%d mute_errno=%d\n",
      label ? label : "?", stage ? stage : "?", fd, (unsigned)channel,
      channel_ret, channel_errno, (unsigned)volume, volume_ret, volume_errno,
      mute, mute_ret, mute_errno);
}

static int media_gb300_auddec_probe_status(int fd, const char *label,
   const char *stage)
{
   struct audio_decore_status dec_status;
   audio_status_t audio_status;
   int64_t cur_time = -1;
   unsigned int underruns = 0;
   int dec_ret;
   int dec_errno;
   int audio_ret;
   int audio_errno;
   int time_ret;
   int time_errno;
   int underrun_ret;
   int underrun_errno;

   memset(&dec_status, 0, sizeof(dec_status));
   memset(&audio_status, 0, sizeof(audio_status));
   errno = 0;
   dec_ret = ioctl(fd, AUDDEC_GET_STATUS, &dec_status);
   dec_errno = errno;
   errno = 0;
   audio_ret = ioctl(fd, AUDIO_GET_STATUS, &audio_status);
   audio_errno = errno;
   errno = 0;
   time_ret = ioctl(fd, AUDDEC_GET_CUR_TIME, &cur_time);
   time_errno = errno;
   errno = 0;
   underrun_ret = ioctl(fd, AUDIO_GET_UNDERRUN_TIMES, &underruns);
   underrun_errno = errno;
   printf("unifrog media gb300_auddec_probe status label=%s stage=%s fd=%d dec=%d dec_errno=%d decoded=%lu rate=%lu ch=%u bits=%u hdr=%u/%u audio=%d audio_errno=%d play=%u mute=%u chsel=%u sync=%u bypass=%u time=%lld time_ret=%d time_errno=%d underruns=%lu underrun_ret=%d underrun_errno=%d\n",
      label ? label : "?", stage ? stage : "?", fd, dec_ret, dec_errno,
      (unsigned long)dec_status.frames_decoded,
      (unsigned long)dec_status.sample_rate, dec_status.channels,
      dec_status.bits_per_sample, dec_status.first_header_got,
      dec_status.first_header_parsed, audio_ret, audio_errno,
      (unsigned)audio_status.play_state, (unsigned)audio_status.mute_state,
      (unsigned)audio_status.channel_select,
      (unsigned)audio_status.AV_sync_state,
      (unsigned)audio_status.bypass_mode, (long long)cur_time, time_ret,
      time_errno, (unsigned long)underruns, underrun_ret, underrun_errno);
   return dec_ret == 0 && media_auddec_status_has_progress(&dec_status);
}

static int media_gb300_auddec_probe_variant(unsigned index,
   const struct media_gb300_auddec_probe_variant *variant)
{
   struct media_auddec auddec;
   struct audio_config cfg;
   int fd;
   int init_ret = -1;
   int init_errno = 0;
   int start_ret = -1;
   int start_errno = 0;
   int send_failed = 0;
   int decode_progress = 0;
   int prime_fd = -1;
   int prime_attempted = 0;
   unsigned sample_rate = variant && variant->sample_rate ?
      variant->sample_rate : MEDIA_GB300_AUDDEC_PROBE_RATE;
   int32_t chunk_ms = (int32_t)((MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES *
      1000u) / sample_rate);

   if (!variant)
      return -1;
   if (variant->gate_before_init)
      unifrog_audio_set_system_output_enabled(1);
   fd = open("/dev/auddec", O_RDWR);
   if (fd < 0) {
      printf("unifrog media gb300_auddec_probe open label=%s idx=%u fd=-1 errno=%d\n",
         variant->label, index, errno);
      return -1;
   }
   if (variant->prime_stage == MEDIA_GB300_I2SO_PRIME_BEFORE_INIT) {
      prime_attempted = 1;
      prime_fd = media_gb300_i2so_prime_open(variant->label, sample_rate,
         MEDIA_GB300_AUDDEC_PROBE_CHANNELS, 16u, AVSYNC_TYPE_FREERUN,
         variant->prime_dest);
   }

   memset(&cfg, 0, sizeof(cfg));
   cfg.codec_id = HC_AVCODEC_ID_PCM_S16LE;
   cfg.sync_mode = AVSYNC_TYPE_FREERUN;
   cfg.bits_per_coded_sample = 16u;
   cfg.channels = MEDIA_GB300_AUDDEC_PROBE_CHANNELS;
   cfg.sample_rate = sample_rate;
   cfg.bit_rate = sample_rate *
      MEDIA_GB300_AUDDEC_PROBE_CHANNELS * 16u;
   cfg.block_align = MEDIA_GB300_AUDDEC_PROBE_CHANNELS *
      (16u / 8u);
   cfg.channel_layout =
      media_audio_output_layout(MEDIA_GB300_AUDDEC_PROBE_CHANNELS);
   cfg.snd_devs = variant->snd_devs;
   cfg.enable_audsink = variant->enable_audsink;
   cfg.audio_flush_thres = variant->audio_flush_thres;
   cfg.kshm_size = variant->kshm_size;
   cfg.buffering_start = MEDIA_AUDIO_BUFFERING_START_MS;
   cfg.buffering_end = MEDIA_AUDIO_BUFFERING_END_MS;

   errno = 0;
   init_ret = ioctl(fd, AUDDEC_INIT, &cfg);
   init_errno = errno;
   if (init_ret == 0 && variant->controls_before_start)
      media_gb300_auddec_probe_controls(fd, variant->label, "before_start");
   errno = 0;
   start_ret = init_ret == 0 ? ioctl(fd, AUDDEC_START, 0) : -1;
   start_errno = errno;
   if (init_ret == 0 && start_ret == 0 &&
       variant->prime_stage == MEDIA_GB300_I2SO_PRIME_AFTER_START) {
      prime_attempted = 1;
      prime_fd = media_gb300_i2so_prime_open(variant->label, sample_rate,
         MEDIA_GB300_AUDDEC_PROBE_CHANNELS, 16u, AVSYNC_TYPE_FREERUN,
         variant->prime_dest);
   }
   printf("unifrog media gb300_auddec_probe init label=%s idx=%u fd=%d init=%d init_errno=%d start=%d start_errno=%d snd=0x%lx audsink=%d flush=%d kshm=%d gate_pre=%d controls_pre=%d prime_stage=%d prime_fd=%d prime_dest=%lu rate=%u ch=%u bits=%u block=%u bitrate=%lu\n",
      variant->label, index, fd, init_ret, init_errno, start_ret,
      start_errno, (unsigned long)cfg.snd_devs, cfg.enable_audsink,
      cfg.audio_flush_thres, cfg.kshm_size, variant->gate_before_init,
      variant->controls_before_start, variant->prime_stage, prime_fd,
      (unsigned long)variant->prime_dest, cfg.sample_rate, cfg.channels,
      cfg.bits_per_coded_sample, cfg.block_align, (unsigned long)cfg.bit_rate);
   if (init_ret == 0 && start_ret == 0 &&
       !variant->controls_before_start)
      media_gb300_auddec_probe_controls(fd, variant->label, "after_start");
   if (!variant->gate_before_init && (!prime_attempted || prime_fd < 0))
      unifrog_audio_set_system_output_enabled(1);
   unifrog_audio_debug_dump(NULL, "gb300_auddec_probe");
   decode_progress |= media_gb300_auddec_probe_status(fd, variant->label,
      "after_start");

   memset(&auddec, 0, sizeof(auddec));
   auddec.fd = fd;
   auddec.prime_fd = -1;
   auddec.stream = 0;
   auddec.time_base = (AVRational){ 1, 1000 };
   auddec.freerun = 1;
   auddec.write_timeout_ms = 1500u;
   if (init_ret == 0 && start_ret == 0) {
      for (unsigned packet = 0;
           packet < MEDIA_GB300_AUDDEC_PROBE_PACKETS; packet++) {
         int32_t pts = (int32_t)(packet * (unsigned)chunk_ms);

         media_fill_gb300_auddec_probe_pcm(index, packet);
         if (media_auddec_send_raw(&auddec,
             (const uint8_t *)media_gb300_auddec_probe_pcm,
             sizeof(media_gb300_auddec_probe_pcm), pts, chunk_ms) != 0) {
            send_failed = 1;
            break;
         }
         usleep(12000);
      }
      decode_progress |= media_gb300_auddec_probe_status(fd, variant->label,
         "after_feed");
      usleep(MEDIA_GB300_AUDDEC_PROBE_PAUSE_US);
      decode_progress |= media_gb300_auddec_probe_status(fd, variant->label,
         "after_pause");
      media_auddec_send_eos(&auddec);
   }

   close(fd);
   media_gb300_i2so_prime_close(&prime_fd, variant->label);
   unifrog_audio_set_system_output_enabled(0);
   usleep(80000);
   printf("unifrog media gb300_auddec_probe done label=%s idx=%u fd=%d packets=%lu send_failed=%d progress=%d\n",
      variant->label, index, fd, (unsigned long)auddec.packets,
      send_failed, decode_progress);
   return init_ret == 0 && start_ret == 0 && !send_failed &&
      decode_progress ? 0 : -1;
}

int media_run_gb300_auddec_probe(const char *tag)
{
   static int running;
   static const struct media_gb300_auddec_probe_variant variants[] = {
      { "i2so_prime_after_dma", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_AFTER_START,
         SND_PCM_DEST_DMA },
      { "i2so_prime_before_dma", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_BEFORE_INIT,
         SND_PCM_DEST_DMA },
      { "default_prime_after_dma", 44100u, AUDDEV_DEFAULT, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_AFTER_START,
         SND_PCM_DEST_DMA },
      { "i2so_prime_after_bypass", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_AFTER_START,
         SND_PCM_DEST_BYPASS },
      { "i2so_audsink_prime_after", 44100u, AUDDEV_I2SO, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_AFTER_START,
         SND_PCM_DEST_DMA },
      { "i2so_current_after", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_gate_before", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 1, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_controls_before", 44100u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 1, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_audsink_after", 44100u, AUDDEV_I2SO, 1, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "default_current_after", 44100u, AUDDEV_DEFAULT, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_flush200_after", 44100u, AUDDEV_I2SO, 0, 200,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_48k_after", 48000u, AUDDEV_I2SO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
      { "i2so_spo_after", 44100u, AUDDEV_I2SO | AUDDEV_SPO, 0, 0,
         MEDIA_AUDIO_KSHM_SIZE, 0, 0, MEDIA_GB300_I2SO_PRIME_NONE,
         SND_PCM_DEST_DMA },
   };
   unsigned ok = 0;

   if (!unifrog_audio_prefers_stereo_output() || running)
      return -1;
   running = 1;
   media_init_drivers_once();
   printf("unifrog media gb300_auddec_probe begin tag=%s variants=%lu rate=%u chunk_frames=%u packets=%u note=auddec_pcm_plus_i2so_prime\n",
      tag ? tag : "?", (unsigned long)ARRAY_SIZE(variants),
      MEDIA_GB300_AUDDEC_PROBE_RATE,
      MEDIA_GB300_AUDDEC_PROBE_CHUNK_FRAMES,
      MEDIA_GB300_AUDDEC_PROBE_PACKETS);
   unifrog_audio_debug_dump(NULL, "gb300_auddec_probe_begin");
   for (unsigned i = 0; i < ARRAY_SIZE(variants); i++) {
      if (media_gb300_auddec_probe_variant(i, &variants[i]) == 0)
         ok++;
   }
   unifrog_audio_debug_dump(NULL, "gb300_auddec_probe_end");
   printf("unifrog media gb300_auddec_probe end tag=%s ok=%u variants=%lu\n",
      tag ? tag : "?", ok, (unsigned long)ARRAY_SIZE(variants));
   running = 0;
   return ok > 0 ? 0 : -1;
}

void media_run_gb300_auddec_probe_once(const char *tag)
{
   static int done;

   if (!MEDIA_GB300_AUDDEC_PROBE_ONCE || done)
      return;
   done = 1;
   (void)media_run_gb300_auddec_probe(tag);
}

int unifrog_media_run_audio_diagnostics_ex(char *summary, size_t summary_size,
   unifrog_media_progress_cb progress, void *userdata)
{
   uint32_t start_ms = unifrog_perf_time_ms();
   int prod_ret;
   int direct_ret;
   int auddec_ret;
   int ret;

   printf("unifrog media audio_diag begin gb300=%d\n",
      unifrog_audio_prefers_stereo_output() ? 1 : 0);
   media_audio_diag_progress(progress, userdata, "production tone", 8, 100);
   unifrog_audio_debug_dump(NULL, "audio_diag_begin");
   prod_ret = media_run_gb300_production_tone_probe("audio_diag");
   media_audio_diag_progress(progress, userdata, "direct routes", 30, 100);
   direct_ret = unifrog_audio_run_gb300_route_probe("audio_diag");
   media_audio_diag_progress(progress, userdata, "auddec routes", 62, 100);
   auddec_ret = media_run_gb300_auddec_probe("audio_diag");
   unifrog_audio_set_system_output_enabled(0);
   unifrog_audio_debug_dump(NULL, "audio_diag_end");
   ret = prod_ret == 0 || direct_ret == 0 || auddec_ret == 0 ? 0 : -1;
   if (summary && summary_size > 0) {
      snprintf(summary, summary_size, "prod=%d direct=%d auddec=%d %lums",
         prod_ret, direct_ret, auddec_ret,
         (unsigned long)(unifrog_perf_time_ms() - start_ms));
   }
   media_audio_diag_progress(progress, userdata, "done", 100, 100);
   printf("unifrog media audio_diag end ret=%d prod=%d direct=%d auddec=%d ms=%lu summary=%s\n",
      ret, prod_ret, direct_ret, auddec_ret,
      (unsigned long)(unifrog_perf_time_ms() - start_ms),
      summary ? summary : "");
   return ret;
}

int unifrog_media_run_audio_diagnostics(char *summary, size_t summary_size)
{
   return unifrog_media_run_audio_diagnostics_ex(summary, summary_size, NULL,
      NULL);
}
