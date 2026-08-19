#ifndef UNIFROG_CLOCK_H
#define UNIFROG_CLOCK_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum unifrog_clock_field {
   UNIFROG_CLOCK_YEAR = 0,
   UNIFROG_CLOCK_MONTH,
   UNIFROG_CLOCK_DAY,
   UNIFROG_CLOCK_HOUR,
   UNIFROG_CLOCK_MINUTE,
};

int unifrog_clock_init(void);
time_t unifrog_clock_now(void);
int unifrog_clock_get_local(struct tm *value);
int unifrog_clock_set_local(const struct tm *value);
int unifrog_clock_adjust(enum unifrog_clock_field field, int amount);
int unifrog_clock_format(char *text, size_t size);
int unifrog_clock_persist(void);
int unifrog_clock_set_runtime_offset_minutes(int minutes);
int unifrog_clock_runtime_offset_minutes(void);

#ifdef __cplusplus
}
#endif

#endif
