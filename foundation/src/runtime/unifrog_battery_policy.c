#include <unifrog/battery.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define BATTERY_RATE_SAMPLE_MS (5u * 60u * 1000u)
#define BATTERY_RATE_MAX_MV_PER_HOUR 2000u
#define BATTERY_LOW_CONFIRM_SAMPLES 3u
#define BATTERY_NORMAL_CONFIRM_SAMPLES 2u
#define BATTERY_RECOVERY_MARGIN_MV 100u

static struct unifrog_battery_calibration active_calibration = {
   { 3500u, 3660u, 3720u, 3800u, 4000u }, 120u, 1,
};

void unifrog_battery_calibration_defaults(
   struct unifrog_battery_calibration *calibration)
{
   if (!calibration)
      return;
   calibration->millivolts[0] = 3500u;
   calibration->millivolts[1] = 3660u;
   calibration->millivolts[2] = 3720u;
   calibration->millivolts[3] = 3800u;
   calibration->millivolts[4] = 4000u;
   calibration->discharge_mv_per_hour = 120u;
   calibration->estimate_discharge = 1;
}

int unifrog_battery_calibration_valid(
   const struct unifrog_battery_calibration *calibration)
{
   if (!calibration || calibration->millivolts[0] < 2500u ||
       calibration->millivolts[4] > 5000u ||
       calibration->discharge_mv_per_hour > BATTERY_RATE_MAX_MV_PER_HOUR ||
       (calibration->estimate_discharge != 0 &&
        calibration->estimate_discharge != 1))
      return 0;
   for (unsigned i = 1; i < UNIFROG_BATTERY_POINT_COUNT; i++) {
      if (calibration->millivolts[i] <= calibration->millivolts[i - 1])
         return 0;
   }
   return 1;
}

void unifrog_battery_set_calibration(
   const struct unifrog_battery_calibration *calibration)
{
   if (unifrog_battery_calibration_valid(calibration))
      active_calibration = *calibration;
}

void unifrog_battery_get_calibration(
   struct unifrog_battery_calibration *calibration)
{
   if (calibration)
      *calibration = active_calibration;
}

void unifrog_battery_status_init(struct unifrog_battery_status *status)
{
   if (!status)
      return;
   memset(status, 0, sizeof(*status));
   status->available = -1;
   status->discharge_mv_per_hour = active_calibration.discharge_mv_per_hour;
   snprintf(status->source, sizeof(status->source), "?");
}

unsigned unifrog_battery_percent_for_mv(unsigned millivolts)
{
   const unsigned *points = active_calibration.millivolts;

   if (millivolts <= points[0])
      return 0;
   if (millivolts >= points[4])
      return 100;
   for (unsigned i = 0; i < 4; i++) {
      unsigned span;

      if (millivolts > points[i + 1])
         continue;
      span = points[i + 1] - points[i];
      return i * 25u + ((millivolts - points[i]) * 25u + span / 2u) / span;
   }
   return 100;
}

unsigned unifrog_battery_bars_for_raw(unsigned raw)
{
   unsigned percent = unifrog_battery_percent_for_mv(raw * 20u);

   return percent >= 100u ? 4u : percent / 25u;
}

static void battery_update_rate(struct unifrog_battery_status *status,
   unsigned millivolts, unsigned now_ms)
{
   uint32_t elapsed;

   if (!status->sample_ms) {
      status->sample_ms = now_ms ? now_ms : 1u;
      status->sample_millivolts = millivolts;
      return;
   }
   elapsed = now_ms - status->sample_ms;
   if (elapsed < BATTERY_RATE_SAMPLE_MS)
      return;

   if (active_calibration.estimate_discharge &&
       status->sample_millivolts > millivolts) {
      uint64_t rate = (uint64_t)(status->sample_millivolts - millivolts) *
         3600000ull / elapsed;

      if (rate > 0 && rate <= BATTERY_RATE_MAX_MV_PER_HOUR) {
         unsigned measured = (unsigned)rate;
         unsigned previous = status->discharge_mv_per_hour;

         status->discharge_mv_per_hour = previous ?
            (previous * 3u + measured + 2u) / 4u : measured;
      }
   }
   status->sample_ms = now_ms ? now_ms : 1u;
   status->sample_millivolts = millivolts;
}

int unifrog_battery_status_apply_sample(struct unifrog_battery_status *status,
   unsigned raw, unsigned millivolts, const char *source, unsigned now_ms)
{
   unsigned percent;
   unsigned bars;
   unsigned rate;
   unsigned minutes = 0;
   int sample_low;
   int low;
   int changed;

   if (!status)
      return -1;
   percent = unifrog_battery_percent_for_mv(millivolts);
   bars = percent >= 100u ? 4u : percent / 25u;
   sample_low = millivolts <= active_calibration.millivolts[1];
   if (status->low) {
      if (millivolts < active_calibration.millivolts[1] +
          BATTERY_RECOVERY_MARGIN_MV) {
         status->normal_confirm_samples = 0;
         low = 1;
      } else {
         status->low_confirm_samples = 0;
         if (status->normal_confirm_samples <
             BATTERY_NORMAL_CONFIRM_SAMPLES)
            status->normal_confirm_samples++;
         low = status->normal_confirm_samples <
            BATTERY_NORMAL_CONFIRM_SAMPLES;
      }
   } else if (sample_low) {
      status->normal_confirm_samples = 0;
      if (status->low_confirm_samples < BATTERY_LOW_CONFIRM_SAMPLES)
         status->low_confirm_samples++;
      low = status->low_confirm_samples >= BATTERY_LOW_CONFIRM_SAMPLES;
   } else {
      status->low_confirm_samples = 0;
      status->normal_confirm_samples = 0;
      low = 0;
   }
   changed = status->available != 1 || status->raw != raw ||
      status->percent != percent || status->bars != bars ||
      status->low != low || strcmp(status->source, source ? source : "?") != 0;

   if (!status->discharge_mv_per_hour)
      status->discharge_mv_per_hour = active_calibration.discharge_mv_per_hour;
   battery_update_rate(status, millivolts, now_ms);
   rate = active_calibration.estimate_discharge ?
      status->discharge_mv_per_hour : active_calibration.discharge_mv_per_hour;
   if (rate && millivolts > active_calibration.millivolts[0])
      minutes = (millivolts - active_calibration.millivolts[0]) * 60u / rate;

   status->available = 1;
   status->raw = raw;
   status->millivolts = millivolts;
   status->percent = percent;
   status->bars = bars;
   status->discharge_mv_per_hour = rate;
   status->estimated_minutes = minutes;
   status->low = low;
   snprintf(status->source, sizeof(status->source), "%s", source ? source : "?");
   return changed;
}
