#ifndef UNIFROG_BATTERY_H
#define UNIFROG_BATTERY_H

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_BATTERY_SOURCE_LEN 12
#define UNIFROG_BATTERY_POINT_COUNT 5

struct unifrog_battery_calibration {
   unsigned millivolts[UNIFROG_BATTERY_POINT_COUNT];
   unsigned discharge_mv_per_hour;
   int estimate_discharge;
};

struct unifrog_battery_status {
   int available;
   unsigned raw;
   unsigned millivolts;
   unsigned percent;
   unsigned bars;
   unsigned discharge_mv_per_hour;
   unsigned estimated_minutes;
   int low;
   char source[UNIFROG_BATTERY_SOURCE_LEN];
   unsigned sample_millivolts;
   unsigned sample_ms;
   unsigned low_confirm_samples;
   unsigned normal_confirm_samples;
};

void unifrog_battery_calibration_defaults(
   struct unifrog_battery_calibration *calibration);
int unifrog_battery_calibration_valid(
   const struct unifrog_battery_calibration *calibration);
void unifrog_battery_set_calibration(
   const struct unifrog_battery_calibration *calibration);
void unifrog_battery_get_calibration(
   struct unifrog_battery_calibration *calibration);
void unifrog_battery_status_init(struct unifrog_battery_status *status);
unsigned unifrog_battery_percent_for_mv(unsigned millivolts);
unsigned unifrog_battery_bars_for_raw(unsigned raw);
int unifrog_battery_status_apply_sample(struct unifrog_battery_status *status,
   unsigned raw, unsigned millivolts, const char *source, unsigned now_ms);
int unifrog_battery_read_raw(unsigned char *raw, const char **source, int force_log);
int unifrog_battery_update(struct unifrog_battery_status *status, int force_log);

#ifdef __cplusplus
}
#endif

#endif
