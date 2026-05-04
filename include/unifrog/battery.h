#ifndef UNIFROG_BATTERY_H
#define UNIFROG_BATTERY_H

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_BATTERY_SOURCE_LEN 12

struct unifrog_battery_status {
   int available;
   unsigned raw;
   unsigned millivolts;
   unsigned bars;
   int low;
   char source[UNIFROG_BATTERY_SOURCE_LEN];
};

void unifrog_battery_status_init(struct unifrog_battery_status *status);
unsigned unifrog_battery_bars_for_raw(unsigned raw);
int unifrog_battery_read_raw(unsigned char *raw, const char **source, int force_log);
int unifrog_battery_update(struct unifrog_battery_status *status, int force_log);

#ifdef __cplusplus
}
#endif

#endif
