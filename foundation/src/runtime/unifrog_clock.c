#include <unifrog/clock.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef __HCRTOS__
#include <sys/unistd.h>
#else
#include <unistd.h>
#endif

#include <unifrog/config.h>
#include <unifrog/log.h>
#include <unifrog/paths.h>
#include <unifrog/perf.h>
#include <unifrog/storage_io.h>

#ifndef UNIFROG_BUILD_EPOCH
#define UNIFROG_BUILD_EPOCH 1704067200L
#endif

#define CLOCK_MIN_EPOCH 1577836800L
#define CLOCK_MAX_EPOCH 2145916800L
#define CLOCK_OFFSET_LIMIT_MINUTES (10 * 366 * 24 * 60)

static time_t clock_base_epoch;
static uint32_t clock_base_ms;
static int clock_initialized;
static int clock_runtime_offset_minutes;

struct clock_load_context {
   time_t epoch;
};

static int clock_state_entry(void *userdata, const char *section,
   const char *key, const char *value, unsigned line_number)
{
   struct clock_load_context *context = userdata;
   char *end;
   long long parsed;

   (void)line_number;
   if (section[0] || strcmp(key, "epoch") != 0)
      return 0;
   parsed = strtoll(value, &end, 10);
   if (end && !*end && parsed >= CLOCK_MIN_EPOCH && parsed <= CLOCK_MAX_EPOCH)
      context->epoch = (time_t)parsed;
   return 0;
}

static time_t clock_release_epoch(void)
{
   struct stat st;

   if (stat(UNIFROG_DIST_MANIFEST_PATH, &st) == 0 &&
       st.st_mtime >= (time_t)CLOCK_MIN_EPOCH &&
       st.st_mtime <= (time_t)CLOCK_MAX_EPOCH)
      return st.st_mtime;
   return (time_t)UNIFROG_BUILD_EPOCH;
}

static int clock_sync_system(void)
{
#ifdef __HCRTOS__
   struct timeval tv;

   tv.tv_sec = unifrog_clock_now() +
      (time_t)clock_runtime_offset_minutes * 60;
   tv.tv_usec = 0;
   return settimeofday(&tv, NULL);
#else
   return 0;
#endif
}

int unifrog_clock_init(void)
{
   struct clock_load_context context;
   unsigned errors = 0;

   if (clock_initialized)
      return 0;
   memset(&context, 0, sizeof(context));
   (void)unifrog_config_read(UNIFROG_CLOCK_STATE_PATH, clock_state_entry,
      &context, &errors);
   clock_base_epoch = context.epoch ? context.epoch : clock_release_epoch();
   if (clock_base_epoch < (time_t)CLOCK_MIN_EPOCH ||
       clock_base_epoch > (time_t)CLOCK_MAX_EPOCH)
      clock_base_epoch = (time_t)CLOCK_MIN_EPOCH;
   clock_base_ms = unifrog_perf_time_ms();
   clock_initialized = 1;
   clock_runtime_offset_minutes = 0;
   (void)clock_sync_system();
   UF_LOG_INFO("clock", "event=init epoch=%lld source=%s parse_errors=%u",
      (long long)clock_base_epoch, context.epoch ? "saved" : "release", errors);
   return 0;
}

time_t unifrog_clock_now(void)
{
   if (!clock_initialized)
      (void)unifrog_clock_init();
   return clock_base_epoch +
      (time_t)((uint32_t)(unifrog_perf_time_ms() - clock_base_ms) / 1000u);
}

int unifrog_clock_get_local(struct tm *value)
{
   time_t now;

   if (!value)
      return -1;
   now = unifrog_clock_now();
   return localtime_r(&now, value) ? 0 : -1;
}

int unifrog_clock_set_local(const struct tm *value)
{
   struct tm copy;
   time_t epoch;

   if (!value)
      return -1;
   copy = *value;
   copy.tm_isdst = -1;
   epoch = mktime(&copy);
   if (epoch < (time_t)CLOCK_MIN_EPOCH || epoch > (time_t)CLOCK_MAX_EPOCH)
      return -1;
   clock_base_epoch = epoch;
   clock_base_ms = unifrog_perf_time_ms();
   clock_initialized = 1;
   if (clock_sync_system() != 0)
      return -1;
   return unifrog_clock_persist();
}

int unifrog_clock_adjust(enum unifrog_clock_field field, int amount)
{
   struct tm value;

   if (unifrog_clock_get_local(&value) != 0)
      return -1;
   switch (field) {
   case UNIFROG_CLOCK_YEAR: value.tm_year += amount; break;
   case UNIFROG_CLOCK_MONTH: value.tm_mon += amount; break;
   case UNIFROG_CLOCK_DAY: value.tm_mday += amount; break;
   case UNIFROG_CLOCK_HOUR: value.tm_hour += amount; break;
   case UNIFROG_CLOCK_MINUTE: value.tm_min += amount; break;
   default: return -1;
   }
   return unifrog_clock_set_local(&value);
}

int unifrog_clock_format(char *text, size_t size)
{
   struct tm value;

   if (!text || size == 0 || unifrog_clock_get_local(&value) != 0)
      return -1;
   return snprintf(text, size, "%04d-%02d-%02d %02d:%02d:%02d",
      value.tm_year + 1900, value.tm_mon + 1, value.tm_mday, value.tm_hour,
      value.tm_min, value.tm_sec) < (int)size ? 0 : -1;
}

int unifrog_clock_persist(void)
{
   char text[256];
   char temporary[sizeof(UNIFROG_CLOCK_STATE_PATH) + 8u];
   time_t now = unifrog_clock_now();
   int len;

   (void)mkdir(UNIFROG_DATA_ROOT, 0777);
   len = snprintf(text, sizeof(text),
      "# UniFrog pseudo-clock state. This is updated by Set Date/Time and Safe\n"
      "# Shutdown. Deleting it resets time to the release file timestamp.\n"
      "epoch=%lld\n", (long long)now);
   if (len <= 0 || len >= (int)sizeof(text))
      return -1;
   snprintf(temporary, sizeof(temporary), "%s.tmp", UNIFROG_CLOCK_STATE_PATH);
   return unifrog_storage_write_atomic(UNIFROG_CLOCK_STATE_PATH, temporary,
      text, (size_t)len, "clock", 4, 100);
}

int unifrog_clock_set_runtime_offset_minutes(int minutes)
{
   long long adjusted;

   if (minutes < -CLOCK_OFFSET_LIMIT_MINUTES ||
       minutes > CLOCK_OFFSET_LIMIT_MINUTES)
      return -1;
   adjusted = (long long)unifrog_clock_now() + (long long)minutes * 60ll;
   if (adjusted < CLOCK_MIN_EPOCH || adjusted > CLOCK_MAX_EPOCH)
      return -1;
   clock_runtime_offset_minutes = minutes;
   return clock_sync_system();
}

int unifrog_clock_runtime_offset_minutes(void)
{
   return clock_runtime_offset_minutes;
}
