#include "unifrog_libretro_internal.h"

#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN
#include <dlfcn.h>
#include <unifrog/linux_host.h>
#endif

#ifndef UNIFROG_LIBRETRO_NATIVE_DLOPEN
void *__dso_handle = &__dso_handle;
#endif

#define UNIFROG_STRONG
#define UNIFROG_WEAK __attribute__((weak))

#define DECLARE_UNPREFIXED_LIBRETRO_API_ATTR(attr) \
extern void retro_set_environment(retro_environment_t cb) attr; \
extern void retro_set_video_refresh(retro_video_refresh_t cb) attr; \
extern void retro_set_audio_sample(retro_audio_sample_t cb) attr; \
extern void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) attr; \
extern void retro_set_input_poll(retro_input_poll_t cb) attr; \
extern void retro_set_input_state(retro_input_state_t cb) attr; \
extern void retro_init(void) attr; \
extern void retro_deinit(void) attr; \
extern unsigned retro_api_version(void) attr; \
extern void retro_get_system_info(struct retro_system_info *info) attr; \
extern void retro_get_system_av_info(struct retro_system_av_info *info) attr; \
extern void retro_set_controller_port_device(unsigned port, unsigned device) attr; \
extern void retro_run(void) attr; \
extern void retro_unload_game(void) attr; \
extern bool retro_load_game(const struct retro_game_info *game) attr; \
extern unsigned retro_get_region(void) attr; \
extern size_t retro_serialize_size(void) attr; \
extern bool retro_serialize(void *data, size_t size) attr; \
extern bool retro_unserialize(const void *data, size_t size) attr; \
extern void *retro_get_memory_data(unsigned id) attr; \
extern size_t retro_get_memory_size(unsigned id) attr; \
extern void retro_cheat_reset(void) attr; \
extern void retro_cheat_set(unsigned index, bool enabled, const char *code) attr

#define DECLARE_PREFIXED_LIBRETRO_API_ATTR(prefix, attr) \
extern void prefix##_retro_set_environment(retro_environment_t cb) attr; \
extern void prefix##_retro_set_video_refresh(retro_video_refresh_t cb) attr; \
extern void prefix##_retro_set_audio_sample(retro_audio_sample_t cb) attr; \
extern void prefix##_retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) attr; \
extern void prefix##_retro_set_input_poll(retro_input_poll_t cb) attr; \
extern void prefix##_retro_set_input_state(retro_input_state_t cb) attr; \
extern void prefix##_retro_init(void) attr; \
extern void prefix##_retro_deinit(void) attr; \
extern unsigned prefix##_retro_api_version(void) attr; \
extern void prefix##_retro_get_system_info(struct retro_system_info *info) attr; \
extern void prefix##_retro_get_system_av_info(struct retro_system_av_info *info) attr; \
extern void prefix##_retro_set_controller_port_device(unsigned port, unsigned device) attr; \
extern void prefix##_retro_run(void) attr; \
extern void prefix##_retro_unload_game(void) attr; \
extern bool prefix##_retro_load_game(const struct retro_game_info *game) attr; \
extern unsigned prefix##_retro_get_region(void) attr; \
extern size_t prefix##_retro_serialize_size(void) attr; \
extern bool prefix##_retro_serialize(void *data, size_t size) attr; \
extern bool prefix##_retro_unserialize(const void *data, size_t size) attr; \
extern void *prefix##_retro_get_memory_data(unsigned id) attr; \
extern size_t prefix##_retro_get_memory_size(unsigned id) attr; \
extern void prefix##_retro_cheat_reset(void) attr; \
extern void prefix##_retro_cheat_set(unsigned index, bool enabled, \
   const char *code) attr

DECLARE_UNPREFIXED_LIBRETRO_API_ATTR(UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(gpsp, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(picodrive, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(snes9x2005, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(snes9x2002, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(quicknes, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(fceumm, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(gearboy, UNIFROG_WEAK);
DECLARE_PREFIXED_LIBRETRO_API_ATTR(pce_fast, UNIFROG_WEAK);

#define PREFIXED_LIBRETRO_CORE(id_literal, prefix) { \
   id_literal, 0, 0, \
   prefix##_retro_set_environment, \
   prefix##_retro_set_video_refresh, \
   prefix##_retro_set_audio_sample, \
   prefix##_retro_set_audio_sample_batch, \
   prefix##_retro_set_input_poll, \
   prefix##_retro_set_input_state, \
   prefix##_retro_init, \
   prefix##_retro_deinit, \
   prefix##_retro_api_version, \
   prefix##_retro_get_system_info, \
   prefix##_retro_get_system_av_info, \
   prefix##_retro_set_controller_port_device, \
   prefix##_retro_run, \
   prefix##_retro_unload_game, \
   prefix##_retro_load_game, \
   prefix##_retro_get_region, \
   prefix##_retro_serialize_size, \
   prefix##_retro_serialize, \
   prefix##_retro_unserialize, \
   prefix##_retro_get_memory_data, \
   prefix##_retro_get_memory_size, \
   prefix##_retro_cheat_reset, \
   prefix##_retro_cheat_set, \
}

static const struct libretro_core_api gambatte_core = {
   "gambatte",
   0,
   0,
   retro_set_environment,
   retro_set_video_refresh,
   retro_set_audio_sample,
   retro_set_audio_sample_batch,
   retro_set_input_poll,
   retro_set_input_state,
   retro_init,
   retro_deinit,
   retro_api_version,
   retro_get_system_info,
   retro_get_system_av_info,
   retro_set_controller_port_device,
   retro_run,
   retro_unload_game,
   retro_load_game,
   retro_get_region,
   retro_serialize_size,
   retro_serialize,
   retro_unserialize,
   retro_get_memory_data,
   retro_get_memory_size,
   retro_cheat_reset,
   retro_cheat_set,
};

static const struct libretro_core_api gpsp_core =
   PREFIXED_LIBRETRO_CORE("gpsp", gpsp);
static const struct libretro_core_api picodrive_core =
   PREFIXED_LIBRETRO_CORE("picodrive", picodrive);
static const struct libretro_core_api snes9x2005_core =
   PREFIXED_LIBRETRO_CORE("snes9x2005", snes9x2005);
static const struct libretro_core_api snes9x2002_core =
   PREFIXED_LIBRETRO_CORE("snes9x2002", snes9x2002);
static const struct libretro_core_api quicknes_core =
   PREFIXED_LIBRETRO_CORE("quicknes", quicknes);
static const struct libretro_core_api fceumm_core =
   PREFIXED_LIBRETRO_CORE("fceumm", fceumm);
static const struct libretro_core_api gearboy_core =
   PREFIXED_LIBRETRO_CORE("gearboy", gearboy);
static const struct libretro_core_api pce_fast_core =
   PREFIXED_LIBRETRO_CORE("pce-fast", pce_fast);

static int libretro_core_available(const struct libretro_core_api *core)
{
   return core &&
      core->set_environment &&
      core->set_video_refresh &&
      core->set_audio_sample &&
      core->set_audio_sample_batch &&
      core->set_input_poll &&
      core->set_input_state &&
      core->init &&
      core->deinit &&
      core->api_version &&
      core->get_system_info &&
      core->get_system_av_info &&
      core->set_controller_port_device &&
      core->run &&
      core->unload_game &&
      core->load_game &&
      core->get_region;
}

static const struct libretro_core_api *libretro_core_if_available(
   const struct libretro_core_api *core)
{
   return libretro_core_available(core) ? core : NULL;
}

struct libretro_host host;
static int experimental_sd_log_defer_begin(const char *tag)
{
   unifrog_log_defer_begin();
   unifrog_platform_set_storage_log_suspended(1);
   printf("unifrog log defer begin tag=%s reason=core_session mode=%s pending=%u capacity=%u\n",
      tag ? tag : "", UNIFROG_SD_MODE,
      (unsigned)unifrog_log_pending(), (unsigned)unifrog_log_capacity());
   return 1;
}

static void experimental_sd_log_defer_end(int active, const char *tag, int ret)
{
   if (!active)
      return;
   printf("unifrog log defer end tag=%s reason=core_session mode=%s ret=%d pending=%u deferred=%d\n",
      tag ? tag : "", UNIFROG_SD_MODE, ret,
      (unsigned)unifrog_log_pending(), unifrog_log_flush_deferred());
   unifrog_platform_set_storage_log_suspended(0);
   unifrog_log_note_storage_quiet(3000u);
   unifrog_log_defer_end();
}

int16_t audio_mix_buffer[2048 * LIBRETRO_AUDIO_MAX_CHANNELS];
int16_t audio_silence_buffer[384 * LIBRETRO_AUDIO_MAX_CHANNELS];
volatile unsigned watchdog_active;
volatile unsigned watchdog_phase;
volatile unsigned watchdog_marker;
volatile unsigned watchdog_heartbeat;

static unsigned host_calibrate_count_hz(void)
{
   uint64_t start_us;
   uint64_t end_us;
   uint32_t start_count;
   uint32_t end_count;
   uint64_t elapsed_us;
   uint32_t elapsed_count;

   start_us = host_time_us();
   do {
      end_us = host_time_us();
   } while (end_us == start_us);

   start_us = end_us;
   start_count = unifrog_perf_count();
   unifrog_perf_delay_us(LIBRETRO_COUNT_CALIBRATE_US);
   end_count = unifrog_perf_count();
   end_us = host_time_us();

   elapsed_us = end_us > start_us ? end_us - start_us : 0;
   if (elapsed_us < LIBRETRO_COUNT_CALIBRATE_US / 2u)
      return 0;

   elapsed_count = unifrog_perf_elapsed(start_count, end_count);
   return (unsigned)(((uint64_t)elapsed_count * 1000000ull) / elapsed_us);
}

unsigned host_compute_frame_budget(unsigned fps, unsigned *scpu_mhz,
   unsigned *count_hz, unsigned *count_hz_calibrated)
{
   struct unifrog_perf_caps caps;
   unsigned counts_per_second;

   if (scpu_mhz)
      *scpu_mhz = 0;
   if (count_hz)
      *count_hz = 0;
   if (count_hz_calibrated)
      *count_hz_calibrated = 0;
   if (!fps || unifrog_perf_query_caps(&caps) != 0 || !caps.scpu_mhz_est)
      return 0;
   if (scpu_mhz)
      *scpu_mhz = caps.scpu_mhz_est;
   counts_per_second = host_calibrate_count_hz();
   if (counts_per_second) {
      if (count_hz_calibrated)
         *count_hz_calibrated = 1;
   } else {
      counts_per_second = caps.scpu_mhz_est * 500000u;
   }
   if (count_hz)
      *count_hz = counts_per_second;
   return counts_per_second / fps;
}

static unsigned host_compute_frame_period_us(double fps, unsigned fallback_fps)
{
   double timing_fps = fps > 1.0 ? fps : (double)fallback_fps;
   unsigned period_us;

   if (timing_fps < 1.0)
      return 0;
   period_us = (unsigned)((1000000.0 / timing_fps) + 0.5);
   return period_us ? period_us : 1u;
}

uint64_t host_time_us(void)
{
   return unifrog_perf_time_us();
}

unsigned host_elapsed_ms(uint64_t start_us, uint64_t end_us)
{
   uint64_t elapsed_us;

   if (end_us <= start_us)
      return 0;
   elapsed_us = end_us - start_us;
   if (elapsed_us / 1000ull > UINT32_MAX)
      return UINT32_MAX;
   return (unsigned)(elapsed_us / 1000ull);
}

void host_pace_begin(void)
{
   uint64_t now_us = host_time_us();

   host.frame_deadline_us = now_us + host.frame_period_us;
   host.report_start_us = now_us;
}

static void host_pace_frame(void)
{
   uint64_t now_us;
   uint64_t after_us;

   if (!host.frame_period_us || host.fast_forward)
      return;

   now_us = host_time_us();
   if (now_us < host.frame_deadline_us) {
      uint64_t wait_us = host.frame_deadline_us - now_us;
      unsigned wait_clamped = wait_us > UINT32_MAX ? UINT32_MAX :
         (unsigned)wait_us;

      if (wait_clamped) {
         unifrog_perf_delay_us(wait_clamped);
         after_us = host_time_us();
         wait_us = after_us > now_us ? after_us - now_us : wait_us;
         if (wait_us > UINT32_MAX)
            wait_us = UINT32_MAX;
         host.pace_wait_frames++;
         host.pace_wait_total_us += wait_us;
         if ((unsigned)wait_us > host.pace_wait_max_us)
            host.pace_wait_max_us = (unsigned)wait_us;
      } else {
         after_us = host_time_us();
      }
   } else {
      host.pace_late_frames++;
      after_us = now_us;
   }

   if (after_us >
       host.frame_deadline_us +
       (uint64_t)host.frame_period_us * LIBRETRO_PACE_RESET_LATE_FRAMES) {
      uint64_t catchup_us =
         (uint64_t)host.frame_period_us * LIBRETRO_PACE_RESET_CATCHUP_FRAMES;

      /*
       * After a large stall, do not sleep the next few cheap/skipped frames.
       * Slow cores such as gpSP on heavy GBA ROMs can alternate expensive CPU
       * frames with cheap skipped frames. Preserve a bounded amount of timing
       * debt so those cheap frames can recover real time, while preventing a
       * long unpaced burst after loading or menu stalls.
       */
      host.frame_deadline_us = after_us > catchup_us ?
         after_us - catchup_us : after_us;
      host.pace_reset_frames++;
   } else {
      host.frame_deadline_us += host.frame_period_us;
   }
}

static unsigned host_audio_capacity_frames(void)
{
   if (!host.audio_open || host.audio.frame_bytes == 0)
      return 0;
   return (host.audio.period_bytes * host.audio.periods) /
      host.audio.frame_bytes;
}

static void host_notify_audio_status(void)
{
   unsigned capacity = host_audio_capacity_frames();
   unsigned long delay = 0;
   unsigned occupancy = 0;
   bool active = false;
   bool underrun_likely = false;

   if (!host.audio_status_enabled || !host.audio_status.callback)
      return;
   if (host.fast_forward)
      return;

   if (host.options.frameskip == UNIFROG_LIBRETRO_FRAMESKIP_AUTO &&
       host.frame_period_us) {
      uint64_t now_us = host_time_us();
      uint64_t late_us = now_us > host.frame_deadline_us ?
         now_us - host.frame_deadline_us : 0;

      active = true;
      if (late_us >= (uint64_t)host.frame_period_us)
         occupancy = 0u;
      else if (late_us > 0) {
         uint64_t late_pct =
            (late_us * 100u) / (uint64_t)host.frame_period_us;

         occupancy = late_pct < 100u ? 100u - (unsigned)late_pct : 0u;
      } else {
         occupancy = 100u;
      }
      underrun_likely = occupancy == 0u;
   } else if (capacity && unifrog_audio_delay(&host.audio, &delay) == 0) {
      active = true;
      occupancy = (unsigned)(((uint64_t)delay * 100u) / capacity);
      if (occupancy > 100u)
         occupancy = 100u;
      underrun_likely = occupancy < 25u;
   }

   if (host.audio_status_count == 0 ||
       occupancy < host.audio_status_occupancy_min)
      host.audio_status_occupancy_min = occupancy;
   if (occupancy > host.audio_status_occupancy_max)
      host.audio_status_occupancy_max = occupancy;
   host.audio_status_count++;
   if (active)
      host.audio_status_active_count++;
   if (underrun_likely)
      host.audio_status_underrun_count++;
   host.audio_status_occupancy_total += occupancy;

#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN
   host.audio_status.callback(active, occupancy, underrun_likely);
#else
   (void)unifrog_mips_call3(host.core_gp ? host.core_gp : host_expected_gp(),
      (uintptr_t)host.audio_status.callback, (uintptr_t)active,
      (uintptr_t)occupancy, (uintptr_t)underrun_likely);
#endif
}

static unsigned host_avg_count(uint64_t total, unsigned samples)
{
   if (!samples)
      return 0;
   return (unsigned)(total / samples);
}

static void host_report_perf(const char *core_id, int final)
{
   struct unifrog_presenter_stats present;
   unsigned frames = host.run_frames - host.run_report_frames;
   unsigned video_frames = host.video_frames - host.video_report_frames;
   unsigned audio_batches = host.audio_batches - host.audio_report_batches;
   unsigned audio_frames = host.audio_frames - host.audio_report_frames;
   unsigned audio_failures = host.audio_failures - host.audio_report_failures;
   unsigned long audio_delay = 0;
   unsigned active_avg;
   unsigned frame_wall_avg_us;
   unsigned audio_status_avg = host_avg_count(host.audio_status_occupancy_total,
      host.audio_status_count);
   uint64_t now_us = host_time_us();
   uint64_t wall_us = host.report_start_us && now_us > host.report_start_us ?
      now_us - host.report_start_us : 0;
   unsigned actual_fps_x100 = wall_us ?
      (unsigned)(((uint64_t)frames * 100000000ull) / wall_us) : 0;
   unsigned pace_wait_avg_us = host_avg_count(host.pace_wait_total_us,
      host.pace_wait_frames);
   unsigned scpu_now = unifrog_scpu_current_mhz();
   int audio_delay_ret = -1;

   if (frames == 0)
      return;

   if (host.presenter_open)
      unifrog_presenter_take_stats(&host.presenter, &present);
   else
      memset(&present, 0, sizeof(present));
   if (host.audio_open)
      audio_delay_ret = unifrog_audio_delay(&host.audio, &audio_delay);
   active_avg = host_avg_count(host.active_total_count, frames);
   frame_wall_avg_us = wall_us ? (unsigned)(wall_us / frames) : 0;

   printf("unifrog perf core=%s final=%d frames=%u fps=%u actual_fps_x100=%u wall_ms=%u frame_wall_avg_us=%u options_audio=%d audio_gain=%u frameskip=%d display=%s fast_forward=%d fast_forward_multiplier=%u scpu_target=%u scpu_now=%u ge_clock=%d backlight=%d pace_period_us=%u pace_wait=%u pace_wait_avg_us=%u pace_wait_max_us=%u pace_late=%u pace_reset=%u\n",
      core_id ? core_id : "?",
      final ? 1 : 0,
      frames, host.fps, actual_fps_x100,
      (unsigned)(wall_us / 1000u),
      frame_wall_avg_us,
      host.audio_enabled,
      host.audio_gain,
      host.options.frameskip,
      display_mode_label(host.display_mode),
      host.fast_forward,
      sanitize_fast_forward_multiplier(host.fast_forward_multiplier),
      host.scpu_target_mhz,
      scpu_now,
      host.options.ge_clock,
      host.options.backlight_level,
      host.frame_period_us,
      host.pace_wait_frames,
      pace_wait_avg_us,
      host.pace_wait_max_us,
      host.pace_late_frames,
      host.pace_reset_frames);

   printf("unifrog perf_cpu core=%s slow=%u slow125=%u slow150=%u slow200=%u scpu=%u count_hz=%u count_cal=%u budget=%u run_avg=%u run_max=%u active_avg=%u active_max=%u active_max_frame=%u video=%u present_frames=%u present_avg=%u present_max=%u ge=%u sync=%u vsync=%u pan=%u blit=%u stretch=%u swfb_req=%u swfb_hit=%u swfb_present=%u dst=%d,%d %dx%d\n",
      core_id ? core_id : "?",
      host.slow_frames,
      host.slow_frames_125,
      host.slow_frames_150,
      host.slow_frames_200,
      host.scpu_mhz_est, host.count_hz_est,
      host.count_hz_calibrated,
      host.frame_budget_count,
      host_avg_count(host.run_total_count, frames),
      host.run_max_count,
      active_avg,
      host.active_max_count,
      host.active_max_frame,
      video_frames,
      present.frames,
      host_avg_count(present.total_count, present.frames),
      present.max_count,
      host_avg_count(present.ge_count, present.frames),
      host_avg_count(present.sync_count, present.frames),
      host_avg_count(present.vsync_count, present.frames),
      host_avg_count(present.pan_count, present.frames),
      present.blits,
      present.stretches,
      host.software_framebuffer_requests,
      host.software_framebuffer_hits,
      host.software_framebuffer_presents,
      present.dst_x, present.dst_y, present.dst_w, present.dst_h);

   printf("unifrog perf_audio core=%s batches=%u frames=%u delay_ret=%d delay=%lu fail=%u write_avg=%u write_max=%u gain=%u peak=%u clip=%u gate=%d quiet_frames=%u status=%u status_active=%u status_under=%u occ_avg=%u occ_min=%u occ_max=%u\n",
      core_id ? core_id : "?",
      audio_batches, audio_frames, audio_delay_ret, audio_delay,
      audio_failures,
      host_avg_count(host.audio_write_total_count, host.audio_write_count),
      host.audio_write_max_count,
      host.audio_gain,
      host.audio_peak_max,
      host.audio_clip_count,
      host.audio_gate_open,
      host.audio_quiet_frames,
      host.audio_status_count,
      host.audio_status_active_count,
      host.audio_status_underrun_count,
      audio_status_avg,
      host.audio_status_count ? host.audio_status_occupancy_min : 0,
      host.audio_status_occupancy_max);

   host.run_report_frames = host.run_frames;
   host.video_report_frames = host.video_frames;
   host.audio_report_batches = host.audio_batches;
   host.audio_report_frames = host.audio_frames;
   host.audio_report_failures = host.audio_failures;
   host.software_framebuffer_requests = 0;
   host.software_framebuffer_hits = 0;
   host.software_framebuffer_presents = 0;
   host.slow_frames = 0;
   host.run_total_count = 0;
   host.run_max_count = 0;
   host.active_total_count = 0;
   host.active_max_count = 0;
   host.active_max_frame = 0;
   host.slow_frames_125 = 0;
   host.slow_frames_150 = 0;
   host.slow_frames_200 = 0;
   host.slow_detail_logs = 0;
   host.audio_write_count = 0;
   host.audio_write_total_count = 0;
   host.audio_write_max_count = 0;
   host.audio_peak_max = 0;
   host.audio_clip_count = 0;
   host.audio_status_count = 0;
   host.audio_status_active_count = 0;
   host.audio_status_underrun_count = 0;
   host.audio_status_occupancy_total = 0;
   host.audio_status_occupancy_min = 0;
   host.audio_status_occupancy_max = 0;
   host.pace_wait_frames = 0;
   host.pace_wait_total_us = 0;
   host.pace_wait_max_us = 0;
   host.pace_late_frames = 0;
   host.pace_reset_frames = 0;
   host.report_start_us = now_us;
}

static void host_draw_battery_warning(uint32_t now_ms)
{
   struct unifrog_surface surface;
   unsigned buffer;
   int y;

   if (!host.presenter_open || !host.battery_warning_until_ms ||
       (int32_t)(host.battery_warning_until_ms - now_ms) <= 0)
      return;
   buffer = host.presenter.active_buffer;
   surface = unifrog_fb_surface_for_buffer(&host.presenter.fb, buffer);
   if (!surface.pixels || surface.height < 52u)
      return;
   y = (int)surface.height - 48;
   unifrog_gfx_fill_rect(&surface, 0, y, surface.width, 48,
      UNIFROG_RGB565(45, 25, 18));
   unifrog_gfx_fill_rect(&surface, 0, y, surface.width, 2,
      UNIFROG_RGB565(255, 180, 70));
   unifrog_gfx_draw_text(&surface, 10, y + 9,
      "LOW BATTERY - Save game and charge",
      UNIFROG_RGB565(255, 245, 225), 1);
   unifrog_gfx_draw_text(&surface, 10, y + 28,
      "SD stays in high-performance mode",
      UNIFROG_RGB565(255, 205, 120), 1);
   unifrog_fb_flush_buffer(&host.presenter.fb, buffer);
}

static void host_update_battery_warning(uint32_t now_ms)
{
   if (now_ms - host.battery_check_ms >= 10000u) {
      int battery_ret;

      host.battery_check_ms = now_ms;
      battery_ret = unifrog_battery_update(&host.battery, 0);
      if (battery_ret == 0 && host.battery.available && host.battery.low) {
         host.battery_warning_until_ms = now_ms + 10000u;
         if (!host.battery_warning_active)
            UF_LOG_WARN("battery",
               "event=low game=1 mv=%u action=save_and_charge sd_mode=unchanged",
               host.battery.millivolts);
         host.battery_warning_active = 1;
      } else {
         host.battery_warning_until_ms = 0;
         host.battery_warning_active = 0;
      }
   }
   host_draw_battery_warning(now_ms);
}

static int run_core(const struct libretro_core_api *core, const char *path,
   const struct unifrog_libretro_run_options *options)
{
   struct retro_system_info info;
   struct retro_system_av_info av;
   struct retro_game_info game;
   uint8_t *rom_data = NULL;
   size_t rom_size = 0;
   char prepared_path[256];
   const char *game_path = path;
   unsigned sample_rate = DEFAULT_SAMPLE_RATE;
   size_t old_log_auto_flush;
   unsigned api_version;
   uintptr_t initial_gp;
   uint64_t total_start_us;
   uint64_t stage_start_us;
   uint64_t core_init_done_us;
   uint64_t content_start_us;
   uint64_t content_done_us;
   uint64_t retro_load_start_us;
   uint64_t retro_load_done_us;
   uint64_t save_load_start_us;
   uint64_t save_load_done_us;
   const char *content_kind = "none";
   int content_cache = 0;
   int ret = -1;
   int game_loaded = 0;
   int core_initialized = 0;
   int run_loop_log_defer = 0;
   int content_prepare_failed = 0;

   if (!libretro_core_available(core) || !path || !path[0])
      return -1;

   unifrog_log_sync("libretro run enter core=%s path=%s external=%d gp=0x%08lx",
      core->id ? core->id : "", path, core->external,
      (unsigned long)core_call_gp(core));
   libretro_activity_set(core, path, UNIFROG_ACTIVITY_PHASE_LIBRETRO_BEGIN,
      core->external ? 1u : 0u);
   total_start_us = host_time_us();
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(LIBRETRO_LOG_AUTO_FLUSH_BYTES);

   memset(&host, 0, sizeof(host));
   host.pixel_format = RETRO_PIXEL_FORMAT_RGB565;
   host.core_id = core->id;
   host.quick_rom_path = path;
   host.core_gp = core_call_gp(core);
   core_options_reset();
   host_configure_options(options);
   if (unifrog_clock_set_runtime_offset_minutes(
       host.options.rtc_offset_minutes) != 0) {
      UF_LOG_ERROR("clock", "event=rtc_offset_apply offset_minutes=%d",
         host.options.rtc_offset_minutes);
      goto out_finish;
   }
   initial_gp = host_read_gp();
   host_force_expected_gp();
   printf("unifrog libretro %s start path=%s\n", core->id, path);
   printf("unifrog libretro gp initial=0x%08lx expected=0x%08lx core=0x%08lx external=%d\n",
      (unsigned long)initial_gp, (unsigned long)host_expected_gp(),
      (unsigned long)host.core_gp, core->external);
   printf("unifrog libretro build commit=%s dirty=%d sdk=%s cores=%s\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY,
      UNIFROG_SDK_GIT_COMMIT, UNIFROG_CORES_GIT_COMMIT);
   printf("unifrog libretro options audio=%d gain=%u scpu=%u ge_clock=%d backlight=%d frameskip=%d display=%s framebuffer=%s keymap=%s\n",
      host.audio_enabled, host.audio_gain, host.scpu_target_mhz,
      host.options.ge_clock, host.options.backlight_level,
      host.options.frameskip, display_mode_label(host.display_mode),
      framebuffer_format_label(host.framebuffer_format),
      input_profile_opt_value(host.input_profile));
   if (host.options.max_frames)
      printf("unifrog libretro bounded_run core=%s max_frames=%u\n",
         core->id, host.options.max_frames);
   unifrog_diag_memory_snapshot("libretro.run_start");
   (void)unifrog_log_flush();
   host_apply_runtime_options();
   (void)unifrog_log_flush();
   loading_draw("LOADING GAME", "START", 2);
   unifrog_log_sync("libretro api_version begin core=%s gp=0x%08lx fn=0x%08lx",
      core->id ? core->id : "",
      (unsigned long)core_call_gp(core),
      (unsigned long)(uintptr_t)core->api_version);
   libretro_activity_set_core_call(core,
      UNIFROG_ACTIVITY_PHASE_LIBRETRO_API, 1u, core->api_version);
   api_version = CORE_CALL0_RET(core, core->api_version);
   unifrog_log_sync("libretro api_version done core=%s api=%u",
      core->id ? core->id : "", api_version);
   printf("unifrog libretro %s api=%u\n", core->id, api_version);
   unifrog_diag_memory_snapshot("libretro.after_api_version");
   (void)unifrog_log_flush();

   printf("unifrog libretro step=set_callbacks\n");
   (void)unifrog_log_flush();
   libretro_activity_set_core_call(core,
      UNIFROG_ACTIVITY_PHASE_LIBRETRO_CALLBACKS, 1u,
      core->set_environment);
   CORE_CALL1_VOID(core, core->set_environment,
      unifrog_libretro_environment_trampoline);
   CORE_CALL1_VOID(core, core->set_video_refresh,
      unifrog_libretro_video_refresh_trampoline);
   CORE_CALL1_VOID(core, core->set_audio_sample,
      unifrog_libretro_audio_sample_trampoline);
   CORE_CALL1_VOID(core, core->set_audio_sample_batch,
      unifrog_libretro_audio_batch_trampoline);
   CORE_CALL1_VOID(core, core->set_input_poll,
      unifrog_libretro_input_poll_trampoline);
   CORE_CALL1_VOID(core, core->set_input_state,
      unifrog_libretro_input_state_trampoline);

   printf("unifrog libretro step=get_system_info_preinit\n");
   (void)unifrog_log_flush();
   memset(&info, 0, sizeof(info));
   libretro_activity_set_core_call(core,
      UNIFROG_ACTIVITY_PHASE_LIBRETRO_CALLBACKS, 2u,
      core->get_system_info);
   CORE_CALL1_VOID(core, core->get_system_info, &info);
   printf("unifrog libretro core preinit name=%s version=%s fullpath=%d exts=%s\n",
      info.library_name ? info.library_name : "?",
      info.library_version ? info.library_version : "?",
      info.need_fullpath ? 1 : 0,
      info.valid_extensions ? info.valid_extensions : "?");
   (void)unifrog_log_flush();

   memset(&game, 0, sizeof(game));
   prepared_path[0] = '\0';
   game.path = path;
   host.content_alloc_appmem = core->external && !info.need_fullpath;
   if (host.content_alloc_appmem) {
      printf("unifrog libretro content appmem_reserve enabled core=%s path=%s\n",
         core->id, path);
      (void)unifrog_log_flush();
   }
   libretro_watchdog_start();
   libretro_watchdog_enter(LIBRETRO_WATCHDOG_PHASE_LOAD, 0);
   probe_rom_seek_path(path);
   unifrog_diag_memory_snapshot("libretro.before_content_prepare");
   content_start_us = host_time_us();
   if (info.need_fullpath) {
      content_kind = "fullpath";
      if (path_is_zip(path) &&
          !unifrog_libretro_policy_native_archive(info.need_fullpath,
             info.valid_extensions, path)) {
         FILE *file = fopen(path, "rb");

         if (!file) {
            printf("unifrog libretro zip open failed path=%s errno=%d\n",
               path, errno);
            (void)libretro_recover_storage("zip_cache_open");
            file = fopen(path, "rb");
            if (!file) {
               printf("unifrog libretro zip reopen failed path=%s errno=%d\n",
                  path, errno);
               content_prepare_failed = 1;
               goto out_content_prepare;
            }
         }
         if (zip_extract_rom_to_cache(file, path, &info, prepared_path,
             sizeof(prepared_path)) != 0) {
            fclose(file);
            content_prepare_failed = 1;
            goto out_content_prepare;
         }
         fclose(file);
         game_path = prepared_path;
         content_kind = "zip_cache";
         content_cache = 1;
      } else if (path_is_zip(path)) {
         content_kind = "native_archive";
      } else if (path_is_wrapped_compressed(path)) {
         if (extract_wrapped_compressed_to_cache(path, &info, prepared_path,
             sizeof(prepared_path)) != 0) {
            content_prepare_failed = 1;
            goto out_content_prepare;
         }
         game_path = prepared_path;
         content_kind = "compressed_cache";
         content_cache = 1;
      }
      game.path = game_path;
      printf("unifrog libretro rom fullpath path=%s source=%s\n",
         game.path, path);
      (void)unifrog_log_flush();
      loading_draw("LOADING ROM", "OPENING", 12);
   } else {
      const char *read_path = path;

      if (path_is_wrapped_compressed(path)) {
         content_kind = "compressed_memory";
         if (load_wrapped_compressed_rom_data(path, &rom_data,
             &rom_size) != 0) {
            printf("unifrog libretro compressed memory fallback path=%s\n",
               path);
            if (extract_wrapped_compressed_to_cache(path, &info,
                prepared_path, sizeof(prepared_path)) != 0) {
               content_prepare_failed = 1;
               goto out_content_prepare;
            }
            read_path = prepared_path;
            content_kind = "compressed_cache_read";
            content_cache = 1;
         }
      } else {
         content_kind = path_is_zip(read_path) ? "zip_memory" : "raw_memory";
         if (path_is_zip(read_path)) {
            if (zip_load_rom_data_stream_path(read_path, &info, &rom_data,
                &rom_size, NULL, 0) != 0) {
               content_prepare_failed = 1;
               goto out_content_prepare;
            }
         } else {
            if (read_path_memory_with_fallback(read_path, &rom_data,
                &rom_size, "READING") != 0) {
               content_prepare_failed = 1;
               goto out_content_prepare;
            }
         }
      }
      if (!rom_data) {
         if (read_path_memory_with_fallback(read_path, &rom_data,
             &rom_size, "READ CACHE") != 0) {
            content_prepare_failed = 1;
            goto out_content_prepare;
         }
      }
      game.data = rom_data;
      game.size = rom_size;
      printf("unifrog libretro rom loaded size=%u path=%s source=%s aligned=%lu\n",
         (unsigned)rom_size, read_path, path,
         (unsigned long)((uintptr_t)rom_data & 31u));
      (void)unifrog_log_flush();
   }
out_content_prepare:
   if (content_prepare_failed)
      goto out_finish;
   host.content_alloc_appmem = 0;
   content_done_us = host_time_us();
   printf("unifrog load_time core=%s stage=content_prepare ms=%u kind=%s cache=%d bytes=%u path=%s source=%s\n",
      core->id, host_elapsed_ms(content_start_us, content_done_us),
      content_kind, content_cache, (unsigned)rom_size,
      game.path ? game.path : "", path);
   unifrog_diag_memory_snapshot("libretro.after_content_prepare");
   (void)unifrog_log_flush();

   printf("unifrog libretro step=retro_init\n");
   (void)unifrog_log_flush();
   loading_draw("LOADING GAME", "CORE INIT", 70);
   stage_start_us = host_time_us();
   libretro_activity_set_core_call(core,
      UNIFROG_ACTIVITY_PHASE_CORE_INIT, 1u, core->init);
   CORE_CALL0_VOID(core, core->init);
   core_initialized = 1;
   unifrog_diag_memory_snapshot("libretro.after_core_init_call");

   memset(&info, 0, sizeof(info));
   libretro_activity_set_core_call(core,
      UNIFROG_ACTIVITY_PHASE_CORE_INIT, 2u, core->get_system_info);
   CORE_CALL1_VOID(core, core->get_system_info, &info);
   core_init_done_us = host_time_us();
   printf("unifrog libretro core name=%s version=%s fullpath=%d exts=%s\n",
      info.library_name ? info.library_name : "?",
      info.library_version ? info.library_version : "?",
      info.need_fullpath ? 1 : 0,
      info.valid_extensions ? info.valid_extensions : "?");
   printf("unifrog load_time core=%s stage=core_init ms=%u\n",
      core->id, host_elapsed_ms(stage_start_us, core_init_done_us));
   unifrog_diag_memory_snapshot("libretro.after_core_init");
   (void)unifrog_log_flush();

   loading_draw("LOADING GAME", "CORE LOAD", 72);
   printf("unifrog libretro step=retro_load_game\n");
   (void)unifrog_log_flush();
   libretro_watchdog_enter(LIBRETRO_WATCHDOG_PHASE_LOAD, 0);
   retro_load_start_us = host_time_us();
   libretro_activity_set_core_call(core,
      UNIFROG_ACTIVITY_PHASE_RETRO_LOAD_GAME, 1u, core->load_game);
   if (!CORE_CALL1_RET(core, core->load_game, &game)) {
      libretro_watchdog_leave();
      retro_load_done_us = host_time_us();
      printf("unifrog load_time core=%s stage=retro_load_game ms=%u ok=0\n",
         core->id, host_elapsed_ms(retro_load_start_us,
         retro_load_done_us));
      printf("unifrog libretro load failed path=%s\n", path);
      unifrog_diag_memory_snapshot("libretro.retro_load_failed");
      goto out_unload;
   }
   retro_load_done_us = host_time_us();
   libretro_watchdog_leave();
   game_loaded = 1;
   printf("unifrog libretro step=retro_load_game_ok\n");
   printf("unifrog load_time core=%s stage=retro_load_game ms=%u ok=1\n",
      core->id, host_elapsed_ms(retro_load_start_us, retro_load_done_us));
   unifrog_diag_memory_snapshot("libretro.after_retro_load_game");

   /*
    * Libretro permits AV information to depend on the loaded content.
    * Stella 2014 does so and hangs if queried before retro_load_game().
    */
   memset(&av, 0, sizeof(av));
   libretro_watchdog_enter(LIBRETRO_WATCHDOG_PHASE_LOAD, 0);
   libretro_activity_set_core_call(core,
      UNIFROG_ACTIVITY_PHASE_RETRO_LOAD_GAME, 2u,
      core->get_system_av_info);
   CORE_CALL1_VOID(core, core->get_system_av_info, &av);
   libretro_watchdog_leave();
   host.fps = av.timing.fps > 1.0 ? (unsigned)(av.timing.fps + 0.5) :
      (CORE_CALL0_RET(core, core->get_region) == RETRO_REGION_PAL ? 50u : 60u);
   host.video_max_width = av.geometry.max_width ? av.geometry.max_width :
      av.geometry.base_width;
   host.video_max_height = av.geometry.max_height ? av.geometry.max_height :
      av.geometry.base_height;
   if (av.timing.sample_rate > 1.0)
      sample_rate = (unsigned)(av.timing.sample_rate + 0.5);
   host.frame_budget_count = host_compute_frame_budget(host.fps,
      &host.scpu_mhz_est, &host.count_hz_est, &host.count_hz_calibrated);
   host.frame_period_us = host_compute_frame_period_us(av.timing.fps,
      host.fps);
   printf("unifrog libretro av base=%ux%u max=%ux%u fps=%u sample_rate=%u\n",
      (unsigned)av.geometry.base_width, (unsigned)av.geometry.base_height,
      (unsigned)av.geometry.max_width, (unsigned)av.geometry.max_height,
      host.fps, sample_rate);
   printf("unifrog libretro perf_setup report_frames=%u scpu=%u count_hz=%u count_cal=%u budget=%u pace_period_us=%u\n",
      LIBRETRO_PERF_REPORT_FRAMES, host.scpu_mhz_est, host.count_hz_est,
      host.count_hz_calibrated, host.frame_budget_count,
      host.frame_period_us);
   (void)unifrog_log_flush();

   save_load_start_us = host_time_us();
   libretro_activity_set(core, path, UNIFROG_ACTIVITY_PHASE_SAVE_LOAD, 1u);
   quick_load_all_memory_files(core, path);
   save_load_done_us = host_time_us();
   printf("unifrog load_time core=%s stage=save_memory_load ms=%u\n",
      core->id, host_elapsed_ms(save_load_start_us, save_load_done_us));
   unifrog_diag_memory_snapshot("libretro.after_save_memory_load");

   loading_draw("LOADING GAME", "READY", 100);
   loading_close();
   printf("unifrog libretro step=presenter_open\n");
   (void)unifrog_log_flush();
   if (unifrog_presenter_open_with_clock(&host.presenter, 3,
       present_flags_for_display_mode(host.display_mode),
       host.ge_clock) == 0) {
      host.presenter_open = 1;
      unifrog_presenter_clear(&host.presenter, 0xff000000u);
      unifrog_diag_memory_snapshot("libretro.after_presenter_open");
   } else {
      printf("unifrog libretro presenter open failed\n");
      goto out_unload;
   }

   printf("unifrog libretro step=audio_open\n");
   (void)unifrog_log_flush();
   host.audio_input_rate = sample_rate;
   host.audio_output_rate = host_audio_output_rate(sample_rate);
   host.audio_channels = host_audio_output_channels();
   host.audio_gate_open = 0;
   host.audio_quiet_frames = 0;
   if (!host.audio_enabled) {
      printf("unifrog libretro audio disabled rate=%u/%u channels=%u route=%s\n",
         host.audio_input_rate, host.audio_output_rate, host.audio_channels,
         host_audio_route_name());
   } else if (unifrog_audio_open_backend(&host.audio, host.audio_output_rate,
       host.audio_channels, host_audio_period_bytes(),
       host_audio_periods(), host_audio_backend()) == 0) {
      int volume_ret;
      int mute_ret;
      int silence_ret = 0;
      int start_ret;
      int unmute_ret;
      int output_ret;
      unsigned silence_frames;
      unsigned audio_volume;

      host.audio_open = 1;
      audio_volume = host_audio_runtime_volume();
      volume_ret = unifrog_audio_set_volume(&host.audio, audio_volume);
      mute_ret = unifrog_audio_set_mute(&host.audio, 1);
      silence_frames = LIBRETRO_AUDIO_WRITE_CHUNK_FRAMES;
      if (host.audio.frame_bytes && host.audio.period_bytes < silence_frames)
         silence_frames = host.audio.period_bytes;
      if (silence_frames > 384)
         silence_frames = 384;
      for (unsigned i = 0; i < 8 && silence_frames > 0; i++) {
         if (unifrog_audio_write(&host.audio, audio_silence_buffer,
             silence_frames) != 0)
            silence_ret = -1;
      }
      start_ret = unifrog_audio_start(&host.audio);
      unmute_ret = unifrog_audio_set_mute(&host.audio, 0);
      output_ret = unifrog_audio_set_output_enabled(&host.audio, 0);
      printf("unifrog libretro audio_open ok rate=%u/%u channels=%u period=%u periods=%u volume=%u gain=%u route=%s volume_ret=%d mute_ret=%d silence_ret=%d start_ret=%d unmute_ret=%d output_ret=%d\n",
         host.audio_input_rate, host.audio_output_rate,
         host.audio.channels, host.audio.period_bytes, host.audio.periods,
         audio_volume, host.audio_gain, host_audio_route_name(),
         volume_ret, mute_ret, silence_ret, start_ret, unmute_ret,
         output_ret);
      unifrog_diag_memory_snapshot("libretro.after_audio_open");
      (void)unifrog_log_flush();
   } else {
      printf("unifrog libretro audio open failed rate=%u channels=%u route=%s\n",
         host.audio_output_rate, host.audio_channels, host_audio_route_name());
   }

   CORE_CALL2_VOID(core, core->set_controller_port_device, 0,
      RETRO_DEVICE_JOYPAD);
   CORE_CALL2_VOID(core, core->set_controller_port_device, 1,
      RETRO_DEVICE_JOYPAD);
   printf("unifrog load_time core=%s stage=total ms=%u content_ms=%u retro_ms=%u save_ms=%u kind=%s cache=%d bytes=%u\n",
      core->id, host_elapsed_ms(total_start_us, host_time_us()),
      host_elapsed_ms(content_start_us, content_done_us),
      host_elapsed_ms(retro_load_start_us, retro_load_done_us),
      host_elapsed_ms(save_load_start_us, save_load_done_us),
      content_kind, content_cache, (unsigned)rom_size);
   printf("unifrog libretro step=run_loop fps=%u\n", host.fps);
   unifrog_diag_memory_snapshot("libretro.before_run_loop");
   (void)unifrog_log_flush();
   host_pace_begin();
   /*
    * Core execution is the hottest and least safe time to touch the SD card.
    * The retained-RAM log survives a reset, so defer every automatic disk
    * flush until the pause menu explicitly flushes or the session closes.
    */
   unifrog_log_defer_begin();
   run_loop_log_defer = 1;

   for (;;) {
      uint32_t run_start = unifrog_perf_count();
      unsigned video_before = host.video_frames;
      unsigned run_count;
      unsigned active_count;
      unsigned vsync_count;

      unifrog_libretro_input_poll_cb();
#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN
      if (unifrog_linux_stop_requested())
         break;
#endif
      if (!exit_combo_down())
         host.quick_combo_armed = 1;
      if (host.quick_combo_armed && exit_combo_down()) {
         host.quick_combo_armed = 0;
         printf("unifrog libretro quick_menu core=%s frame=%u\n",
            core->id, host.run_frames);
         (void)libretro_log_flush_force_if_safe();
         if (quick_menu_run(core, path)) {
            printf("unifrog libretro return_to_frontend core=%s frame=%u\n",
               core->id, host.run_frames);
            (void)libretro_log_flush_force_if_safe();
            break;
         }
         host_pace_begin();
         continue;
      }

      host_notify_audio_status();
      libretro_watchdog_enter(LIBRETRO_WATCHDOG_PHASE_RUN,
         host.run_frames + 1u);
      if ((host.run_frames & 31u) == 0)
         libretro_activity_set_core_call(core,
            UNIFROG_ACTIVITY_PHASE_RUN_FRAME, host.run_frames + 1u,
            core->run);
      CORE_CALL0_VOID(core, core->run);
      libretro_watchdog_leave();
      (void)host_audio_flush_sample_buffer();
      if (host.launch_requested || host.shutdown_requested) {
         printf("unifrog libretro requested_return core=%s reason=%s target=%s path=%s\n",
            core->id, host.launch_requested ? "launch" : "shutdown",
            host.launch_core_id, host.launch_path);
         break;
      }
      host_update_battery_warning(unifrog_perf_time_ms());
      run_count = unifrog_perf_elapsed(run_start, unifrog_perf_count());
      vsync_count = host.presenter_open && host.video_frames != video_before ?
         host.presenter.last_vsync_count : 0;
      active_count = run_count;
      if (vsync_count < active_count)
         active_count -= vsync_count;
      host.run_frames++;
      host.run_total_count += run_count;
      host.active_total_count += active_count;
      if (run_count > host.run_max_count)
         host.run_max_count = run_count;
      if (active_count > host.active_max_count) {
         host.active_max_count = active_count;
         host.active_max_frame = host.run_frames + 1u;
      }
      if (host.frame_budget_count && active_count > host.frame_budget_count) {
         unsigned budget = host.frame_budget_count;

         host.slow_frames++;
         if ((uint64_t)active_count * 100u >= (uint64_t)budget * 125u)
            host.slow_frames_125++;
         if ((uint64_t)active_count * 100u >= (uint64_t)budget * 150u)
            host.slow_frames_150++;
         if ((uint64_t)active_count * 100u >= (uint64_t)budget * 200u)
            host.slow_frames_200++;
         if (host.slow_detail_logs < 8 &&
             (uint64_t)active_count * 100u >= (uint64_t)budget * 150u) {
            unsigned long audio_delay = 0;
            int audio_delay_ret = host.audio_open ?
               unifrog_audio_delay(&host.audio, &audio_delay) : -1;

            /*
             * Bounded spike logging is intentionally sparse: it identifies
             * whether a visible hitch coincides with presenter work, audio
             * backpressure, or pure core CPU without flooding the SD log.
             */
            host.slow_detail_logs++;
            printf("unifrog perf_slow core=%s frame=%u run=%u active=%u budget=%u vsync=%u video_delta=%u present_last=%u present_ge=%u present_sync=%u present_pan=%u audio_delay_ret=%d audio_delay=%lu audio_fail=%u\n",
               core->id, host.run_frames + 1u, run_count, active_count,
               budget, vsync_count, host.video_frames - video_before,
               host.presenter.last_present_count,
               host.presenter.last_ge_count,
               host.presenter.last_sync_count,
               host.presenter.last_pan_count,
               audio_delay_ret, audio_delay, host.audio_failures);
         }
      }
      if (host.run_frames >= LIBRETRO_NO_OUTPUT_GRACE_FRAMES &&
          host.video_frames == 0 && host.audio_batches == 0) {
         printf("unifrog libretro no_output_abort core=%s path=%s frames=%u video=%u audio=%u\n",
            core->id, path, host.run_frames, host.video_frames,
            host.audio_batches);
         unifrog_log_sync("libretro no_output_abort core=%s path=%s frames=%u video=%u audio=%u",
            core->id, path, host.run_frames, host.video_frames,
            host.audio_batches);
         ret = -1;
         break;
      }
      if (host.options.max_frames &&
          host.run_frames >= host.options.max_frames) {
         printf("unifrog libretro max_frames_return core=%s frames=%u max=%u\n",
            core->id, host.run_frames, host.options.max_frames);
         unifrog_log_sync("libretro max_frames_return core=%s path=%s frames=%u max=%u",
            core->id, path, host.run_frames, host.options.max_frames);
         break;
      }
      if (!host.fast_forward)
         host_pace_frame();
      if ((host.run_frames - host.run_report_frames) >=
          LIBRETRO_PERF_REPORT_FRAMES)
         host_report_perf(core->id, 0);
   }

   ret = 0;

out_unload:
   unifrog_diag_memory_snapshot("libretro.out_unload");
   libretro_watchdog_stop();
   if (game_loaded) {
      libretro_activity_set(core, path, UNIFROG_ACTIVITY_PHASE_SAVE_MEMORY,
         1u);
      quick_save_all_memory_files(core, path);
   }
   if (core_initialized) {
      libretro_activity_set_core_call(core,
         UNIFROG_ACTIVITY_PHASE_UNLOAD_GAME, 1u, core->unload_game);
      CORE_CALL0_VOID(core, core->unload_game);
   }
out_deinit:
   unifrog_diag_memory_snapshot("libretro.out_deinit");
   libretro_watchdog_stop();
   (void)host_audio_flush_sample_buffer_force();
   host_report_perf(core->id, 1);
   if (run_loop_log_defer)
      unifrog_log_defer_end();
   if (host.audio_open)
      unifrog_audio_close(&host.audio);
   if (host.presenter_open)
      unifrog_presenter_close(&host.presenter);
   unifrog_surface_free(host.software_framebuffer);
   host.software_framebuffer = NULL;
   if (core_initialized) {
      libretro_activity_set_core_call(core,
         UNIFROG_ACTIVITY_PHASE_DEINIT, 1u, core->deinit);
      CORE_CALL0_VOID(core, core->deinit);
   }
out_finish:
   (void)unifrog_clock_set_runtime_offset_minutes(0);
   libretro_watchdog_stop();
   host.content_alloc_appmem = 0;
   rom_free_aligned(rom_data);
   unifrog_diag_memory_snapshot("libretro.out_finish");
   loading_close();
   host_restore_runtime_options();
   unifrog_input_recover_after_core();
   printf("unifrog libretro %s end ret=%d video=%ux%u pitch=%u\n",
      core->id, ret, host.video_width, host.video_height, host.video_pitch);
   unifrog_exception_activity_clear();
   unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
   (void)unifrog_log_flush();
   return ret;
}

static const char *libretro_canonical_core_id(const char *id)
{
   if (!id || !id[0])
      return NULL;
   if (strcmp(id, "pce_fast") == 0)
      return "pce-fast";
   return id;
}

static const struct libretro_core_api *libretro_builtin_core_for_id(
   const char *id)
{
   if (!id || !id[0])
      return NULL;
   if (strcmp(id, "gambatte") == 0)
      return libretro_core_if_available(&gambatte_core);
   if (strcmp(id, "gpsp") == 0)
      return libretro_core_if_available(&gpsp_core);
   if (strcmp(id, "picodrive") == 0)
      return libretro_core_if_available(&picodrive_core);
   if (strcmp(id, "snes9x2005") == 0)
      return libretro_core_if_available(&snes9x2005_core);
   if (strcmp(id, "snes9x2002") == 0)
      return libretro_core_if_available(&snes9x2002_core);
   if (strcmp(id, "quicknes") == 0)
      return libretro_core_if_available(&quicknes_core);
   if (strcmp(id, "fceumm") == 0)
      return libretro_core_if_available(&fceumm_core);
   if (strcmp(id, "gearboy") == 0)
      return libretro_core_if_available(&gearboy_core);
   if (strcmp(id, "pce-fast") == 0 || strcmp(id, "pce_fast") == 0)
      return libretro_core_if_available(&pce_fast_core);
   return NULL;
}

#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN
struct libretro_native_core_loaded {
   void *handle;
};

static void *libretro_dlsym_required(void *handle, const char *name,
   const char *id)
{
   void *sym = dlsym(handle, name);

   if (!sym)
      printf("unifrog libretro native_core missing core=%s symbol=%s\n",
         id ? id : "", name ? name : "");
   return sym;
}

static int libretro_load_external_core(const char *id, const char *core_path,
   struct libretro_native_core_loaded *loaded, struct libretro_core_api *api)
{
   char path[256];
   const char *env_root;

   if (!id || !id[0] || !loaded || !api)
      return -1;
   memset(loaded, 0, sizeof(*loaded));
   memset(api, 0, sizeof(*api));
   if (core_path && core_path[0]) {
      unifrog_text_copy(path, sizeof(path), core_path);
   } else {
      env_root = getenv("UNIFROG_LINUX_CORE_ROOT");
      if (env_root && env_root[0])
         snprintf(path, sizeof(path), "%s/%s_libretro.so", env_root, id);
      else
         snprintf(path, sizeof(path), "output/linux/cores/%s_libretro.so", id);
   }
   loading_draw("LOADING CORE", id, 4);
   printf("unifrog libretro native_core begin core=%s path=%s\n", id, path);
   loaded->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!loaded->handle) {
      printf("unifrog libretro native_core dlopen_failed core=%s path=%s error=%s\n",
         id, path, dlerror());
      return -1;
   }

   api->id = id;
   api->external = 1;
   api->set_environment = libretro_dlsym_required(loaded->handle,
      "retro_set_environment", id);
   api->set_video_refresh = libretro_dlsym_required(loaded->handle,
      "retro_set_video_refresh", id);
   api->set_audio_sample = libretro_dlsym_required(loaded->handle,
      "retro_set_audio_sample", id);
   api->set_audio_sample_batch = libretro_dlsym_required(loaded->handle,
      "retro_set_audio_sample_batch", id);
   api->set_input_poll = libretro_dlsym_required(loaded->handle,
      "retro_set_input_poll", id);
   api->set_input_state = libretro_dlsym_required(loaded->handle,
      "retro_set_input_state", id);
   api->init = libretro_dlsym_required(loaded->handle, "retro_init", id);
   api->deinit = libretro_dlsym_required(loaded->handle, "retro_deinit", id);
   api->api_version = libretro_dlsym_required(loaded->handle,
      "retro_api_version", id);
   api->get_system_info = libretro_dlsym_required(loaded->handle,
      "retro_get_system_info", id);
   api->get_system_av_info = libretro_dlsym_required(loaded->handle,
      "retro_get_system_av_info", id);
   api->set_controller_port_device = libretro_dlsym_required(loaded->handle,
      "retro_set_controller_port_device", id);
   api->run = libretro_dlsym_required(loaded->handle, "retro_run", id);
   api->unload_game = libretro_dlsym_required(loaded->handle,
      "retro_unload_game", id);
   api->load_game = libretro_dlsym_required(loaded->handle,
      "retro_load_game", id);
   api->get_region = libretro_dlsym_required(loaded->handle,
      "retro_get_region", id);
   api->serialize_size = dlsym(loaded->handle, "retro_serialize_size");
   api->serialize = dlsym(loaded->handle, "retro_serialize");
   api->unserialize = dlsym(loaded->handle, "retro_unserialize");
   api->get_memory_data = dlsym(loaded->handle, "retro_get_memory_data");
   api->get_memory_size = dlsym(loaded->handle, "retro_get_memory_size");
   api->cheat_reset = dlsym(loaded->handle, "retro_cheat_reset");
   api->cheat_set = dlsym(loaded->handle, "retro_cheat_set");
   if (!libretro_core_available(api)) {
      dlclose(loaded->handle);
      memset(loaded, 0, sizeof(*loaded));
      return -1;
   }
   printf("unifrog libretro native_core ready core=%s path=%s\n", id, path);
   return 0;
}

static void libretro_unload_external_core(
   struct libretro_native_core_loaded *loaded)
{
   if (loaded && loaded->handle) {
      dlclose(loaded->handle);
      loaded->handle = NULL;
   }
}
#else
static int libretro_load_external_core(const char *id, const char *core_path,
   struct unifrog_core_module_loaded *loaded, struct libretro_core_api *api)
{
   char path[256];
   const struct unifrog_core_module_exports *exports;
   uint64_t load_start_us;
   uint64_t load_done_us;
   size_t old_log_auto_flush;
   int ret = -1;

   if (!id || !id[0] || !loaded || !api)
      return -1;

   if (core_path && core_path[0])
      unifrog_text_copy(path, sizeof(path), core_path);
   else
      snprintf(path, sizeof(path), UNIFROG_CORE_ROOT "/%s.bin", id);
   old_log_auto_flush = unifrog_log_auto_flush_bytes();
   loading_draw("LOADING CORE", id, 4);
   unifrog_log_sync("external_core begin core=%s path=%s", id, path);
   (void)unifrog_log_flush();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_diag_memory_snapshot("libretro.before_external_core_load");
   load_start_us = host_time_us();
   loading_draw("LOADING CORE", "BOOT READ", 8);
   printf("unifrog libretro external_core boot_read core=%s path=%s\n",
      id, path);
   unifrog_log_sync("external_core boot_read core=%s path=%s", id, path);
   if (unifrog_core_module_load_file(path, id, loaded) != 0) {
      (void)libretro_recover_storage("external_core_boot_read");
      if (unifrog_core_module_load_file(path, id, loaded) != 0)
         goto out_restore_log;
   }
   load_done_us = host_time_us();
   printf("unifrog load_time core=%s stage=external_core_load ms=%u path=%s\n",
      id, host_elapsed_ms(load_start_us, load_done_us), path);
   unifrog_log_sync("external_core done core=%s path=%s ms=%u",
      id, path, host_elapsed_ms(load_start_us, load_done_us));
   (void)unifrog_log_flush();

   exports = loaded->exports;
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_EXTERNAL_CORE_API,
      unifrog_exception_activity_hash(id), (uint32_t)(uintptr_t)exports,
      (uint32_t)loaded->gp_addr);
   unifrog_log_sync("external_core api begin core=%s exports=0x%08lx size=%u gp=0x%08lx image=%u memory=%u",
      id, (unsigned long)(uintptr_t)exports,
      exports ? exports->size : 0u, (unsigned long)loaded->gp_addr,
      (unsigned)loaded->image_size, (unsigned)loaded->memory_size);
   memset(api, 0, sizeof(*api));
   api->id = exports->core_id;
   api->call_gp = loaded->gp_addr;
   api->external = 1;
   api->set_environment = exports->retro_set_environment;
   api->set_video_refresh = exports->retro_set_video_refresh;
   api->set_audio_sample = exports->retro_set_audio_sample;
   api->set_audio_sample_batch = exports->retro_set_audio_sample_batch;
   api->set_input_poll = exports->retro_set_input_poll;
   api->set_input_state = exports->retro_set_input_state;
   api->init = exports->retro_init;
   api->deinit = exports->retro_deinit;
   api->api_version = exports->retro_api_version;
   api->get_system_info = exports->retro_get_system_info;
   api->get_system_av_info = exports->retro_get_system_av_info;
   api->set_controller_port_device =
      exports->retro_set_controller_port_device;
   api->run = exports->retro_run;
   api->unload_game = exports->retro_unload_game;
   api->load_game = exports->retro_load_game;
   api->get_region = exports->retro_get_region;
   if (exports->size >= offsetof(struct unifrog_core_module_exports,
       retro_unserialize) + sizeof(exports->retro_unserialize)) {
      api->serialize_size = exports->retro_serialize_size;
      api->serialize = exports->retro_serialize;
      api->unserialize = exports->retro_unserialize;
   }
   if (exports->size >= offsetof(struct unifrog_core_module_exports,
       retro_cheat_set) + sizeof(exports->retro_cheat_set)) {
      api->get_memory_data = exports->retro_get_memory_data;
      api->get_memory_size = exports->retro_get_memory_size;
      api->cheat_reset = exports->retro_cheat_reset;
      api->cheat_set = exports->retro_cheat_set;
   }
   if (!libretro_core_available(api)) {
      printf("unifrog libretro external_core incomplete id=%s\n", id);
      unifrog_log_sync("external_core api incomplete core=%s exports=0x%08lx id=0x%08lx init=0x%08lx api=0x%08lx load=0x%08lx run=0x%08lx",
         id, (unsigned long)(uintptr_t)exports,
         (unsigned long)(uintptr_t)api->id,
         (unsigned long)(uintptr_t)api->init,
         (unsigned long)(uintptr_t)api->api_version,
         (unsigned long)(uintptr_t)api->load_game,
         (unsigned long)(uintptr_t)api->run);
      unifrog_core_module_unload(loaded);
      unifrog_diag_memory_snapshot("libretro.external_core_unloaded_incomplete");
      goto out_restore_log;
   }
   unifrog_log_sync("external_core api ready core=%s id=%s gp=0x%08lx api=0x%08lx init=0x%08lx load=0x%08lx run=0x%08lx",
      id, api->id ? api->id : "",
      (unsigned long)api->call_gp,
      (unsigned long)(uintptr_t)api->api_version,
      (unsigned long)(uintptr_t)api->init,
      (unsigned long)(uintptr_t)api->load_game,
      (unsigned long)(uintptr_t)api->run);
   ret = 0;

out_restore_log:
   unifrog_log_set_auto_flush_bytes(old_log_auto_flush);
   if (ret != 0) {
      unifrog_log_sync("external_core fail core=%s path=%s", id, path);
      (void)unifrog_log_flush();
   }
   return ret;
}

static void libretro_unload_external_core(
   struct unifrog_core_module_loaded *loaded)
{
   unifrog_core_module_unload(loaded);
}
#endif

static int run_core_id(const char *id, const char *path,
   const struct unifrog_libretro_run_options *options)
{
#ifdef UNIFROG_LIBRETRO_NATIVE_DLOPEN
   struct libretro_native_core_loaded loaded;
#else
   struct unifrog_core_module_loaded loaded;
#endif
   struct libretro_core_api external_core;
   const struct libretro_core_api *core;
   const char *canonical = libretro_canonical_core_id(id);
   int loaded_external = 0;
   int ret;

   if (!canonical)
      return -1;

   unifrog_diag_memory_snapshot("libretro.core_id_start");
   unifrog_input_recover_core_transition("core_load");
   core = libretro_builtin_core_for_id(canonical);
   if (!core &&
       libretro_load_external_core(canonical,
       options ? options->core_path : NULL, &loaded, &external_core) == 0) {
      core = &external_core;
      loaded_external = 1;
   }
   if (!core) {
      printf("unifrog libretro core unavailable id=%s\n", canonical);
      return -1;
   }

   ret = run_core(core, path, options);
   if (loaded_external) {
      libretro_unload_external_core(&loaded);
      unifrog_diag_memory_snapshot("libretro.after_external_core_unload");
   }
   return ret;
}

int unifrog_libretro_run_gambatte_ex(const char *path,
   const struct unifrog_libretro_run_options *options)
{
   return run_core_id("gambatte", path, options);
}

int unifrog_libretro_run_gpsp_ex(const char *path,
   const struct unifrog_libretro_run_options *options)
{
   return run_core_id("gpsp", path, options);
}

static const char *libretro_default_core_id_for_path(const char *path)
{
   static const struct {
      const char *suffix;
      const char *core;
   } defaults[] = {
      { ".js", "js2300" }, { ".mjs", "js2300" },
      { ".ch8", "js2300" }, { ".chip8", "js2300" },
      { ".gba", "gpsp" },
      { ".gb", "gambatte" }, { ".gbc", "gambatte" },
      { ".md", "picodrive" }, { ".gen", "picodrive" },
      { ".smd", "picodrive" }, { ".sms", "picodrive" },
      { ".gg", "picodrive" }, { ".sg", "picodrive" },
      { ".32x", "picodrive" },
      { ".sfc", "snes9x2005" }, { ".smc", "snes9x2005" },
      { ".nes", "fceumm" }, { ".fds", "fceumm" },
      { ".pce", "pce-fast" }, { ".sgx", "pce-fast" },
      { ".cue", "qpsx" }, { ".iso", "qpsx" }, { ".img", "qpsx" },
      { ".pbp", "qpsx" }, { ".bin", "qpsx" },
   };
   char stripped[256];
   const char *detect_path = path;

   if (!path)
      return NULL;
   if (path_is_wrapped_compressed(path) &&
       copy_path_without_last_extension(path, stripped, sizeof(stripped)) == 0)
      detect_path = stripped;
   for (unsigned i = 0; i < ARRAY_SIZE(defaults); i++) {
      if (unifrog_text_ends_with_ci(detect_path, defaults[i].suffix))
         return defaults[i].core;
   }
   return NULL;
}

static long libretro_min_content_size_for_path(const char *path,
   const char *core_id)
{
   (void)core_id;

   if (unifrog_text_ends_with_ci(path, ".md") ||
       unifrog_text_ends_with_ci(path, ".gen") ||
       unifrog_text_ends_with_ci(path, ".smd") ||
       unifrog_text_ends_with_ci(path, ".bin"))
      return 512;
   if (unifrog_text_ends_with_ci(path, ".nes") ||
       unifrog_text_ends_with_ci(path, ".fds"))
      return 16;
   if (unifrog_text_ends_with_ci(path, ".gb") ||
       unifrog_text_ends_with_ci(path, ".gbc") ||
       unifrog_text_ends_with_ci(path, ".gba") ||
       unifrog_text_ends_with_ci(path, ".sfc") ||
       unifrog_text_ends_with_ci(path, ".smc") ||
       unifrog_text_ends_with_ci(path, ".pce"))
      return 512;
   return 1;
}

static int libretro_validate_content_file(const char *path,
   const char *core_id)
{
   unsigned char sample;
   struct stat st;
   FILE *file;
   long min_size;
   int stat_errno;
   int read_errno;

   errno = 0;
   if (stat(path, &st) != 0) {
      stat_errno = errno;
      printf("unifrog libretro content invalid path=%s core=%s reason=stat errno=%d\n",
         path ? path : "", core_id ? core_id : "", stat_errno);
      unifrog_log_sync("libretro dispatch fail invalid_file path=%s core=%s reason=stat errno=%d",
         path ? path : "", core_id ? core_id : "", stat_errno);
      return -1;
   }

   min_size = libretro_min_content_size_for_path(path, core_id);
   if (!S_ISREG(st.st_mode) || st.st_size < min_size) {
      printf("unifrog libretro content invalid path=%s core=%s mode=0x%lx size=%ld min=%ld\n",
         path, core_id ? core_id : "", (unsigned long)st.st_mode,
         (long)st.st_size, min_size);
      unifrog_log_sync("libretro dispatch fail invalid_file path=%s core=%s mode=0x%lx size=%ld min=%ld",
         path, core_id ? core_id : "", (unsigned long)st.st_mode,
         (long)st.st_size, min_size);
      return -1;
   }

   errno = 0;
   file = fopen(path, "rb");
   read_errno = errno;
   if (!file) {
      printf("unifrog libretro content invalid path=%s core=%s reason=open errno=%d\n",
         path, core_id ? core_id : "", read_errno);
      unifrog_log_sync("libretro dispatch fail invalid_file path=%s core=%s reason=open errno=%d",
         path, core_id ? core_id : "", read_errno);
      return -1;
   }
   errno = 0;
   if (fread(&sample, 1, 1, file) != 1) {
      read_errno = errno;
      fclose(file);
      printf("unifrog libretro content invalid path=%s core=%s reason=read_first errno=%d\n",
         path, core_id ? core_id : "", read_errno);
      unifrog_log_sync("libretro dispatch fail invalid_file path=%s core=%s reason=read_first errno=%d",
         path, core_id ? core_id : "", read_errno);
      return -1;
   }
   fclose(file);
   return 0;
}

int unifrog_libretro_run_path_ex(const char *path,
   const struct unifrog_libretro_run_options *options)
{
   struct unifrog_libretro_run_options run_options;
   char current_path[UNIFROG_LIBRETRO_CONTENT_PATH_MAX];
   char requested_core[UNIFROG_LIBRETRO_CORE_ID_MAX];
   const char *core_id;
   unsigned launches = 0;
   int log_defer;
   int ret = -1;

   if (!path) {
      printf("unifrog libretro dispatch failed path=null\n");
      (void)unifrog_log_flush();
      return -1;
   }
   unifrog_log_set_disk_suspended(0);
   log_defer = experimental_sd_log_defer_begin("libretro_dispatch");
   if (options)
      run_options = *options;
   else
      unifrog_libretro_run_options_init(&run_options);
   unifrog_text_copy(current_path, sizeof(current_path), path);
   requested_core[0] = '\0';

dispatch:
   if (UNIFROG_SD_EXPERIMENTAL)
      printf("unifrog libretro dispatch storage_precheck=skipped mode=%s path=%s\n",
         UNIFROG_SD_MODE, current_path);
   core_id = run_options.core_id[0] ?
      libretro_canonical_core_id(run_options.core_id) :
      libretro_default_core_id_for_path(current_path);
   printf("unifrog libretro dispatch path=%s requested_core=%s\n",
      current_path, run_options.core_id[0] ? run_options.core_id : "auto");
   unifrog_diag_memory_snapshot("libretro.dispatch");
   (void)unifrog_log_flush();
   if (core_id) {
      if (libretro_validate_content_file(current_path, core_id) != 0) {
         (void)unifrog_log_flush();
         ret = -1;
         goto out;
      }
      ret = run_core_id(core_id, current_path, &run_options);
      printf("unifrog libretro dispatch core=%s ret=%d\n", core_id, ret);
      (void)unifrog_log_flush();
      if (ret == 0 && host.launch_requested) {
         if (++launches > 8u) {
            UF_LOG_ERROR("libretro", "event=launch_limit count=%u", launches);
            ret = -1;
            goto out;
         }
         unifrog_text_copy(requested_core, sizeof(requested_core),
            host.launch_core_id);
         unifrog_text_copy(current_path, sizeof(current_path),
            host.launch_path);
         unifrog_text_copy(run_options.core_id,
            sizeof(run_options.core_id), requested_core);
         run_options.core_path[0] = '\0';
         UF_LOG_INFO("libretro", "event=launch_dispatch count=%u core=%s path=%s",
            launches, run_options.core_id, current_path);
         goto dispatch;
      }
      goto out;
   }

   printf("unifrog libretro dispatch unsupported path=%s requested_core=%s\n",
      current_path, run_options.core_id[0] ? run_options.core_id : "auto");
   (void)unifrog_log_flush();
   ret = -1;

out:
   experimental_sd_log_defer_end(log_defer, "libretro_dispatch", ret);
   return ret;
}

int unifrog_libretro_run_gambatte(const char *path)
{
   return unifrog_libretro_run_gambatte_ex(path, NULL);
}

int unifrog_libretro_run_gpsp(const char *path)
{
   return unifrog_libretro_run_gpsp_ex(path, NULL);
}

int unifrog_libretro_run_path(const char *path)
{
   return unifrog_libretro_run_path_ex(path, NULL);
}
